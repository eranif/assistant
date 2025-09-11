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
    OLOG(LogLevel::kDebug) << "==> " << context->request_;

    std::string model_name = context->request_["model"].get<std::string>();

    // Prepare chat user data.
    ChatUserData user_data{.manager = this, .model = model_name};
    user_data.model_can_think =
        ModelHasCapability(model_name, ModelCapabilities::kThinking);
    if (user_data.model_can_think) {
      auto iter = m_model_options.find(model_name);
      if (iter != m_model_options.end()) {
        const auto& mo = iter->second;
        user_data.thinking_start_tag = mo.think_start_tag;
        user_data.thinking_end_tag = mo.think_end_tag;
      }
    }

    m_ollama.chat(context->request_, &Manager::OnResponse,
                  static_cast<void*>(&user_data));
    if (!context->func_calls_.empty()) {
      context->InvokeTools(this);
    }
  } catch (std::exception& e) {
    context->callback_(e.what(), Reason::kFatalError, false);
    SoftReset();
  }
}

bool Manager::HandleResponse(const ollama::response& resp,
                             ChatUserData& chat_user_data) {
  std::shared_ptr<ChatContext> req = m_queue.Top();
  if (req == nullptr) {
    OLOG(LogLevel::kError) << "No matching request for the current response!";
    return false;
  }
  if (m_interrupt.load(std::memory_order_relaxed)) {
    m_interrupt.store(false, std::memory_order_relaxed);
    req->callback_("Request cancelled by user", ollama::Reason::kCancelled,
                   false);
    return false;
  }
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

    bool token_is_part_of_thinking_process = chat_user_data.thinking;
    if (chat_user_data.model_can_think && content.has_value()) {
      if (chat_user_data.thinking &&
          content.value() == chat_user_data.thinking_end_tag) {
        // we change the inner state to "not thinking"
        // but we want this token to be reported as "thinking" to the caller.
        chat_user_data.thinking = false;
        token_is_part_of_thinking_process = true;
      } else if (!chat_user_data.thinking &&
                 content.value() == chat_user_data.thinking_start_tag) {
        // Thinking process started.
        chat_user_data.thinking = true;
        token_is_part_of_thinking_process = true;
      }
    }

    if (content.has_value()) {
      req->callback_(content.value(), reason,
                     token_is_part_of_thinking_process);
    } else if (is_done) {
      req->callback_({}, reason, token_is_part_of_thinking_process);
    }

    if (content.has_value()) {
      m_current_response += content.value();
    }

    switch (reason) {
      case Reason::kDone:
      case Reason::kFatalError: {
        // Store the AI response, as a message in our history.
        ollama::message msg{std::string{kAssistantRole}, m_current_response};
        OLOG(LogLevel::kDebug) << "<== " << msg;
        AddMessage(std::move(msg));
        m_current_response.clear();
      } break;
      default:
        break;
    }
  }
  return !is_done;
}

bool Manager::OnResponse(const ollama::response& resp, void* user_data) {
  ChatUserData* cud = reinterpret_cast<ChatUserData*>(user_data);
  return cud->manager->HandleResponse(resp, *cud);
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
  m_interrupt.store(true, std::memory_order_relaxed);
  m_shutdown_flag.store(true);
  if (m_worker) {
    m_worker->join();
  }

  if (m_model_puller_thread) {
    m_model_puller_thread->join();
  }
  m_puller_busy.store(false);
  m_worker.reset();
  m_model_puller_thread.reset();
}

void Manager::Startup() {
  m_shutdown_flag.store(false);
  m_worker.reset(new std::thread(Manager::WorkerMain, this));
  m_interrupt.store(false, std::memory_order_relaxed);
}

void Manager::Interrupt() {
  m_interrupt.store(true, std::memory_order_relaxed);
  m_ollama.interrupt();
}

void Manager::SetUrl(const std::string& url) {
  m_url = url;
  m_ollama.setServerURL(url);
}

void Manager::SoftReset() {
  m_queue.Clear();
  m_messages.clear();
  m_current_response.clear();
}

void Manager::Reset() {
  SoftReset();
  m_function_table.Clear();
  ClearSystemMessages();
  m_interrupt.store(false, std::memory_order_relaxed);
}

void Manager::ApplyConfig(const ollama::Config* conf) {
  if (!conf) {
    return;
  }
  SetUrl(conf->GetUrl());
  m_windows_size = conf->GetHistorySize();
  m_function_table.ReloadMCPServers(conf);
  m_model_options = conf->GetModelOptionsMap();
  m_default_model_options = Config::CreaetDefaultModelOptions();
  auto iter = conf->GetModelOptionsMap().find("default");
  if (iter != conf->GetModelOptionsMap().end()) {
    m_default_model_options = iter->second;
  }
  SetLogLevel(conf->GetLogLevel());
  SetHeaders(conf->GetHeaders());
}

