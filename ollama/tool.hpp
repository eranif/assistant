#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ollama/function.hpp"

namespace assistant {

class ResponseParser {
 public:
  static std::optional<std::vector<FunctionCall>> GetTools(
      const assistant::response& resp) {
    try {
      json j = resp.as_json();
      std::vector<FunctionCall> calls;
      std::vector<json> tools = j["message"]["tool_calls"];
      for (json tool : tools) {
        FunctionCall function_call;
        function_call.name = tool["function"]["name"];
        json arguments = tool["function"]["arguments"];
        function_call.args = std::move(arguments);
        calls.push_back(std::move(function_call));
      }
      return calls;
    } catch (std::exception& e) {
      return std::nullopt;
    }
  }

  static std::optional<assistant::message> GetResponseMessage(
      const assistant::response& resp) {
    try {
      json j = resp.as_json();
      auto msg = assistant::message(j["message"]["role"], j["message"]["content"]);
      if (j["message"].contains("tool_calls")) {
        msg["tool_calls"] = j["message"]["tool_calls"];
      }
      return msg;
    } catch (std::exception& e) {
      return std::nullopt;
    }
  }

  static std::optional<std::string> GetContent(const assistant::response& resp) {
    try {
      json j = resp.as_json();
      return j["message"]["content"];
    } catch (std::exception& e) {
      return std::nullopt;
    }
  }

  static bool IsDone(const assistant::response& resp) {
    try {
      json j = resp.as_json();
      return j["done"];
    } catch (std::exception&) {
    }
    return false;
  }
};
}  // namespace assistant
