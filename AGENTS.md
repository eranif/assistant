# AGENTS.md

> Read this first. It is the navigation guide for AI agents working in this repository. For deeper detail, follow the pointers into `.agents/summary/`.

## What this repo is

A C++20 static library, **`assistantlib`**, that provides a unified runtime API for talking to multiple LLM providers (Anthropic Claude, OpenAI `/v1/responses`, OpenAI-compatible `/v1/chat/completions` for Moonshot AI and Minimax, and local/remote Ollama), plus tool calling (in-process and via the **Model Context Protocol** — stdio / SSE / remote stdio over SSH). A demo executable, **`code-assist`**, exercises the library as an interactive REPL.

## Repo map

```
assistant/                  core library (assistantlib)
├── assistant.hpp           umbrella header — MakeClient(Config|content|path) factory
├── assistantlib.hpp        legacy umbrella — message/request/response/ITransport/EndpointKind/TransportType
├── client/                 ClientBase + provider clients
│   ├── client_base.hpp     abstract API + ChatRequest, ChatRequestQueue, History (with MessageType tags)
│   ├── ollama_client.*     base concrete impl (others inherit from this, not from ClientBase)
│   ├── claude_client.*     Anthropic
│   ├── openai_client.*     OpenAI /v1/responses
│   └── openai_messages_client.*   OpenAI-compatible /v1/chat/completions (Moonshot AI, Minimax)
├── config.hpp+cpp          Config / ConfigBuilder / Endpoint subclasses / MCPServerConfig
├── EnvExpander.hpp+cpp     ${VAR} / $VAR expansion across JSON trees
├── function.hpp+cpp        Param / FunctionBase / InProcessFunction / ExternalFunction / FunctionBuilder / FunctionTable
├── mcp.hpp+cpp             MCPClient (stdio | SSE | SSH-stdio)
├── tool.hpp                inline ResponseParser for Ollama-shaped responses
├── claude_response_parser.*           streaming parsers for each provider
├── openai_response_parser.*
├── chat_completions_response_parser.*
├── Curl.hpp+cpp            alternate ITransport that shells out to system `curl`
├── Process.hpp+cpp         cross-platform process runner (one-shot + bidirectional interactive)
├── logger.hpp              OLOG(level), OLOG_INFO/DEBUG/...; sink/file overrides
├── common.hpp              Locker<T>, ChatOptions, ModelCapabilities, Reason, Pricing, Usage, PRICING_TABLE
├── helpers.hpp             Result<V,E>, JoinArray, trim, split_into_lines, try_read_jsons_from_string, ASSIGN_OPT_OR_RETURN macros
├── attributes.hpp          Clang thread-safety annotation macros (GUARDED_BY/REQUIRES/...)
├── thread_notifier.hpp     condvar-backed one-shot value slot
├── cpp-mcp/                MCP protocol implementation, built as static lib `mcp-cpp`
└── common/                 vendored single-header deps:
                              json.hpp (nlohmann), httplib.h (cpp-httplib),
                              base64.hpp, magic_enum.hpp
                            plus first-party tokens.hpp (CountTokens estimator)

cli/                        code-assist demo executable (gated by ASSISTANTLIB_BUILD_EXAMPLE)
tests/                      10 GoogleTest binaries (gated by ASSISTANTLIB_BUILD_TESTS or ENABLE_TESTS)
submodules/googletest/      git submodule
.github/workflows/          macos.yml, ubuntu.yml, windows.yml (workflow name `msys2`)
.agents/summary/            generated knowledge base — see "Deeper docs" below
```

## Key entry points

- **`assistant/assistant.hpp` → `MakeClient(Config|json_content|path)`** — the only factory you need to construct a client. Returns `optional<shared_ptr<ClientBase>>`. Picks the concrete client class from `Endpoint::type_` (`ollama`/`anthropic`/`openai`/`moonshotai`/`minimax` — the last two share `OpenAIMessagesClient`).
- **`assistant/client/client_base.hpp` → `ClientBase`** — the runtime API every client implements: `Chat`, `IsRunning`, `List`/`ListJSON`, `GetModelInfo`, `GetModelCapabilities`, `CreateAndPushChatRequest`, `AddToolsResult`, `Compact`, `ApplyConfig`, `Interrupt`, plus history/system-message/cost/usage/transport accessors.
- **`assistant/config.hpp` → `ConfigBuilder::FromFile/FromContent`** — the only public constructor for `Config`; runs `EnvExpander` first, then validates and applies defaults.
- **`assistant/function.hpp` → `FunctionBuilder`** — fluent registration of in-process tools. `FunctionTable::AddMCPServer` registers MCP-backed tools.
- **`cli/main.cpp` → `main`** — the demo. Read this if you need an end-to-end example.

