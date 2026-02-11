/// A demo program for using the "assistant" library.

#include "assistant/logger.hpp"
#ifdef __WIN32
#include <winsock2.h>
#endif
#include <chrono>
#include <iostream>
#include <thread>

#include "assistant/assistant.hpp"
#include "utils.hpp"

using FunctionTable = assistant::FunctionTable;
using FunctionBuilder = assistant::FunctionBuilder;
using ResponseParser = assistant::ResponseParser;

namespace {

const std::string_view kCyan = "\033[36m";
const std::string_view kReset = "\033[0m";
const std::string_view kYellow = "\033[33m";
const std::string_view kGray = "\033[37m";

std::string Cyan(std::string_view word) {
  std::stringstream ss;
  ss << kCyan << word << kReset;
  return ss.str();
}

std::string Gray(std::string_view word) {
  std::stringstream ss;
  ss << kGray << word << kReset;
  return ss.str();
}

std::string Yellow(std::string_view word) {
  std::stringstream ss;
  ss << kYellow << word << kReset;
  return ss.str();
}

std::mutex prompt_queue_mutex;
std::vector<std::pair<std::string, assistant::ChatOptions>> prompt_queue;
std::condition_variable cv;

/// Push prompt to the queue
void push_prompt(std::string prompt, assistant::ChatOptions options) {
  std::unique_lock lk{prompt_queue_mutex};
  prompt_queue.push_back({std::move(prompt), std::move(options)});
  cv.notify_one();
}

/// Get prompt from the queue
std::optional<std::pair<std::string, assistant::ChatOptions>> pop_prompt() {
  std::unique_lock lk{prompt_queue_mutex};
  auto res = cv.wait_for(lk, std::chrono::milliseconds(500),
                         []() { return !prompt_queue.empty(); });
  if (!res || prompt_queue.empty()) {
    return std::nullopt;
  }

  auto item = std::move(prompt_queue.front());
  prompt_queue.erase(prompt_queue.begin());
  return item;
}

}  // namespace
struct ArgvIter {
 public:
  ArgvIter(int argc, char** argv) : m_argc(argc), m_argv(argv) {}
  inline bool Valid() const { return m_current_pos < m_argc; }
  inline std::string GetArgument() const {
    return std::string{m_argv[m_current_pos]};
  }
  inline void Next() { ++m_current_pos; }

 private:
  int m_argc{0};
  char** m_argv{nullptr};
  int m_current_pos{0};
};

struct Args {
  std::string log_file;
  bool verbose{false};
  bool enable_builtin_mcps{true};
  OLogLevel log_level{OLogLevel::kInfo};
  std::string config_file;
};

Args ParseCommandLine(int argc, char** argv) {
  ArgvIter iter(argc - 1, argv + 1);
  Args args;
  while (iter.Valid()) {
    auto arg = iter.GetArgument();
    iter.Next();
    if ((arg == "--loglevel" || arg == "--log-level") && iter.Valid()) {
      args.log_level = assistant::Logger::FromString(iter.GetArgument());
      iter.Next();
    } else if ((arg == "-c" || arg == "--config") && iter.Valid()) {
      args.config_file = iter.GetArgument();
      iter.Next();
    } else if (arg == "--logfile" && iter.Valid()) {
      args.log_file = iter.GetArgument();
      iter.Next();
    } else if ((arg == "-v" || arg == "--verbose")) {
      std::cout << "Verbose mode enabled" << std::endl;
      args.verbose = true;
    } else if (arg == "--no-builtin-mcp") {
      std::cout << "Built In MCPs are disabled" << std::endl;
      args.enable_builtin_mcps = false;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage:" << std::endl;
      std::cout << argv[0]
                << " [--loglevel <LEVEL>] [-c | --config <CONFIG_PATH>] "
                   "[--logfile <LOG_FILE>] [-v | --verbose] [--no-builtin-mcp]"
                << std::endl;
      exit(0);
    }
  }
  return args;
}

assistant::FunctionResult WriteFileContent(const assistant::json& args) {
  std::stringstream ss;
  if (args.size() != 2) {
    return assistant::FunctionResult{.isError = true,
                                     .text = "Invalid number of arguments"};
  }

  ASSIGN_FUNC_ARG_OR_RETURN(
      std::string filepath,
      ::assistant::GetFunctionArg<std::string>(args, "filepath"));
  ASSIGN_FUNC_ARG_OR_RETURN(
      std::string file_content,
      ::assistant::GetFunctionArg<std::string>(args, "file_content"));

  auto res = CreateDirectoryForFile(filepath);
  if (!res.IsOk()) {
    ss << "Error creating directory for file: '" << filepath << "' to disk. "
       << res.GetError();
  } else {
    std::ofstream out_file(filepath);
    if (out_file.is_open()) {
      // 3. Write data to the file using the insertion operator (<<).
      out_file << file_content;
      out_file.flush();
      out_file.close();
      ss << "file: '" << filepath << "' successfully written to disk!.";
    } else {
      ss << "Error while writing file: '" << filepath << "' to disk.";
    }
  }
  return assistant::FunctionResult{.text = ss.str()};
}

