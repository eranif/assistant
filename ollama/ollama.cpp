#include "ollama/ollama.hpp"

#include "ollama/config.hpp"
#include "ollama/cpp-mcp/mcp_logger.h"
#include "ollama/logger.hpp"
#include "ollama/macros.hpp"
#include "ollama/ollamalib.hpp"

namespace ollama {

Client::Client(const std::string& url,
               const std::unordered_map<std::string, std::string>& headers) {
  m_url = url;
  m_http_headers = headers;
  ollama::show_requests(false);
  ollama::show_replies(false);
  ollama::allow_exceptions(true);
  m_ollama.setServerURL(url);
  m_ollama.setReadTimeout(300);
  m_ollama.setWriteTimeout(300);
  mcp::set_log_level(mcp::log_level::error);
  SetHeadersInternal(headers);
  Startup();
}

Client::~Client() { Shutdown(); }

void Client::Interrupt() {
  ClientBase::Interrupt();
  m_ollama.interrupt();
}

void Client::ChatImpl(
    ollama::request& request,
    std::function<bool(const ollama::response& resp, void* user_data)>
        on_response,
    void* user_data) {
  m_ollama.chat(request, on_response, user_data);
}

void Client::ApplyConfig(const ollama::Config* conf) {
  ClientBase::ApplyConfig(conf);
  m_ollama.setServerURL(m_url);
  SetHeadersInternal(m_http_headers);
}

std::vector<std::string> Client::List() {
  if (!IsRunning()) {
    return {};
  }
  try {
    return m_ollama.list_models();
  } catch (...) {
    return {};
  }
}

json Client::ListJSON() {
  if (!IsRunning()) {
    return {};
  }
  try {
    return m_ollama.list_model_json();
  } catch (...) {
    return {};
  }
}

std::optional<json> Client::GetModelInfo(const std::string& model) {
  if (!IsRunning()) {
    return std::nullopt;
  }
  try {
    OLOG(LogLevel::kInfo) << "Fetching info for model: " << model;
    return m_ollama.show_model_info(model);
  } catch (std::exception& e) {
    return std::nullopt;
  }
}

std::optional<ModelCapabilities> Client::GetModelCapabilities(
    const std::string& model) {
  auto opt = GetModelInfo(model);
  if (!opt.has_value()) {
    return std::nullopt;
  }

  auto& j = opt.value();
  OLOG(LogLevel::kTrace) << "Model info:";
  OLOG(LogLevel::kTrace) << std::setw(2) << j["capabilities"];
  OLOG(LogLevel::kTrace) << std::setw(2) << j["model_info"];

  ModelCapabilities flags{ModelCapabilities::kNone};
  try {
    std::vector<std::string> capabilities = j["capabilities"];
    for (const auto& c : capabilities) {
      if (c == "completion") {
        AddFlagSet(flags, ModelCapabilities::kCompletion);
      } else if (c == "tools") {
        AddFlagSet(flags, ModelCapabilities::kTooling);
      } else if (c == "thinking") {
        AddFlagSet(flags, ModelCapabilities::kThinking);
      } else if (c == "insert") {
        AddFlagSet(flags, ModelCapabilities::kInsert);
      } else if (c == "vision") {
        AddFlagSet(flags, ModelCapabilities::kVision);
      } else {
        std::cerr << "unknown capability: " << c << std::endl;
      }
    }
    return flags;
  } catch (...) {
    return std::nullopt;
  }
}

void Client::PullModel(const std::string& name, OnResponseCallback cb) {
  try {
    Ollama ol;
    std::stringstream ss;
    ol.setServerURL(m_url);
    ss << "Pulling model: " << name;
    cb(ss.str(), Reason::kLogNotice, false);
    ol.pull_model(name, true);
    cb("Model successfully pulled.", Reason::kDone, false);
  } catch (std::exception& e) {
    cb(e.what(), Reason::kFatalError, false);
  }
}

bool Client::IsRunning() {
  m_ollama.setServerURL(m_url);
  return m_ollama.is_running();
}

bool Client::ModelHasCapability(const std::string& model_name,
                                ModelCapabilities c) {
  if (m_model_capabilities.count(model_name) == 0) {
    // Load the model capabilities
    auto capabilities = GetModelCapabilities(model_name);
    if (capabilities.has_value()) {
      m_model_capabilities.insert({model_name, capabilities.value()});
    } else {
      m_model_capabilities.insert({model_name, ModelCapabilities::kNone});
    }
  }

  auto flags = m_model_capabilities.find(model_name)->second;
  return IsFlagSet(flags, c);
}

void Client::SetHeadersInternal(
    const std::unordered_map<std::string, std::string>& headers) {
  httplib::Headers h;
  for (const auto& [header_name, header_value] : headers) {
    OLOG(LogLevel::kInfo) << "Adding HTTP header: " << header_name << ": "
                          << header_value;
    h.insert({header_name, header_value});
  }
  m_ollama.setHttpHeaders(std::move(h));
}
}  // namespace ollama
