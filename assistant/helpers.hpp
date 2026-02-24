#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "assistant/attributes.hpp"
#include "common/json.hpp"

#define CONCAT_IMPL(a, b) a##b
#define CONCAT(a, b) CONCAT_IMPL(a, b)
#define UNIQUE_VAR(name) CONCAT(name, __LINE__)

#define ASSIGN_OPT_OR_RETURN(Decl, Expr, ReturnValue) \
  auto UNIQUE_VAR(__result) = (Expr);                 \
  if (!UNIQUE_VAR(__result).has_value()) {            \
    return ReturnValue;                               \
  }                                                   \
  Decl = UNIQUE_VAR(__result).value();

#define ASSIGN_OPT_OR_RETURN_NULLOPT(Decl, Expr) \
  ASSIGN_OPT_OR_RETURN(Decl, Expr, std::nullopt)

namespace assistant {

/**
 * @brief Joins the elements of a container into a single string with a
 * separator.
 *
 * @tparam Container A container type (e.g., std::vector, std::list,
 * std::array).
 * @param elements The container of elements to join.
 * @param separator The string used to separate the elements.
 * @return The joined string.
 */
template <typename Container>
std::string JoinArray(const Container& elements, const std::string& separator) {
  // Return an empty string immediately if the container is empty.
  if (elements.empty()) {
    return "";
  }

  std::ostringstream oss;

  // Use an iterator to handle all container types.
  auto it = elements.begin();

  // Append the first element without a separator.
  oss << "[" << *it;
  ++it;

  // Append the remaining elements with the separator.
  for (; it != elements.end(); ++it) {
    oss << separator << *it;
  }
  oss << "]";
  return oss.str();
}

template <typename T>
inline std::ostream& operator<<(std::ostream& o, const std::vector<T>& v) {
  o << JoinArray(v, ",");
  return o;
}

inline std::string_view trim(const std::string_view& str) {
  // Find the first non-whitespace character
  size_t start = str.find_first_not_of(" \t\n\r\f\v");

  // If the string contains only whitespace, return empty string_view
  if (start == std::string_view::npos) {
    return "";
  }

  // Find the last non-whitespace character
  size_t end = str.find_last_not_of(" \t\n\r\f\v");

  // Return substring view between start and end positions
  return str.substr(start, end - start + 1);
}

/// This function returns a vector of all complete lines.
/// If the original string ends with an incomplete line,
/// that line is returned separately as a string.
inline std::pair<std::vector<std::string>, std::string> split_into_lines(
    const std::string& text, bool wants_empty_lines = false) {
  std::vector<std::string> complete_lines;
  std::string incomplete_line;
  std::istringstream stream(text);
  std::string line;

  // Use a while loop with std::getline to extract all lines
  while (std::getline(stream, line)) {
    auto trimmed_line = trim(line);
    if (!wants_empty_lines && trimmed_line.empty()) {
      continue;
    }
    complete_lines.push_back(line);
  }

  // After the loop, the stream's state can tell us about the last line.
  // If the stream reached EOF AND the last char of the original string
  // was not a newline, then the last line read was incomplete.
  if (!complete_lines.empty() && stream.eof() && text.back() != '\n') {
    incomplete_line = complete_lines.back();
    complete_lines.pop_back();  // Remove it from the complete lines vector
  }

  return {complete_lines, incomplete_line};
}

inline std::string_view after_first(const std::string_view& str,
                                    const std::string_view& delimiter) {
  size_t pos = str.find(delimiter);
  if (pos != std::string_view::npos) {
    return str.substr(pos + delimiter.length());
  }
  return "";  // Return empty string_view if delimiter is not found
}

/***
 * @brief Attempts to parse multiple JSON objects from a given string.
 *
 * The function reads JSON objects from the input string sequentially until
 * parsing fails or the end of the string is reached. It returns a pair
 * containing the vector of successfully parsed JSON objects and the remaining
 * unparsed portion of the string.
 *
 * @param instr The input string containing JSON objects.
 *
 * @return A pair where the first element is a vector of successfully parsed
 *         nlohmann::ordered_json objects and the second element is the
 *         remainder of the input string after the last successful parse.
 *
 * @throws None.
 */
inline std::pair<std::vector<nlohmann::ordered_json>, std::string>
try_read_jsons_from_string(const std::string& instr) {
  std::vector<nlohmann::ordered_json> result;
  std::stringstream ss{instr};
  std::streampos last_good_pos = 0;

  while (true) {
    try {
      nlohmann::ordered_json j;

      // Save position before parsing attempt
      std::streampos pos_before = ss.tellg();
      if (pos_before == -1) {
        pos_before = 0;
      }

      ss >> j;

      // Check if parsing succeeded (stream is still good)
      if (ss.fail()) {
        break;
      }

      result.push_back(std::move(j));

      // Update last good position after successful parse
      last_good_pos = ss.tellg();
      if (last_good_pos == -1) {
        // End of stream reached
        last_good_pos = instr.size();
        break;
      }

    } catch (const nlohmann::json::exception& e) {
      // JSON parsing threw an exception, stop here
      break;
    } catch (...) {
      // Unexpected exception, stop parsing
      break;
    }
  }

  // Extract remainder from the last successful position
  std::string remainder = instr.substr(static_cast<size_t>(last_good_pos));

  return {result, remainder};
}

/**
 * @brief Writes a string content to a file. The string is not assumed to be
 * null-terminated.
 *
 * This function writes the specified number of bytes from the given string
 * to a file. It's useful for writing binary data or strings that may
 * contain embedded null characters.
 *
 * @param filepath The path to the file to write.
 * @param content The string content to write to the file.
 * @return true if the write operation was successful, false otherwise.
 */
inline bool WriteStringToFile(const std::string& filepath,
                              const std::string& content) {
  std::ofstream file(filepath, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }
  file.write(content.data(), content.size());
  return file.good();
}

/**
 * @brief Generates a unique random filename and writes the string content to
 * it.
 *
 * This function creates a file with a randomly generated name in the system's
 * temporary directory and writes the specified data to it. The string is not
 * assumed to be null-terminated.
 *
 * @param content The string content to write to the file.
 * @return std::optional<std::string> containing the full path to the created
 *         file on success, or std::nullopt on failure.
 */
inline std::optional<std::string> WriteStringToRandomFile(
    const std::string& content) {
  namespace fs = std::filesystem;

  // Get the system's temporary directory
  std::error_code ec;
  fs::path temp_dir = fs::temp_directory_path(ec);
  if (ec) {
    return std::nullopt;
  }

  // Random number generation setup
  std::random_device rd;
  std::mt19937_64 gen(rd());
  std::uniform_int_distribution<uint64_t> dis;

  // Try to create a unique file (with retry logic in case of collision)
  constexpr int max_attempts = 100;
  for (int attempt = 0; attempt < max_attempts; ++attempt) {
    // Generate random filename
    uint64_t random_num = dis(gen);
    std::ostringstream oss;
    oss << "temp_" << std::hex << random_num << ".tmp";

    fs::path filepath = temp_dir / oss.str();

    // Check if file already exists (very unlikely but possible)
    if (fs::exists(filepath)) {
      continue;
    }

    // Try to write to the file
    if (WriteStringToFile(filepath.string(), content)) {
      return filepath.string();
    } else {
      return std::nullopt;
    }
  }

  // Failed to generate a unique filename after max attempts
  return std::nullopt;
}

/**
 * @brief Deletes a file at the specified path.
 *
 * @param filepath The path to the file to delete.
 * @return true if the file was successfully deleted, false otherwise.
 */
inline bool DeleteFileFromDisk(const std::string& filepath) {
  std::error_code ec;
  return std::filesystem::remove(filepath, ec);
}

/**
 * @brief RAII wrapper for automatic file deletion.
 *
 * This class provides automatic cleanup of temporary files. The file at the
 * specified path will be deleted when the object goes out of scope.
 */
class ScopedFileDeleter {
 public:
  /**
   * @brief Constructs a ScopedFileDeleter with the given file path.
   *
   * @param filepath The path to the file that should be deleted on destruction.
   */
  explicit ScopedFileDeleter(const std::string& filepath)
      : filepath_(filepath), enabled_(true) {}

