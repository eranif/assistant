#include "ollama.hpp"

#include "ollamalib.hpp"
#include "tool.hpp"

namespace ollama {
using FunctionBuilder = ollama::tool::FunctionBuilder;
using ParamType = ollama::tool::ParamType;
using ResponseParser = ollama::tool::ResponseParser;

constexpr std::string_view kDefaultOllamaUrl = "http://127.0.0.1:11434";

void Manager::WorkerMain() {
  auto& queue = GetInstance().m_queue;
  while (!GetInstance().m_shutdown_flag.load(std::memory_order_relaxed)) {
    if (queue.IsEmpty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    GetInstance().ProcessContext(queue.Top());
    queue.Pop();
  }
}

void Manager::ProcessContext(std::shared_ptr<ChatContext> context) {
  try {
    ollama::chat(context->request_, &Manager::OnResponse);
    if (!context->func_calls_.empty()) {
      context->InvokeTools(this);
    }
  } catch (std::exception& e) {
    std::cerr << "ollama::chat threw an exception:" << e.what() << std::endl;
    context->callback_("", Reason::kFatalError);
    m_queue.Clear();
  }
}

bool Manager::OnResponse(const ollama::response& resp) {
  std::shared_ptr<ChatContext> req = GetInstance().m_queue.Top();
  auto calls = ResponseParser::GetTools(resp);
  bool is_done = ResponseParser::IsDone(resp);
  if (calls.has_value() && !calls.value().empty()) {
    // Add the AI response
    auto ai_message_opt = ResponseParser::GetMessage(resp);
    if (ai_message_opt.has_value()) {
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
  ollama::setServerURL(
      std::string{kDefaultOllamaUrl.data(), kDefaultOllamaUrl.size()});
  m_url = kDefaultOllamaUrl;
  ollama::setReadTimeout(300);
  ollama::setWriteTimeout(300);
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
  m_worker.reset(new std::thread(Manager::WorkerMain));
}

void Manager::SetUrl(const std::string& url) {
  m_url = url;
  ollama::setServerURL(url);
}

void Manager::Reset() {
  m_queue.Clear();
  m_functionTable.Clear();
}

void Manager::AsyncChat(std::string msg, OnResponseCallback cb,
                        std::string model) {
  ollama::message ollama_message{"user", msg};
  CreateAndPushContext({ollama_message}, cb, model);
}

void Manager::CreateAndPushContext(ollama::messages msgs, OnResponseCallback cb,
                                   std::string model) {
  ollama::options opts;
  opts["temperature"] = 0.0;
  ollama::request req{model, msgs, opts, true};
  req["tools"] = m_functionTable.ToJSON();
  ChatContext ctx = {
      .callback_ = cb,
      .request_ = req,
      .history_ = {msgs},
      .model_ = model,
  };
  m_queue.PushBack(std::make_shared<ChatContext>(ctx));
}

void ChatContext::InvokeTools(Manager* manager) {
  if (func_calls_.empty()) {
    return;
  }
  ollama::messages msgs{history_};
  for (auto [msg, calls] : func_calls_) {
    msgs.push_back(msg);
    for (auto func_call : calls) {
      std::stringstream ss;
      ss << "Invoking tool: '" << func_call.name << "', args:\n";
      for (const auto& arg : func_call.args.args) {
        ss << std::setw(2) << "  " << arg.name << " => " << arg.value << "\n";
      }
      callback_(ss.str(), Reason::kLogNotice);
      auto result = manager->m_functionTable.Call(func_call);
      ss = {};
      ss << "Tool output:\n" << result;
      callback_(ss.str(), Reason::kLogNotice);
      // Add the tool response
      ollama::message msg{"tool", result};
      msgs.push_back(msg);
    }
  }
  manager->CreateAndPushContext(msgs, callback_, model_);
}

std::vector<std::string> Manager::List() const {
  if (!IsRunning()) {
    return {};
  }
  try {
    return ollama::list_models();
  } catch (...) {
    return {};
  }
}

json Manager::ListJSON() const {
  if (!IsRunning()) {
    return {};
  }
  try {
    return ollama::list_model_json();
  } catch (...) {
    return {};
  }
}

std::optional<json> Manager::GetModelInfo(const std::string& model) const {
  if (!IsRunning()) {
    return std::nullopt;
  }
  try {
    return ollama::show_model_info(model);
  } catch (std::exception& e) {
    return std::nullopt;
  }
}

std::optional<ModelCapabilities> Manager::GetModelCapabilities(
    const std::string& model) const {
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
    const std::string& model) const {
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

void Manager::PullModel(const std::string& name, OnResponseCallback cb) {
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
}  // namespace ollama
