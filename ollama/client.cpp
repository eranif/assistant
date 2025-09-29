#include "ollama/client.hpp"

#include "ollama/config.hpp"
#include "ollama/cpp-mcp/mcp_logger.h"
#include "ollama/helpers.hpp"
#include "ollama/logger.hpp"
#include "ollama/ollamalib.hpp"

namespace ollama {

Client::Client(const std::string& url,
               const std::unordered_map<std::string, std::string>& headers) {
  m_url.set_value(url);
  m_http_headers.set_value(headers);

  ollama::show_requests(false);
  ollama::show_replies(false);
  ollama::allow_exceptions(true);
  m_ollama.setServerURL(url);
  m_ollama.setReadTimeout(10);
  m_ollama.setWriteTimeout(10);
  m_ollama.setConnectTimeout(10);
  mcp::set_log_level(mcp::log_level::error);
  SetHeadersInternal(m_ollama, headers);
  Startup();
}

Client::~Client() { Shutdown(); }

void Client::Interrupt() {
  ClientBase::Interrupt();
  std::scoped_lock lk{m_ollama_mutex};
  try {
    m_ollama.interrupt();
  } catch (std::exception& e) {
    OLOG(LogLevel::kWarning)
        << "an error occurred while interrupting client. " << e.what();
  }
}

void Client::ChatImpl(
    ollama::request& request,
    std::function<bool(const ollama::response& resp, void* user_data)>
        on_response,
    void* user_data) {
  std::scoped_lock lk{m_ollama_mutex};
  m_ollama.chat(request, on_response, user_data);
}

void Client::ApplyConfig(const ollama::Config* conf) {
  ClientBase::ApplyConfig(conf);
  {
    std::scoped_lock lk{m_ollama_mutex};
    m_ollama.setServerURL(m_url.get_value());
    auto server_timeout_settings = m_server_timeout.get_value();
    m_ollama.setConnectTimeout(
        server_timeout_settings.GetConnectTimeout().first,
        server_timeout_settings.GetConnectTimeout().second);
    m_ollama.setReadTimeout(server_timeout_settings.GetReadTimeout().first,
                            server_timeout_settings.GetReadTimeout().second);
    m_ollama.setWriteTimeout(server_timeout_settings.GetWriteTimeout().first,
                             server_timeout_settings.GetWriteTimeout().second);
    SetHeadersInternal(m_ollama, m_http_headers.get_value());
  }
}

std::vector<std::string> Client::List() {
  std::scoped_lock lk{m_ollama_mutex};
  if (!IsRunningInternal(m_ollama)) {
    return {};
  }
  try {
    return m_ollama.list_models();
  } catch (...) {
    return {};
  }
}

json Client::ListJSON() {
  std::scoped_lock lk{m_ollama_mutex};
  if (!IsRunningInternal(m_ollama)) {
    return {};
  }
  try {
    return m_ollama.list_model_json();
  } catch (...) {
    return {};
  }
}

std::optional<json> Client::GetModelInfo(const std::string& model) {
  std::scoped_lock lk{m_ollama_mutex};
  if (!IsRunningInternal(m_ollama)) {
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
    auto capabilities = j["capabilities"].get<std::vector<std::string>>();
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
    ol.setServerURL(m_url.get_value());
    ss << "Pulling model: " << name;
    cb(ss.str(), Reason::kLogNotice, false);
    ol.pull_model(name, true);
    cb("Model successfully pulled.", Reason::kDone, false);
  } catch (std::exception& e) {
    cb(e.what(), Reason::kFatalError, false);
  }
}

bool Client::IsRunning() {
  std::scoped_lock lk{m_ollama_mutex};
  return IsRunningInternal(m_ollama);
}

void Client::SetHeadersInternal(
    Ollama& client,
    const std::unordered_map<std::string, std::string>& headers) {
  httplib::Headers h;
  for (const auto& [header_name, header_value] : headers) {
    h.insert({header_name, header_value});
  }
  client.setHttpHeaders(std::move(h));
}

bool Client::IsRunningInternal(Ollama& client) const {
  try {
    return client.is_running();
  } catch (const std::exception& e) {
    OLOG(LogLevel::kDebug) << e.what();
    return false;
  }
}
}  // namespace ollama
