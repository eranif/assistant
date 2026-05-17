# Interfaces

<!-- meta:purpose=public APIs, callbacks, integration points -->
<!-- meta:audience=ai-assistants,library-consumers -->

## Top-level factory

```cpp
// In: #include "assistant/assistant.hpp"
namespace assistant {
  std::optional<std::shared_ptr<ClientBase>> MakeClient(std::optional<Config>);
  std::optional<std::shared_ptr<ClientBase>> MakeClient(const std::string& json_content);
  std::optional<std::shared_ptr<ClientBase>> MakeClient(const std::filesystem::path&);
}
```

`MakeClient` selects the concrete client class by `Config::GetEndpoint()->type_` (an `EndpointKind`):

| `EndpointKind` value | Concrete client |
|---|---|
| `ollama` | `OllamaClient` |
| `anthropic` | `ClaudeClient` |
| `openai` | `OpenAIClient` (`/v1/responses`) |
| `moonshotai` | `OpenAIMessagesClient` (`/v1/chat/completions`) |

After construction, the factory calls `client->ApplyConfig(&conf)` so MCP servers, log level, timeouts, and other config-derived state are wired up before the caller receives the pointer.

## `ClientBase` — runtime API

Defined in `assistant/client/client_base.hpp`. The methods below are stable surface area (subclasses override but do not remove). Methods marked **(virtual)** are provider-specific extension points; **(impl)** are concrete on `ClientBase` itself.

### Lifecycle

| Method | Purpose |
|---|---|
| `void Startup()` (virtual, default impl clears interrupt) | Optional — used by some clients to spawn a worker thread. |
| `void Shutdown()` (virtual) | Interrupts, then clears the queue, system messages, history, and function table. Called from the destructor. |
| `void Interrupt()` (virtual) | Sets `m_interrupt = true`; concrete clients also abort the in-flight transport. |
| `bool IsInterrupted() const` (impl) | Reads the atomic flag. |

### Chat

```cpp
virtual void Chat(std::string msg, OnResponseCallback cb, ChatOptions opts) = 0;
virtual void CreateAndPushChatRequest(
    std::optional<assistant::message> msg, OnResponseCallback cb,
    std::string model, ChatOptions chat_options,
    std::shared_ptr<ChatRequestFinaliser> finaliser) = 0;
virtual void AddToolsResult(
    std::vector<std::pair<FunctionCall, FunctionResult>> result) = 0;
```

`OnResponseCallback` signature:

```cpp
using OnResponseCallback = std::function<bool(
    const std::string& text, Reason call_reason, bool thinking)>;
```

Returning `false` from the callback signals "stop processing further chunks for this request". The CLI demo always returns `true`.

`Reason` values delivered to the callback (from `common.hpp`):

| Reason | Meaning |
|---|---|
| `kPartialResult` | Streaming text (or thinking) chunk |
| `kDone` | Final chunk; turn complete |
| `kFatalError` | Non-recoverable error (transport, parse, etc.) |
| `kCancelled` | Aborted by `Interrupt()` |
| `kLogNotice` / `kLogDebug` | Non-content informational messages |
| `kRequestCost` | Per-request cost summary string |
| `kToolDenied` / `kToolAllowed` | Result of human-in-the-loop tool gate |
| `kMaxTokensReached` | Generation hit `max_tokens`; caller may continue |
| `kServerCompaction` | Server-side context compaction notice |

### Models and capabilities

```cpp
virtual bool IsRunning() = 0;
virtual std::vector<std::string> List() = 0;
virtual json ListJSON() = 0;
virtual std::optional<json> GetModelInfo(const std::string& model) = 0;
virtual std::optional<ModelCapabilities> GetModelCapabilities(const std::string& model) = 0;
```

`ModelCapabilities` is a bitflag enum — test with `assistant::IsFlagSet(caps, ModelCapabilities::kTools)` etc.

### State accessors

