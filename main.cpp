#include <iostream>

#include "ollama/ollama.hpp"
#include "ollama/tool.hpp"

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
  ss << "file: '" << filepath << "' successfully written to disk!.";
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
  ollama::Manager::GetInstance().SetFunctionTable(table);
  std::cout << (ollama::is_running() ? "Ollama is running"
                                     : "Ollama is not running")
            << std::endl;

  std::atomic<ollama::Reason> ready{ollama::Reason::kPartialResult};
  ollama::Manager::GetInstance().AsyncChat(
      "Create an hello world program in C++. Write the content of the program "
      "into the file main.cpp. In addition, create a CMakeLists.txt file to "
      "build the project. Once the files are written, open them for editiing "
      "in the editor.",
      [&ready](std::string output, ollama::Reason is_done) {
        std::cout << output;
        ready.store(is_done);
        switch (is_done) {
          case ollama::Reason::kDone:
            std::cout << "\n\n Completed! \n\n" << std::endl;
            break;
          case ollama::Reason::kPartialResult:
            break;
          case ollama::Reason::kFatalError:
            std::cout << "\n\n Fatal error occurred!! \n\n" << std::endl;
            break;
        }
      });
  while (ready.load(std::memory_order_relaxed) ==
         ollama::Reason::kPartialResult) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return 0;
}
