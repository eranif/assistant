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
  std::cout << "Worker thread started" << std::endl;
  while (!GetInstance().m_shutdown_flag.load(std::memory_order_relaxed)) {
    if (queue.IsEmpty()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
      continue;
    }
    GetInstance().ProcessContext(queue.Top());
    queue.Pop();
  }
  std::cout << "Worker thread exited" << std::endl;
}

void Manager::ProcessContext(std::shared_ptr<ChatContext> context) {
  try {
    ollama::chat(context->request_, &Manager::OnResponse);
    if (!context->func_calls_.empty()) {
      context->InvokeTools(this);
    }
  } catch (std::exception& e) {
    std::cerr << "ollama::chat threw an exception:" << e.what() << std::endl;
    context->done_cb_("", Reason::kFatalError);
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
      req->done_cb_(content.value(), reason);
    } else if (is_done) {
      req->done_cb_({}, reason);
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
  m_worker.reset();
}

void Manager::Startup() {
  Shutdown();
  m_shutdown_flag.store(false);
  m_worker.reset(new std::thread(Manager::WorkerMain));
}

void Manager::SetUrl(std::string_view url) {
  ollama::setServerURL(
      std::string{kDefaultOllamaUrl.data(), kDefaultOllamaUrl.size()});
}

void Manager::Reset() {
  m_queue.Clear();
  m_functionTable.Clear();
}

void Manager::AsyncChat(std::string msg, OnResponseCallback cb,
                        std::string model) {
  ollama::message ollama_message{"user", msg};
  ollama::options opts;
  opts["temperature"] = 0.0;
  ollama::request req{model, {ollama_message}, opts, true};
  req["tools"] = m_functionTable.ToJSON();
  ChatContext ctx = {
      .done_cb_ = cb,
      .request_ = req,
      .history_ = {ollama_message},
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
      auto result = manager->m_functionTable.Call(func_call);
      // Add the tool response
      ollama::message msg("tool", result);
      msgs.push_back(msg);
    }
  }

  // Send back the tool responses to the AI
  ollama::options opts;
  opts["temperature"] = 0.0;
  ollama::request chat_request{model_, msgs, opts, true};
  history_ = msgs;
  func_calls_.clear();
  chat_request["tools"] = manager->m_functionTable.ToJSON();
  ollama::chat(chat_request, &Manager::OnResponse);
}
}  // namespace ollama
