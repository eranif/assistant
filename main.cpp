#include "ollama/logger.hpp"
#ifdef __WIN32
#include <winsock2.h>
#endif
#include <iostream>

#include "ollama/config.hpp"
#include "ollama/mcp_local_process.hpp"
#include "ollama/ollama.hpp"
#include "ollama/tool.hpp"
#include "utils.hpp"

using FunctionTable = ollama::FunctionTable;
using FunctionBuilder = ollama::FunctionBuilder;
using ResponseParser = ollama::ResponseParser;
using McpClientStdio = ollama::MCPStdioClient;

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
  OLogLevel log_level{OLogLevel::kInfo};
  std::string config_file;
};

Args ParseCommandLine(int argc, char** argv) {
  ArgvIter iter(argc - 1, argv + 1);
  Args args;
  while (iter.Valid()) {
    auto arg = iter.GetArgument();
    iter.Next();
    if (arg == "--loglevel" && iter.Valid()) {
      args.log_level = ollama::Logger::FromString(iter.GetArgument());
      iter.Next();
    } else if ((arg == "-c" || arg == "--config") && iter.Valid()) {
      args.config_file = iter.GetArgument();
      iter.Next();
    } else if (arg == "--logfile" && iter.Valid()) {
      args.log_file = iter.GetArgument();
      iter.Next();
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage:" << std::endl;
      std::cout << argv[0]
                << " [--loglevel <LEVEL>] [-c | --config <CONFIG_PATH>] "
                   "[--logfile <LOG_FILE>]"
                << std::endl;
      exit(0);
    }
  }
  return args;
}

std::string WriteFileContent(const ollama::json& args) {
  std::stringstream ss;
  if (args.size() != 2) {
    return "Invalid number of arguments";
  }

  ASSIGN_FUNC_ARG_OR_RETURN(
      std::string filepath,
      ::ollama::GetFunctionArg<std::string>(args, "filepath"));
  ASSIGN_FUNC_ARG_OR_RETURN(
      std::string file_content,
      ::ollama::GetFunctionArg<std::string>(args, "file_content"));

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
  return ss.str();
}

std::string OpenFileInEditor(const ollama::json& args) {
  std::stringstream ss;
  if (args.size() != 1) {
    return "invalid number of arguments";
  }

  ASSIGN_FUNC_ARG_OR_RETURN(
      std::string file_name,
      ::ollama::GetFunctionArg<std::string>(args, "filepath"));
  ss << "file '" << file_name << "' successfully opened file in the editor.";
  return ss.str();
}

int main(int argc, char** argv) {
  auto args = ParseCommandLine(argc, argv);
  if (!args.log_file.empty()) {
    ollama::Logger::Instance().SetLogFile(args.log_file);
  }
  ollama::Logger::Instance().SetLogLevel(args.log_level);

  FunctionTable table;
  table.Add(FunctionBuilder("Open file in editor")
                .SetDescription(
                    "Given a file path, open it inside the editor for editing.")
                .AddRequiredParam("filepath",
                                  "the path of the file on the disk.", "string")
                .SetCallback(OpenFileInEditor)
                .Build());
  table.Add(
      FunctionBuilder("Write file content to disk at a given path")
          .SetDescription("Write file content to disk at a given path. Create "
                          "the file if it does not exist.")
          .AddRequiredParam("filepath", "the path of the file on the disk.",
                            "string")
          .AddRequiredParam("file_content", "the content of the file", "string")
          .SetCallback(WriteFileContent)
          .Build());

  if (!args.config_file.empty()) {
    ollama::Config conf{args.config_file};
    for (const auto& s : conf.GetServers()) {
      if (!s.enabled) {
        continue;
      }
      if (s.type == ollama::kServerKindStdio) {
        std::shared_ptr<McpClientStdio> client{nullptr};
        if (s.IsRemote()) {
          client =
              std::make_shared<McpClientStdio>(s.ssh_login.value(), s.args);
        } else {
          client = std::make_shared<McpClientStdio>(s.args);
        }
        if (client->Initialise()) {
          table.AddMCPServer(client);
        }
      } else {
        OLOG(OLogLevel::kWarning)
            << "Server of type: " << s.type << " are not supported yet";
      }
    }
  }

  auto& ollama_manager = ollama::Manager::GetInstance();
  if (!ollama_manager.IsRunning()) {
    OLOG(OLogLevel::kError)
        << "Make sure ollama server is running and try again";
    return 1;
  }

  std::cout << "Available functions:" << std::endl;
  std::cout << "====================" << std::endl;

  ollama::json tools_json = table.ToJSON();
  for (const auto& func_obj : tools_json) {
    std::cout << "- " << func_obj["function"]["name"] << std::endl;
  }

  ollama_manager.SetFunctionTable(std::move(table));
  auto models = ollama_manager.List();
  std::cout << "Available models:" << std::endl;
  std::cout << "=================" << std::endl;
  for (size_t i = 0; i < models.size(); ++i) {
    std::cout << i << ") " << models[i] << std::endl;
  }

  std::cout << "=================" << std::endl;
  if (models.empty()) {
    OLOG(OLogLevel::kError)
        << "No models available, please pull at least 1 model and try again.";
    return 1;
  }

  std::string model_name = models[GetChoiceFromUser(models)];
  while (true) {
    std::string prompt = GetTextFromUser("Ask me anything");
    if (prompt == "q" || prompt == "exit" || prompt == "quit") {
      break;
    }

    std::atomic_bool done{false};
    ollama_manager.AsyncChat(
        prompt,
        [&done](std::string output, ollama::Reason reason) {
          OLOG(OLogLevel::kDebug)
              << "Chat callback called: reason: " << static_cast<int>(reason);
          switch (reason) {
            case ollama::Reason::kDone:
              std::cout << std::endl;
              OLOG(OLogLevel::kInfo) << "Completed!";
              done = true;
              break;
            case ollama::Reason::kLogNotice:
              OLOG(OLogLevel::kInfo) << output;
              break;
            case ollama::Reason::kLogDebug:
              OLOG(OLogLevel::kDebug) << output;
              break;
            case ollama::Reason::kPartialResult:
              std::cout << output;
              break;
            case ollama::Reason::kFatalError:
              OLOG(OLogLevel::kError)
                  << "** Fatal error occurred**: " << output;
              done = true;
              break;
          }
        },
        model_name);
    while (!done.load(std::memory_order_relaxed)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  return 0;
}