  /**
   * @brief Constructs a ScopedFileDeleter with an optional file path.
   *
   * @param filepath Optional file path. If nullopt, no deletion occurs.
   */
  explicit ScopedFileDeleter(const std::optional<std::string>& filepath)
      : filepath_(filepath.value_or("")), enabled_(filepath.has_value()) {}

  // Disable copying
  ScopedFileDeleter(const ScopedFileDeleter&) = delete;
  ScopedFileDeleter& operator=(const ScopedFileDeleter&) = delete;

  // Enable moving
  ScopedFileDeleter(ScopedFileDeleter&&) = default;
  ScopedFileDeleter& operator=(ScopedFileDeleter&&) = default;

  /**
   * @brief Destructor that deletes the file.
   */
  ~ScopedFileDeleter() {
    if (enabled_ && !filepath_.empty()) {
      DeleteFileFromDisk(filepath_);
    }
  }

  /**
   * @brief Releases ownership of the file, preventing deletion.
   *
   * @return The file path that was being managed.
   */
  std::string Release() {
    enabled_ = false;
    return filepath_;
  }

  /**
   * @brief Gets the file path being managed.
   *
   * @return The file path.
   */
  const std::string& GetPath() const { return filepath_; }

 private:
  std::string filepath_;
  bool enabled_;
};

inline std::ostream& operator<<(std::ostream& o,
                                const std::vector<std::string>& v) {
  auto str = assistant::JoinArray(v, ",");
  o << str;
  return o;
}

}  // namespace assistant
