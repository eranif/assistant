# Knowledge base index

<!-- meta:purpose=primary entry point for AI assistants -->
<!-- meta:audience=ai-assistants -->

> Read this file first. It tells you which other files in this directory contain the answer to which kinds of question.

## How to use this knowledge base

1. **Always start here.** `index.md` is the routing table.
2. **Load the next-most-relevant file** for the user's question — do not preload everything. Each file is self-contained for its topic.
3. If documentation and source code disagree, **trust the source code** and update the docs.
4. Cross-references in this index are always relative paths inside `.agents/summary/`.

## Repository at a glance

- C++20 static library `assistantlib` plus an interactive demo executable `code-assist`.
- One unified runtime API (`assistant::ClientBase`) over four LLM providers: Ollama, Anthropic Claude, OpenAI (`/v1/responses`), and OpenAI-compatible messages (`/v1/chat/completions`, used for Moonshot AI).
- Tool calling for both in-process C++ functions and **Model Context Protocol (MCP)** servers — over stdio, SSE, or remote stdio tunnelled through SSH.
- JSON configuration with `${VAR}` environment-variable expansion.
- Built with CMake; CI on macOS, Ubuntu, and Windows (MSYS2 clang64); GoogleTest is a git submodule.

## Documents in this knowledge base

| File | When to read it |
|---|---|
| [`codebase_info.md`](codebase_info.md) | First-pass questions about repo layout, languages, build options, CI, vendored deps, lint config. |
| [`architecture.md`](architecture.md) | "How is this designed?" — layering, client family inheritance, concurrency model, transport, tool-calling shape. |
| [`components.md`](components.md) | "What lives where?" — every module, header, and class with one-line responsibilities. |
| [`interfaces.md`](interfaces.md) | "What's the API?" — `ClientBase`, `ConfigBuilder`, `FunctionBuilder`, `MCPClient`, callbacks, public factory. |
| [`data_models.md`](data_models.md) | "What does the JSON / state look like?" — config schema, `Endpoint`, MCP server config, `History`, `Pricing`/`Usage`, bitflag enums. |
| [`workflows.md`](workflows.md) | "How does it run?" — startup, chat turns, tool calls, MCP wiring, the CLI REPL, build/test, CI flow. |
| [`dependencies.md`](dependencies.md) | "What's needed outside the repo?" — vendored deps, OpenSSL, GoogleTest submodule, runtime binaries (`curl`, `ssh`), provider auth. |
| [`review_notes.md`](review_notes.md) | Documentation gaps, source/doc mismatches, and follow-ups identified during this generation pass. |

## Topic → file routing

Use this table to pick the right file before searching.

| Question/topic | Primary file | Secondary |
|---|---|---|
| "How do I build / run tests?" | `workflows.md` (§7) | `codebase_info.md` (CMake options) |
| "Which CMake flags exist?" | `codebase_info.md` | `dependencies.md` |
| "How do I create a client / pick a provider?" | `interfaces.md` (`MakeClient`) | `architecture.md` (client family) |
| "What's in the config JSON?" | `data_models.md` (config schema) | `interfaces.md` (`ConfigBuilder`) |
| "How do tools / function calling work?" | `interfaces.md` (`FunctionBuilder`, `FunctionTable`) | `architecture.md` (tool flow), `workflows.md` (tool turn) |
| "How is MCP wired up?" | `components.md` (MCP), `interfaces.md` (`MCPClient`) | `workflows.md` (MCP turn) |
| "What does the streaming callback look like?" | `interfaces.md` (`OnResponseCallback`, `Reason`) | `workflows.md` (chat turn) |
| "How is concurrency handled?" | `architecture.md` (concurrency model) | `components.md` (`Locker`, `attributes.hpp`, `ThreadNotifier`) |
| "How does the CLI demo work?" | `workflows.md` (§5) | `components.md` (CLI section) |
| "What providers are supported, with what auth?" | `dependencies.md` (Provider runtime) | `data_models.md` (Endpoint hierarchy) |
| "What's the cost model?" | `data_models.md` (Pricing/Usage) | `interfaces.md` (pricing accessors) |
| "Which tests exist and what do they cover?" | `components.md` (Tests) | `codebase_info.md` (test list) |
| "Known gaps / inconsistencies" | `review_notes.md` | — |

## File-by-file summaries