## Repo-specific patterns and gotchas

These deviate from C++/CMake defaults or are easy to miss when reading code in isolation.

### Client inheritance tree is not what it looks like

`OllamaClient` is the only direct subclass of `ClientBase`. The other clients (`ClaudeClient`, `OpenAIClient`, `OpenAIMessagesClient`) inherit from `OllamaClient` and override only the methods that differ from the Ollama wire format. When tracing a method like `Chat`, look in `ollama_client.cpp` first — the override probably exists only there.

### Two umbrella headers

`assistant/assistant.hpp` is the high-level umbrella (factory + most of what consumers need). `assistant/assistantlib.hpp` is the low-level umbrella (`message`, `messages`, `request`, `response`, `ITransport`, `EndpointKind`, `TransportType`). The low-level header is included transitively. Add `#include "assistant/assistant.hpp"` in new consumer code.

### Thread-safety analysis is on

Top-level `CMakeLists.txt` enables `-Wthread-safety` and `-D_LIBCPP_ENABLE_THREAD_SAFETY_ANNOTATIONS` for Clang/AppleClang. Use the macros from `assistant/attributes.hpp` (`GUARDED_BY`, `REQUIRES`, `FUNCTION_LOCKS`, etc.) when you add new mutex-protected state. The `Locker<T>` template in `common.hpp` is the preferred way to bind a value to a mutex when you don't need the analysis to span call sites.

### `Locker<T>` access pattern

Read with `lk.get_value()` (returns a copy) or `lk.with([](const T&){})` (lock held during callback). Write with `lk.set_value(v)` or `lk.with_mut([](T&){})`. Do not return references out of `with`/`with_mut` — the lock is released when the callback returns.

### Tool schemas are endpoint-specific

`FunctionBase::ToJSON(EndpointKind)` produces three different shapes:
- **Ollama / Moonshot / Minimax**: nested `{type:"function", function:{name, parameters}}` with a `required` array.
- **OpenAI** (`/v1/responses`): flat `{type:"function", name, parameters, strict:true}` with `additionalProperties:false` and **all parameters listed in `required`** (OpenAI strict mode); optional types widen to `["<type>","null"]`.
- **Anthropic**: `{name, input_schema}`; with `CachePolicy::kStatic`, the last tool gains `cache_control:{type:"ephemeral"}`.

When adding a parameter, run the response-format tests (`test_openai_response_format`) — they fixture this serialisation.

### Two transports: `httplib` (default) and `curl`

`OllamaClient::CreateClient()` builds an `ITransport` from `m_transport_type`. Setting `endpoints[].transport: "curl"` in the config (or calling `client->SetTransportType(TransportType::curl)`) routes requests through `assistant::Curl`, which writes the payload to a temp file and shells out to the system `curl` binary. Useful for environments where the bundled httplib's TLS or proxy support falls short.

### MCP servers can be remote

`MCPClient(SSHLogin, args, env)` runs a stdio MCP server on a remote host by composing an `ssh ... -p PORT HOST "<wrapped command>"` invocation. The wrapper escapes embedded quotes and adds `ServerAliveInterval=30` for keepalive. This is configured via `mcp_servers[*].stdio.ssh` in the JSON config.

### History compaction is two separate mechanisms

Every history entry carries a `MessageType` tag (`kNormal`/`kToolRequest`/`kToolResponse`). Client-side compaction (`ClientBase::Compact(keep = 3)`) replaces old tool-response content with a fixed truncation marker and returns estimated tokens trimmed — it is **caller-initiated**; despite its name, the config key `endpoints[].auto_compact_threshold` does not trigger anything automatically in the library (and the `CompactIfNeeded()` mentioned in a `config.hpp` comment does not exist). Server-side compaction is separate: Anthropic via the `endpoints[].server_compaction` config block (beta header + `context_management.edits`), OpenAI `/v1/responses` via `auto_compact_threshold` forwarded as `compact_threshold`. Both surface to callbacks as `Reason::kServerCompaction`.

### Built-in CLI tools are demo-only

The three tools registered in `cli/main.cpp` (`Write_file_content_to_disk_at_a_given_path`, `Read_file_content_from_a_given_path`, `Create_new_file`) live in the **demo**, not the library. Disable them with `--no-builtin-mcp`. Don't import them from library code — they exist to show what `FunctionBuilder` looks like in practice.

