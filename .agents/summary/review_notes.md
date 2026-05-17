# Review notes

<!-- meta:purpose=consistency check, gaps, follow-ups -->
<!-- meta:audience=ai-assistants,maintainers -->

This page lists inconsistencies discovered between source and existing documentation, places where this knowledge base abbreviated or skipped detail, and recommended follow-ups. It was produced by the `codebase-summary` SOP run that generated the rest of the files in this directory.

## Source ↔ docs inconsistencies

### README mentions a non-existent directory

`README.md` (in the "Project Layout" section) lists `examples/ - sample configuration and usage artifacts`. There is no `examples/` directory in the repository. Sample configurations live in build directories (`.build-release/config.json`, `config-with-mcp.json`, `config-python-mcp.json`) and in test fixtures.

**Fix candidate:** either add a real `examples/` directory and move the sample configs into it, or strike that line from the README.

### CLI demo helpers duplicated between two files

Several interactive helpers (`ReadYesOrNoFromUser`, `GetTextFromUser`, `GetChoiceFromUser`, `ReadFileContent`, `CreateNewFile`, `CreateDirectoryForFile`) are defined as `inline` functions in **both** `assistant/helpers.hpp` and `cli/utils.hpp`. As long as the CLI translation unit only includes one of them, ODR is preserved, but maintainers updating one copy will silently miss the other.

**Fix candidate:** delete the duplicated definitions from `cli/utils.hpp` and rely on `assistant/helpers.hpp`.

### Windows workflow YAML naming

`.github/workflows/windows.yml` declares `name: msys2` (and the path filters key on `.github/workflows/msys2.yml`, not `windows.yml`). The internal job name is `msys2` and the workflow file is `windows.yml`. This means GitHub UI shows the run as "msys2", and any path-filter prediction someone does by reading the file will be slightly wrong (the file watcher condition would not match the file's actual name).

**Fix candidate:** rename the file to `msys2.yml` or update the path-filter to `'.github/workflows/windows.yml'`.

### `OllamaCloudEndpoint` and `OllamaLocalEndpoint` URLs swapped

In `assistant/config.hpp`:

- `OllamaLocalEndpoint::OllamaLocalEndpoint() { url_ = kEndpointOllamaCloud; ... }`
- `OllamaCloudEndpoint::OllamaCloudEndpoint() { url_ = kEndpointOllamaLocal; headers_ = kDefaultOllamaHeaders; ... }`

Where `kEndpointOllamaLocal = "http://127.0.0.1:11434"` and `kEndpointOllamaCloud = "https://ollama.com"`. These two pre-built endpoints have their default URLs **swapped** relative to their names. Any consumer that relies on `OllamaLocalEndpoint{}` getting `127.0.0.1:11434` will instead silently target `ollama.com`.

**Fix candidate:** swap the assignments so the defaults match the class names.

### CMake convention name mismatch

`CMakeLists.txt` sets `set(OLLAMLIB_ROOT ${CMAKE_CURRENT_LIST_DIR})` (note: `OLLAMLIB`, no `LIB`). This variable is referenced by `assistant/CMakeLists.txt` and `assistant/cpp-mcp/CMakeLists.txt` for include directories. The library itself is now named `assistantlib`, so the CMake variable is a leftover from when the project was named "ollam(a)lib". It still works, but it is a maintenance hazard.

**Fix candidate:** rename to `ASSISTANTLIB_ROOT` (or use `${CMAKE_SOURCE_DIR}` directly).

### Unreachable startup loop in CLI demo

`cli/main.cpp` contains a `while (false) { ... }` block intended to wait for the configured server to become reachable (calling `cli->IsRunning()`). Because the loop condition is hard-coded `false`, the wait is never executed; the demo continues immediately and lets the first chat call surface any connection error.

**Fix candidate:** change to `while (true)` (or a bounded retry loop), or remove the dead code.

### `MakeClient` ignores `Curl` transport selection

`MakeClient` calls `client->ApplyConfig(&conf.value())` after constructing the client, but the constructor for each concrete client takes only an `Endpoint`. The endpoint's `transport_` field is honoured by `ApplyConfig` only insofar as `OllamaClient::CreateClient()` reads `m_transport_type`, which `ApplyConfig` is expected to set. If a consumer uses one of the convenience constructors (`OllamaClient(OllamaLocalEndpoint{})`) without going through `ApplyConfig`, the curl transport selection in the `Endpoint` is silently ignored.

**Fix candidate:** have the client constructors copy `Endpoint::transport_` into `m_transport_type` directly.

## Documented but not exhaustively verified

The following were inferred from headers, the umbrella `assistantlib.hpp`, and the CLI demo. Source-level confirmation is recommended if a consumer depends on exact behaviour:

- The exact JSON shape of streaming events for each provider (parsers handle these but the wire-level details are not enumerated in this knowledge base).
- Behaviour of `CachePolicy::kAuto` — currently treated as a marker; whether any provider currently honours it differently from `kNone` is not documented.
- The behaviour of `History::ShrinkToFit` relative to tool-call sequences (a `tool_calls` request and its `tool_result` response should not be split during shrink, but the current implementation simply trims from the front).
- The exact shape returned by `ClientBase::GetModelInfo(model)` differs by provider; the knowledge base treats it as `optional<json>` without documenting per-provider keys.

## Coverage gaps

- **No automated documentation refresh.** This file and its siblings are produced by running the `codebase-summary` SOP. No CI step regenerates or validates them, so they will drift unless refreshed on a schedule or by a pre-commit hook.
- **No public API stability statement.** Headers carry no `[[deprecated]]` markers, no semantic-versioning policy, and no `<package>Config.cmake` export. Downstream consumers should treat the API as evolving.
- **No security guidance.** The configuration format encourages `${VAR}` for secrets, but there is no explicit policy on logging headers, redacting tokens in `OLOG_DEBUG()`, or handling MCP server output that contains secrets. Worth adding a short security note in the README.
- **No MSVC support.** The compile-options block in the top-level `CMakeLists.txt` only adjusts flags for Clang/AppleClang. MSVC consumers will get the default warning set with no thread-safety analysis.

## Topic areas not yet covered in this knowledge base

These are subjects an AI assistant might be asked about that this generation pass did not write up in detail. They are good candidates for a follow-up pass:

- The exact request/response examples for each provider (could ship as a `wire_examples.md` derived from test fixtures).
- The MCP server side of `cpp-mcp/` (`mcp_server.h`, `mcp_resource.h`, `mcp_thread_pool.h`) — this knowledge base focuses on the **client** side because that is what `assistantlib` exposes externally.
- A stress / benchmark plan — the codebase has no benchmarks; if performance becomes a question, instrumentation hooks (in `Logger` and `Curl`) are the only data sources.

## Recommendations to the user

1. Decide whether the documented inconsistencies (`OllamaCloudEndpoint`/`OllamaLocalEndpoint` URL swap, `OLLAMLIB_ROOT` naming, dead `while (false)` loop, README's missing `examples/` directory, `cli/utils.hpp` duplication) are bugs to fix or expected. Those that are bugs can be addressed in small focused commits.
2. Add a CI check that runs `clang-format --dry-run --Werror` on every push so the existing `.clang-format` file is enforced (no such check is currently present).
3. Consider adding a CI step that validates the JSON examples in this knowledge base parse via `ConfigBuilder::FromContent`, so the docs cannot drift from the parser.
4. If automation is desired, schedule a periodic regeneration of `.agents/summary/` (e.g. via a manual workflow_dispatch) and review the diff like a PR.