### `codebase_info.md`
Repo identity (project name, targets, language), top-level directory map, CMake options table, CI matrix summary, lint/format config, vendored dependency inventory, the test binary list, and "what is **not** in this repo".

### `architecture.md`
- High-level dependency diagram from your application down through the client class hierarchy and into the transport.
- Layered diagram showing how foundation utilities, core types, response parsers, clients, and the MCP library compose.
- Class diagram of `ClientBase → OllamaClient → {ClaudeClient, OpenAIClient, OpenAIMessagesClient}` showing the override pattern.
- Concurrency model: `Locker<T>`, `ThreadNotifier<Value>`, `attributes.hpp` annotations, atomic interrupt flag.
- Transport layer (`ITransport` via `httplib` or `curl`).
- Configuration model and tool-calling shape, including a sequence diagram of a tool-calling turn.

### `components.md`
A module-by-module catalogue. Each entry names the file(s), the public types, and a one-paragraph description. Sections: Foundation (`logger`, `common`, `helpers`, `attributes`, `thread_notifier`, `Process`, `Curl`), Core types (`assistantlib.hpp`, `tool.hpp`), Configuration (`config`, `EnvExpander`), Function calling (`function`), MCP (`mcp.hpp`, `cpp-mcp/`), Provider clients, Response parsers, CLI demo, Tests.

### `interfaces.md`
The contract surface: `MakeClient` factory, full `ClientBase` API table (lifecycle, chat, models, state, tools, pricing/usage), `Config`/`ConfigBuilder`, `FunctionBuilder` fluent API and per-endpoint serialisation rules, `FunctionTable` operations, `MCPClient` constructors and methods, `Logger` API, transport selection. Includes an `OnResponseCallback` `Reason` enum table.

### `data_models.md`
- Annotated example configuration JSON with constraints and default values.
- `Endpoint` and pre-built endpoint subclass diagram.
- MCP server configuration class diagram (`MCPServerConfig`, `StdioParams`, `SseParams`, `SSHLogin`).
- Message/request/response shapes from `assistantlib.hpp`.
- `FunctionCall`, `FunctionResult`, `CanInvokeToolResult`, `Param` definitions.
- `History` state machine.
- `Pricing`, `Usage`, `TokenUsageStats` field definitions and helper methods.
- Bitflag enums (`ModelCapabilities`, `ChatOptions`) and `CachePolicy` semantics.

### `workflows.md`
Eight numbered runtime flows with Mermaid sequence and flowcharts:
1. Application startup (`MakeClient`).
2. Single chat turn without tools.
3. Tool-calling turn with human-in-the-loop.
4. MCP-backed tool turn.
5. CLI startup and REPL (with command-line flags table, slash command table, and built-in tools table).
6. Max-token continuation strategy.
7. Local build and test commands.
8. CI flow per platform.

### `dependencies.md`
- Dependency map diagram.
- Vendored single-header inventory.
- Git submodules.
- CMake-discovered dependencies (OpenSSL, Threads, Win32 system libs).
- Runtime-only binaries (`curl`, `ssh`, MCP server commands).
- Provider runtime dependencies (auth headers, base URLs).
- Compiler/toolchain matrix from CI.
- Explicit list of "what we do not depend on".

### `review_notes.md`
Inconsistencies found between docs and source, gaps where source was abbreviated or unread, and recommended follow-ups.

## Example queries this knowledge base should answer well

- "Where does the JSON config get validated?" → `interfaces.md` and `components.md` point to `assistant/config.cpp` (`ConfigBuilder::FromContent`).
- "Why are there so many response parsers?" → `architecture.md` (one parser per provider wire format) and `components.md` (parser table).
- "How do I add a tool that needs operator approval?" → `interfaces.md` (`FunctionBuilder::SetHumanInTheLoopCallback`) plus `workflows.md` (HITL tool flow).
- "Can I run an MCP server on another machine?" → `interfaces.md` (`MCPClient(SSHLogin, args, env)`) plus `workflows.md` (MCP turn).
- "What overrides the default streaming behaviour?" → `components.md` (`OpenAIClient::IsStreaming` returns `true`) and `interfaces.md` (`IsStreaming()` is `virtual`).
- "How does the library handle being interrupted mid-stream?" → `architecture.md` (interrupt flag) and `interfaces.md` (`Interrupt()` semantics).
