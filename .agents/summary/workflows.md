# Workflows

<!-- meta:purpose=runtime sequences and operational flows -->
<!-- meta:audience=ai-assistants,operators -->

## 1. Application startup (using `MakeClient`)

```mermaid
sequenceDiagram
  participant App
  participant Builder as ConfigBuilder
  participant Env as EnvExpander
  participant Factory as MakeClient
  participant Client as ClientBase

  App->>Builder: FromFile("config.json", env_map?)
  Builder->>Env: Expand JSON tree
  Env-->>Builder: expanded JSON (or error)
  Builder-->>App: ParseResult{config_, errmsg_}
  alt parse failed
    Builder-->>App: errmsg_ set, config_ empty
  else parse succeeded
    App->>Factory: MakeClient(config)
    Factory->>Client: new OllamaClient/ClaudeClient/...
    Factory->>Client: ApplyConfig(&config)
    Client->>Client: Load endpoint, MCP servers, log level, timeouts, compaction threshold
    Factory-->>App: shared_ptr<ClientBase>
  end
```

Failure modes a caller should handle:

- `ConfigBuilder` returns `ParseResult` with `config_ = nullopt` and a human-readable `errmsg_` when JSON is malformed, structurally invalid, or contains an unknown `EndpointKind`/`TransportType`.
- `MakeClient` returns `nullopt` when the config is missing, has no endpoint, or a constructor throws.

## 2. Single chat turn (no tools)

```mermaid
sequenceDiagram
  participant App
  participant Client as ClientBase
  participant Q as ChatRequestQueue
  participant T as ITransport
  participant LLM as Provider API

  App->>Client: Chat("hello", cb, ChatOptions::kDefault)
  Client->>Client: build assistant::request (model, messages, system, tools)
  Client->>Q: push ChatRequest
  Client->>Q: ProcessChatRequestQueue()
  Q-->>Client: pop one request
  Client->>T: stream POST /chat or /messages
  loop streaming chunks
    LLM-->>T: chunk
    T-->>Client: on_raw / on_response
    Client->>Client: parse chunk via provider parser
    Client->>App: cb(text, kPartialResult, thinking)
  end
  LLM-->>T: terminal event (done, usage)
  T-->>Client: final
  Client->>Client: update history, pricing, usage
  Client->>App: cb("...", kRequestCost, false)
  Client->>App: cb("", kDone, false)
```

## 3. Tool-calling turn (with human-in-the-loop)

```mermaid
sequenceDiagram
  participant App
  participant Client as ClientBase
  participant FT as FunctionTable
  participant Tool as Tool (in-proc)
  participant LLM as Provider API

  App->>Client: Chat("delete /tmp/foo", cb, kDefault)
  Client->>LLM: chat with tool schemas
  LLM-->>Client: stream containing tool_call(name, args, id)
  Client->>FT: CanRunTool(name, args)
  alt per-tool callback registered
    FT->>Tool: tool->CanRun(args)
    Tool-->>FT: CanInvokeToolResult
  else fallback to client-wide
    Client->>App: m_on_invoke_tool_cb(name, args)
    App-->>Client: CanInvokeToolResult
  end
  alt approved
    Client->>App: cb("Tool allowed: name", kToolAllowed, false)
    Client->>FT: Call({name,args})
    FT->>Tool: Tool::Call(args)
    Tool-->>FT: FunctionResult
    FT-->>Client: FunctionResult
    Client->>Client: AddToolsResult([(call,result)])
    Client->>LLM: continue with tool result
    LLM-->>Client: stream final text
    Client->>App: cb(text, kPartialResult/kDone, ...)
  else denied
    Client->>App: cb("Tool denied: name (reason)", kToolDenied, false)
    Client->>Client: synthesise "tool denied" turn
    Client->>LLM: continue with denial message
  end
```

