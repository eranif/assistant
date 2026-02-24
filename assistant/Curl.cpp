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
void AddHeader(std::stringstream& ss, const std::string& header_name,
               const std::string& header_value) {
  if (header_value.empty()) {
    return;
  }
  ss << "header = \"" << header_name << ": " << header_value << "\"\n";
}
}  // namespace

std::unique_ptr<BuildCommandResult> Curl::BuildRequestCommand(
    const std::string& path, const httplib::Headers& headers,
    const std::string& content_type, std::optional<std::string> payload) {
  // Create the request file.
  auto result = std::make_unique<BuildCommandResult>();
  std::stringstream request_data;
  request_data << "url = " << getServerURL() << path << "\n";
  request_data << "silent\n";
  request_data << "location\n";
  request_data << "insecure\n";

  AddHeader(request_data, "Content-Type", content_type);
  for (const auto& [h_name, h_value] : headers) {
    AddHeader(request_data, h_name, h_value);
  }

  if (payload.has_value()) {
    auto file = assistant::WriteStringToRandomFile(payload.value());
    if (!file.has_value()) {
      result->ok = false;
      return result;
    }
    request_data << "data = @" << file.value() << "\n";
    result->data_path = file.value();
  }

  auto request_path = assistant::WriteStringToRandomFile(request_data.str());
  result->request_path = request_path.value_or("");
  result->command = {m_curl, "--config", request_path.value_or("")};
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
  if (!result->ok) {
    return false;
  }

  int exit_code =
      Process::RunProcessAndWait(result->command, stream_callback, false);
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
  if (!result->ok) {
    return false;
  }

  int exit_code =
      Process::RunProcessAndWait(result->command, stream_callback, false);
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
  if (!result->ok) {
    return models;
  }

  auto res = Process::RunProcessAndWait(result->command);
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
  if (!result->ok) {
    return response;
  }

  auto res = Process::RunProcessAndWait(result->command);
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
  if (!res->ok) {
    return false;
  }
  return Process::RunProcessAndWait(res->command).ok;
}

#if CPPHTTPLIB_OPENSSL_SUPPORT
void Curl::verifySSLCertificate([[maybe_unused]] bool b) {}
#endif

}  // namespace assistant