| Method | Notes |
|---|---|
| `ApplyConfig(const Config*)` | Loads endpoint, MCP servers, log level, timeouts, compaction threshold |
| `AddSystemMessage(std::string)` / `ClearSystemMessages()` | System messages are sent on every request |
| `ClearHistoryMessages()` / `GetHistory()` / `SetHistory(messages)` | Conversation history (active history honours `SwapToTempHistory`) |
| `ClearMessageQueue()` / `ClearFunctionTable()` | Drain the queue / drop tools |
| `GetUrl()` / `GetHttpHeaders()` / `GetEndpointKind()` / `SetEndpointKind(kind)` | Endpoint metadata |
| `GetMaxTokens()` / `SetMaxTokens(n)` / `GetContextSize()` | Per-endpoint generation limits |
| `SetEndpoint(Endpoint)` | Replace the active endpoint wholesale |
| `GetModel()` | Currently configured model id |
| `SetTransportType(t)` / `GetTransportType()` | Pick `httplib` or `curl` |
| `IsStreaming()` (virtual) | OpenAI clients force `true` |
| `SetCachingPolicy(p)` / `GetCachingPolicy()` | `kNone` \| `kAuto` \| `kStatic` |

### Tools

```cpp
const FunctionTable& GetFunctionTable() const;
FunctionTable& GetFunctionTable();
void SetToolInvokeCallback(OnToolInvokeCallback);
virtual void InvokeTools(std::shared_ptr<ChatRequest>);
```

`SetToolInvokeCallback` registers a **client-wide** approval gate. Per-tool approval can be set on `InProcessFunction` via `FunctionBuilder::SetHumanInTheLoopCallback(...)` and overrides the client-wide one for that tool.

### Pricing and usage

| Method | Notes |
|---|---|
| `SetPricing(Pricing)` / `GetPricing()` | Per-token USD rates |
| `GetLastRequestCost()` / `GetTotalCost()` / `ResetCost()` | Aggregated USD cost |
| `GetLastRequestUsage()` / `GetAggregatedUsage()` | `Usage` struct (input/output/cache tokens) |
| `GetTokenUsageStats()` / `GetAggregatedTokenUsageStats()` | Returns `TokenUsageStats` (percentages, remaining tokens) |
| `IsNearContextLimit(threshold_percentage = 80.0)` | Convenience predicate |

The pricing table for known models is hard-coded in `common.hpp` (`PRICING_TABLE`) and looked up by `assistant::FindPricing(model_name)`. Custom prices can be inserted at runtime via `assistant::AddPricing(...)`.

## `Config` and `ConfigBuilder`

```cpp
// In: #include "assistant/config.hpp"
class ConfigBuilder {
 public:
  static ParseResult FromFile(const std::string& path,
                              std::optional<EnvMap> env = std::nullopt);
  static ParseResult FromContent(const std::string& json_content,
                                 std::optional<EnvMap> env = std::nullopt);
};

struct ParseResult {
  std::string errmsg_;
  std::optional<Config> config_;
  bool ok() const;
};
```

`Config` exposes read accessors only (`GetEndpoint()`, `GetEndpoints()`, `GetServers()`, `GetLogLevel()`, `GetKeepAlive()`, `IsStream()`, `GetServerTimeoutSettings()`). All mutation happens inside `ConfigBuilder`, which is `friend`-ed.

`EnvMap = std::unordered_map<std::string, std::string>` — pass an explicit map to override or augment process environment for `${VAR}` expansion.

## `FunctionBuilder` — fluent tool registration

```cpp
auto fn = FunctionBuilder("my_tool")
    .SetDescription("...")
    .AddRequiredParam("path", "the path", "string")
    .AddOptionalParam("count", "optional count", "number")
    .AddMinMaxValidation("count", 1, 100)
    .AddStringEnumValidation("mode", {"read", "write"})
    .SetCallback([](const json& args) -> FunctionResult {
        // ...
        return FunctionResult{.text = "ok"};
    })
    .SetHumanInTheLoopCallback(
        [](const std::string& tool_name, json args) {
            return CanInvokeToolResult{.can_invoke = true};
        })
    .Build();

client->GetFunctionTable().Add(fn);
```

`Param` types accepted in `AddRequiredParam`/`AddOptionalParam`: `"string"`, `"number"`, `"integer"`, `"boolean"`, `"array"`, `"object"`. Validation:

- `AddMinMaxValidation(name, min, max)` adds JSON Schema `minimum`/`maximum` (numeric).
- `AddStringEnumValidation(name, values)` adds JSON Schema `enum`.

Per-endpoint serialisation:

- **Ollama / Moonshot**: nested `{"type":"function","function":{"name":...,"parameters":{...}}}` with a `required` array.
- **OpenAI** (`/v1/responses`): flat `{"type":"function","name":...,"strict":true,"parameters":{...,"additionalProperties":false}}`. **All** parameters are listed in `required` (OpenAI strict mode); optional types are widened to `["<type>","null"]`.
- **Anthropic**: `{"name":...,"input_schema":{...}}` plus `cache_control: {type:"ephemeral"}` on the last tool when `CachePolicy::kStatic`.

### Inside `FunctionTable`

Methods of interest beyond `Add`:

| Method | Behaviour |
|---|---|
| `AddMCPServer(shared_ptr<MCPClient>)` | Initialises the MCP client and registers each tool as an `ExternalFunction` |
| `ReloadMCPServers(const Config*)` | Re-reads `Config::GetServers()` and rebuilds external tools |
| `Merge(const FunctionTable&)` | Adopt entries from another table |
| `EnableAll(bool)` / `EnableFunction(name, bool)` | Soft enable/disable without removing |
| `CanRunTool(name, args) → optional<CanInvokeToolResult>` | Returns `nullopt` if the tool has no approval callback |
| `Call(FunctionCall) → FunctionResult` | Dispatches by name; catches exceptions into `isError = true` |
| `ToJSON(EndpointKind, CachePolicy) → json` | Wire schema for the active endpoint |
| `GetFunctionsCount()` / `IsEmpty()` | Counts only **enabled** functions |

## MCP integration

```cpp
// In: #include "assistant/mcp.hpp"
namespace assistant {

struct SSHLogin {
  std::string ssh_program{"ssh"};
  std::string ssh_key;     // -i <key>
  std::string user;        // -l <user>
  std::string hostname{"127.0.0.1"};
  int port{22};
};

class MCPClient {
 public:
  // local stdio
  MCPClient(const std::vector<std::string>& args,
            std::optional<json> env = {});
  // SSE
  MCPClient(const std::string& base_url,
            const std::string& sse_endpoint = "/sse",
            const std::string& auth_token = {},
            const std::vector<std::pair<std::string, std::string>>& headers = {});
  // remote stdio over SSH
  MCPClient(const SSHLogin& ssh_login,
            const std::vector<std::string>& args,
            std::optional<json> env = {});

  bool Initialise();
  bool IsRemote() const;
  const std::vector<mcp::tool>& GetTools() const;
  FunctionResult Call(const mcp::tool& t, const json& args) const;
  std::vector<std::shared_ptr<FunctionBase>> GetFunctions() const;
};

}  // namespace assistant
```

The recommended path is to declare MCP servers in the configuration file and let `ApplyConfig` instantiate them. Direct construction is exposed for embedders that want to manage MCP lifecycles manually.

## Logging

```cpp
// In: #include "assistant/logger.hpp"
assistant::SetLogLevel(assistant::LogLevel::kInfo);
assistant::SetLogFile("/tmp/assistant.log");
assistant::SetLogSink([](assistant::LogLevel lvl, std::string line) {
    // forward to your application log
});

OLOG_INFO()  << "structured " << "stream " << 42;
OLOG_DEBUG() << "...";
OLOG_ERROR() << "...";
```

`Logger::FromString("trace"|"debug"|"info"|"warn"|"error")` parses a config string. Unknown levels default to `kInfo`.

## Process and Curl utilities

Both are part of the public include surface (`assistant/Process.hpp`, `assistant/Curl.hpp`) — the curl transport is selectable via `client->SetTransportType(TransportType::curl)` after construction or by setting `transport` in the configuration.

## Stability and ABI notes

- All public headers live under `assistant/` and rely on the vendored `assistant/common/` headers; consumers must add the repo root to their include path.
- The library installs no headers and produces no CMake export. Integrate by `add_subdirectory()` or by replicating the file list in a parent build.
- The library is C++20; consumers must enable `-std=c++20` (or the MSVC equivalent).