The CLI demo wires the prompt-style approval gate at the **client level** with `cli->SetToolInvokeCallback(CanRunTool)` and **also** registers a per-tool `SetHumanInTheLoopCallback(CanRunTool)` on `Read_file_content_from_a_given_path` to demonstrate that the per-tool callback overrides the client-wide one.

## 4. MCP-backed tool turn

```mermaid
sequenceDiagram
  participant Client as ClientBase
  participant FT as FunctionTable
  participant Ext as ExternalFunction
  participant MCP as MCPClient
  participant Server as MCP server (stdio / SSE / SSH-stdio)

  Note over Client,Server: At ApplyConfig time:
  Client->>FT: ReloadMCPServers(config)
  FT->>MCP: new MCPClient(...) + Initialise()
  MCP->>Server: initialize + ping + tools/list
  Server-->>MCP: tool list
  MCP-->>FT: shared_ptr<ExternalFunction> per tool
  FT->>FT: Add each ExternalFunction to registry

  Note over Client,Server: At chat time:
  Client->>FT: Call({name, args})
  FT->>Ext: Call(args)
  Ext->>MCP: Call(tool, args)
  MCP->>Server: tools/call (JSON-RPC)
  Server-->>MCP: { isError, content[0].text }
  MCP-->>Ext: FunctionResult
  Ext-->>Client: FunctionResult
```

For remote stdio, `MCPClient` builds an `ssh ... -p PORT HOST "<wrapped command>"` invocation and treats the SSH process as the stdio transport. The command is wrapped with double quotes if it contains spaces, and embedded `"` characters are escaped. `ServerAliveInterval=30` is appended for keepalive.

## 5. CLI startup and REPL

The `code-assist` executable (`cli/main.cpp`):

```mermaid
flowchart TB
  Start([./code-assist ...]) --> Parse[ParseCommandLine]
  Parse --> SetLogFile{--logfile?}
  SetLogFile -->|yes| LogToFile[SetLogFile]
  SetLogFile -->|no| Continue1[continue]
  LogToFile --> Continue1
  Continue1 --> ReadConfig{-c provided?}
  ReadConfig -->|yes| ParseConfig[ConfigBuilder::FromFile]
  ParseConfig -->|fail| Exit1[exit 1]
  ParseConfig -->|ok| MakeCli[MakeClient]
  ReadConfig -->|no| MakeCli
  MakeCli -->|nullopt| Exit2[exit 1]
  MakeCli -->|ok| Pricing[FindPricing for active model and SetPricing]
  Pricing --> RegisterTools{--no-builtin-mcp?}
  RegisterTools -->|no| BuiltIn[Add 3 built-in file tools to FunctionTable]
  RegisterTools -->|yes| SkipBuiltIn[skip]
  BuiltIn --> SetCB[SetToolInvokeCallback CanRunTool]
  SkipBuiltIn --> SetCB
  SetCB --> Sys[AddSystemMessage x3]
  Sys --> Banner[print banner and slash command help]
  Banner --> Loop[REPL loop GetTextFromUser]
  Loop --> Input{prompt}
  Input -->|q exit quit| Done([return 0])
  Input -->|"/info"| ShowInfo[GetModelInfo and reprint prompt]
  ShowInfo --> Loop
  Input -->|/no_tools etc| ToggleFlags[mutate ChatOptions or CachePolicy]
  ToggleFlags --> Loop
  Input -->|/reset| ResetState[ClearHistoryMessages and ClearMessageQueue]
  ResetState --> Loop
  Input -->|/int| Interrupt[cli->Interrupt and break]
  Interrupt --> Done
  Input -->|"@filename"| ReadFile[ReadFileContent and replace prompt]
  ReadFile --> Send[HandlePrompt]
  Input -->|other| Send
  Send --> Loop
```

### CLI command-line flags

