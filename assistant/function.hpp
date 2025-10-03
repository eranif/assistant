#pragma once

#include <vector>

#include "assistant/cpp-mcp/mcp_tool.h"
#include "assistant/function_base.hpp"
#include "assistant/assistantlib.hpp"

namespace assistant {
class MCPStdioClient;

using FunctionSignature = std::function<FunctionResult(const json&)>;

class InProcessFunction : public FunctionBase {
 public:
  InProcessFunction(const std::string& name, const std::string& desc)
      : FunctionBase(name, desc) {}
  FunctionResult Call(const json& args) const override {
    return m_callback(args);
  }

 protected:
  FunctionSignature m_callback;
  friend class FunctionBuilder;
};

class ExternalFunction : public FunctionBase {
 public:
  ExternalFunction(assistant::MCPStdioClient* client, mcp::tool t);
  FunctionResult Call(const json& args) const override;

 protected:
  assistant::MCPStdioClient* m_client{nullptr};
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

  std::shared_ptr<FunctionBase> Build() {
    auto f = std::make_shared<InProcessFunction>(m_name, m_desc);
    f->m_params = std::move(m_params);
    f->m_callback = std::move(m_func);
    return f;
  }

 private:
  std::string m_name;
  std::string m_desc;
  FunctionSignature m_func;
  std::vector<Param> m_params;
};

}  // namespace assistant