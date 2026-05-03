#pragma once

#include <optional>
#include <string>
#include <vector>

#include "assistant/function.hpp"

namespace assistant {

class ResponseParser {
 public:
  static std::optional<std::vector<FunctionCall>> GetTools(
      const assistant::response& resp) {
    try {
      json j = resp.as_json();
      std::vector<FunctionCall> calls;
      for (json tool : j["message"]["tool_calls"]) {
        FunctionCall function_call;
        function_call.name = tool["function"]["name"].get<std::string>();
        json arguments = tool["function"]["arguments"];
        function_call.args = std::move(arguments);
        calls.push_back(std::move(function_call));
      }
      return calls;
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }

  static std::optional<assistant::message> GetResponseMessage(
      const assistant::response& resp) {
    try {
      json j = resp.as_json();
      auto msg = assistant::message(j["message"]["role"].get<std::string>(),
                                    j["message"]["content"].get<std::string>());
      if (j["message"].contains("tool_calls")) {
        msg["tool_calls"] = j["message"]["tool_calls"];
      }
      return msg;
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }

  static std::optional<std::string> GetContent(const assistant::response& resp) {
    try {
      json j = resp.as_json();
      return j["message"]["content"].get<std::string>();
    } catch (const std::exception&) {
      return std::nullopt;
    }
  }

  static bool IsDone(const assistant::response& resp) {
    try {
      json j = resp.as_json();
      return j["done"].get<bool>();
    } catch (const std::exception&) {
      return false;
    }
  }
};
}  // namespace assistant
