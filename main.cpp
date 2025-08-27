#include <iostream>

#include "ollama/ollama.hpp"
#include "ollama/tool.hpp"
#include "utils.hpp"

using FunctionTable = ollama::tool::FunctionTable;
using FunctionBuilder = ollama::tool::FunctionBuilder;
using ParamType = ollama::tool::ParamType;
using ResponseParser = ollama::tool::ResponseParser;
using FunctionArgumentVec = ollama::tool::FunctionArgumentVec;

std::string WriteFileContent(const FunctionArgumentVec& params) {
  std::stringstream ss;
  if (params.GetSize() != 2) {
    return "invalid number of arguments";
  }

  ASSIGN_FUNC_ARG_OR_RETURN(std::string filepath, params.GetArg("filepath"));
  ASSIGN_FUNC_ARG_OR_RETURN(std::string file_content,
                            params.GetArg("file_content"));

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

std::string OpenFileInEditor(const FunctionArgumentVec& params) {
  std::stringstream ss;
  if (params.GetSize() != 1) {
    return "invalid number of arguments";
  }

  ASSIGN_FUNC_ARG_OR_RETURN(std::string file_name, params.GetArg("filepath"));
  ss << "file '" << file_name << "' successfully opened file in the editor.";
  return ss.str();
}

int main() {
  FunctionTable table;
  table.Add(FunctionBuilder("Open file in editor")
                .SetDescription(
                    "Given a file path, open it inside the editor for editing.")
                .AddRequiredParam("filepath",
                                  "the path of the file on the disk.",
                                  ParamType::kString)
                .AddCallback(OpenFileInEditor)
                .Build());
  table.Add(
      FunctionBuilder("Write file content to disk at a given path")
          .SetDescription("Write file content to disk at a given path. Create "
                          "the file if it does not exist.")
          .AddRequiredParam("filepath", "the path of the file on the disk.",
                            ParamType::kString)
          .AddRequiredParam("file_content", "the content of the file",
                            ParamType::kString)
          .AddCallback(WriteFileContent)
          .Build());
  auto& manager = ollama::Manager::GetInstance();
  manager.SetFunctionTable(table);

  manager.PullModel("llama3.1:8b", [](std::string what, ollama::Reason reason) {
    std::cout << what << std::endl;
  });
  std::cout << (manager.IsRunning() ? "Ollama is running"
                                    : "Ollama is not running")
            << std::endl;

  auto models = manager.List();
  std::cout << "Available models:" << std::endl;
  std::cout << "=================" << std::endl;
  for (size_t i = 0; i < models.size(); ++i) {
    std::cout << i << ") " << models[i] << ", Capabilities: ["
              << JoinArray(
                     manager.GetModelCapabilitiesString(models[i]).value(), ",")
              << "]" << std::endl;
  }
  std::cout << "=================" << std::endl;
  if (models.empty()) {
    std::cout << "No models available" << std::endl;
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
              std::cout << "** Fatal error occurred!!**" << std::endl;
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
