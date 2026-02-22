#ifndef OLLAMA_HPP
#define OLLAMA_HPP

/*  MIT License

    Copyright (c) 2025 James Montgomery (jmont)

    Permission is hereby granted, free of charge, to any person obtaining a copy
    of this software and associated documentation files (the "Software"), to
   deal in the Software without restriction, including without limitation the
   rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
   sell copies of the Software, and to permit persons to whom the Software is
    furnished to do so, subject to the following conditions:

    The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
   IN THE SOFTWARE.
*/

/*  About this software:

    Ollama is a high-quality REST server and API providing an interface to run
    language models locally via llama.cpp.

    Ollama was made by Jeffrey Morgan (@jmorganca) and the Ollama team and is
    available under the MIT License. To support this project or for more details
    go to https://github.com/ollama or https://ollama.com/.

    This library is a header-only C++ integration of the Ollama API providing
   access to most API features while integrating them with std library classes
   or popular header-only libraries within the community. The following external
   libraries are used:
*/

/*
    httplib is a header-only C++ http/https library.
    This library was created by Yuji Hirose and is available under the MIT
   License. For more details visit: https://github.com/yhirose/cpp-httplib
*/
#include "assistant/common/httplib.h"

/*
    nlohmnann JSON is a feature-rich header-only C++ JSON implementation.
    This library was created by Niels Lohmann and is available under the MIT
   License. For more details visit: https://github.com/nlohmann/json
*/
#include "assistant/common/json.hpp"

/*
    Base64.h is a header-only C++ library for encoding and decoding Base64
   values. This library was created by tomykaira and is available under the MIT
   License. For more details visit:
    https://gist.github.com/tomykaira/f0fd86b6c73063283afe550bc5d77594
*/
#include <exception>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>

#include "assistant/common/base64.hpp"
#include "assistant/helpers.hpp"

// Namespace types and classes

static constexpr const char* kApplicationJson = "application/json";

namespace assistant {

enum class EndpointKind {
  ollama,
  anthropic,
};

using json = nlohmann::ordered_json;
using base64 = macaron::Base64;

static bool use_exceptions = true;  // Change this to false to avoid throwing
                                    // exceptions within the library.
static bool log_requests =
    false;  // Log raw requests to the Ollama server. Useful when debugging.
static bool log_replies =
    false;  // Log raw replies from the Ollama server. Useful when debugging.

inline void allow_exceptions(bool enable) { use_exceptions = enable; }
inline void show_requests(bool enable) { log_requests = enable; }
inline void show_replies(bool enable) { log_replies = enable; }

enum class message_type { generation, chat, embedding };

class exception : public std::exception {
 private:
  std::string message;

 public:
  exception(const std::string& msg) : message(msg) {}
  const char* what() const noexcept override { return message.c_str(); }
};

class invalid_json_exception : public assistant::exception {
 public:
  using exception::exception;
};

class image {
 public:
  image(const std::string base64_sequence, bool valid = true) {
    this->base64_sequence = base64_sequence;
    this->valid = valid;
  }
  ~image() {};

  static image from_file(const std::string& filepath) {
    bool valid = true;
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
      if (assistant::use_exceptions)
        throw assistant::exception("Unable to open image file from path.");
      valid = false;
      return image("", valid);
    }

    std::string file_contents((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

    return image(macaron::Base64::Encode(file_contents), valid);
  }

  static image from_base64_string(const std::string& base64_string) {
    return image(base64_string);
  }

  const std::string as_base64_string() const { return base64_sequence; }

  bool is_valid() { return valid; }

  operator std::string() const { return base64_sequence; }

  operator std::vector<assistant::image>() const {
    std::vector<assistant::image> images;
    images.push_back(*this);
    return images;
  }
  operator std::vector<std::string>() const {
    std::vector<std::string> images;
    images.push_back(*this);
    return images;
  }

 private:
  std::string base64_sequence;
  bool valid;
};

class images : public std::vector<std::string> {
 public:
  images() : std::vector<std::string>(0) {}
  images(const std::initializer_list<assistant::image>& list) {
    for (assistant::image value : list) {
      this->push_back(value);
    }
  }
  ~images() {};
  std::vector<std::string> to_strings() {
    std::vector<std::string> strings;
    for (auto it = this->begin(); it != this->end(); ++it)
      strings.push_back(*it);

    return strings;
  }
};

class options : public json {
 public:
  options() : json() { this->emplace("options", nlohmann::ordered_json::object()); }

  nlohmann::ordered_json& operator[](const std::string& key) {
    if (!this->at("options").contains(key))
      this->at("options").emplace(key, nlohmann::ordered_json::object());
    return this->at("options").at(key);
  }
  nlohmann::ordered_json& operator[](const char* key) {
    return this->operator[](std::string(key));
  };

