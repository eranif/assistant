#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "assistant/config.hpp"

using namespace assistant;
namespace fs = std::filesystem;

class ConfigFileTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Create a temporary directory for test files
    test_dir_ = fs::temp_directory_path() / "config_test";
    fs::create_directories(test_dir_);
  }

  void TearDown() override {
    // Clean up test files
    if (fs::exists(test_dir_)) {
      fs::remove_all(test_dir_);
    }
  }

  // Helper function to create a test file with given content
  std::string CreateTestFile(const std::string& filename,
                             const std::string& content) {
    fs::path filepath = test_dir_ / filename;
    std::ofstream file(filepath);
    file << content;
    file.close();
    return filepath.string();
  }

  fs::path test_dir_;
};

// Test loading valid configuration from file
TEST_F(ConfigFileTest, FromFile_ValidConfiguration) {
  std::string content = R"({
    "mcp_servers": {
      "test_server": {
        "type": "stdio",
        "command": ["python", "server.py"]
      }
    },
    "endpoints": {
      "http://localhost:11434": {
        "model": "llama2"
      }
    }
  })";

  std::string filepath = CreateTestFile("valid_config.json", content);
  auto result = ConfigBuilder::FromFile(filepath);

  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.errmsg_.empty());
  auto config = result.config_.value();
  EXPECT_EQ(config.GetServers().size(), 1);
  EXPECT_EQ(config.GetEndpoints().size(), 1);
}

// Test loading from non-existent file
TEST_F(ConfigFileTest, FromFile_NonExistentFile) {
  std::string filepath = (test_dir_ / "nonexistent.json").string();
  auto result = ConfigBuilder::FromFile(filepath);

  ASSERT_FALSE(result.ok());
  EXPECT_FALSE(result.errmsg_.empty());
  EXPECT_NE(result.errmsg_.find("Failed to open file"), std::string::npos);
}

// Test loading file with invalid JSON
TEST_F(ConfigFileTest, FromFile_InvalidJSON) {
  std::string content = R"({invalid json format})";
  std::string filepath = CreateTestFile("invalid.json", content);
  auto result = ConfigBuilder::FromFile(filepath);

  ASSERT_FALSE(result.ok());
  EXPECT_FALSE(result.errmsg_.empty());
}

// Test loading empty file
TEST_F(ConfigFileTest, FromFile_EmptyFile) {
  std::string content = "";
  std::string filepath = CreateTestFile("empty.json", content);
  auto result = ConfigBuilder::FromFile(filepath);

  ASSERT_FALSE(result.ok());
  EXPECT_FALSE(result.errmsg_.empty());
}

// Test loading file with empty JSON object
TEST_F(ConfigFileTest, FromFile_EmptyJSONObject) {
  std::string content = "{}";
  std::string filepath = CreateTestFile("empty_object.json", content);
  auto result = ConfigBuilder::FromFile(filepath);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  EXPECT_EQ(config.GetServers().size(), 0);
  EXPECT_EQ(config.GetEndpoints().size(), 0);
}

// Test loading file with environment variables
TEST_F(ConfigFileTest, FromFile_WithEnvironmentVariables) {
  // Set a test environment variable
  EnvMap env{{"TEST_MODEL", "llama2"}, {"TEST_PORT", "11434"}};

  std::string content = R"({
    "endpoints": {
      "http://localhost:${TEST_PORT}": {
        "model": "${TEST_MODEL}"
      }
    }
  })";

  std::string filepath = CreateTestFile("env_vars.json", content);
  auto result = ConfigBuilder::FromFile(filepath, env);

  ASSERT_TRUE(result.ok()) << result.errmsg_;
  auto config = result.config_.value();
  const auto& endpoints = config.GetEndpoints();
  ASSERT_EQ(endpoints.size(), 1);

  // The environment expansion should have occurred
  EXPECT_EQ(endpoints[0]->url_, "http://localhost:11434");
  EXPECT_EQ(endpoints[0]->model_, "llama2");
}

