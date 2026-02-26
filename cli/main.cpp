/// A demo program for using the "assistant" library.

#include "assistant/logger.hpp"
#ifdef __WIN32
#include <winsock2.h>
#endif
#include <chrono>
#include <iostream>
#include <thread>

#include "assistant/Process.hpp"
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
  bool print_to_stdout{true};
  bool enable_builtin_mcps{true};
  std::optional<OLogLevel> log_level{std::nullopt};
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
      std::cout << "Using Log Level: " << iter.GetArgument() << std::endl;
      iter.Next();
    } else if ((arg == "-c" || arg == "--config") && iter.Valid()) {
      args.config_file = iter.GetArgument();
      iter.Next();
    } else if (arg == "--logfile" && iter.Valid()) {
      args.log_file = iter.GetArgument();
      iter.Next();
    } else if ((arg == "-s" || arg == "--silence")) {
      args.print_to_stdout = false;
    } else if (arg == "--no-builtin-mcp") {
      args.enable_builtin_mcps = false;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage:" << std::endl;
      std::cout << argv[0]
                << " [--loglevel <LEVEL>] [-c | --config <CONFIG_PATH>] "
                   "[--logfile <LOG_FILE>] [-s | --silence] [--no-builtin-mcp]"
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
  prompt << "\n\xE2\x9D\x93 The model wants to run tool: \"" << tool_name
         << "\", allow it [y/n]?";
  return ReadYesOrNoFromUser(prompt.str());
}

static Args args;

void PrintPrompt() {
  if (!args.print_to_stdout) {
    return;
  }
  std::cout << "\n> ";
  std::cout.flush();
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
            OLOG_INFO() << "Completed!";
            done = true;
            break;
          case assistant::Reason::kLogNotice:
            OLOG_INFO() << output;
            break;
          case assistant::Reason::kLogDebug:
            OLOG_DEBUG() << output;
            break;
          case assistant::Reason::kPartialResult:
            if (thinking) {
              std::cout << Gray(output);
            } else {
              std::cout << output;
            }
            std::cout.flush();
            break;
          case assistant::Reason::kRequestCost:
            std::cout << "\n\n" << Gray(output) << std::endl;
            break;
          case assistant::Reason::kFatalError:
            OLOG_ERROR() << output;
            done = true;
            break;
          case assistant::Reason::kCancelled:
            OLOG_WARN() << output;
            done = true;
            break;
        }
        // continue
        return true;
      },
      options);
  PrintPrompt();
}