  const nlohmann::ordered_json& operator[](const std::string& key) const {
    return this->at("options").at(key);
  }
  const nlohmann::ordered_json& operator[](const char* key) const {
    return this->operator[](std::string(key));
  };
};

class message : public json {
 public:
  message(const std::string& role, const std::string& content,
          const std::vector<assistant::image>& images)
      : json() {
    (*this)["role"] = role;
    (*this)["content"] = content;
    (*this)["images"] = images;
  }
  message(const std::string& role, const std::string& content) : json() {
    (*this)["role"] = role;
    (*this)["content"] = content;
  }
  message() : json() {}
  ~message() {}

  std::string as_json_string() const { return this->dump(); }

  operator std::string() const { return this->as_json_string(); }
};

class messages : public std::vector<message> {
 public:
  messages() : std::vector<message>(0) {}
  messages(assistant::message message) : messages() {
    this->push_back(message);
  }

  messages(const std::initializer_list<message>& list) {
    for (message value : list) {
      this->push_back(value);
    }
  }
  ~messages() {};
  const std::vector<std::string> to_strings() const {
    std::vector<std::string> strings;
    for (auto it = this->begin(); it != this->end(); ++it)
      strings.push_back(*it);

    return strings;
  }

  const std::vector<json> to_json() const {
    std::vector<json> output;
    for (auto it = this->begin(); it != this->end(); ++it)
      output.push_back(*it);
    return output;
  }

  operator std::vector<json>() const { return std::vector<json>(); }
  operator std::vector<std::string>() const { return this->to_strings(); }
};

class request : public json {
 public:
  // Create a request for a generation.
  request(const std::string& model, const std::string& prompt,
          const json& options = nullptr, bool stream = false,
          const std::vector<std::string>& images = std::vector<std::string>())
      : request() {
    (*this)["model"] = model;
    (*this)["prompt"] = prompt;
    (*this)["stream"] = stream;

    if (options.is_object()) (*this)["options"] = options["options"];
    if (!images.empty()) (*this)["images"] = images;

    type = message_type::generation;
  }

  // Create a request for a chat completion.
  request(const std::string& model, const assistant::messages& messages,
          const json& options = nullptr, bool stream = false,
          const std::string& format = "json",
          const std::string& keep_alive_duration = "")
      : request() {
    (*this)["model"] = model;
    (*this)["messages"] = messages.to_json();
    (*this)["stream"] = stream;

    if (options.is_object()) (*this)["options"] = options["options"];
    (void)format;  //(*this)["format"] = format; // Commented out as providing
    // the format causes issues with some models.
    if (!keep_alive_duration.empty()) {
      (*this)["keep_alive"] = keep_alive_duration;
    }
    type = message_type::chat;
  }
  // Request for a chat completion with a single message
  request(const std::string& model, const assistant::message& message,
          const json& options = nullptr, bool stream = false,
          const std::string& format = "json",
          const std::string& keep_alive_duration = "5m")
      : request(model, messages(message), options, stream, format,
                keep_alive_duration) {}

  request(message_type type) : request() { this->type = type; }

  request() : json() {}
  ~request() {};

  static assistant::request from_embedding(
      const std::string& model, const std::string& input,
      const json& options = nullptr, bool truncate = true,
      const std::string& keep_alive_duration = "5m") {
    assistant::request request(message_type::embedding);

    request["model"] = model;
    request["input"] = input;
    if (options.is_object()) request["options"] = options["options"];
    request["truncate"] = truncate;
    request["keep_alive"] = keep_alive_duration;

    return request;
  }

  const message_type& get_type() const { return type; }

 private:
  message_type type;
};

class response {
 public:
  response(const std::string& json_string,
           message_type type = message_type::generation)
      : type(type) {
    this->json_string = json_string;
    try {
      json_data = json::parse(json_string);

      if (type == message_type::generation && json_data.contains("response"))
        simple_string = json_data["response"].get<std::string>();
      else if (type == message_type::embedding &&
               json_data.contains("embeddings"))
        simple_string = json_data["embeddings"].get<std::string>();
      else if (type == message_type::chat && json_data.contains("message"))
        simple_string = json_data["message"]["content"].get<std::string>();

      if (json_data.contains("error"))
        error_string = json_data["error"].get<std::string>();
    } catch (const std::exception& e) {
      if (assistant::use_exceptions) {
        std::stringstream ss;
        ss << "Unable to parse JSON string: " << e.what() << ". Input string:\n"
           << this->json_string;
        throw assistant::invalid_json_exception(ss.str());
      }
      valid = false;
    }
  }

  response() {
    json_string = "";
    valid = false;
  }
  ~response() {};

  bool is_valid() const { return valid; };

  const std::string& as_json_string() const { return json_string; }

  const json& as_json() const { return json_data; }

