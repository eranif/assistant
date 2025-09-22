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
  SetHeadersInternal(m_ollama, headers);
  Startup();
}

Client::~Client() { Shutdown(); }

void Client::IsAliveThreadMain(
    std::string server_url,
    std::unordered_map<std::string, std::string> headers) {
  OLOG(LogLevel::kInfo) << "Ollama 'is alive' thread started.";
  while (!m_shutdown_flag.load()) {
    Ollama client;
    client.setServerURL(server_url);
    SetHeadersInternal(client, headers);

    m_is_running_flag.store(client.is_running());
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  OLOG(LogLevel::kInfo) << "Ollama 'is alive' thread exited.";
}

void Client::StartIsAliveThread() {
  std::string server_url = m_url;
  auto t = new std::thread(
      [](Client* c, std::string server_url,
         std::unordered_map<std::string, std::string> headers) {
        c->IsAliveThreadMain(std::move(server_url), std::move(headers));
      },
      this, m_url, m_http_headers);
  m_isAliveThread.reset(t);
}

void Client::StopIsAliveThread() {
  m_shutdown_flag.store(true);
  if (m_isAliveThread) {
    m_isAliveThread->join();
    m_isAliveThread.reset();
  }
  m_shutdown_flag.store(false);
}

void Client::Startup() {
  ClientBase::Startup();
  StartIsAliveThread();
}

void Client::Shutdown() {
  ClientBase::Shutdown();
  StopIsAliveThread();
}

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

  // Restart the "Is Alive" thread
  StopIsAliveThread();
  StartIsAliveThread();

  SetHeadersInternal(m_ollama, m_http_headers);
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

bool Client::IsRunning() { return m_is_running_flag.load(); }

void Client::SetHeadersInternal(
    Ollama& client,
    const std::unordered_map<std::string, std::string>& headers) {
  httplib::Headers h;
  for (const auto& [header_name, header_value] : headers) {
    h.insert({header_name, header_value});
  }
  client.setHttpHeaders(std::move(h));
}
}  // namespace ollama