assistant::FunctionResult ToolReadFileContent(const assistant::json& args) {
  std::stringstream ss;
  if (args.size() != 1) {
    return assistant::FunctionResult{.isError = true,
                                     .text = "Invalid number of arguments"};
  }

  ASSIGN_FUNC_ARG_OR_RETURN(
      std::string filepath,
      ::assistant::GetFunctionArg<std::string>(args, "filepath"));

  auto res = ReadFileContent(filepath);
  if (!res.IsOk()) {
    ss << "Error creating directory for file: '" << filepath << "' to disk. "
       << res.GetError();
    return assistant::FunctionResult{.isError = true, .text = ss.str()};
  }
  return assistant::FunctionResult{.isError = false, .text = res.GetValue()};
}

assistant::FunctionResult OpenFileInEditor(const assistant::json& args) {
  std::stringstream ss;
  if (args.size() != 1) {
    return assistant::FunctionResult{.isError = true,
                                     .text = "Invalid number of arguments"};
  }

  ASSIGN_FUNC_ARG_OR_RETURN(
      std::string file_name,
      ::assistant::GetFunctionArg<std::string>(args, "filepath"));
  ss << "file '" << file_name << "' successfully opened file in the editor.";
  return assistant::FunctionResult{.text = ss.str()};
}

bool CanRunTool(const std::string& tool_name) {
  std::stringstream prompt;
  prompt << "The model wants to run tool: \"" << tool_name
         << "\", allow it [y/n]?";
  return ReadYesOrNoFromUser(prompt.str());
}

void HandlePrompt(std::shared_ptr<assistant::ClientBase> cli,
                  const std::string& model_name, const std::string& prompt,
                  assistant::ChatOptions options) {
  std::atomic_bool done{false};
  std::atomic_bool saved_thinking_state{false};
  cli->Chat(
      prompt,
      [&done, &saved_thinking_state](
          std::string output, assistant::Reason reason, bool thinking) -> bool {
        if (saved_thinking_state != thinking) {
          // we switched state
          if (thinking) {
            // the new state is "thinking"
            std::cout << Cyan("Thinking... ") << std::endl;
          } else {
            std::cout << Cyan("... done thinking") << std::endl;
          }
        }

        saved_thinking_state = thinking;
        switch (reason) {
          case assistant::Reason::kDone:
            std::cout << std::endl;
            OLOG(OLogLevel::kInfo) << "Completed!";
            done = true;
            break;
          case assistant::Reason::kLogNotice:
            OLOG(OLogLevel::kInfo) << output;
            break;
          case assistant::Reason::kLogDebug:
            OLOG(OLogLevel::kDebug) << output;
            break;
          case assistant::Reason::kPartialResult:
            if (thinking) {
              std::cout << Gray(output);
            } else {
              std::cout << output;
            }
            std::cout.flush();
            break;
          case assistant::Reason::kFatalError:
            OLOG(OLogLevel::kError) << output;
            done = true;
            break;
          case assistant::Reason::kCancelled:
            OLOG(OLogLevel::kWarning) << output;
            done = true;
            break;
        }
        // continue
        return true;
      },
      options);
  std::cout << "\n>";
  std::cout.flush();
}