  const std::string& as_simple_string() const { return simple_string; }

  bool has_error() const {
    if (json_data.contains("error")) return true;
    return false;
  }

  const std::string& get_error() const { return error_string; }

  friend std::ostream& operator<<(std::ostream& os,
                                  const assistant::response& response) {
    os << response.as_simple_string();
    return os;
  }

  const message_type& get_type() const { return type; }

  // operator std::string() const { return this->as_simple_string(); }
  operator std::string() const { return this->as_simple_string(); }
  // const operator std::string() const { return this->as_simple_string(); }

 private:
  std::string json_string;
  std::string simple_string;
  std::string error_string;

  json json_data;
  message_type type;
  bool valid;
};

using on_respons_callback =
    std::function<bool(const assistant::response&, void*)>;

using on_raw_respons_callback = std::function<bool(const std::string&, void*)>;

class ClientImpl {
  using json = nlohmann::ordered_json;

 public:
  ClientImpl(const std::string& url) {
    this->server_url = url;
    this->cli = new httplib::Client(url);
    this->setReadTimeout(120);
  }

  ClientImpl() : ClientImpl("http://localhost:11434") {}
  ~ClientImpl() { delete this->cli; }

  assistant::response generate(
      const std::string& model, const std::string& prompt,
      const assistant::response& context, const json& options = nullptr,
      const std::vector<std::string>& images = std::vector<std::string>()) {
    assistant::request request(model, prompt, options, false, images);
    if (context.as_json().contains("context"))
      request["context"] = context.as_json()["context"];
    return generate(request);
  }

  assistant::response generate(
      const std::string& model, const std::string& prompt,
      const json& options = nullptr,
      const std::vector<std::string>& images = std::vector<std::string>()) {
    assistant::request request(model, prompt, options, false, images);
    return generate(request);
  }

  // Generate a non-streaming reply as a string.
  assistant::response generate(assistant::request& request) {
    assistant::response response;

    request["stream"] = false;
    std::string request_string = request.dump();
    if (assistant::log_requests) std::cout << request_string << std::endl;

    if (auto res = this->cli->Post(GetGeneratePath(), headers_, request_string,
                                   kApplicationJson)) {
      if (assistant::log_replies) std::cout << res->body << std::endl;

      response = assistant::response(res->body);
      if (response.has_error()) {
        if (assistant::use_exceptions)
          throw assistant::exception("Ollama response returned error: " +
                                     response.get_error());
      }

    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response returned from server " + this->server_url +
            ". Error was: " + httplib::to_string(res.error()));
    }

    return response;
  }

  bool generate(
      const std::string& model, const std::string& prompt,
      assistant::response& context, on_respons_callback on_receive_token,
      void* user_data, const json& options = nullptr,
      const std::vector<std::string>& images = std::vector<std::string>()) {
    assistant::request request(model, prompt, options, true, images);
    if (context.as_json().contains("context"))
      request["context"] = context.as_json()["context"];
    return generate(request, on_receive_token, user_data);
  }

  bool generate(
      const std::string& model, const std::string& prompt,
      on_respons_callback on_receive_token, void* user_data,
      const json& options = nullptr,
      const std::vector<std::string>& images = std::vector<std::string>()) {
    assistant::request request(model, prompt, options, true, images);
    return generate(request, on_receive_token, user_data);
  }

  // Generate a streaming reply where a user-defined callback function is
  // invoked when each token is received.
  bool generate(assistant::request& request,
                on_respons_callback on_receive_token, void* user_data) {
    request["stream"] = true;

    std::string request_string = request.dump();
    if (assistant::log_requests) std::cout << request_string << std::endl;

    std::shared_ptr<std::vector<std::string>> partial_responses =
        std::make_shared<std::vector<std::string>>();

    auto stream_callback = [on_receive_token, user_data, partial_responses](
                               const char* data, size_t data_length) -> bool {
      std::string message(data, data_length);
      bool continue_stream = true;

      if (assistant::log_replies) std::cout << message << std::endl;
      try {
        partial_responses->push_back(message);
        std::string total_response =
            std::accumulate(partial_responses->begin(),
                            partial_responses->end(), std::string(""));
        assistant::response response(total_response);
        partial_responses->clear();
        continue_stream = on_receive_token(response, user_data);
      } catch (const assistant::invalid_json_exception&
                   e) { /* Partial response was received. Will do nothing and
                           attempt to concatenate with the next response. */
      }

      return continue_stream;
    };

    if (auto res = this->cli->Post(GetGeneratePath(), headers_, request_string,
                                   kApplicationJson, stream_callback)) {
      return true;
    } else if (res.error() ==
               httplib::Error::Canceled) { /* Request cancelled by user. */
      return true;
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response from server returned at URL: " + this->server_url +
            "\nError: " + httplib::to_string(res.error()));
    }

