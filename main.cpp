#include <iostream>

#include "ollama/ollama.hpp"
#include "tool.hpp"

bool OnReponse(const ollama::response& resp) {
  bool is_done = resp.as_json()["done"];
  if (resp.as_json().contains("message") &&
      resp.as_json()["message"].contains("tool_calls")) {
    std::cout << "Will call: " << resp.as_json()["message"]["tool_calls"]
              << std::endl;
  } else {
    std::string content = resp.as_json()["message"]["content"];
    std::cout << content;
  }
  return !is_done;
}

int main() {
  using FunctionTable = ollama::tool::FunctionTable;
  using FunctionBuilder = ollama::tool::FunctionBuilder;
  using ParamType = ollama::tool::ParamType;

  ollama::show_requests(true);
  ollama::show_replies(false);
  ollama::allow_exceptions(true);
  ollama::setServerURL("http://127.0.0.1:11434");
  ollama::setReadTimeout(300);
  ollama::setWriteTimeout(300);

  ollama::options opts;

  FunctionTable table;
  table.Add(FunctionBuilder("Read file content")
                .SetDescription("Read file content from the disk.")
                .AddRequiredParam("filepath",
                                  "the path of the file on the disk.",
                                  ParamType::kString)
                .Build());
  table.Add(
      FunctionBuilder("Write file content to disk at a given path")
          .SetDescription("Write file content to disk at a given path. Create "
                          "the file if it does not exist.")
          .AddRequiredParam("filepath", "the path of the file on the disk.",
                            ParamType::kString)
          .AddRequiredParam("file_content", "the content of the file",
                            ParamType::kString)
          .Build());

  opts["temperature"] = 0.0;
  ollama::message msg{
      "user",
      "Create an hello world program in C++. Write the content "
      "of the program into the "
      "file main.cpp. In addition, create and write the CMakeLists.txt "
      "file to build the project."};
  std::cout << (ollama::is_running() ? "Ollama is running"
                                     : "Ollama is not running")
            << std::endl;
  ollama::request req{"qwen2.5:7b", {msg}, opts, true};
  req["tools"] = table.ToJSON();
  auto response = ollama::chat(req, OnReponse);
  return 0;
}
