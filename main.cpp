#ifdef __WIN32
#include <winsock2.h>
#endif
#include <iostream>

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

int main() {
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

  // Add external functions from the test mcp server.
  std::vector<std::string> args = {
      R"#(C:\msys64\home\eran\devl\test-mcp\env\bin\python3.exe)#",
      R"#(C:\msys64\home\eran\devl\test-mcp\add.py)#"};

  std::shared_ptr<McpClientStdio> client =
      std::make_shared<McpClientStdio>(args);
  if (client->Initialise()) {
    table.AddMCPServer(client);
  }
  auto& manager = ollama::Manager::GetInstance();
  if (!manager.IsRunning()) {
    std::cerr << "Make sure ollama server is running and try again"
              << std::endl;
    return 1;
  }

  std::cout << (manager.IsRunning() ? "Ollama is running"
                                    : "Ollama is not running")
            << std::endl;

  std::cout << "Available functions:" << std::endl;
  std::cout << "=================" << std::endl;

  ollama::json tools_json = table.ToJSON();
  for (const auto& func_obj : tools_json) {
    std::cout << "- " << func_obj["function"]["name"] << std::endl;
  }

  manager.SetFunctionTable(table);
  auto models = manager.List();
  std::cout << "Available models:" << std::endl;
  std::cout << "=================" << std::endl;
  for (size_t i = 0; i < models.size(); ++i) {
    std::cout << i << ") " << models[i] << std::endl;
  }

  std::cout << "=================" << std::endl;
  if (models.empty()) {
    std::cerr
        << "No models available, please pull at least 1 model and try again."
        << std::endl;
    return 1;
  }

  std::string model_name = models[GetChoiceFromUser(models)];
  while (true) {
    std::string prompt = GetTextFromUser("Ask me anything");
    if (prompt == "q" || prompt == "exit" || prompt == "quit") {
      break;
    }

    std::atomic_bool done{false};
    manager.AsyncChat(
        prompt,
        [&done](std::string output, ollama::Reason reason) {
          switch (reason) {
            case ollama::Reason::kDone:
              std::cout << "\n\nCompleted!" << std::endl;
              done = true;
              break;
            case ollama::Reason::kLogNotice:
              std::cout << "NOTICE: " << output << std::endl;
              break;
            case ollama::Reason::kLogDebug:
              std::cout << "DEBUG: " << output << std::endl;
              break;
            case ollama::Reason::kPartialResult:
              std::cout << output;
              break;
            case ollama::Reason::kFatalError:
              std::cout << "** Fatal error occurred**: " << output << std::endl;
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
