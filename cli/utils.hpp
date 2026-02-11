#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

template <typename Error>
class Err {
 public:
  Err(Error val) : val_(std::move(val)) {}
  Error GetValue() const { return val_; }

 private:
  Error val_;
};

template <typename Value, typename Error>
class Result {
 public:
  Result(Value val) : val_(std::move(val)) {}
  Result(Err<Error> err) : err_(std::move(err)) {}
  inline bool IsOk() const { return val_.has_value(); }
  inline Error GetError() const { return err_.value().GetValue(); }
  inline Value GetValue() const { return val_.value(); }

 private:
  std::optional<Value> val_;
  std::optional<Err<Error>> err_;
};

/// Given `choices`, present the user with a list of options to pick from.
/// Return the selected index.
inline size_t GetChoiceFromUser(const std::vector<std::string>& choices) {
  size_t num = (size_t)-1;
  while (true) {
    num = (size_t)-1;
    std::cout << "Enter your choice (0-" << choices.size() - 1 << ")>";
    std::string answer;
    std::getline(std::cin, answer);
    if (answer.empty()) {
      continue;
    }
    try {
      num = std::atol(answer.c_str());
    } catch (std::exception& e) {
      std::cerr << e.what() << std::endl;
      continue;
    }

    if (num >= choices.size()) {
      std::cerr << "Invalid number, choose a number between 0-"
                << choices.size() - 1 << std::endl;
      continue;
    }
    return num;
  }
}

inline bool ReadYesOrNoFromUser(const std::string& prompt) {
  while (true) {
    std::cout << prompt;

    std::string input;
    std::getline(std::cin, input);

    // Check if input stream failed (e.g., EOF)
    if (std::cin.fail()) {
      std::cin.clear();
      std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
      std::cout << "Invalid input. Please enter 'y' or 'n'." << std::endl;
      continue;
    }

    // Check for single character 'y' or 'n' (case-insensitive)
    if (input.length() == 1) {
      char c = std::tolower(input[0]);
      if (c == 'y') {
        return true;
      } else if (c == 'n') {
        return false;
      }
    }

    std::cout << "Invalid input. Please enter 'y' or 'n'." << std::endl;
  }
}

inline Result<std::string, std::string> ReadFileContent(
    const std::string& path) {
  std::ifstream file(path, std::ios::binary);  // open in binary mode
  if (!file) {
    std::stringstream ss;
    ss << "Error: could not open file '" << path << "'\n";
    return Err(ss.str());
  }

  // Read the file into a stringstream
  std::ostringstream buffer;
  buffer << file.rdbuf();  // read the whole stream

  // Return the string
  return buffer.str();
}

inline std::string GetTextFromUser() {
  while (true) {
    std::string text;
    std::getline(std::cin, text);
    if (text.empty()) {
      continue;
    }
    return text;
  }
}

inline Result<bool, std::string> CreateDirectoryForFile(
    const std::string& file) {
  // Define the full path for the file, including the non-existent folder.
  std::filesystem::path file_path = file;

  // 1. Extract the directory path from the full file path.
  // The parent_path() function returns the path to the directory containing the
  // file.
  std::filesystem::path dir_path = file_path.parent_path();
  if (dir_path.empty()) {
    return true;
  }

  // 2. Recursively create all intermediate directories if they don't exist.
  // create_directories() will not fail if the path already exists.
  try {
    std::filesystem::create_directories(dir_path);
    return true;
  } catch (const std::filesystem::filesystem_error& e) {
    std::stringstream ss;
    ss << "Error creating directories: " << e.what();
    return Err(ss.str());
  }
}