| Flag | Behaviour |
|---|---|
| `--loglevel <LEVEL>` / `--log-level <LEVEL>` | One of `trace`/`debug`/`info`/`warn`/`error` (default `info`). Overrides config. |
| `-c <path>` / `--config <path>` | Configuration file. If omitted, the CLI starts without an LLM client and exits. |
| `--logfile <path>` | Redirect logs to a file instead of stderr. |
| `-s` / `--silence` | Suppress banner and prompt printing (machine-readable mode). |
| `--no-builtin-mcp` | Skip registration of the three built-in file tools. |
| `-h` / `--help` | Print usage and exit 0. |

### CLI slash commands

| Command | Effect |
|---|---|
| `/info` | Print `GetModelInfo(model)` JSON. |
| `/default` | Reset `ChatOptions` to `kDefault`. |
| `/no_tools` | Set `ChatOptions::kNoTools` (this turn and onwards). |
| `/no_history` | Set `ChatOptions::kNoHistory`. |
| `/reset` | Clear history + queue, restore default options. |
| `/int` | `cli->Interrupt()` and exit the REPL. |
| `/cache_static` | `SetCachingPolicy(kStatic)`. |
| `/cache_auto` | `SetCachingPolicy(kAuto)`. |
| `/cache_none` | `SetCachingPolicy(kNone)`. |
| `q` / `quit` / `exit` | Exit the REPL. |
| `@<path>` | Replace the next prompt with the file content at `<path>`. |

### CLI built-in tools (when `--no-builtin-mcp` is **not** passed)

| Tool name | Required params | Optional params | Validation |
|---|---|---|---|
| `Write_file_content_to_disk_at_a_given_path` | `filepath: string`, `file_content: string` | — | — |
| `Read_file_content_from_a_given_path` | `filepath: string`, `start_line: number`, `count: number` | — | `count` ∈ `[1,5]`; per-tool `[y/n]` approval |
| `Create_new_file` | `filepath: string` | `file_content: string` | — |

## 6. Max-token continuation

When the response callback receives `Reason::kMaxTokensReached`, the CLI demo replaces the user prompt with `"Please continue from exactly where you left off."` and re-issues the chat call. This loop is implemented in `HandlePrompt(...)` and continues until `Reason::kDone` or another terminal reason is delivered.

## 7. Build and test

### Local build (Debug)

```bash
mkdir -p .build-debug
cd .build-debug
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
cmake --build . --parallel
```

### Local build (Release, matches CI)

```bash
mkdir -p .build-release
cd .build-release
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON
cmake --build . --parallel
ctest --output-on-failure
```

Disable OpenSSL (matches the second matrix leg in CI):

```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DASSISTANTLIB_WITH_OPENSSL=OFF
```

The repo's `assistant.workspace` (CodeLite) and the existing `AGENTS.md` reflect this two-build-dir convention (`.build-debug/` and `.build-release/`).

### Running an individual gtest binary

```bash
.build-release/tests/test_config
.build-release/tests/test_history
# or via ctest:
ctest --test-dir .build-release -R test_config --output-on-failure
```

`gtest_discover_tests` registers each `TEST_F`/`TEST` so `--gtest_filter` works as usual.

## 8. CI flow (GitHub Actions)

Each workflow (`macos.yml`, `ubuntu.yml`, `windows.yml`):

```mermaid
flowchart LR
  A[push or PR] --> B[checkout submodules:true]
  B --> C[print cmake --version]
  C --> D[mkdir build-release && cmake .. with options matrix]
  D --> E[cmake --build . --config Release --parallel nproc]
  E --> F[ctest --output-on-failure]
```

The Windows workflow additionally installs MSYS2 with `clang64` and `pacman` packages: `cmake`, `make`, `clang`, `clang-tools-extra`, `openssl`. It uses `-G "MinGW Makefiles"`. The internal job is named `msys2` while the YAML file is named `windows.yml`.

The matrix runs each platform with options `''` (default) and `-DASSISTANTLIB_WITH_OPENSSL=OFF`. `fail-fast: false` means a failure in one matrix leg does not cancel the others.