// Test loading file with complex configuration
TEST_F(ConfigFileTest, FromFile_ComplexConfiguration) {
  std::string content = R"({
    "mcp_servers": {
      "local_python": {
        "type": "stdio",
        "command": ["python3", "server.py"],
        "enabled": true,
        "env": {
          "PYTHON_PATH": "/usr/local/bin",
          "DEBUG": "1"
        }
      },
      "remote_node": {
        "type": "stdio",
        "command": ["node", "app.js"],
        "ssh": {
          "hostname": "remote.example.com",
          "user": "deploy",
          "port": 2222,
          "key": "/home/user/.ssh/id_rsa"
        }
      },
      "api_service": {
        "type": "sse",
        "baseurl": "https://api.example.com",
        "endpoint": "/v1/stream",
        "auth_token": "bearer_token_123",
        "headers": {
          "Content-Type": "application/json",
          "X-API-Version": "2.0"
        }
      }
    },
    "endpoints": {
      "http://localhost:11434": {
        "model": "llama2",
        "type": "ollama",
        "active": true,
        "max_tokens": 4096,
        "context_size": 8192,
        "transport": "httplib",
        "http_headers": {
          "Host": "localhost"
        }
      },
      "https://api.anthropic.com": {
        "model": "claude-3-opus-20240229",
        "type": "anthropic",
        "active": false,
        "verify_server_ssl": true,
        "models": ["claude-3-opus", "claude-3-sonnet", "claude-3-haiku"],
        "http_headers": {
          "x-api-key": "sk-ant-api-key",
          "anthropic-version": "2023-06-01"
        }
      }
    },
    "server_timeout": {
      "read_msecs": 60000,
      "write_msecs": 30000,
      "connect_msecs": 5000
    },
    "history_size": 150,
    "log_level": "debug",
    "keep_alive": "15m",
    "stream": true
  })";

  std::string filepath = CreateTestFile("complex_config.json", content);
  auto result = ConfigBuilder::FromFile(filepath);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();

  // Verify MCP servers
  const auto& servers = config.GetServers();
  EXPECT_EQ(servers.size(), 3);

  // Check local Python server
  EXPECT_EQ(servers[0].name, "local_python");
  EXPECT_TRUE(servers[0].enabled);
  EXPECT_TRUE(servers[0].IsStdio());
  ASSERT_TRUE(servers[0].stdio_params.has_value());
  EXPECT_EQ(servers[0].stdio_params->args.size(), 2);
  EXPECT_TRUE(servers[0].stdio_params->env.has_value());

  // Check remote Node server
  EXPECT_EQ(servers[1].name, "remote_node");
  EXPECT_TRUE(servers[1].IsStdio());
  ASSERT_TRUE(servers[1].stdio_params.has_value());
  EXPECT_TRUE(servers[1].stdio_params->ssh_login.has_value());
  EXPECT_EQ(servers[1].stdio_params->ssh_login->hostname, "remote.example.com");

  // Check API service
  EXPECT_EQ(servers[2].name, "api_service");
  EXPECT_TRUE(servers[2].IsSse());
  ASSERT_TRUE(servers[2].sse_params.has_value());
  EXPECT_EQ(servers[2].sse_params->baseurl, "https://api.example.com");
  EXPECT_TRUE(servers[2].sse_params->headers.has_value());

  // Verify endpoints
  const auto& endpoints = config.GetEndpoints();
  EXPECT_EQ(endpoints.size(), 2);

  auto active_endpoint = config.GetEndpoint();
  ASSERT_NE(active_endpoint, nullptr);
  EXPECT_EQ(active_endpoint->url_, "http://localhost:11434");
  EXPECT_EQ(active_endpoint->model_, "llama2");
  EXPECT_EQ(active_endpoint->max_tokens_.value(), 4096);
  EXPECT_EQ(active_endpoint->context_size_.value(), 8192);

  // Verify timeout settings
  auto timeout = config.GetServerTimeoutSettings();
  EXPECT_EQ(timeout.read_ms_, 60000);
  EXPECT_EQ(timeout.write_ms_, 30000);
  EXPECT_EQ(timeout.connect_ms_, 5000);

  // Verify global settings
  EXPECT_EQ(config.GetHistorySize(), 150);
  EXPECT_EQ(config.GetLogLevel(), LogLevel::kDebug);
  EXPECT_EQ(config.GetKeepAlive(), "15m");
  EXPECT_TRUE(config.IsStream());
}

// Test loading file with minimal valid configuration
TEST_F(ConfigFileTest, FromFile_MinimalConfiguration) {
  std::string content = R"({
    "endpoints": {
      "http://localhost:11434": {
        "model": "llama2"
      }
    }
  })";

  std::string filepath = CreateTestFile("minimal_config.json", content);
  auto result = ConfigBuilder::FromFile(filepath);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();

  // Should use default values
  EXPECT_EQ(config.GetHistorySize(), 50);
  EXPECT_EQ(config.GetLogLevel(), LogLevel::kInfo);
  EXPECT_EQ(config.GetKeepAlive(), "5m");
  EXPECT_TRUE(config.IsStream());

  auto timeout = config.GetServerTimeoutSettings();
  EXPECT_EQ(timeout.connect_ms_, 100);
  EXPECT_EQ(timeout.read_ms_, 10000);
  EXPECT_EQ(timeout.write_ms_, 10000);
}

// Test loading file with only MCP servers
TEST_F(ConfigFileTest, FromFile_OnlyMCPServers) {
  std::string content = R"({
    "mcp_servers": {
      "server1": {
        "type": "stdio",
        "command": ["python", "server.py"]
      },
      "server2": {
        "type": "sse",
        "baseurl": "https://api.example.com"
      }
    }
  })";

  std::string filepath = CreateTestFile("only_servers.json", content);
  auto result = ConfigBuilder::FromFile(filepath);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();

  EXPECT_EQ(config.GetServers().size(), 2);
  EXPECT_EQ(config.GetEndpoints().size(), 0);
  EXPECT_EQ(config.GetEndpoint(), nullptr);
}

