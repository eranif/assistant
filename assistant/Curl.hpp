#pragma once

#include <optional>

#include "assistant/assistantlib.hpp"

namespace assistant {
struct BuildCommandResult {
  bool ok{true};
  std::vector<std::string> cmd;
  std::string filepath;
};

class Curl : public ITransport {
 public:
  Curl(const std::string& curl_exe);
  ~Curl() override;

  bool chat_raw_output(assistant::request& request,
                       on_raw_respons_callback on_receive_token,
                       void* user_data) override;
  bool chat(assistant::request& request, on_respons_callback on_receive_token,
            void* user_data) override;
  json list_model_json() override;

  void setReadTimeout(const int seconds, const int usecs = 0) override;
  void setWriteTimeout(const int seconds, const int usecs = 0) override;
  void setConnectTimeout(const int secs, const int usecs = 0) override;

  void interrupt() override;
  json show_model_info(const std::string& model, bool verbose = false) override;
  bool is_running() override;

#if CPPHTTPLIB_OPENSSL_SUPPORT
  void verifySSLCertificate(bool b) override;
#endif
  BuildCommandResult BuildRequestCommand(const std::string& path,
                                         const httplib::Headers& headers,
                                         const std::string& content_type,
                                         std::optional<std::string> payload);

 private:
  int m_runningProcessId{-1};
  std::string m_curl;
};
}  // namespace assistant