    return false;
  }

  assistant::response chat(const std::string& model,
                           const assistant::messages& messages,
                           json options = nullptr,
                           const std::string& format = "json",
                           const std::string& keep_alive_duration = "5m") {
    assistant::request request(model, messages, options, false, format,
                               keep_alive_duration);
    return chat(request);
  }

  // Generate a non-streaming reply as a string.
  assistant::response chat(assistant::request& request) {
    assistant::response response;

    request["stream"] = false;
    std::string request_string = request.dump();
    if (assistant::log_requests) std::cout << request_string << std::endl;

    if (auto res = this->cli->Post(GetChatPath(), headers_, request_string,
                                   kApplicationJson)) {
      if (assistant::log_replies) std::cout << res->body << std::endl;

      response = assistant::response(res->body, assistant::message_type::chat);
      if (response.has_error()) {
        if (assistant::use_exceptions)
          throw assistant::exception("Ollama response returned error: " +
                                     response.get_error());
      }

    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response returned from server " + this->server_url +
            ". Error was: " + httplib::to_string(res.error()));
    }

    return response;
  }

  bool chat(const std::string& model, const assistant::messages& messages,
            on_respons_callback on_receive_token, void* user_data,
            const json& options = nullptr, const std::string& format = "json",
            const std::string& keep_alive_duration = "5m") {
    assistant::request request(model, messages, options, true, format,
                               keep_alive_duration);
    return chat(request, on_receive_token, user_data);
  }

  bool chat(assistant::request& request, on_respons_callback on_receive_token,
            void* user_data) {
    assistant::response response;
    request["stream"] = true;

    std::string request_string = request.dump();
    if (assistant::log_requests) std::cout << request_string << std::endl;

    std::shared_ptr<std::string> partial_responses =
        std::make_shared<std::string>();

    auto stream_callback = [on_receive_token, user_data, partial_responses](
                               const char* data, size_t data_length) -> bool {
      std::string message(data, data_length);
      bool continue_stream = true;

      if (assistant::log_replies) {
        std::cout << message << std::endl;
      }

      partial_responses->append(message);
      // we can have multiple messages
      auto lines = split_into_lines(*partial_responses);
      for (const auto& line : lines.first) {
        if (!continue_stream) {
          return false;
        }
        try {
          assistant::response response(line, assistant::message_type::chat);
          if (response.has_error()) {
            if (assistant::use_exceptions)
              throw assistant::exception("Ollama response returned error: " +
                                         response.get_error());
          }
          continue_stream = on_receive_token(response, user_data);
        } catch (const assistant::invalid_json_exception& e) {
          // since we are dealing with complete lines, we don't expect invalid
          // JSON input.
          if (assistant::use_exceptions) {
            std::stringstream ss;
            ss << "Ollama responded with an invalid JSON. " << e.what()
               << ". JSON:\n"
               << line << "\nComplete message is:\n"
               << *partial_responses;
            throw assistant::exception(ss.str());
          }
          // Abort the stream.
          return false;
        }
      }
      // try to process the last incomplete line.
      try {
        assistant::response response(lines.second,
                                     assistant::message_type::chat);
        if (response.has_error()) {
          if (assistant::use_exceptions)
            throw assistant::exception("Ollama response returned error: " +
                                       response.get_error());
        }
        continue_stream = on_receive_token(response, user_data);
        // if we got here, it means we were able to process the complete
        // output.
        partial_responses->clear();

      } catch (...) {
        // keep the last line for next iteration.
        partial_responses->swap(lines.second);
      }
      return continue_stream;
    };

    if (auto res = this->cli->Post(GetChatPath(), headers_, request_string,
                                   kApplicationJson, stream_callback)) {
      if (res.value().status >= 400) {
        // error code.
        if (assistant::use_exceptions) {
          throw assistant::exception("Server responded with an error. " +
                                     res.value().reason + " (" +
                                     std::to_string(res.value().status) + ")");
        }
        return false;
      }
      return true;
    } else if (res.error() ==
               httplib::Error::Canceled) { /* Request cancelled by user. */
      return true;
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response from server returned at URL: " + this->server_url +
            "\nError: " + httplib::to_string(res.error()));
    }
    return false;
  }

  /// Similar to the above "chat" method, but do not assume JSON response.
  /// Useful for non-ollama servers, like Anthropic's "claude".
  bool chat_raw_output(assistant::request& request,
                       on_raw_respons_callback on_receive_token,
                       void* user_data) {
    assistant::response response;
    request["stream"] = true;

    std::string request_string = request.dump();
    if (assistant::log_requests) std::cout << request_string << std::endl;

    auto stream_callback = [on_receive_token, user_data](
                               const char* data, size_t data_length) -> bool {
      std::string message(data, data_length);
      return on_receive_token(message, user_data);
    };

    if (auto res = this->cli->Post(GetChatPath(), headers_, request_string,
                                   kApplicationJson, stream_callback)) {
      if (res.value().status >= 400) {
        // error code.
        if (assistant::use_exceptions) {
          throw assistant::exception("Server responded with an error. " +
                                     res.value().reason + " (" +
                                     std::to_string(res.value().status) + ")");
        }
        return false;
      }
      return true;
    } else if (res.error() ==
               httplib::Error::Canceled) { /* Request cancelled by user. */
      return true;
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response from server returned at URL: " + this->server_url +
            "\nError: " + httplib::to_string(res.error()));
    }
    return false;
  }

  bool create_model(const std::string& modelName, const std::string& modelFile,
                    bool loadFromFile = true) {
    // Generate the JSON request
    json request;
    request["name"] = modelName;

    if (loadFromFile) {
      // Open the file
      std::ifstream file(modelFile, std::ios::binary);

      // Check if the file is open
      if (!file.is_open()) {
        if (assistant::use_exceptions)
          throw assistant::exception("Failed to open file " + modelFile);
        return false;
      }

      // Read the entire file into a string using iterators
      std::string file_contents((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());

      request["modelFile"] = file_contents;
    } else
      request["modelFile"] = modelFile;

    std::string request_string = request.dump();
    if (assistant::log_requests) std::cout << request_string << std::endl;

    std::string response;

    if (auto res = this->cli->Post("/api/create", headers_, request_string,
                                   kApplicationJson)) {
      if (assistant::log_replies) std::cout << res->body << std::endl;

      json chunk = json::parse(res->body);
      if (chunk["status"] == "success") return true;
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception("No response returned: " +
                                   httplib::to_string(res.error()));
    }

    return false;
  }

  bool load_model(const std::string& model) {
    if (endpoint_kind_ != EndpointKind::ollama) {
      if (assistant::use_exceptions) {
        throw std::runtime_error(
            "Load model is only supported by Ollama server");
      } else {
        return false;
      }
    }
    json request;
    request["model"] = model;
    std::string request_string = request.dump();
    if (assistant::log_requests) std::cout << request_string << std::endl;

    // Send a blank request with the model name to instruct the server to load
    // the model into memory.
    if (auto res = this->cli->Post(GetGeneratePath(), headers_, request_string,
                                   kApplicationJson)) {
      if (assistant::log_replies) std::cout << res->body << std::endl;
      json response = json::parse(res->body);
      return response["done"].get<bool>();
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response returned from server when loading model: " +
            httplib::to_string(res.error()));
    }

    // If we didn't get a response from the server indicating the model was
    // created, return false.
    return false;
  }

  bool is_running() {
    switch (endpoint_kind_) {
      case assistant::EndpointKind::ollama: {
        auto res = cli->Get("/", headers_);
        if (res && res.value().status < 400) {
          // anything below 400 is fine by us.
          return true;
        }
        return false;
      } break;
      case assistant::EndpointKind::anthropic: {
        time_t secs, usecs;
        cli->get_connection_timeout(secs, usecs);
        cli->set_connection_timeout(1, 0);
        auto res = cli->Get("/", headers_);
        // restore the timeout
        cli->set_connection_timeout(secs, usecs);
        return res;
      } break;
    }
    return false;
  }

  json list_model_json() {
    json models;
    if (auto res = cli->Get(GetListPath(), headers_)) {
      if (assistant::log_replies) std::cout << res->body << std::endl;
      models = json::parse(res->body);
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response returned from server when querying model list: " +
            httplib::to_string(res.error()));
    }

    return models;
  }

  std::vector<std::string> list_models() {
    std::vector<std::string> models;

    json json_response = list_model_json();

    switch (endpoint_kind_) {
      case assistant::EndpointKind::ollama: {
        for (auto& model : json_response["models"]) {
          models.push_back(model["name"].get<std::string>());
        }
      } break;
      case assistant::EndpointKind::anthropic: {
        for (auto& model : json_response["data"]) {
          models.push_back(model["id"].get<std::string>());
        }
      } break;
    }

    return models;
  }

  json running_model_json() {
    if (endpoint_kind_ != EndpointKind::ollama) {
      if (assistant::use_exceptions) {
        throw std::runtime_error(
            "List running model is only supported by Ollama server");
      } else {
        return {};
      }
    }

    json models;
    if (auto res = cli->Get("/api/ps", headers_)) {
      if (assistant::log_replies) std::cout << res->body << std::endl;
      models = json::parse(res->body);
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response returned from server when querying running "
            "models: " +
            httplib::to_string(res.error()));
    }

    return models;
  }

  std::vector<std::string> list_running_models() {
    std::vector<std::string> models;

    json json_response = running_model_json();

    for (auto& model : json_response["models"]) {
      models.push_back(model["name"].get<std::string>());
    }

    return models;
  }

  bool blob_exists(const std::string& digest) {
    if (endpoint_kind_ != EndpointKind::ollama) {
      if (assistant::use_exceptions) {
        throw std::runtime_error(
            "Blob exists API is only supported by Ollama server");
      } else {
        return false;
      }
    }

    if (auto res = cli->Head("/api/blobs/" + digest)) {
      if (res->status == httplib::StatusCode::OK_200) return true;
      if (res->status == httplib::StatusCode::NotFound_404) return false;
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response returned from server when checking if blob "
            "exists: " +
            httplib::to_string(res.error()));
    }

    return false;
  }

  bool create_blob(const std::string& digest) {
    if (endpoint_kind_ != EndpointKind::ollama) {
      if (assistant::use_exceptions) {
        throw std::runtime_error(
            "Create blob API is only supported by Ollama server");
      } else {
        return false;
      }
    }

    if (auto res = cli->Post("/api/blobs/" + digest, headers_)) {
      if (res->status == httplib::StatusCode::Created_201) return true;
      if (res->status == httplib::StatusCode::BadRequest_400) {
        if (assistant::use_exceptions)
          throw assistant::exception(
              "Received bad request (Code 400) from Ollama server when "
              "creating blob.");
      }
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response returned from server when creating blob: " +
            httplib::to_string(res.error()));
    }

    return false;
  }

  json show_model_info(const std::string& model, bool verbose = false) {
    json request, response;
    request["name"] = model;
    if (verbose) request["verbose"] = true;

    std::string request_string = request.dump();
    if (assistant::log_requests) std::cout << request_string << std::endl;

    if (auto res = cli->Post(GetShowPath(), headers_, request_string,
                             kApplicationJson)) {
      if (assistant::log_replies)
        std::cout << "Reply was " << res->body << std::endl;
      try {
        response = json::parse(res->body);
      } catch (...) {
        if (assistant::use_exceptions)
          throw assistant::exception(
              "Received bad response from server when querying model "
              "info.");
      }
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response returned from server when querying model info: " +
            httplib::to_string(res.error()));
    }

    return response;
  }

  bool copy_model(const std::string& source_model,
                  const std::string& dest_model) {
    json request;
    request["source"] = source_model;
    request["destination"] = dest_model;

    std::string request_string = request.dump();
    if (assistant::log_requests) std::cout << request_string << std::endl;

    if (auto res = cli->Post("/api/copy", headers_, request_string,
                             kApplicationJson)) {
      if (res->status == httplib::StatusCode::OK_200) return true;
      if (res->status == httplib::StatusCode::NotFound_404) {
        if (assistant::use_exceptions)
          throw assistant::exception(
              "Source model not found when copying model (Code 404).");
      }
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response returned from server when copying model: " +
            httplib::to_string(res.error()));
    }

    return false;
  }

  bool delete_model(const std::string& model) {
    json request;
    request["name"] = model;

    std::string request_string = request.dump();
    if (assistant::log_requests) std::cout << request_string << std::endl;

    if (auto res =
            cli->Delete("/api/delete", request_string, kApplicationJson)) {
      if (res->status == httplib::StatusCode::OK_200) return true;
      if (res->status == httplib::StatusCode::NotFound_404) {
        if (assistant::use_exceptions)
          throw assistant::exception(
              "Model not found when trying to delete (Code 404).");
      }
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response returned from server when deleting model: " +
            httplib::to_string(res.error()));
    }

    return false;
  }

  bool pull_model(const std::string& model, bool allow_insecure = false) {
    json request, response;
    request["name"] = model;
    request["insecure"] = allow_insecure;
    request["stream"] = false;

    std::string request_string = request.dump();
    if (assistant::log_requests) std::cout << request_string << std::endl;

    if (auto res = cli->Post("/api/pull", headers_, request_string,
                             kApplicationJson)) {
      if (res->status == httplib::StatusCode::OK_200) return true;
      if (res->status == httplib::StatusCode::NotFound_404) {
        if (assistant::use_exceptions)
          throw assistant::exception(
              "Model not found when trying to pull (Code 404).");
        return false;
      }

      response = json::parse(res->body);
      if (response.contains("error")) {
        if (assistant::use_exceptions)
          throw assistant::exception(
              "Error returned from ollama when pulling model: " +
              response["error"].get<std::string>());
        return false;
      }
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response returned from server when pulling model: " +
            httplib::to_string(res.error()));
    }

    return false;
  }

  bool push_model(const std::string& model, bool allow_insecure = false) {
    json request, response;
    request["name"] = model;
    request["insecure"] = allow_insecure;
    request["stream"] = false;

    std::string request_string = request.dump();
    if (assistant::log_requests) std::cout << request_string << std::endl;

    if (auto res = cli->Post("/api/push", headers_, request_string,
                             kApplicationJson)) {
      if (res->status == httplib::StatusCode::OK_200) return true;
      if (res->status == httplib::StatusCode::NotFound_404) {
        if (assistant::use_exceptions)
          throw assistant::exception(
              "Model not found when trying to push (Code 404).");
        return false;
      }

      response = json::parse(res->body);
      if (response.contains("error")) {
        if (assistant::use_exceptions)
          throw assistant::exception(
              "Error returned from ollama when pushing model: " +
              response["error"].get<std::string>());
        return false;
      }
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response returned from server when pushing model: " +
            httplib::to_string(res.error()));
    }

    return false;
  }

  assistant::response generate_embeddings(
      const std::string& model, const std::string& input,
      const json& options = nullptr, bool truncate = true,
      const std::string& keep_alive_duration = "5m") {
    assistant::request request = assistant::request::from_embedding(
        model, input, options, truncate, keep_alive_duration);
    return generate_embeddings(request);
  }

  assistant::response generate_embeddings(assistant::request& request) {
    assistant::response response;

    std::string request_string = request.dump();
    if (assistant::log_requests) std::cout << request_string << std::endl;

    if (auto res = cli->Post("/api/embed", headers_, request_string,
                             kApplicationJson)) {
      if (assistant::log_replies) std::cout << res->body << std::endl;

      if (res->status == httplib::StatusCode::OK_200) {
        response = assistant::response(res->body);
        return response;
      };
      if (res->status == httplib::StatusCode::NotFound_404) {
        if (assistant::use_exceptions)
          throw assistant::exception(
              "Model not found when trying to push (Code 404).");
      }

      if (response.has_error()) {
        if (assistant::use_exceptions)
          throw assistant::exception(
              "Error returned from ollama when generating embeddings: " +
              response.get_error());
      }
    } else {
      if (assistant::use_exceptions)
        throw assistant::exception(
            "No response returned from server when pushing model: " +
            httplib::to_string(res.error()));
    }

    return response;
  }

  std::string get_version() {
    std::string version;

    auto res = this->cli->Get("/api/version", headers_);

    if (res) {
      json response = json::parse(res->body);
      version = response["version"].get<std::string>();
    } else {
      throw assistant::exception(std::string{"Error retrieving version: "} +
                                 std::to_string(res->status));
    }

    return version;
  }

  void setKeepAlive(bool b) {
    if (this->cli) {
      this->cli->set_keep_alive(b);
    }
  }

  bool setServerURL(const std::string& server_url) {
    if (this->server_url == server_url) {
      // No need to change
      return false;
    }
    this->server_url = server_url;
    delete (this->cli);
    this->cli = new httplib::Client(server_url);
    return true;
  }

  std::string getServerURL() const { return this->server_url; }

  void interrupt() {
    httplib::detail::shutdown_socket(this->cli->socket());
    httplib::detail::close_socket(this->cli->socket());
  }

  void setEndpointKind(EndpointKind kind) { endpoint_kind_ = kind; }
  EndpointKind getEndpointKind() const { return endpoint_kind_; }

  void setReadTimeout(const int seconds, const int usecs = 0) {
    if (this->cli == nullptr) {
      return;
    }
    this->cli->set_read_timeout(seconds, usecs);
  }

  void setWriteTimeout(const int seconds, const int usecs = 0) {
    if (this->cli == nullptr) {
      return;
    }
    this->cli->set_write_timeout(seconds, usecs);
  }

  void setConnectTimeout(const int secs, const int usecs = 0) {
    if (this->cli == nullptr) {
      return;
    }
    this->cli->set_connection_timeout(secs, usecs);
  }
  void setHttpHeaders(httplib::Headers headers) {
    headers_ = std::move(headers);
    if (this->endpoint_kind_ == EndpointKind::anthropic) {
      // Mandatory header for "claude"
      headers_.insert({"anthropic-version", "2023-06-01"});
    }
  }

