# Assistant Library

A modern C++ library for seamlessly interacting with AI language models through a unified interface. The library supports both local models (via Ollama) and cloud-based models (Anthropic's Claude).

## Features

- **Unified API**: Single interface for multiple AI model providers (Ollama, Claude)
- **Function Calling**: Built-in support for tool/function calling with automatic invocation
- **Streaming Responses**: Real-time streaming of model responses
- **Thread-Safe**: Designed with thread safety in mind using mutex and atomic operations
- **Session Management**: Maintain conversation history with configurable window size
- **Configuration-Based**: JSON-based configuration for easy model and endpoint management
- **Model Capabilities Detection**: Automatic detection and handling of model-specific features (thinking, tools, vision, etc.)
- **Interactive Chat**: Built-in support for interactive command-line chat sessions
- **Flexible Logging**: Configurable logging levels and output destinations

## Architecture

### Core Components

- **ClientBase**: Abstract base class defining the common interface for all AI model providers
- **OllamaClient**: Implementation for local Ollama models
- **ClaudeClient**: Implementation for Anthropic's Claude API
- **FunctionTable**: Registry for tool/function definitions and callbacks
- **Config**: JSON-based configuration management
- **ResponseParser**: Handles parsing and processing of model responses

## Getting Started

### Prerequisites

- C++17 or later
- CMake (for building)
- Access to either:
  - Local Ollama installation, or
  - Anthropic API key for Claude

### Basic Usage

```cpp
#include "assistant/assistant.hpp"

int main() {
    // Create client from config file
    auto conf = assistant::Config::FromFile("config.json");
    if (!conf) {
        std::cerr << "Failed to parse configuration" << std::endl;
        return 1;
    }
    
    auto cli_opt = assistant::MakeClient(conf);
    if (!cli_opt.has_value()) {
        std::cerr << "Failed to create client" << std::endl;
        return 1;
    }
    
    std::shared_ptr<assistant::ClientBase> cli = cli_opt.value();
    
    // Simple chat interaction
    cli->Chat("Hello, how are you?",
        [](std::string output, assistant::Reason reason, bool thinking) -> bool {
            if (reason == assistant::Reason::kPartialResult) {
                std::cout << output;
                std::cout.flush();
            } else if (reason == assistant::Reason::kDone) {
                std::cout << std::endl;
            }
            return true; // Continue processing
        },
        assistant::ChatOptions::kDefault);
    
    return 0;
}
```

### Adding Function/Tool Support

The library supports defining custom functions that the AI model can call:

```cpp
#include "assistant/assistant.hpp"
#include "assistant/function.hpp"

using FunctionBuilder = assistant::FunctionBuilder;

// Define the function callback
assistant::FunctionResult ReadFile(const assistant::json& args) {
    ASSIGN_FUNC_ARG_OR_RETURN(
        std::string filepath,
        assistant::GetFunctionArg<std::string>(args, "filepath"));
    
    // Read file logic here
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return assistant::FunctionResult{
            .isError = true,
            .text = "Failed to open file"
        };
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    
    return assistant::FunctionResult{
        .isError = false,
        .text = buffer.str()
    };
}

int main() {
    // ... create client ...
    
    // Register the function
    cli->GetFunctionTable().Add(
        FunctionBuilder("Read_file_content_from_a_given_path")
            .SetDescription("Read file content from the disk at a given path.")
            .AddRequiredParam("filepath", "the path of the file on the disk.", "string")
            .SetCallback(ReadFile)
            .Build()
    );
    
    // Now the AI model can call this function during chat
    cli->Chat("Can you read the file config.json?", callback, options);
    
    return 0;
}
```

## Configuration

Create a JSON configuration file to specify your model and endpoint:

### Ollama Example

```json
{
  "endpoint": {
    "type": "ollama",
    "url": "http://localhost:11434",
    "model": "llama3.2:latest"
  },
  "log_level": "info"
}
```

### Claude Example

```json
{
  "endpoint": {
    "type": "anthropic",
    "url": "https://api.anthropic.com",
    "model": "claude-3-5-sonnet-20241022",
    "headers": {
      "x-api-key": "your-api-key-here"
    },
    "max_tokens": 4096
  },
  "log_level": "info"
}
```

## API Overview

### Client Interface

#### Core Methods

- `Chat(msg, callback, options)` - Start a chat conversation
- `IsRunning()` - Check if the AI server is available
- `List()` - Get list of available models
- `GetModelInfo(model)` - Get detailed information about a model
- `PullModel(name, callback)` - Download a model (Ollama)

#### Session Management

- `Shutdown()` - Clean up and reset the session
- `Interrupt()` - Interrupt ongoing requests
- `SetHistorySize(count)` - Set conversation history window size
- `ClearHistoryMessages()` - Clear conversation history
- `AddSystemMessage(msg)` - Add system-level instructions

#### Function/Tool Management

- `GetFunctionTable()` - Access the function registry
- `ClearFunctionTable()` - Remove all registered functions

### Chat Options

Control chat behavior with `ChatOptions` flags:

- `kDefault` - All features enabled (default)
- `kNoTools` - Disable tool/function calling for this request
- `kNoHistory` - Don't include conversation history

Combine flags using bitwise OR:
```cpp
auto options = assistant::ChatOptions::kNoTools | assistant::ChatOptions::kNoHistory;
```

### Response Callback

The response callback provides:
- `output` - Text content from the model
- `reason` - Why the callback was invoked:
  - `kPartialResult` - Streaming chunk
  - `kDone` - Request completed
  - `kFatalError` - Unrecoverable error
  - `kCancelled` - User cancelled
  - `kLogNotice` / `kLogDebug` - Log messages
- `thinking` - Whether model is in "thinking" mode (for models that support it)

Return `true` to continue processing, `false` to cancel.

### Model Capabilities

The library automatically detects model capabilities:

- `kThinking` - Extended reasoning/thinking support
- `kTools` - Function/tool calling
- `kCompletion` - Text completion
- `kInsert` - Text insertion
- `kVision` - Image/vision processing

Query capabilities with:
```cpp
auto caps = cli->GetModelCapabilities("model-name");
```

## Demo Application

The included `main.cpp` demonstrates a full-featured interactive chat application with:

- Interactive command-line interface
- File reading and writing tools
- Editor integration
- Thread-safe prompt queue
- Colored output
- Session management commands:
  - `/info` - Display model information
  - `/no_tools` - Disable tool calls
  - `/chat_defaults` - Reset options
  - `clear` or `reset` - Clear session
  - `@filename` - Load prompt from file
  - `q`, `quit`, or `exit` - Exit application

## Thread Safety

The library is designed with thread safety in mind:

- Request queues are protected with mutexes
- Atomic operations for interrupt and state flags
- Lock guards for shared resources
- Thread-safe message history management

## Logging

Configure logging via the API:

```cpp
assistant::SetLogLevel(assistant::LogLevel::kError);
assistant::SetLogFile("assistant.log");
```

Available log levels:
- `kDebug`
- `kInfo`
- `kWarning`
- `kError`

## Error Handling

The library uses `std::optional` for operations that may fail:

```cpp
auto client = assistant::MakeClient(conf);
if (!client.has_value()) {
    // Handle error
}
```

Function callbacks use `FunctionResult` with error flags:

```cpp
return assistant::FunctionResult{
    .isError = true,
    .text = "Error description"
};
```

## Building

```bash
mkdir build
cd build
cmake ..
make
```

## Dependencies

- nlohmann/json (for JSON parsing)
- libcurl (for HTTP requests)
- Platform-specific networking libraries

## License

See LICENSE file for details.

## Contributing

Contributions are welcome! Please ensure code follows the existing style and includes appropriate tests.

## Support

For issues, questions, or contributions, please refer to the project's issue tracker.