void Manager::AsyncChat(std::string msg, OnResponseCallback cb,
                        std::string model) {
  ollama::message ollama_message{"user", msg};
  CreateAndPushContext(ollama_message, cb, model);
}

void Manager::CreateAndPushContext(std::optional<ollama::message> msg,
                                   OnResponseCallback cb, std::string model) {
  ollama::options opts;

  std::optional<bool> think, hidethinking;
  auto where = m_model_options.find(model);
  if (where == m_model_options.end()) {
    // Can't fail. We always include the "default"
    where = m_model_options.find("default");
  }

  ModelOptions model_options;
  if (where == m_model_options.end()) {
    OLOG(LogLevel::kWarning) << "Missing 'default' model setup in "
                                "configuration file. Creating and using one.";
    model_options = Config::CreaetDefaultModelOptions();
    m_model_options.insert({model, model_options});
  } else {
    model_options = where->second;
  }
  for (const auto& [name, value] : model_options.options.items()) {
    opts[name] = value;
  }
  think = model_options.think;
  hidethinking = model_options.hidethinking;

  AddMessage(msg);
  auto history = GetMessages();

  // Build the request
  ollama::request req{model, history, opts, true};
  if (think.has_value()) {
    req["think"] = think.value();
  }

  if (hidethinking.has_value()) {
    req["hidethinking"] = hidethinking.value();
  }

  if (ModelHasCapability(model, ModelCapabilities::kTooling)) {
    req["tools"] = m_function_table.ToJSON();
  } else {
    OLOG(LogLevel::kWarning)
        << "The selected model: " << model << " does not support 'tools'";
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

      callback_(ss.str(), Reason::kLogNotice, false);
      auto result = manager->m_function_table.Call(func_call);
      ss = {};
      ss << "Tool output: " << result;
      callback_(ss.str(), Reason::kLogNotice, false);

      ss = {};
      // Add the tool response
      if (result.isError) {
        ss << "An error occurred while executing tool: '" << func_call.name
           << "'. Reason: " << result.text;
      } else {
        ss << "Tool '" << func_call.name
           << "' completed successfully. Output:\n"
           << result.text;
      }
      ollama::message msg{"tool", ss.str()};
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
    OLOG(LogLevel::kInfo) << "Fetching info for model: " << model;
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
  OLOG(LogLevel::kTrace) << "Model info:";
  OLOG(LogLevel::kTrace) << std::setw(2) << j["capabilities"];
  OLOG(LogLevel::kTrace) << std::setw(2) << j["model_info"];

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
    cb("Server is busy pulling another model", Reason::kFatalError, false);
    return;
  }
  std::string url = m_url;
  m_model_puller_thread =
      std::make_shared<std::thread>([this, name, cb = std::move(cb), url]() {
        try {
          Ollama ol;
          std::stringstream ss;
          ol.setServerURL(url);
          ss << "Pulling model: " << name;
          cb(ss.str(), Reason::kLogNotice, false);
          ol.pull_model(name, true);
          cb("Model successfully pulled.", Reason::kDone, false);
          m_puller_busy.store(false);
        } catch (std::exception& e) {
          cb(e.what(), Reason::kFatalError, false);
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

ollama::messages Manager::GetMessages() const {
  ollama::messages msgs;
  msgs.reserve(m_system_messages.size() + m_messages.size());

  if (!m_system_messages.empty()) {
    msgs.insert(msgs.end(), m_system_messages.begin(), m_system_messages.end());
  }
  if (!m_messages.empty()) {
    msgs.insert(msgs.end(), m_messages.begin(), m_messages.end());
  }
  return msgs;
}

bool Manager::IsRunning() {
  m_ollama.setServerURL(m_url);
  return m_ollama.is_running();
}

bool Manager::ModelHasCapability(const std::string& model_name,
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
void Manager::SetHeaders(
    const std::unordered_map<std::string, std::string>& headers) {
  httplib::Headers h;
  for (const auto& [header_name, header_value] : headers) {
    OLOG(LogLevel::kInfo) << "Adding HTTP header: " << header_name << ": "
                          << header_value;
    h.insert({header_name, header_value});
  }
  m_ollama.setHttpHeaders(std::move(h));
}

void Manager::AddSystemMessage(const std::string& msg) {
  m_system_messages.push_back(ollama::message{"system", msg});
}

void Manager::ClearSystemMessages() { m_system_messages.clear(); }
}  // namespace ollama
