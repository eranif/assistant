#include "assistant/Curl.hpp"

#include "assistant/Process.hpp"

namespace assistant {

Curl::Curl(const std::string& curl_exe) : m_curl{curl_exe} {}
Curl::~Curl() {
  if (m_runningProcessId != -1) {
    Process::TerminateProcess(m_runningProcessId);
    m_runningProcessId = -1;
  }
}

namespace {
void AddHeader(std::vector<std::string>& command,
               const std::string& header_name,
               const std::string& header_value) {
  std::stringstream ss;
  if (header_value.empty()) {
    return;
  }
  ss << "\"" << header_name << ": " << header_value << "\"";
  command.push_back("-H");
  command.push_back(ss.str());
}
}  // namespace

BuildCommandResult Curl::BuildRequestCommand(
    const std::string& path, const httplib::Headers& headers,
    const std::string& content_type, std::optional<std::string> payload) {
  std::vector<std::string> command_line = {m_curl, "-s", "-L"};
  AddHeader(command_line, "Content-Type", content_type);
  for (const auto& [h_name, h_value] : headers) {
    AddHeader(command_line, h_name, h_value);
  }

  std::string server_endpoint = getServerURL();
  if (server_endpoint.starts_with("https://")) {
    command_line.push_back("--insecure");
  }

  std::stringstream url;
  url << server_endpoint << path;
  command_line.push_back(url.str());

  BuildCommandResult result{.ok = true};
  if (payload.has_value()) {
    auto file = assistant::WriteStringToRandomFile(payload.value());
    if (!file.has_value()) {
      return BuildCommandResult{.ok = false};
    }
    command_line.push_back("-d");
    command_line.push_back("@" + file.value());
    result.filepath = file.value();
  }
  result.cmd = command_line;
  return result;
}

bool Curl::chat_raw_output(assistant::request& request,
                           on_raw_respons_callback on_receive_token,
                           void* user_data) {
  assistant::response response;
  request["stream"] = true;

  std::string request_string = request.dump();
  if (assistant::log_requests) std::cout << request_string << std::endl;

  std::stringstream errstream;
  auto stream_callback = [&errstream, on_receive_token, user_data](
                             const std::string& out,
                             const std::string& err) -> bool {
    errstream << err;
    return on_receive_token(out, user_data);
  };

  auto result = BuildRequestCommand(GetChatPath(), headers_, kApplicationJson,
                                    request_string);
  if (!result.ok) {
    return false;
  }

  // Its ok to accept an empty path
  assistant::ScopedFileDeleter deleter{result.filepath};

  int exit_code =
      Process::RunProcessAndWait(result.cmd, stream_callback, false);
  if (exit_code == 0) {
    return true;
  }

  if (assistant::use_exceptions) {
    throw assistant::exception("Server responded with an error. stderr: " +
                               errstream.str());
  }
  return false;
}

bool Curl::chat(assistant::request& request,
                on_respons_callback on_receive_token, void* user_data) {
  assistant::response response;
  request["stream"] = true;

  std::string request_string = request.dump();
  if (assistant::log_requests) {
    std::cout << request_string << std::endl;
  }

  std::string partial_responses;
  std::stringstream errstream;
  auto stream_callback = [&errstream, on_receive_token, user_data,
                          &partial_responses](const std::string& out,
                                              const std::string& err) -> bool {
    std::string message = out;
    errstream << err;
    if (Process::IsExecLogEnabled()) {
      std::cout << "<== " << message << std::endl;
    }

    partial_responses.append(message);

    auto result = assistant::try_read_jsons_from_string(partial_responses);
    if (result.first.empty()) {
      // no complete jsons
      return true;
    }

    // "second" holds the remainder
    partial_responses = result.second;

    // Process the jsons
    for (const auto& j : result.first) {
      try {
        assistant::response response(j.dump(), assistant::message_type::chat);
        if (response.has_error()) {
          if (assistant::use_exceptions)
            throw assistant::exception("Server response returned error: " +
                                       response.get_error());
        }
        if (!on_receive_token(response, user_data)) {
          return false;
        }
      } catch (const assistant::invalid_json_exception& e) {
        // Could not parse a response object.
        if (assistant::use_exceptions) {
          std::stringstream ss;
          ss << "Could not parse response." << e.what() << "\n"
             << "Response JSON:\n"
             << j.dump(2) << "\n";
          throw assistant::exception(ss.str());
        }
        // Abort the stream.
        return false;
      }
    }
    return true;
  };

  auto result = BuildRequestCommand(GetChatPath(), headers_, kApplicationJson,
                                    request_string);
  if (!result.ok) {
    return false;
  }

  // Its ok to accept an empty path
  assistant::ScopedFileDeleter deleter{result.filepath};

  int exit_code =
      Process::RunProcessAndWait(result.cmd, stream_callback, false);
  if (exit_code == 0) {
    return true;
  }

  if (assistant::use_exceptions) {
    throw assistant::exception("Server responded with an error. " +
                               errstream.str());
  }
  return false;
}

json Curl::list_model_json() {
  json models;
  auto result = BuildRequestCommand(GetListPath(), headers_, kApplicationJson,
                                    std::nullopt);
  if (!result.ok) {
    return models;
  }
  auto res = Process::RunProcessAndWait(result.cmd);
  if (res.ok) {
    models = json::parse(res.out);
  }
  return models;
}

void Curl::setReadTimeout([[maybe_unused]] const int seconds,
                          [[maybe_unused]] const int usecs) {}
void Curl::setWriteTimeout([[maybe_unused]] const int seconds,
                           [[maybe_unused]] const int usecs) {}
void Curl::setConnectTimeout([[maybe_unused]] const int secs,
                             [[maybe_unused]] const int usecs) {}

void Curl::interrupt() {
  if (m_runningProcessId == -1) {
    return;
  }
  if (Process::IsAlive(m_runningProcessId)) {
    Process::TerminateProcess(m_runningProcessId);
    m_runningProcessId = -1;
  }
}

json Curl::show_model_info(const std::string& model, bool verbose) {
  json request, response;
  request["name"] = model;
  if (verbose) request["verbose"] = true;

  std::string request_string = request.dump();
  if (assistant::log_requests) {
    std::cout << request_string << std::endl;
  }

  auto result = BuildRequestCommand(GetShowPath(), headers_, kApplicationJson,
                                    request_string);
  if (!result.ok) {
    return response;
  }

  // Its ok to accept an empty path
  assistant::ScopedFileDeleter deleter{result.filepath};

  auto res = Process::RunProcessAndWait(result.cmd);
  if (res.ok) {
    if (Process::IsExecLogEnabled()) {
      std::cout << "<== " << res.out << std::endl;
    }
    try {
      response = json::parse(res.out);
    } catch (...) {
      if (assistant::use_exceptions) {
        throw assistant::exception(
            "Received bad response from server when querying model "
            "info. " +
            res.out + ". " + res.err);
      }
    }
  } else {
    if (assistant::use_exceptions) {
      throw assistant::exception(
          "No response returned from server when querying model info");
    }
  }
  return response;
}

bool Curl::is_running() {
  auto res = BuildRequestCommand("/", headers_, "", std::nullopt);
  if (!res.ok) {
    return false;
  }
  return Process::RunProcessAndWait(res.cmd).ok;
}

#if CPPHTTPLIB_OPENSSL_SUPPORT
void Curl::verifySSLCertificate([[maybe_unused]] bool b) {}
#endif

}  // namespace assistant