### Two build dirs by convention

The repo expects `.build-debug/` and `.build-release/` at the root. The CodeLite workspace, the existing `compile_commands.json` symlink, and CI all assume this layout. CI uses `Release` only.

### `OLLAMLIB_ROOT` is a leftover name

The top-level `CMakeLists.txt` defines `set(OLLAMLIB_ROOT ${CMAKE_CURRENT_LIST_DIR})` (note: missing the second `LIB`). Sub-CMakeLists reference it for include directories. The variable name is from a prior project rename. Keep it as-is unless you want to land a focused refactor.

### Streaming callbacks: return value matters

`OnResponseCallback` returns `bool`. Returning `false` aborts the stream for that request. The CLI demo always returns `true`. The first callback for a turn typically arrives with `Reason::kPartialResult` (text chunks), and the last is one of `kDone`, `kFatalError`, `kCancelled`, or `kMaxTokensReached`. The `kRequestCost`, `kToolDenied`, `kToolAllowed`, `kServerCompaction`, `kLogNotice`, and `kLogDebug` reasons are out-of-band events — they may interleave with `kPartialResult`.

### Vendored single-header deps

`assistant/common/` ships nlohmann/json, cpp-httplib, magic_enum, and a base64 helper. Do not add a `find_package` for these; do not pull them via a package manager. To upgrade, drop the new single-file release into `assistant/common/`.

## CMake options

Set with `-D<NAME>=ON|OFF`:

| Option | Default | Effect |
|---|---|---|
| `ASSISTANTLIB_BUILD_EXAMPLE` | `ON` | Build `code-assist` from `cli/` |
| `ASSISTANTLIB_WITH_OPENSSL` | `ON` | Link OpenSSL, define `CPPHTTPLIB_OPENSSL_SUPPORT=1`, link `Crypt32` on Windows |
| `ASSISTANTLIB_BUILD_TESTS` | `OFF` | Build `tests/` (also enabled by `ENABLE_TESTS=ON`) |
| `ENABLE_TLS` | unset | Alias for OpenSSL |
| `ENABLE_TESTS` | unset | Alias for tests |

## CI summary

`.github/workflows/macos.yml`, `ubuntu.yml`, `windows.yml`. All do `actions/checkout@v6` with `submodules: 'true'`, configure with `-DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON`, build with `--parallel`, and run `ctest --output-on-failure`. The matrix tests both with and without OpenSSL. The Windows job installs MSYS2 with `clang64` and uses `-G "MinGW Makefiles"`. There is **no** MSVC build path. Dependabot updates `github-actions` weekly (`.github/dependabot.yml`).

## Lint / format / hooks

- `.clang-format` — Google base, IndentWidth 2, ColumnLimit 80. There is no CI step that enforces this; if you run formatting, run it on the files you touched.
- `.cmake-format` — present; CMakeLists files follow it.
- No `.editorconfig`, no `.pre-commit-config.yaml`, no other linters or git hooks.

## Deeper docs (`.agents/summary/`)

Use these when you need more than navigation. Each file has YAML-style metadata at the top describing its purpose.

- `.agents/summary/index.md` — routing table; load this **next** if a question doesn't match a section above.
- `.agents/summary/codebase_info.md` — repo identity, build options, CI matrix, vendored deps.
- `.agents/summary/architecture.md` — layering, client family, concurrency model, transport, tool flow.
- `.agents/summary/components.md` — module-by-module catalogue with one-paragraph descriptions.
- `.agents/summary/interfaces.md` — full `ClientBase`, `ConfigBuilder`, `FunctionBuilder`, `MCPClient` API surface.
- `.agents/summary/data_models.md` — config JSON schema, `Endpoint`, `History`, `Pricing`/`Usage`, bitflag enums.
- `.agents/summary/workflows.md` — startup, chat turns, tool calls, MCP, the CLI REPL, build/test, CI flow.
- `.agents/summary/dependencies.md` — vendored deps, OpenSSL, GoogleTest submodule, runtime tools.
- `.agents/summary/review_notes.md` — known source/doc inconsistencies and follow-ups identified during this generation pass.

If documentation conflicts with source, **trust source** and update the docs.

## Custom Instructions

<!-- This section is maintained by developers and agents during day-to-day work.
     It is NOT auto-generated by codebase-summary and MUST be preserved during refreshes.
     Add project-specific conventions, gotchas, and workflow requirements here. -->
