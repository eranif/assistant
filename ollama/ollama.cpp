#include "ollama.hpp"

#include "ollama/config.hpp"
#include "ollama/cpp-mcp/mcp_logger.h"
#include "ollama/logger.hpp"
#include "ollama/ollamalib.hpp"
#include "ollama/tool.hpp"

namespace ollama {

constexpr std::string_view kDefaultOllamaUrl = "http://127.0.0.1:11434";
constexpr std::string_view kAssistantRole = "assistant";

void Manager::WorkerMain(Manager* manager) {
  auto& queue = manager->m_queue;
  while (!manager->m_shutdown_flag.load(std::memory_order_relaxed)) {
    if (queue.IsEmpty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    manager->ProcessContext(queue.Top());
    queue.Pop();
  }
}

void Manager::ProcessContext(std::shared_ptr<ChatContext> context) {
  try {
    m_ollama.chat(context->request_, &Manager::OnResponse, this);
    if (!context->func_calls_.empty()) {
      context->InvokeTools(this);
    }
  } catch (std::exception& e) {
    context->callback_(e.what(), Reason::kFatalError);
    m_queue.Clear();
  }
}

bool Manager::OnResponse(const ollama::response& resp, void* user_data) {
  Manager* manager = reinterpret_cast<Manager*>(user_data);
  std::shared_ptr<ChatContext> req = manager->m_queue.Top();
  auto calls = ResponseParser::GetTools(resp);
  bool is_done = ResponseParser::IsDone(resp);
  if (calls.has_value() && !calls.value().empty()) {
    // Add the AI response
    auto ai_message_opt = ResponseParser::GetResponseMessage(resp);
    if (ai_message_opt.has_value()) {
      auto func_calls = calls.value();
      req->func_calls_.push_back(
          {ai_message_opt.value(), std::move(calls.value())});
    }
  } else {
    auto content = ResponseParser::GetContent(resp);
    auto reason = (is_done && req->func_calls_.empty())
                      ? Reason::kDone
                      : Reason::kPartialResult;

    if (content.has_value()) {
      req->callback_(content.value(), reason);
    } else if (is_done) {
      req->callback_({}, reason);
    }

    if (content.has_value()) {
      manager->m_current_response += content.value();
    }

    switch (reason) {
      case Reason::kDone:
      case Reason::kFatalError: {
        // Store the AI response, as a message in our history.
        ollama::message msg{std::string{kAssistantRole},
                            manager->m_current_response};
        manager->AddMessage(std::move(msg));
        manager->m_current_response.clear();
      } break;
      default:
        break;
    }
  }
  return !is_done;
}

Manager& Manager::GetInstance() {
  static Manager mgr;
  return mgr;
}

Manager::Manager() {
  ollama::show_requests(false);
  ollama::show_replies(false);
  ollama::allow_exceptions(true);
  m_ollama.setServerURL(
      std::string{kDefaultOllamaUrl.data(), kDefaultOllamaUrl.size()});
  m_url = kDefaultOllamaUrl;
  m_ollama.setReadTimeout(300);
  m_ollama.setWriteTimeout(300);
  mcp::set_log_level(mcp::log_level::error);
  Startup();
}

Manager::~Manager() { Shutdown(); }

void Manager::Shutdown() {
  m_queue.Clear();
  m_shutdown_flag.store(true);
  if (m_worker) {
    m_worker->join();
  }

  if (m_puller_thread) {
    m_puller_thread->join();
  }
  m_puller_busy.store(false);
  m_worker.reset();
  m_puller_thread.reset();
}

void Manager::Startup() {
  Shutdown();
  m_shutdown_flag.store(false);
  m_worker.reset(new std::thread(Manager::WorkerMain, this));
}

void Manager::SetUrl(const std::string& url) {
  m_url = url;
  m_ollama.setServerURL(url);
}

void Manager::Reset() {
  m_queue.Clear();
  m_function_table.Clear();
  m_messages.clear();
  m_current_response.clear();
}

void Manager::ApplyConfig(const ollama::Config* conf) {
  if (!conf) {
    return;
  }
  m_url = conf->GetUrl();
  m_windows_size = conf->GetHistorySize();
  m_function_table.ReloadMCPServers(conf);
  m_model_options = conf->GetModelOptionsMap();
  m_default_model_options = conf->GetModelOptionsMap().find("default")->second;
}

void Manager::AsyncChat(std::string msg, OnResponseCallback cb,
                        std::string model) {
  ollama::message ollama_message{"user", msg};
  CreateAndPushContext(ollama_message, cb, model);
}

void Manager::CreateAndPushContext(std::optional<ollama::message> msg,
                                   OnResponseCallback cb, std::string model) {
  ollama::options opts;

  std::optional<bool> think;
  auto where = m_model_options.find(model);
  if (where == m_model_options.end()) {
    // Can't fail. We always include the "default"
    where = m_model_options.find("default");
  }

  const auto& model_options = where->second;
  for (const auto& [name, value] : model_options.options.items()) {
    opts[name] = value;
  }
  think = model_options.think;

  AddMessage(msg);
  auto history = GetMessages();

  // Build the request
  ollama::request req{model, history, opts, true};
  if (think.has_value()) {
    req["think"] = think.value();
  }

  OLOG(LogLevel::kDebug) << "Pushing message to the queue.";
  OLOG(LogLevel::kInfo) << req;

  if (ModelHasCapability(model, ModelCapabilities::kTooling)) {
    req["tools"] = m_function_table.ToJSON();
  }

  ChatContext ctx = {
      .callback_ = cb,
      .request_ = req,
      .model_ = std::move(model),
  };
  m_queue.PushBack(std::make_shared<ChatContext>(ctx));
}

void ChatContext::InvokeTools(Manager* manager) {
  if (func_calls_.empty()) {
    return;
  }

  for (auto [msg, calls] : func_calls_) {
    manager->AddMessage(std::move(msg));
    for (auto func_call : calls) {
      std::stringstream ss;
      ss << "Invoking tool: '" << func_call.name << "', args:\n";
      auto args = func_call.args.items();
      for (const auto& [name, value] : args) {
        ss << std::setw(2) << "  " << name << " => " << value << "\n";
      }
      callback_(ss.str(), Reason::kLogNotice);
      auto result = manager->m_function_table.Call(func_call);
      ss = {};
      ss << "Tool output: " << result;
      callback_(ss.str(), Reason::kLogNotice);
      // Add the tool response
      ollama::message msg{"tool", result};
      manager->AddMessage(std::move(msg));
    }
  }
  manager->CreateAndPushContext(std::nullopt, callback_, model_);
}

std::vector<std::string> Manager::List() {
  if (!IsRunning()) {
    return {};
  }
  try {
    return m_ollama.list_models();
  } catch (...) {
    return {};
  }
}

json Manager::ListJSON() {
  if (!IsRunning()) {
    return {};
  }
  try {
    return m_ollama.list_model_json();
  } catch (...) {
    return {};
  }
}

std::optional<json> Manager::GetModelInfo(const std::string& model) {
  if (!IsRunning()) {
    return std::nullopt;
  }
  try {
    return m_ollama.show_model_info(model);
  } catch (std::exception& e) {
    return std::nullopt;
  }
}

std::optional<ModelCapabilities> Manager::GetModelCapabilities(
    const std::string& model) {
  auto opt = GetModelInfo(model);
  if (!opt.has_value()) {
    return std::nullopt;
  }

  auto& j = opt.value();
  ModelCapabilities flags{ModelCapabilities::kNone};
  try {
    std::vector<std::string> capabilities = j["capabilities"];
    for (const auto& c : capabilities) {
      if (c == "completion") {
        flags |= ModelCapabilities::kCompletion;
      } else if (c == "tools") {
        flags |= ModelCapabilities::kTooling;
      } else if (c == "thinking") {
        flags |= ModelCapabilities::kThinking;
      } else if (c == "insert") {
        flags |= ModelCapabilities::kInsert;
      } else {
        std::cerr << "unknown capability: " << c << std::endl;
      }
    }
    return flags;
  } catch (...) {
    return std::nullopt;
  }
}
std::optional<std::vector<std::string>> Manager::GetModelCapabilitiesString(
    const std::string& model) {
  auto opt = GetModelInfo(model);
  if (!opt.has_value()) {
    return std::nullopt;
  }

  auto& j = opt.value();
  try {
    return j["capabilities"];
  } catch (...) {
    return std::nullopt;
  }
}

void Manager::AsyncPullModel(const std::string& name, OnResponseCallback cb) {
  bool expected{false};
  // If m_puller_busy is == "expected", change it to "true" and return true.
  if (!m_puller_busy.compare_exchange_strong(expected, true)) {
    cb("Server is busy pulling another model", Reason::kFatalError);
    return;
  }
  std::string url = m_url;
  m_puller_thread =
      std::make_shared<std::thread>([this, name, cb = std::move(cb), url]() {
        try {
          Ollama ol;
          std::stringstream ss;
          ol.setServerURL(url);
          ss << "Pulling model: " << name;
          cb(ss.str(), Reason::kLogNotice);
          ol.pull_model(name, true);
          cb("Model successfully pulled.", Reason::kDone);
          m_puller_busy.store(false);
        } catch (std::exception& e) {
          cb(e.what(), Reason::kFatalError);
        }
      });
}

void Manager::AddMessage(std::optional<ollama::message> msg) {
  if (!msg.has_value()) {
    return;
  }

  m_messages.push_back(std::move(*msg));
  // truncate the history to fit the window size
  if (m_messages.size() >= m_windows_size) {
    m_messages.erase(m_messages.begin());
  }
}

ollama::messages ollama::Manager::GetMessages() const { return m_messages; }

bool ollama::Manager::ModelHasCapability(const std::string& model_name,
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
}  // namespace ollama