#if CPPHTTPLIB_OPENSSL_SUPPORT
  void verifySSLCertificate(bool b) {
    if (!this->cli) {
      return;
    }
    this->cli->enable_server_certificate_verification(b);
  }
#endif

 private:
  std::string GetChatPath() const {
    switch (endpoint_kind_) {
      case assistant::EndpointKind::anthropic:
        return "/v1/messages";
      default:
      case assistant::EndpointKind::ollama:
        return "/api/chat";
    }
  }

  std::string GetGeneratePath() const { return "/api/generate"; }
  std::string GetShowPath() const { return "/api/show"; }
  std::string GetListPath() const {
    switch (endpoint_kind_) {
      case assistant::EndpointKind::anthropic:
        return "/v1/models";
      default:
      case assistant::EndpointKind::ollama:
        return "/api/tags";
    }
  }

  EndpointKind endpoint_kind_{EndpointKind::ollama};
  std::string server_url;
  httplib::Headers headers_;
  httplib::Client* cli;
};

// Use directly from the namespace as a singleton
static ClientImpl client_impl;

inline void setServerURL(const std::string& server_url) {
  client_impl.setServerURL(server_url);
}

inline assistant::response generate(
    const std::string& model, const std::string& prompt,
    const json& options = nullptr,
    const std::vector<std::string>& images = std::vector<std::string>()) {
  return client_impl.generate(model, prompt, options, images);
}