int main(int argc, char** argv) {
#ifdef __WIN32
  SetConsoleOutputCP(65001);
#endif
  auto args = ParseCommandLine(argc, argv);
  if (!args.log_file.empty()) {
    assistant::SetLogFile(args.log_file);
  }

  assistant::SetLogLevel(assistant::LogLevel::kError);
  std::optional<assistant::Config> conf;
  if (!args.config_file.empty()) {
    conf = assistant::Config::FromFile(args.config_file);
    if (!conf) {
      std::cerr << "Failed to parse configuration file." << std::endl;
      return 1;
    }
    assistant::SetLogLevel(conf.value().GetLogLevel());
  }

  auto cli_opt = assistant::MakeClient(conf);
  if (!cli_opt.has_value()) {
    std::cerr << "Failed to create client." << std::endl;
    return 1;
  }
  std::shared_ptr<assistant::ClientBase> cli = cli_opt.value();

  if (args.enable_builtin_mcps) {
    cli->GetFunctionTable().Add(
        FunctionBuilder("Open_file_in_editor")
            .SetDescription(
                "Given a file path, open it inside the editor for editing.")
            .AddRequiredParam("filepath", "the path of the file on the disk.",
                              "string")
            .SetCallback(OpenFileInEditor)
            .Build());

    cli->GetFunctionTable().Add(
        FunctionBuilder("Write_file_content_to_disk_at_a_given_path")
            .SetDescription(
                "Write file content to disk at a given path. Create "
                "the file if it does not exist.")
            .AddRequiredParam("filepath", "the path of the file on the disk.",
                              "string")
            .AddRequiredParam("file_content", "the content of the file",
                              "string")
            .SetCallback(WriteFileContent)
            .Build());

    cli->GetFunctionTable().Add(
        FunctionBuilder("Read_file_content_from_a_given_path")
            .SetDescription("Read file content from the disk at a given path.")
            .AddRequiredParam("filepath", "the path of the file on the disk.",
                              "string")
            .SetCallback(ToolReadFileContent)
            .Build());
  }

  // Set a Human-In-Loop callback.
  cli->SetTookInvokeCallback(CanRunTool);

  std::cout << "Waiting for: " << cli->GetUrl() << " to become available..."
            << std::endl;

  while (true) {
    if (cli->IsRunning()) {
      OLOG(OLogLevel::kInfo) << "Server: " << cli->GetUrl() << " is running!";
      break;
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  std::cout << "\n";
  std::cout << "Available functions:" << std::endl;
  std::cout << "====================" << std::endl;

  std::cout << cli->GetFunctionTable() << std::endl;

  std::cout << "Using Model " << Cyan(cli->GetModel()) << std::endl;
  std::string model_name = cli->GetModel();

  std::cout << "" << std::endl;
  std::cout << Yellow("#") << " Interactive session started." << std::endl;
  std::cout << Yellow("#") << " Type " << Cyan("q") << ", " << Cyan("quit")
            << " or " << Cyan("exit") << " to exit." << std::endl;
  std::cout << Yellow("#") << " Type " << Cyan("clear") << " or "
            << Cyan("reset") << " to clear the session." << std::endl;
  std::cout << Yellow("#") << " Type " << Cyan("/info")
            << " to get model information." << std::endl;
  std::cout << Yellow("#") << " To read prompt from a file, use " << Cyan("@")
            << "filename followed by ENTER" << std::endl;
  std::cout << Yellow("#") << " Use " << Cyan("/no_tools")
            << " to disable tool calls." << std::endl;
  std::cout << Yellow("#") << " Use " << Cyan("/chat_defaults")
            << " to restore chat options to default." << std::endl;
  std::cout << Yellow("#") << " Use " << Cyan("/int")
            << " to interrupt the connection." << std::endl;
  std::cout << "" << std::endl;

  std::cout << ">";
  std::cout.flush();
  assistant::ChatOptions options{assistant::ChatOptions::kDefault};

  while (true) {
    std::string prompt = GetTextFromUser();
    if (prompt.empty()) {
      continue;
    }
    if (prompt == "q" || prompt == "exit" || prompt == "quit") {
      break;
    } else if (prompt == "/no_tools") {
      assistant::AddFlagSet(options, assistant::ChatOptions::kNoTools);
      std::cout << "Tools are disabled" << std::endl;
      std::cout << "\n>";
      std::cout.flush();
      continue;
    } else if (prompt == "/int") {
      cli->Interrupt();
      std::cout << "Main Thread: Interrupted" << std::endl;
      break;
    } else if (prompt == "/chat_defaults") {
      options = assistant::ChatOptions::kDefault;
      std::cout << "Chat options restored to defaults." << std::endl;
      std::cout << "\n>";
      std::cout.flush();
      continue;
    } else if (prompt == "/info") {
      auto model_options = cli->GetModelInfo(model_name);
      if (model_options.has_value()) {
        std::cout << std::setw(2) << model_options.value()["capabilities"]
                  << std::endl;
        std::cout << std::setw(2) << model_options.value()["model_info"]
                  << std::endl;
        std::cout << "\n>";
        std::cout.flush();
      } else {
        std::cerr << "Could not loading information for model: " << model_name
                  << std::endl;
        std::cout << "\n>";
        std::cout.flush();
      }
      continue;
    } else if (prompt == "clear" || prompt == "reset") {
      // Clear the session
      cli->Shutdown();
      std::cout << "Session cleared." << std::endl;
      std::cout << "\n>";
      std::cout.flush();
      continue;
    }

    if (prompt.starts_with("@")) {
      auto content = ReadFileContent(prompt.substr(1));
      if (!content.IsOk()) {
        std::cerr << "Error reading prompt. " << content.GetError()
                  << std::endl;
        std::cout << "\n>";
        std::cout.flush();
        continue;
      } else {
        prompt = content.GetValue();
      }
    }
    HandlePrompt(cli, model_name, prompt, options);
  }
  return 0;
}
