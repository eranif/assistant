#pragma once

#include <vector>

#include "assistant/assistantlib.hpp"
#include "assistant/cpp-mcp/mcp_tool.h"
#include "assistant/function_base.hpp"

namespace assistant {
class MCPClient;

using FunctionSignature = std::function<FunctionResult(const json&)>;

class InProcessFunction : public FunctionBase {
 public:
  InProcessFunction(const std::string& name, const std::string& desc)
      : FunctionBase(name, desc) {}
  FunctionResult Call(const json& args) const override {
    return m_callback(args);
  }

  /**
   * Determines whether this action can be executed based on human-in-the-loop
   * callback.
   *
   * Queries the registered human-in-the-loop callback to determine if the
   * action identified by GetName() is permitted to run with the given
   * arguments. If no callback is registered, returns std::nullopt to indicate
   * an indeterminate state.
   *
   * Parameters:
   *   args - JSON object containing the arguments to be passed to the action.
   *
   * Returns:
   *   CanInvokeToolResult or std::nullopt if no human-in-the-loop callback is
   * registered.
   */
  inline std::optional<CanInvokeToolResult> CanRun(
      const json& args) const override {
    if (m_humanInTheLoopCB) {
      return m_humanInTheLoopCB(GetName(), args);
    }
    return std::nullopt;
  }

 protected:
  FunctionSignature m_callback;
  OnToolInvokeCallback m_humanInTheLoopCB{nullptr};
  friend class FunctionBuilder;
};

class ExternalFunction : public FunctionBase {
 public:
  ExternalFunction(assistant::MCPClient* client, mcp::tool t);
  FunctionResult Call(const json& args) const override;

 protected:
  assistant::MCPClient* m_client{nullptr};
  mcp::tool m_tool;
};

class FunctionBuilder {
 public:
  FunctionBuilder(const std::string& name) : m_name(name) {}
  FunctionBuilder& SetDescription(const std::string& desc) {
    m_desc = desc;
    return *this;
  }

  FunctionBuilder& AddParam(Param param) {
    m_params.push_back(std::move(param));
    return *this;
  }

  FunctionBuilder& AddRequiredParam(const std::string& name,
                                    const std::string& desc,
                                    const std::string& type) {
    m_params.push_back({name, desc, type, true});
    return *this;
  }

  FunctionBuilder& AddOptionalParam(const std::string& name,
                                    const std::string& desc,
                                    const std::string& type) {
    m_params.push_back({name, desc, type, false});
    return *this;
  }

  FunctionBuilder& SetCallback(FunctionSignature func) {
    m_func = std::move(func);
    return *this;
  }

  FunctionBuilder& SetHumanInTheLoopCallabck(OnToolInvokeCallback func) {
    m_humanInTheLoopCB = std::move(func);
    return *this;
  }

  std::shared_ptr<FunctionBase> Build() {
    auto f = std::make_shared<InProcessFunction>(m_name, m_desc);
    f->m_params = std::move(m_params);
    f->m_callback = std::move(m_func);
    f->m_humanInTheLoopCB = std::move(m_humanInTheLoopCB);
    return f;
  }

 private:
  std::string m_name;
  std::string m_desc;
  FunctionSignature m_func{nullptr};
  OnToolInvokeCallback m_humanInTheLoopCB{nullptr};
  std::vector<Param> m_params;
};

}  // namespace assistant