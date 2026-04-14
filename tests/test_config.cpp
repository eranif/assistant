#include <gtest/gtest.h>

#include "assistant/config.hpp"

using namespace assistant;

// Test parsing valid configuration with stdio MCP server
TEST(ConfigBuilderTest, FromContent_ValidStdioServer) {
  std::string json_content = R"({
    "mcp_servers": {
      "test_server": {
        "type": "stdio",
        "enabled": true,
        "command": ["python", "server.py"]
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.errmsg_.empty());

  auto config = result.config_.value();
  const auto& servers = config.GetServers();

  ASSERT_EQ(servers.size(), 1);
  EXPECT_EQ(servers[0].name, "test_server");
  EXPECT_TRUE(servers[0].enabled);
  EXPECT_TRUE(servers[0].IsStdio());
  EXPECT_FALSE(servers[0].IsSse());

  ASSERT_TRUE(servers[0].stdio_params.has_value());
  const auto& params = servers[0].stdio_params.value();
  ASSERT_EQ(params.args.size(), 2);
  EXPECT_EQ(params.args[0], "python");
  EXPECT_EQ(params.args[1], "server.py");
  EXPECT_FALSE(params.ssh_login.has_value());
  EXPECT_FALSE(params.env.has_value());
}

// Test parsing valid configuration with SSE MCP server
TEST(ConfigBuilderTest, FromContent_ValidSseServer) {
  std::string json_content = R"({
    "mcp_servers": {
      "sse_server": {
        "type": "sse",
        "enabled": true,
        "baseurl": "https://example.com",
        "endpoint": "/api/sse",
        "auth_token": "secret123"
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  const auto& servers = config.GetServers();

  ASSERT_EQ(servers.size(), 1);
  EXPECT_EQ(servers[0].name, "sse_server");
  EXPECT_TRUE(servers[0].enabled);
  EXPECT_FALSE(servers[0].IsStdio());
  EXPECT_TRUE(servers[0].IsSse());

  ASSERT_TRUE(servers[0].sse_params.has_value());
  const auto& params = servers[0].sse_params.value();
  EXPECT_EQ(params.baseurl, "https://example.com");
  EXPECT_EQ(params.endpoint, "/api/sse");
  ASSERT_TRUE(params.auth_token.has_value());
  EXPECT_EQ(params.auth_token.value(), "secret123");
}

// Test parsing stdio server with SSH configuration
TEST(ConfigBuilderTest, FromContent_StdioServerWithSSH) {
  std::string json_content = R"({
    "mcp_servers": {
      "remote_server": {
        "type": "stdio",
        "command": ["python3", "remote.py"],
        "ssh": {
          "hostname": "192.168.1.100",
          "user": "ubuntu",
          "port": 2222,
          "key": "/home/user/.ssh/id_rsa",
          "ssh_program": "/usr/bin/ssh"
        }
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  const auto& servers = config.GetServers();

  ASSERT_EQ(servers.size(), 1);
  ASSERT_TRUE(servers[0].stdio_params.has_value());
  const auto& params = servers[0].stdio_params.value();

  ASSERT_TRUE(params.ssh_login.has_value());
  const auto& ssh = params.ssh_login.value();
  EXPECT_EQ(ssh.hostname, "192.168.1.100");
  EXPECT_EQ(ssh.user, "ubuntu");
  EXPECT_EQ(ssh.port, 2222);
  EXPECT_EQ(ssh.ssh_key, "/home/user/.ssh/id_rsa");
  EXPECT_EQ(ssh.ssh_program, "/usr/bin/ssh");
  EXPECT_TRUE(params.IsRemote());
}

// Test parsing stdio server with environment variables
TEST(ConfigBuilderTest, FromContent_StdioServerWithEnv) {
  std::string json_content = R"({
    "mcp_servers": {
      "env_server": {
        "type": "stdio",
        "command": ["node", "app.js"],
        "env": {
          "NODE_ENV": "production",
          "API_KEY": "abc123"
        }
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  const auto& servers = config.GetServers();

  ASSERT_EQ(servers.size(), 1);
  ASSERT_TRUE(servers[0].stdio_params.has_value());
  const auto& params = servers[0].stdio_params.value();

  ASSERT_TRUE(params.env.has_value());
  const auto& env = params.env.value();
  EXPECT_TRUE(env.contains("NODE_ENV"));
  EXPECT_TRUE(env.contains("API_KEY"));
  EXPECT_EQ(env["NODE_ENV"].get<std::string>(), "production");
  EXPECT_EQ(env["API_KEY"].get<std::string>(), "abc123");
}

// Test parsing SSE server with headers
TEST(ConfigBuilderTest, FromContent_SseServerWithHeaders) {
  std::string json_content = R"({
    "mcp_servers": {
      "sse_server": {
        "type": "sse",
        "baseurl": "https://api.example.com",
        "headers": {
          "Authorization": "Bearer token123",
          "X-Custom-Header": "value"
        }
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  const auto& servers = config.GetServers();

  ASSERT_EQ(servers.size(), 1);
  ASSERT_TRUE(servers[0].sse_params.has_value());
  const auto& params = servers[0].sse_params.value();

  ASSERT_TRUE(params.headers.has_value());
  const auto& headers = params.headers.value();
  EXPECT_TRUE(headers.contains("Authorization"));
  EXPECT_TRUE(headers.contains("X-Custom-Header"));
}

// Test disabled MCP server
TEST(ConfigBuilderTest, FromContent_DisabledServer) {
  std::string json_content = R"({
    "mcp_servers": {
      "disabled_server": {
        "type": "stdio",
        "enabled": false,
        "command": ["echo", "hello"]
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  const auto& servers = config.GetServers();

  ASSERT_EQ(servers.size(), 1);
  EXPECT_FALSE(servers[0].enabled);
}

// Test default values for MCP server (type defaults to stdio, enabled defaults
// to true)
TEST(ConfigBuilderTest, FromContent_DefaultServerValues) {
  std::string json_content = R"({
    "mcp_servers": {
      "default_server": {
        "command": ["python", "script.py"]
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  const auto& servers = config.GetServers();

  ASSERT_EQ(servers.size(), 1);
  EXPECT_TRUE(servers[0].enabled);    // default is true
  EXPECT_TRUE(servers[0].IsStdio());  // default type is stdio
}

// Test parsing endpoint configuration
TEST(ConfigBuilderTest, FromContent_ValidEndpoint) {
  std::string json_content = R"({
    "endpoints": {
      "http://localhost:11434": {
        "model": "llama2",
        "type": "ollama",
        "active": true,
        "max_tokens": 2048,
        "context_size": 4096
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  const auto& endpoints = config.GetEndpoints();

  ASSERT_EQ(endpoints.size(), 1);
  auto endpoint = endpoints[0];
  EXPECT_EQ(endpoint->url_, "http://localhost:11434");
  EXPECT_EQ(endpoint->model_, "llama2");
  EXPECT_EQ(endpoint->type_, EndpointKind::ollama);
  EXPECT_TRUE(endpoint->active_);
  EXPECT_EQ(endpoint->max_tokens_.value(), 2048);
  EXPECT_EQ(endpoint->context_size_.value(), 4096);
}

// Test endpoint with HTTP headers
TEST(ConfigBuilderTest, FromContent_EndpointWithHeaders) {
  std::string json_content = R"({
    "endpoints": {
      "https://api.anthropic.com": {
        "model": "claude-3",
        "type": "anthropic",
        "http_headers": {
          "x-api-key": "sk-ant-123",
          "anthropic-version": "2023-06-01"
        }
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  const auto& endpoints = config.GetEndpoints();

  ASSERT_EQ(endpoints.size(), 1);
  auto endpoint = endpoints[0];
  EXPECT_EQ(endpoint->type_, EndpointKind::anthropic);
  EXPECT_EQ(endpoint->headers_.size(), 2);
  EXPECT_EQ(endpoint->headers_["x-api-key"], "sk-ant-123");
  EXPECT_EQ(endpoint->headers_["anthropic-version"], "2023-06-01");
}

// Test endpoint with SSL verification disabled
TEST(ConfigBuilderTest, FromContent_EndpointSSLVerification) {
  std::string json_content = R"({
    "endpoints": {
      "https://example.com": {
        "model": "test-model",
        "verify_server_ssl": false
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  const auto& endpoints = config.GetEndpoints();

  ASSERT_EQ(endpoints.size(), 1);
  EXPECT_FALSE(endpoints[0]->verify_server_ssl_);
}

// Test endpoint with transport type
TEST(ConfigBuilderTest, FromContent_EndpointTransportType) {
  std::string json_content = R"({
    "endpoints": {
      "https://example.com": {
        "model": "test-model",
        "transport": "httplib"
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  const auto& endpoints = config.GetEndpoints();

  ASSERT_EQ(endpoints.size(), 1);
  EXPECT_EQ(endpoints[0]->transport_, TransportType::httplib);
}

// Test endpoint with multiple models
TEST(ConfigBuilderTest, FromContent_EndpointMultipleModels) {
  std::string json_content = R"({
    "endpoints": {
      "http://localhost:11434": {
        "model": "llama2",
        "models": ["llama2", "codellama", "mistral"]
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  const auto& endpoints = config.GetEndpoints();

  ASSERT_EQ(endpoints.size(), 1);
  EXPECT_EQ(endpoints[0]->model_, "llama2");
  ASSERT_EQ(endpoints[0]->models_.size(), 3);
  EXPECT_EQ(endpoints[0]->models_[0], "llama2");
  EXPECT_EQ(endpoints[0]->models_[1], "codellama");
  EXPECT_EQ(endpoints[0]->models_[2], "mistral");
}

// Test missing required 'model' field in endpoint
TEST(ConfigBuilderTest, FromContent_EndpointMissingModel) {
  std::string json_content = R"({
    "endpoints": {
      "http://localhost:11434": {
        "type": "ollama"
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_FALSE(result.ok());
  EXPECT_FALSE(result.errmsg_.empty());
  EXPECT_NE(result.errmsg_.find("missing the 'model' property"),
            std::string::npos);
}

// Test invalid endpoint type
TEST(ConfigBuilderTest, FromContent_InvalidEndpointType) {
  std::string json_content = R"({
    "endpoints": {
      "http://localhost:11434": {
        "model": "test",
        "type": "invalid_type"
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_FALSE(result.ok());
  EXPECT_FALSE(result.errmsg_.empty());
  EXPECT_NE(result.errmsg_.find("Invalid endpoint type"), std::string::npos);
}

// Test invalid transport type
TEST(ConfigBuilderTest, FromContent_InvalidTransportType) {
  std::string json_content = R"({
    "endpoints": {
      "http://localhost:11434": {
        "model": "test",
        "transport": "invalid_transport"
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_FALSE(result.ok());
  EXPECT_FALSE(result.errmsg_.empty());
  EXPECT_NE(result.errmsg_.find("Invalid transport"), std::string::npos);
}

// Test server timeout configuration
TEST(ConfigBuilderTest, FromContent_ServerTimeout) {
  std::string json_content = R"({
    "server_timeout": {
      "read_msecs": 30000,
      "write_msecs": 20000,
      "connect_msecs": 5000
    },
    "endpoints": {
      "http://localhost:11434": {
        "model": "test"
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  auto timeout = config.GetServerTimeoutSettings();

  EXPECT_EQ(timeout.read_ms_, 30000);
  EXPECT_EQ(timeout.write_ms_, 20000);
  EXPECT_EQ(timeout.connect_ms_, 5000);
}

// Test global configuration options
TEST(ConfigBuilderTest, FromContent_GlobalOptions) {
  std::string json_content = R"({
    "history_size": 100,
    "log_level": "debug",
    "keep_alive": "10m",
    "stream": false,
    "endpoints": {
      "http://localhost:11434": {
        "model": "test"
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();

  EXPECT_EQ(config.GetLogLevel(), LogLevel::kDebug);
  EXPECT_EQ(config.GetKeepAlive(), "10m");
  EXPECT_FALSE(config.IsStream());
}

// Test default history size
TEST(ConfigBuilderTest, FromContent_DefaultHistorySize) {
  std::string json_content = R"({
    "endpoints": {
      "http://localhost:11434": {
        "model": "test"
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
}

// Test multiple endpoints with exactly one active
TEST(ConfigBuilderTest, FromContent_MultipleEndpointsOneActive) {
  std::string json_content = R"({
    "endpoints": {
      "http://endpoint1": {
        "model": "model1",
        "active": true
      },
      "http://endpoint2": {
        "model": "model2",
        "active": true
      },
      "http://endpoint3": {
        "model": "model3",
        "active": false
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  const auto& endpoints = config.GetEndpoints();

  ASSERT_EQ(endpoints.size(), 3);

  // Count active endpoints - should be exactly 1
  int active_count = 0;
  for (const auto& ep : endpoints) {
    if (ep->active_) {
      active_count++;
    }
  }
  EXPECT_EQ(active_count, 1);
}

// Test no active endpoints - first should be made active
TEST(ConfigBuilderTest, FromContent_NoActiveEndpointsMakesFirstActive) {
  std::string json_content = R"({
    "endpoints": {
      "http://endpoint1": {
        "model": "model1",
        "active": false
      },
      "http://endpoint2": {
        "model": "model2"
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  auto active_endpoint = config.GetEndpoint();

  ASSERT_NE(active_endpoint, nullptr);
  EXPECT_EQ(active_endpoint->url_, "http://endpoint1");
}

// Test GetEndpoint returns active endpoint
TEST(ConfigBuilderTest, FromContent_GetActiveEndpoint) {
  std::string json_content = R"({
    "endpoints": {
      "http://endpoint1": {
        "model": "model1",
        "active": false
      },
      "http://endpoint2": {
        "model": "model2",
        "active": true
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  auto active_endpoint = config.GetEndpoint();

  ASSERT_NE(active_endpoint, nullptr);
  EXPECT_EQ(active_endpoint->url_, "http://endpoint2");
  EXPECT_TRUE(active_endpoint->active_);
}

// Test empty configuration
TEST(ConfigBuilderTest, FromContent_EmptyConfig) {
  std::string json_content = R"({})";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  EXPECT_EQ(config.GetServers().size(), 0);
  EXPECT_EQ(config.GetEndpoints().size(), 0);
  EXPECT_EQ(config.GetEndpoint(), nullptr);
}

// Test invalid JSON
TEST(ConfigBuilderTest, FromContent_InvalidJSON) {
  std::string json_content = R"({invalid json})";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_FALSE(result.ok());
  EXPECT_FALSE(result.errmsg_.empty());
}

// Test multiple MCP servers
TEST(ConfigBuilderTest, FromContent_MultipleServers) {
  std::string json_content = R"({
    "mcp_servers": {
      "server1": {
        "type": "stdio",
        "command": ["python", "server1.py"]
      },
      "server2": {
        "type": "sse",
        "baseurl": "https://example.com"
      },
      "server3": {
        "type": "stdio",
        "enabled": false,
        "command": ["node", "server3.js"]
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  const auto& servers = config.GetServers();

  EXPECT_EQ(servers.size(), 3);
  EXPECT_EQ(servers[0].name, "server1");
  EXPECT_EQ(servers[1].name, "server2");
  EXPECT_EQ(servers[2].name, "server3");
  EXPECT_TRUE(servers[0].IsStdio());
  EXPECT_TRUE(servers[1].IsSse());
  EXPECT_FALSE(servers[2].enabled);
}

// Test ServerTimeout utility methods
TEST(ServerTimeoutTest, ToSecsAndMicros) {
  ServerTimeout timeout;
  timeout.connect_ms_ = 1500;
  timeout.read_ms_ = 10000;
  timeout.write_ms_ = 500;

  auto [connect_secs, connect_micros] = timeout.GetConnectTimeout();
  EXPECT_EQ(connect_secs, 1);
  EXPECT_EQ(connect_micros, 500000);

  auto [read_secs, read_micros] = timeout.GetReadTimeout();
  EXPECT_EQ(read_secs, 10);
  EXPECT_EQ(read_micros, 0);

  auto [write_secs, write_micros] = timeout.GetWriteTimeout();
  EXPECT_EQ(write_secs, 0);
  EXPECT_EQ(write_micros, 500000);
}

// Test ServerTimeout default values
TEST(ServerTimeoutTest, DefaultValues) {
  ServerTimeout timeout;

  EXPECT_EQ(timeout.connect_ms_, 100);
  EXPECT_EQ(timeout.read_ms_, 10000);
  EXPECT_EQ(timeout.write_ms_, 10000);
}

// Test Config default values
TEST(ConfigTest, DefaultValues) {
  Config config;

  EXPECT_EQ(config.GetLogLevel(), LogLevel::kInfo);
  EXPECT_EQ(config.GetKeepAlive(), "5m");
  EXPECT_TRUE(config.IsStream());
  EXPECT_EQ(config.GetServers().size(), 0);
  EXPECT_EQ(config.GetEndpoints().size(), 0);
}

// Test Endpoint constants
TEST(EndpointTest, Constants) {
  EXPECT_EQ(kMaxTokensDefault, 4096);
  EXPECT_EQ(kDefaultContextSize, 32 * 1024);
  EXPECT_EQ(kEndpointOllamaLocal, "http://127.0.0.1:11434");
  EXPECT_EQ(kEndpointAnthropic, "https://api.anthropic.com");
  EXPECT_EQ(kEndpointOllamaCloud, "https://ollama.com");
  EXPECT_EQ(kEndpointOpenAI, "https://api.openai.com");
}

// Test Endpoint default values
TEST(EndpointTest, DefaultValues) {
  Endpoint endpoint;

  EXPECT_EQ(endpoint.url_, kEndpointOllamaLocal);
  EXPECT_EQ(endpoint.type_, EndpointKind::ollama);
  EXPECT_FALSE(endpoint.active_);
  EXPECT_EQ(endpoint.max_tokens_.value(), kMaxTokensDefault);
  EXPECT_EQ(endpoint.context_size_.value(), kDefaultContextSize);
  EXPECT_TRUE(endpoint.verify_server_ssl_);
  EXPECT_EQ(endpoint.transport_, TransportType::httplib);
}

// Test specialized endpoint constructors
TEST(EndpointTest, SpecializedEndpoints) {
  AnthropicEndpoint anthropic;
  EXPECT_EQ(anthropic.url_, kEndpointAnthropic);
  EXPECT_EQ(anthropic.type_, EndpointKind::anthropic);

  OpenAIEndpoint openai;
  EXPECT_EQ(openai.url_, kEndpointOpenAI);
  EXPECT_EQ(openai.type_, EndpointKind::openai);

  OllamaLocalEndpoint ollama_local;
  EXPECT_EQ(ollama_local.url_, kEndpointOllamaCloud);
  EXPECT_EQ(ollama_local.type_, EndpointKind::ollama);

  OllamaCloudEndpoint ollama_cloud;
  EXPECT_EQ(ollama_cloud.url_, kEndpointOllamaLocal);
  EXPECT_EQ(ollama_cloud.type_, EndpointKind::ollama);
  EXPECT_FALSE(ollama_cloud.headers_.empty());
}

// Test MCPServerConfig IsRemote
TEST(MCPServerConfigTest, IsRemote) {
  MCPServerConfig config;
  config.name = "test";

  StdioParams params;
  params.args = {"python", "script.py"};
  config.stdio_params = params;

  EXPECT_FALSE(config.stdio_params->IsRemote());

  SSHLogin ssh;
  ssh.hostname = "example.com";
  params.ssh_login = ssh;
  config.stdio_params = params;

  EXPECT_TRUE(config.stdio_params->IsRemote());
}

// Test complex configuration with all features
TEST(ConfigBuilderTest, FromContent_ComplexConfiguration) {
  std::string json_content = R"({
    "mcp_servers": {
      "local_server": {
        "type": "stdio",
        "command": ["python", "local.py"],
        "env": {
          "DEBUG": "true"
        }
      },
      "remote_server": {
        "type": "stdio",
        "command": ["python3", "remote.py"],
        "ssh": {
          "hostname": "server.example.com",
          "user": "admin",
          "port": 22
        }
      },
      "api_server": {
        "type": "sse",
        "baseurl": "https://api.example.com",
        "endpoint": "/stream",
        "auth_token": "secret",
        "headers": {
          "X-Custom": "value"
        }
      }
    },
    "endpoints": {
      "http://localhost:11434": {
        "model": "llama2",
        "type": "ollama",
        "active": true,
        "max_tokens": 2048,
        "context_size": 8192,
        "http_headers": {
          "Host": "localhost"
        }
      },
      "https://api.anthropic.com": {
        "model": "claude-3-opus",
        "type": "anthropic",
        "verify_server_ssl": true,
        "http_headers": {
          "x-api-key": "sk-ant-123"
        }
      }
    },
    "server_timeout": {
      "read_msecs": 30000,
      "write_msecs": 15000,
      "connect_msecs": 3000
    },
    "history_size": 75,
    "log_level": "info",
    "keep_alive": "8m",
    "stream": true
  })";

  auto result = ConfigBuilder::FromContent(json_content);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();

  // Verify MCP servers
  const auto& servers = config.GetServers();
  EXPECT_EQ(servers.size(), 3);

  // Verify endpoints
  const auto& endpoints = config.GetEndpoints();
  EXPECT_EQ(endpoints.size(), 2);
  auto active_endpoint = config.GetEndpoint();
  ASSERT_NE(active_endpoint, nullptr);
  EXPECT_EQ(active_endpoint->url_, "http://localhost:11434");

  // Verify timeout
  auto timeout = config.GetServerTimeoutSettings();
  EXPECT_EQ(timeout.read_ms_, 30000);
  EXPECT_EQ(timeout.write_ms_, 15000);
  EXPECT_EQ(timeout.connect_ms_, 3000);

  // Verify global options
  EXPECT_EQ(config.GetLogLevel(), LogLevel::kInfo);
  EXPECT_EQ(config.GetKeepAlive(), "8m");
  EXPECT_TRUE(config.IsStream());
}