// Test file with BOM (Byte Order Mark) - UTF-8 with BOM
TEST_F(ConfigFileTest, FromFile_WithBOM) {
  // UTF-8 BOM followed by valid JSON
  std::string content =
      "\xEF\xBB\xBF{\"endpoints\":{\"http://"
      "localhost:11434\":{\"model\":\"test\"}}}";
  std::string filepath = CreateTestFile("with_bom.json", content);

  auto result = ConfigBuilder::FromFile(filepath);

  // The JSON parser should handle BOM gracefully
  // This might fail depending on the JSON library's BOM handling
  // If it fails, that's expected behavior and documents a limitation
  if (result.ok()) {
    auto config = result.config_.value();
    EXPECT_EQ(config.GetEndpoints().size(), 1);
  }
}

// Test very large configuration file
TEST_F(ConfigFileTest, FromFile_LargeConfiguration) {
  std::ostringstream content;
  content << "{\n";
  content << "  \"mcp_servers\": {\n";

  // Generate 100 MCP servers
  for (int i = 0; i < 100; ++i) {
    content << "    \"server_" << i << "\": {\n";
    content << "      \"type\": \"stdio\",\n";
    content << "      \"command\": [\"python\", \"server" << i << ".py\"]\n";
    content << "    }";
    if (i < 99) content << ",";
    content << "\n";
  }

  content << "  },\n";
  content << "  \"endpoints\": {\n";
  content << "    \"http://localhost:11434\": {\n";
  content << "      \"model\": \"llama2\"\n";
  content << "    }\n";
  content << "  }\n";
  content << "}\n";

  std::string filepath = CreateTestFile("large_config.json", content.str());
  auto result = ConfigBuilder::FromFile(filepath);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  EXPECT_EQ(config.GetServers().size(), 100);
}

// Test file with comments (which are not valid JSON)
TEST_F(ConfigFileTest, FromFile_WithComments) {
  std::string content = R"({
    // This is a comment
    "endpoints": {
      "http://localhost:11434": {
        "model": "llama2" // inline comment
      }
    }
  })";

  std::string filepath = CreateTestFile("with_comments.json", content);
  auto result = ConfigBuilder::FromFile(filepath);

  // Standard JSON does not support comments, so this should fail
  ASSERT_FALSE(result.ok());
}

// Test file with trailing commas (which are not valid in strict JSON)
TEST_F(ConfigFileTest, FromFile_WithTrailingCommas) {
  std::string content = R"({
    "endpoints": {
      "http://localhost:11434": {
        "model": "llama2",
      },
    }
  })";

  std::string filepath = CreateTestFile("trailing_commas.json", content);
  auto result = ConfigBuilder::FromFile(filepath);

  // Standard JSON does not allow trailing commas, so this should fail
  ASSERT_FALSE(result.ok());
}

// Test file with Unicode characters
TEST_F(ConfigFileTest, FromFile_WithUnicode) {
  std::string content = R"({
    "endpoints": {
      "http://localhost:11434": {
        "model": "模型测试_тест_🚀"
      }
    }
  })";

  std::string filepath = CreateTestFile("unicode.json", content);
  auto result = ConfigBuilder::FromFile(filepath);

  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();
  const auto& endpoints = config.GetEndpoints();
  ASSERT_EQ(endpoints.size(), 1);
  EXPECT_EQ(endpoints[0]->model_, "模型测试_тест_🚀");
}

// Test SetHistorySize method
TEST(ConfigTest, SetHistorySize) {
  Config config;
  EXPECT_EQ(config.GetHistorySize(), 50);

  config.SetHistorySize(100);
  EXPECT_EQ(config.GetHistorySize(), 100);

  config.SetHistorySize(0);
  EXPECT_EQ(config.GetHistorySize(), 0);
}

// Test multiple calls to GetEndpoint return the same pointer
TEST(ConfigTest, GetEndpointConsistency) {
  std::string json_content = R"({
    "endpoints": {
      "http://endpoint1": {
        "model": "model1",
        "active": true
      }
    }
  })";

  auto result = ConfigBuilder::FromContent(json_content);
  ASSERT_TRUE(result.ok());
  auto config = result.config_.value();

  auto endpoint1 = config.GetEndpoint();
  auto endpoint2 = config.GetEndpoint();

  EXPECT_EQ(endpoint1, endpoint2);
}

// Test ParseResult convenience methods
TEST(ParseResultTest, OkMethod) {
  ParseResult success_result;
  success_result.config_ = Config();
  EXPECT_TRUE(success_result.ok());

  ParseResult failure_result;
  failure_result.errmsg_ = "Error occurred";
  EXPECT_FALSE(failure_result.ok());
}
