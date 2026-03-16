# Assistant Library - Modern C++ Agent Framework

A powerful, production-grade C++ library for building AI-powered agents that seamlessly interact with multiple AI model providers. The library provides a unified, provider-agnostic interface for creating intelligent agents with support for function calling, streaming responses, and advanced agent orchestration patterns.

## Table of Contents

1. [Overview](#overview)
2. [Key Features](#key-features)
3. [Architecture](#architecture)
4. [Getting Started](#getting-started)
5. [Core Components](#core-components)
6. [API Reference](#api-reference)
7. [Advanced Topics](#advanced-topics)
8. [Configuration](#configuration)
9. [Building](#building)
10. [Examples](#examples)

## Overview

The Assistant Library is designed to abstract away provider-specific API details, allowing developers to focus on building intelligent agents without worrying about which AI backend they're using. Whether you're using local models via Ollama, cloud-based Claude APIs, or OpenAI endpoints, the library provides a consistent interface.

### Supported Providers

- **Anthropic Claude** - Cloud-based API
- **OpenAI** - GPT models via API
- **Ollama** - Local model inference

### Design Philosophy

- **Unified Interface**: Single API for all providers
- **Zero Provider Lock-in**: Switch providers by changing configuration
- **Type-Safe**: Leverages modern C++20 features
- **Thread-Safe**: Built with concurrency in mind
- **Extensible**: Plugin-based architecture for custom tools/agents

## Key Features

### 1. **Multi-Provider Support**
Work with different AI models and providers interchangeably without changing application code. Simply update the configuration.

### 2. **Function/Tool Calling**
- Native support for AI-called tools/functions
- Automatic tool invocation and result handling
- Tool registry management
- Support for both local and remote tools via MCP (Model Context Protocol)

### 3. **Streaming Responses**
- Real-time response streaming as generated
- Extended thinking/reasoning display
- Progress callbacks with detailed status information
- Low-latency response delivery

### 4. **Conversation Management**
- Automatic conversation history tracking
- Configurable history window size
- Conversation state management
- System message support for agent behavior guidance

### 5. **Advanced Caching**
- Static content caching (supported by Claude)
- Automatic caching management
- Cache policy configuration
- Token usage optimization

### 6. **Cost Tracking & Analytics**
- Per-request cost calculation
- Aggregated usage tracking
- Token usage monitoring
- Built-in pricing tables for major providers

### 7. **MCP Integration**
- Native Model Context Protocol support
- Both STDIO and SSE transport mechanisms
- Remote MCP server support via SSH
- Dynamic tool registration from MCP servers

### 8. **Thread Safety**
- Atomic operations for state management
- Mutex-protected shared resources
- Thread-safe request queuing
- Safe concurrent access patterns

### 9. **Human-in-the-Loop**
- Optional approval callbacks before tool execution
- Fine-grained control over agent actions
- User override capabilities

### 10. **Extended Reasoning**
- Support for extended thinking models
- Thinking state tracking
- Separate thinking/output display

## Architecture

### Core Design Pattern

```
┌─────────────────────────────────────────────┐
│         Application Code                    │
└─────────────────────────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────┐
│         ClientBase Interface                │
│  (Chat, Stream, Tools, Config, History)    │
└─────────────────────────────────────────────┘
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
    OllamaClient  ClaudeClient  OpenAIClient
   (Implementation) (Implementation) (Implementation)
        │           │           │
        └───────────┼───────────┘
                    │
        ┌───────────┼───────────┐
        ▼           ▼           ▼
    STDIO        HTTP/REST     API-Specific
   Requests    Connections     Serialization
```

### Key Components

#### 1. **ClientBase**
Abstract base class defining the unified agent interface:
- Chat management
- Model interaction
- Function table management
- History management
- Configuration application

#### 2. **Specific Clients**
Provider-specific implementations:
- `OllamaClient` - Local model inference
- `ClaudeClient` - Anthropic Claude API
- `OpenAIClient` - OpenAI models

#### 3. **FunctionTable**
Registry for all available tools:
- Thread-safe function management
- Dynamic function registration
- Format conversion for different providers
- Function lookup and invocation

#### 4. **ChatRequest**
Manages individual chat requests:
- Request metadata
- Callback handling
- Tool invocation tracking
- Finalization logic

#### 5. **Configuration System**
JSON-based configuration:
- Endpoint specification
- MCP server registration
- Timeout settings
- Logging configuration
- History window size

## Getting Started

### Minimal Example

```cpp
#include "assistant/assistant.hpp"
#include <iostream>

using namespace assistant;

int main() {
    // Create client from configuration
    auto cli_opt = MakeClient("config.json");
    if (!cli_opt.has_value()) {
        std::cerr << "Failed to create client" << std::endl;
        return 1;
    }

    auto client = cli_opt.value();

    // Send a message
    client->Chat(
        "Hello, what is 2+2?",
        [](const std::string& text, Reason reason, bool thinking) -> bool {
            switch (reason) {
                case Reason::kPartialResult:
                    std::cout << text;
                    std::cout.flush();
                    break;
                case Reason::kDone:
                    std::cout << std::endl;
                    break;
                case Reason::kFatalError:
                    std::cerr << "Error: " << text << std::endl;
                    break;
            }
            return true; // Continue processing
        },
        ChatOptions::kDefault
    );

    return 0;
}
```

### Configuration File

```json
{
  "endpoint": {
    "type": "anthropic",
    "url": "https://api.anthropic.com",
    "model": "claude-3-5-sonnet-20241022",
    "headers": {
      "x-api-key": "${ANTHROPIC_API_KEY}"
    },
    "max_tokens": 4096
  },
  "log_level": "error",
  "window_size": 50
}
```

## Core Components

### 1. ClientBase - The Agent Interface

The foundation of the library. All agents inherit from or use `ClientBase`.

#### Primary Methods

```cpp
// Chat - The main interaction method
virtual void Chat(std::string msg,
                  OnResponseCallback cb,
                  ChatOptions chat_options) = 0;

// Server status
virtual bool IsRunning() = 0;

// Model listing
virtual std::vector<std::string> List() = 0;

// Model information
virtual std::optional<json> GetModelInfo(const std::string& model) = 0;
virtual std::optional<ModelCapabilities> GetModelCapabilities(
    const std::string& model) = 0;
```

#### Callback Definition

```cpp
using OnResponseCallback = std::function<bool(
    const std::string& text,    // Response content
    Reason call_reason,         // Why callback invoked
    bool thinking               // In thinking mode?
)>;
```

#### Response Reasons

- `kPartialResult` - Streaming chunk of response
- `kDone` - Request completed successfully
- `kFatalError` - Unrecoverable error
- `kCancelled` - User cancelled the request
- `kLogNotice` - Informational log message
- `kLogDebug` - Debug-level log message
- `kRequestCost` - Cost/usage information

### 2. Function/Tool System

#### Registering Functions

```cpp
using FunctionBuilder = assistant::FunctionBuilder;

// Create and register a function
client->GetFunctionTable().Add(
    FunctionBuilder("GetWeather")
        .SetDescription("Get the weather for a location")
        .AddRequiredParam("location", "City name", "string")
        .AddOptionalParam("unit", "Temperature unit (C/F)", "string")
        .SetCallback([](const json& args) -> FunctionResult {
            // Extract arguments
            auto location = assistant::GetFunctionArg<std::string>(
                args, "location");
            if (!location.has_value()) {
                return FunctionResult{
                    .isError = true,
                    .text = "Missing location parameter"
                };
            }

            // Implement logic
            std::string result = "Weather for " + location.value();

            return FunctionResult{
                .isError = false,
                .text = result
            };
        })
        .Build()
);
```

#### Function Result Structure

```cpp
struct FunctionResult {
    bool isError{false};          // true if error occurred
    std::string text;              // Result or error message
};
```

#### Macro Helper for Safe Argument Extraction

```cpp
#define ASSIGN_FUNC_ARG_OR_RETURN(var, expr)                    \
  if (!expr.has_value()) {                                      \
    return assistant::FunctionResult{                           \
        .isError = true,                                        \
        .text = "Missing mandatory argument"                    \
    };                                                          \
  }                                                             \
  var = expr.value();
```

### 3. Configuration System

#### Config Builder

```cpp
// Load from file
auto result = ConfigBuilder::FromFile("config.json");
if (!result.ok()) {
    std::cerr << "Config error: " << result.errmsg_ << std::endl;
}
auto config = result.config_.value();

// Load from string
auto result = ConfigBuilder::FromContent(json_string);
```

#### Configuration Options

```json
{
  "endpoint": {
    "type": "anthropic",  // or "ollama", "openai"
    "url": "https://api.anthropic.com",
    "model": "claude-3-5-sonnet-20241022",
    "headers": {
      "x-api-key": "${ANTHROPIC_API_KEY}"
    },
    "max_tokens": 4096,
    "verify_server_ssl": true,
    "context_size": 32768
  },
  "log_level": "error",  // debug, info, warning, error
  "window_size": 50,     // Conversation history size
  "keep_alive": "5m",
  "stream": true,
  "timeout": {
    "connect_ms": 100,
    "read_ms": 10000,
    "write_ms": 10000
  },
  "mcp_servers": [
    {
      "name": "my_tools",
      "enabled": true,
      "stdio": {
        "command": ["/usr/bin/my-tool"],
        "env": {
          "API_KEY": "${API_KEY}"
        }
      }
    }
  ]
}
```

### 4. Chat Options

Control chat behavior with flags:

```cpp
enum class ChatOptions {
    kDefault = 0,           // All features enabled
    kNoTools = (1 << 0),   // Disable tool calls
    kNoHistory = (1 << 1), // Don't use conversation history
};

// Combine flags
auto options = ChatOptions::kNoTools | ChatOptions::kNoHistory;

client->Chat(prompt, callback, options);
```

### 5. History Management

```cpp
// Add system messages (always included)
client->AddSystemMessage("You are an expert C++ developer");
client->AddSystemMessage("Always be concise");

// Control history size
client->SetHistorySize(100);  // Keep last 100 messages

// Clear history
client->ClearHistoryMessages();

// Get current history
auto messages = client->GetHistory();
for (const auto& msg : messages) {
    std::cout << "[" << msg.role << "] " << msg.text << std::endl;
}

// Replace history
std::vector<Message> custom_history = { /* ... */ };
client->SetHistory(custom_history);
```

### 6. Model Capabilities

```cpp
enum class ModelCapabilities {
    kNone = 0,
    kThinking = (1 << 0),   // Extended reasoning
    kTools = (1 << 1),      // Function calling
    kCompletion = (1 << 2), // Text completion
    kInsert = (1 << 3),     // Text insertion
    kVision = (1 << 4),     // Image processing
};

// Check model capabilities
auto caps = client->GetModelCapabilities("claude-3-5-sonnet");
if (caps.has_value()) {
    bool can_think = IsFlagSet(caps.value(),
                               ModelCapabilities::kThinking);
}
```

### 7. Caching Policies

```cpp
enum class CachePolicy {
    kNone,    // No caching
    kAuto,    // Service decides
    kStatic,  // Cache static content
};

// Set caching policy
client->SetCachingPolicy(CachePolicy::kStatic);
```

### 8. Cost Tracking

```cpp
// Set pricing information
assistant::Pricing pricing{
    .input_tokens = 0.000003,
    .output_tokens = 0.000015,
    // ... other fields
};
client->SetPricing(pricing);

// After each request
auto usage = client->GetLastRequestUsage();
auto cost = client->GetLastRequestCost();
auto total = client->GetTotalCost();

// Reset cost tracking
client->ResetCost();
```

## API Reference

### ClientBase Public Interface

#### Chat & Interaction

```cpp
// Primary chat interface
virtual void Chat(std::string msg, OnResponseCallback cb,
                  ChatOptions chat_options) = 0;

// Check server availability
virtual bool IsRunning() = 0;

// List available models
virtual std::vector<std::string> List() = 0;
virtual json ListJSON() = 0;

// Get model information
virtual std::optional<json> GetModelInfo(const std::string& model) = 0;
virtual std::optional<ModelCapabilities> GetModelCapabilities(
    const std::string& model) = 0;
```

#### Lifecycle Management

```cpp
// Initialize client
virtual void Startup() { m_interrupt.store(false); }

// Cleanup and shutdown
virtual void Shutdown() {
    Interrupt();
    ClearMessageQueue();
    ClearSystemMessages();
    ClearHistoryMessages();
    ClearFunctionTable();
}

// Stop current operation
virtual void Interrupt() { m_interrupt.store(true); }
bool IsInterrupted() const;
```

#### Message Management

```cpp
// Add system instructions
void AddSystemMessage(const std::string& msg);
void ClearSystemMessages();

// History control
void SetHistorySize(size_t count);
size_t GetHistorySize() const;
void ClearHistoryMessages();
std::vector<Message> GetHistory() const;
void SetHistory(const std::vector<Message>& history);

// Message queue
void ClearMessageQueue();
```

#### Function/Tool Management

```cpp
FunctionTable& GetFunctionTable();
const FunctionTable& GetFunctionTable() const;
void ClearFunctionTable();

// Tool invocation callback
void SetTookInvokeCallback(OnToolInvokeCallback cb);
```

#### Configuration & Settings

```cpp
// Apply configuration
virtual void ApplyConfig(const assistant::Config* conf);

// Endpoint configuration
std::string GetUrl() const;
std::string GetModel() const;
EndpointKind GetEndpointKind() const;
std::unordered_map<std::string, std::string> GetHttpHeaders() const;

// Token limits
size_t GetMaxTokens() const;
void SetMaxTokens(size_t count);
size_t GetContextSize() const;

// Timeouts
ServerTimeout GetServerTimeoutSettings() const;

// Caching
CachePolicy GetCachingPolicy() const;
void SetCachingPolicy(CachePolicy policy);

// Transport
TransportType GetTransportType() const;
void SetTransportType(TransportType type);
```

#### Cost & Usage Tracking

```cpp
// Pricing information
std::optional<Pricing> GetPricing() const;
void SetPricing(const Pricing& cost);

// Cost tracking
double GetLastRequestCost() const;
double GetTotalCost() const;
void ResetCost();

// Usage tracking
std::optional<Usage> GetLastRequestUsage() const;
Usage GetAggregatedUsage() const;
```

### FunctionTable Interface

```cpp
// Register a function
void Add(std::shared_ptr<FunctionBase> f);

// Register MCP server
void AddMCPServer(std::shared_ptr<MCPClient> client);

// Call a function
FunctionResult Call(const FunctionCall& func_call) const;

// Function control
void EnableAll(bool b);
bool EnableFunction(const std::string& name, bool b);

// Utility
size_t GetFunctionsCount() const;
bool IsEmpty() const;
json ToJSON(EndpointKind kind, CachePolicy cache_policy) const;
void Clear();
```

### FunctionBuilder Fluent API

```cpp
FunctionBuilder("tool_name")
    .SetDescription("What this tool does")
    .AddRequiredParam("param1", "description", "type")
    .AddOptionalParam("param2", "description", "type")
    .SetCallback(implementation_function)
    .Build()
```

## Advanced Topics

### 1. Custom Tool Implementation

```cpp
assistant::FunctionResult MyCustomTool(const assistant::json& args) {
    // 1. Validate arguments
    if (args.size() != 2) {
        return FunctionResult{
            .isError = true,
            .text = "Expected 2 arguments"
        };
    }

    // 2. Extract with safe macro
    ASSIGN_FUNC_ARG_OR_RETURN(
        std::string input,
        assistant::GetFunctionArg<std::string>(args, "input"));

    // 3. Implement logic
    std::string result = ProcessInput(input);

    // 4. Return result
    return FunctionResult{
        .isError = false,
        .text = result
    };
}

// Register it
cli->GetFunctionTable().Add(
    FunctionBuilder("MyTool")
        .SetDescription("My custom tool")
        .AddRequiredParam("input", "input data", "string")
        .SetCallback(MyCustomTool)
        .Build()
);
```

### 2. Human-in-the-Loop Approval

```cpp
// Set approval callback
client->SetTookInvokeCallback([](const std::string& tool_name) -> bool {
    std::cout << "Allow tool execution: " << tool_name << "? [y/n] ";
    std::string answer;
    std::getline(std::cin, answer);
    return answer == "y" || answer == "yes";
});

// Now when the model calls a tool, user approval is requested first
```

### 3. Extended Thinking

For models that support extended reasoning:

```cpp
// Callback receives thinking flag
client->Chat(prompt,
    [](const std::string& text, Reason reason, bool thinking) -> bool {
        if (thinking) {
            // Display in different color/format
            std::cout << "[THINKING] " << text;
        } else {
            std::cout << text;
        }
        return true;
    },
    ChatOptions::kDefault);
```

### 4. MCP Server Integration

Configure MCP servers in JSON:

```json
{
  "mcp_servers": [
    {
      "name": "file_tools",
      "enabled": true,
      "stdio": {
        "command": ["/opt/mcp-file-tools"],
        "env": {
          "ALLOWED_PATHS": "/home/user/files"
        }
      }
    },
    {
      "name": "remote_tools",
      "enabled": true,
      "stdio": {
        "command": ["/opt/ssh-mcp-client"],
        "ssh_login": {
          "host": "example.com",
          "port": 22,
          "username": "user"
        }
      }
    }
  ]
}
```

### 5. Streaming Response Handling

```cpp
bool StreamingHandler(const std::string& chunk,
                      Reason reason,
                      bool thinking) -> bool {
    switch (reason) {
        case Reason::kPartialResult:
            // Display chunk immediately
            std::cout << chunk;
            std::cout.flush();  // Force display
            break;

        case Reason::kDone:
            std::cout << std::endl;
            break;

        case Reason::kRequestCost:
            // Display cost in gray or separate section
            std::cout << "[COST] " << chunk << std::endl;
            break;

        case Reason::kFatalError:
            std::cerr << "[ERROR] " << chunk << std::endl;
            return false;  // Stop processing

        default:
            break;
    }
    return true;  // Continue processing
}

client->Chat(prompt, StreamingHandler, ChatOptions::kDefault);
```

### 6. Thread Safety & Concurrency

The library is thread-safe for:
- Multiple Chat requests (queued internally)
- Function table modifications
- History access
- Configuration changes

```cpp
// Safe to call from multiple threads
std::thread t1([&client] {
    client->Chat("Query 1", callback, ChatOptions::kDefault);
});

std::thread t2([&client] {
    client->Chat("Query 2", callback, ChatOptions::kDefault);
});

t1.join();
t2.join();
```

### 7. History Window Management

The library automatically maintains a conversation window:

```cpp
// Set window size (e.g., keep last 50 messages)
client->SetHistorySize(50);

// History automatically shrinks when exceeding limit
// Oldest messages are removed first (FIFO)

// Messages are added after each chat request:
// 1. User message is added to history
// 2. Assistant response is added to history
// 3. Tool results are added to history
// 4. Messages are trimmed to fit window size
```

## Configuration

### Environment Variable Expansion

Configuration supports environment variable expansion:

```json
{
  "endpoint": {
    "headers": {
      "x-api-key": "${ANTHROPIC_API_KEY}",
      "authorization": "Bearer ${TOKEN}"
    }
  },
  "mcp_servers": [
    {
      "stdio": {
        "env": {
          "API_KEY": "${MY_API_KEY}",
          "DEBUG": "${DEBUG_MODE}"
        }
      }
    }
  ]
}
```

### Endpoint Types

#### Ollama (Local)
```json
{
  "endpoint": {
    "type": "ollama",
    "url": "http://localhost:11434",
    "model": "llama3.2"
  }
}
```

#### Anthropic Claude
```json
{
  "endpoint": {
    "type": "anthropic",
    "url": "https://api.anthropic.com",
    "model": "claude-3-5-sonnet-20241022",
    "headers": {
      "x-api-key": "${ANTHROPIC_API_KEY}"
    },
    "max_tokens": 4096
  }
}
```

#### OpenAI
```json
{
  "endpoint": {
    "type": "openai",
    "url": "https://api.openai.com",
    "model": "gpt-4",
    "headers": {
      "Authorization": "Bearer ${OPENAI_API_KEY}"
    }
  }
}
```

## Building

### Prerequisites

- C++20 or later
- CMake 3.10+
- OpenSSL (optional, for TLS)
- libcurl (for HTTP requests)

### Build Instructions

#### Debug Build
```bash
mkdir -p .build-debug
cd .build-debug
cmake .. -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON
make -j10
```

#### Release Build
```bash
mkdir -p .build-release
cd .build-release
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j10
```

### CMake Options

```bash
# Enable TLS support (default: ON)
cmake .. -DASSISTANTLIB_WITH_OPENSSL=ON

# Build example (default: ON)
cmake .. -DASSISTANTLIB_BUILD_EXAMPLE=ON

# Build tests (default: OFF)
cmake .. -DASSISTANTLIB_BUILD_TESTS=ON
```

### Running Tests

```bash
# Debug
.build-debug/tests/test_name

# Release
.build-release/tests/test_name
```

## Examples

See the `cli/main.cpp` example for a complete, production-ready interactive chat application demonstrating:

- Client creation and configuration
- Function registration and tool calling
- History management
- Cost tracking
- User approval for tool execution
- Streaming response handling
- Special command processing

### Key Patterns from Example

```cpp
// 1. Load configuration
auto result = assistant::ConfigBuilder::FromFile(config_file);
if (!result.ok()) {
    std::cerr << "Config error: " << result.errmsg_ << std::endl;
    return 1;
}

// 2. Create client
auto cli_opt = assistant::MakeClient(result.config_.value());
if (!cli_opt.has_value()) {
    std::cerr << "Failed to create client" << std::endl;
    return 1;
}

// 3. Configure client
auto client = cli_opt.value();
client->SetHistorySize(50);
client->AddSystemMessage("You are an expert assistant");

// 4. Register tools
client->GetFunctionTable().Add(
    FunctionBuilder("MyTool")
        .SetDescription("Tool description")
        .AddRequiredParam("param", "description", "string")
        .SetCallback(ToolImplementation)
        .Build()
);

// 5. Set approval callback
client->SetTookInvokeCallback(CanRunTool);

// 6. Wait for server
while (!client->IsRunning()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// 7. Chat
client->Chat(
    prompt,
    [](const std::string& text, Reason reason, bool thinking) -> bool {
        // Handle response
        return true;
    },
    ChatOptions::kDefault
);
```

## Error Handling

The library uses `std::optional` for fallible operations:

```cpp
// Configuration loading
auto result = ConfigBuilder::FromFile("config.json");
if (!result.ok()) {
    std::cerr << "Error: " << result.errmsg_ << std::endl;
}

// Client creation
auto client = MakeClient(result.config_);
if (!client.has_value()) {
    std::cerr << "Failed to create client" << std::endl;
}

// Model information
auto info = client->GetModelInfo("model-name");
if (!info.has_value()) {
    std::cerr << "Model not found" << std::endl;
}

// Function arguments
auto arg = GetFunctionArg<std::string>(args, "param");
if (!arg.has_value()) {
    return FunctionResult{.isError = true, .text = "Missing param"};
}
```

## Pricing Integration

Built-in pricing tables for major models:

```cpp
// Automatically find pricing
auto pricing = assistant::FindPricing("claude-3-5-sonnet-20241022");
if (pricing.has_value()) {
    client->SetPricing(pricing.value());
}

// Or add custom pricing
assistant::AddPricing("my-custom-model", {
    .input_tokens = 0.001,
    .output_tokens = 0.002
});
```

## Thread Safety Patterns

### Safe Concurrent Access

```cpp
// Locker<T> provides thread-safe access
Locker<Messages> history;

// Read access
history.with([](const Messages& msgs) {
    for (const auto& msg : msgs) {
        std::cout << msg << std::endl;
    }
});

// Write access
history.with_mut([](Messages& msgs) {
    msgs.push_back(new_message);
});

// Get copy
auto copy = history.get_value();
```

## Logging

Configure logging via:

```cpp
// Log level
assistant::SetLogLevel(assistant::LogLevel::kDebug);

// Log file
assistant::SetLogFile("agent.log");
```

Available levels: `kDebug`, `kInfo`, `kWarning`, `kError`

## Performance Considerations

1. **Streaming**: Responses are streamed to minimize perceived latency
2. **Caching**: Static content caching reduces redundant API calls
3. **History Window**: Limited history window prevents memory bloat
4. **Async Operations**: Request queuing enables non-blocking usage
5. **Thread Safety**: Lock-free atomic operations where possible

## Use Cases

1. **Code Analysis Agent** - Analyze and explain code
2. **Document Generation** - Automated document creation
3. **Technical Support Bot** - AI-powered customer service
4. **Data Analysis Assistant** - Process and analyze data
5. **Writing Assistant** - Content generation and editing
6. **Research Agent** - Literature search and summarization
7. **DevOps Assistant** - Infrastructure and deployment help
8. **Educational Tutor** - Interactive learning system

## License

See LICENSE file for details.

## Contributing

Contributions are welcome! Please ensure:
- Code follows existing style (C++20)
- All tests pass
- Documentation is updated
- Changes are well-tested

## Support

For issues, feature requests, or questions:
1. Check existing documentation
2. Review example code in `cli/main.cpp`
3. Check unit tests in `tests/` directory
4. Open an issue with detailed information