inline assistant::response generate(
    const std::string& model, const std::string& prompt,
    const assistant::response& context, const json& options = nullptr,
    const std::vector<std::string>& images = std::vector<std::string>()) {
  return client_impl.generate(model, prompt, context, options, images);
}

inline assistant::response generate(assistant::request& request) {
  return client_impl.generate(request);
}

inline bool generate(
    const std::string& model, const std::string& prompt,
    on_respons_callback on_receive_response, void* user_data,
    const json& options = nullptr,
    const std::vector<std::string>& images = std::vector<std::string>()) {
  return client_impl.generate(model, prompt, on_receive_response, user_data,
                              options, images);
}

inline bool generate(
    const std::string& model, const std::string& prompt,
    assistant::response& context, on_respons_callback on_receive_response,
    void* user_data, const json& options = nullptr,
    const std::vector<std::string>& images = std::vector<std::string>()) {
  return client_impl.generate(model, prompt, context, on_receive_response,
                              user_data, options, images);
}

inline bool generate(assistant::request& request,
                     on_respons_callback on_receive_response, void* user_data) {
  return client_impl.generate(request, on_receive_response, user_data);
}

inline assistant::response chat(const std::string& model,
                                const assistant::messages& messages,
                                const json& options = nullptr,
                                const std::string& format = "json",
                                const std::string& keep_alive_duration = "5m") {
  return client_impl.chat(model, messages, options, format,
                          keep_alive_duration);
}

