#include "mcp_local_process.hpp"

namespace ollama {
MCPStdioClient::MCPStdioClient(const std::vector<std::string>& args)
    : args_(args) {}

bool MCPStdioClient::Initialise() {
  try {
    std::stringstream ss;
    for (size_t i = 0; i < args_.size(); ++i) {
      if (i != 0) {
        ss << " ";
      }
      if (args_[i].find(" ") != std::string::npos) {
        ss << "\"" << args_[i] << "\"";
      } else {
        ss << args_[i];
      }
    }

    client_.reset(new mcp::stdio_client(ss.str()));
    client_->initialize("assistant", "1.0");
    client_->ping();
    tools_ = client_->get_tools();
    return true;
  } catch (std::exception& e) {
    return false;
  }
}

std::string MCPStdioClient::Call(
    [[maybe_unused]] const mcp::tool& t,
    [[maybe_unused]] const FunctionArgumentVec& params) const {
  return "";
}
}  // namespace ollama