int main(int argc, char** argv) {
#ifdef __WIN32
  SetConsoleOutputCP(65001);
#endif
  args = ParseCommandLine(argc, argv);
  if (!args.log_file.empty()) {
    assistant::SetLogFile(args.log_file);
  }

  assistant::SetLogLevel(assistant::LogLevel::kError);
  std::optional<assistant::Config> conf;
  if (!args.config_file.empty()) {
    auto result = assistant::ConfigBuilder::FromFile(args.config_file);
    if (!result.ok()) {
      std::cerr << "Failed to parse configuration file. " << result.errmsg_
                << std::endl;
      return 1;
    }
    conf = result.config_.value();
  }

  if (args.log_level.has_value()) {
    assistant::SetLogLevel(args.log_level.value());
  } else if (conf.has_value()) {
    assistant::SetLogLevel(conf.value().GetLogLevel());
  }

  auto cli_opt = assistant::MakeClient(conf);
  if (!cli_opt.has_value()) {
    std::cerr << "Failed to create client." << std::endl;
    return 1;
  }
  std::shared_ptr<assistant::ClientBase> cli = cli_opt.value();

  // Simulate cost based on claude-sonnet-4.5
  auto pricing = assistant::FindPricing("claude-sonnet-4-5");
  if (pricing.has_value()) {
    cli->SetPricing(pricing.value());
  }

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
  cli->AddSystemMessage("You are an expert C++ & Rust coder");
  cli->AddSystemMessage("Always answer briefly.");
  cli->AddSystemMessage("If you use markdown, prefer bullets over tables.");

  if (args.print_to_stdout) {
    std::cout << "Waiting for: " << cli->GetUrl() << " to become available..."
              << std::endl;
  }

  assistant::Process::EnableExecLog(false);

  while (true) {
    if (cli->IsRunning()) {
      if (args.print_to_stdout) {
        std::cout << "Server: " << cli->GetUrl() << " is running!" << std::endl;
      }
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::string model_name = cli->GetModel();
  if (args.print_to_stdout) {
    std::cout << "\n";
    std::cout << "Available functions:" << std::endl;
    std::cout << "====================" << std::endl;
    std::cout << cli->GetFunctionTable() << std::endl;
    std::cout << "Using Model " << Cyan(cli->GetModel()) << std::endl;
    std::cout << "" << std::endl;
    std::cout << Yellow("#") << " Interactive session started." << std::endl;
    std::cout << Yellow("#") << " Type " << Cyan("q") << ", " << Cyan("quit")
              << " or " << Cyan("exit") << " to exit." << std::endl;
    std::cout << Yellow("#") << " Type " << Cyan("/info")
              << " to get model information." << std::endl;
    std::cout << Yellow("#") << " Type " << Cyan("/default")
              << " restore to chat default options." << std::endl;
    std::cout << Yellow("#") << " To read prompt from a file, use " << Cyan("@")
              << "filename followed by ENTER" << std::endl;
    std::cout << Yellow("#") << " Use " << Cyan("/no_tools")
              << " to disable tool calls." << std::endl;
    std::cout << Yellow("#") << " Use " << Cyan("/no_history")
              << " to run requests without storing them in the history"
              << std::endl;
    std::cout
        << Yellow("#") << " Use " << Cyan("/reset")
        << " to restore chat options to default and clear the chat history."
        << std::endl;
    std::cout << Yellow("#") << " Use " << Cyan("/int")
              << " to interrupt the connection." << std::endl;
    std::cout << Yellow("#") << " Use " << Cyan("/cache_static")
              << " to cache static content" << std::endl;
    std::cout << Yellow("#") << " Use " << Cyan("/cache_auto")
              << " to enable static caching" << std::endl;
    std::cout << Yellow("#") << " Use " << Cyan("/cache_none")
              << " to disable caching" << std::endl;
    std::cout << "" << std::endl;
    PrintPrompt();
  }

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
      std::cout << ">> Tools are disabled" << std::endl;
      PrintPrompt();
      continue;
    } else if (prompt == "/int") {
      cli->Interrupt();
      break;
    } else if (prompt == "/cache_static") {
      cli->SetCachingPolicy(assistant::CachePolicy::kStatic);
      std::cout << ">> Static caching is enabled" << std::endl;
      PrintPrompt();
      continue;
    } else if (prompt == "/cache_auto") {
      cli->SetCachingPolicy(assistant::CachePolicy::kAuto);
      std::cout << ">> Auto caching is enabled" << std::endl;
      PrintPrompt();
      continue;
    } else if (prompt == "/cache_none") {
      cli->SetCachingPolicy(assistant::CachePolicy::kNone);
      std::cout << ">> Cache is disabled" << std::endl;
      PrintPrompt();
      continue;
    } else if (prompt == "/no_history") {
      std::cout << ">> History is disabled!" << std::endl;
      assistant::AddFlagSet(options, assistant::ChatOptions::kNoHistory);
      PrintPrompt();
      continue;
    } else if (prompt == "/reset") {
      cli->ClearHistoryMessages();
      cli->ClearMessageQueue();
      options = assistant::ChatOptions::kDefault;
      if (args.print_to_stdout) {
        std::cout
            << ">> Chat history is cleared + options restored to defaults."
            << std::endl;
      }
      PrintPrompt();
      continue;
    } else if (prompt == "/default") {
      options = assistant::ChatOptions::kDefault;
      if (args.print_to_stdout) {
        std::cout << ">> Chat options restored to defaults." << std::endl;
      }
      PrintPrompt();
      continue;
    } else if (prompt == "/info") {
      auto model_options = cli->GetModelInfo(model_name);
      if (model_options.has_value()) {
        std::cout << std::setw(2) << model_options.value()["capabilities"]
                  << std::endl;
        std::cout << std::setw(2) << model_options.value()["model_info"]
                  << std::endl;
        PrintPrompt();
      } else {
        std::cerr << ">> Could not loading information for model: "
                  << model_name << std::endl;
        PrintPrompt();
      }
      continue;
    }

    if (prompt.starts_with("@")) {
      auto content = ReadFileContent(prompt.substr(1));
      if (!content.IsOk()) {
        std::cerr << "Error reading prompt. " << content.GetError()
                  << std::endl;
        PrintPrompt();
        continue;
      } else {
        prompt = content.GetValue();
      }
    }
    HandlePrompt(cli, model_name, prompt, options);
  }
  return 0;
}