inline assistant::response chat(assistant::request& request) {
  return client_impl.chat(request);
}

inline bool chat(const std::string& model, const assistant::messages& messages,
                 on_respons_callback on_receive_response, void* user_data,
                 const json& options = nullptr,
                 const std::string& format = "json",
                 const std::string& keep_alive_duration = "5m") {
  return client_impl.chat(model, messages, on_receive_response, user_data,
                          options, format, keep_alive_duration);
}

inline bool chat(assistant::request& request,
                 on_respons_callback on_receive_response, void* user_data) {
  return client_impl.chat(request, on_receive_response, user_data);
}

inline bool create(const std::string& modelName, const std::string& modelFile,
                   bool loadFromFile = true) {
  return client_impl.create_model(modelName, modelFile, loadFromFile);
}

inline bool is_running() { return client_impl.is_running(); }

inline bool load_model(const std::string& model) {
  return client_impl.load_model(model);
}

inline std::string get_version() { return client_impl.get_version(); }

inline std::vector<std::string> list_models() {
  return client_impl.list_models();
}

inline json list_model_json() { return client_impl.list_model_json(); }

inline std::vector<std::string> list_running_models() {
  return client_impl.list_running_models();
}

inline json running_model_json() { return client_impl.running_model_json(); }

inline bool blob_exists(const std::string& digest) {
  return client_impl.blob_exists(digest);
}

