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

  if (argc > 1) {
    ollama::Config conf{argv[1]};
    for (const auto& s : conf.GetServers()) {
      std::shared_ptr<McpClientStdio> client{nullptr};
      if (s.IsRemote()) {
        client = std::make_shared<McpClientStdio>(s.ssh_login.value(), s.args);
      } else {
        client = std::make_shared<McpClientStdio>(s.args);
      }
      if (client->Initialise()) {
        table.AddMCPServer(client);
      }
    }
  }

  auto& ollama_manager = ollama::Manager::GetInstance();
  if (!ollama_manager.IsRunning()) {
    LG_ERROR() << "Make sure ollama server is running and try again";
    return 1;
  }

  LG_INFO() << "Available functions:";
  LG_INFO() << "=================";

  ollama::json tools_json = table.ToJSON();
  for (const auto& func_obj : tools_json) {
    LG_INFO() << "- " << func_obj["function"]["name"] << ": "
              << func_obj["function"]["description"].get<std::string>();
  }

  ollama_manager.SetFunctionTable(std::move(table));
  auto models = ollama_manager.List();
  LG_INFO() << "Available models:";
  LG_INFO() << "=================";
  for (size_t i = 0; i < models.size(); ++i) {
    LG_INFO() << i << ") " << models[i];
  }

  LG_INFO() << "=================";
  if (models.empty()) {
    LG_ERROR()
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
          switch (reason) {
            case ollama::Reason::kDone:
              std::cout << std::endl;
              LG_INFO() << "Completed!";
              done = true;
              break;
            case ollama::Reason::kLogNotice:
              LG_INFO() << output;
              break;
            case ollama::Reason::kLogDebug:
              LG_DEBUG() << output;
              break;
            case ollama::Reason::kPartialResult:
              std::cout << output;
              break;
            case ollama::Reason::kFatalError:
              LG_ERROR() << "** Fatal error occurred**: " << output;
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