inline bool create_blob(const std::string& digest) {
  return client_impl.create_blob(digest);
}

inline json show_model_info(const std::string& model, bool verbose = false) {
  return client_impl.show_model_info(model, verbose);
}

inline bool copy_model(const std::string& source_model,
                       const std::string& dest_model) {
  return client_impl.copy_model(source_model, dest_model);
}

inline bool delete_model(const std::string& model) {
  return client_impl.delete_model(model);
}

inline bool pull_model(const std::string& model, bool allow_insecure = false) {
  return client_impl.pull_model(model, allow_insecure);
}

inline bool push_model(const std::string& model, bool allow_insecure = false) {
  return client_impl.push_model(model, allow_insecure);
}

inline assistant::response generate_embeddings(
    const std::string& model, const std::string& input,
    const json& options = nullptr, bool truncate = true,
    const std::string& keep_alive_duration = "5m") {
  return client_impl.generate_embeddings(model, input, options, truncate,
                                         keep_alive_duration);
}

inline assistant::response generate_embeddings(assistant::request& request) {
  return client_impl.generate_embeddings(request);
}

inline void setReadTimeout(const int& seconds) {
  client_impl.setReadTimeout(seconds);
}

inline void setWriteTimeout(const int& seconds) {
  client_impl.setWriteTimeout(seconds);
}

}  // namespace assistant

#endif
