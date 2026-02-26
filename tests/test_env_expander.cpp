#include <gtest/gtest.h>

#include "assistant/EnvExpander.hpp"

using namespace assistant;

// Test expanding a simple environment variable with ${VAR} format
TEST(EnvExpanderTest, Expand_SimpleBracesFormat) {
  EnvExpander expander;
  EnvMap env_map = {{"HOME", "/home/user"}, {"USER", "testuser"}};

  std::string input = "My home is ${HOME}";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "My home is /home/user");
}

// Test expanding a simple environment variable with $VAR format
TEST(EnvExpanderTest, Expand_SimpleNoBracesFormat) {
  EnvExpander expander;
  EnvMap env_map = {{"HOME", "/home/user"}, {"USER", "testuser"}};

  std::string input = "User: $USER";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "User: testuser");
}

// Test expanding multiple variables in one string
TEST(EnvExpanderTest, Expand_MultipleVariables) {
  EnvExpander expander;
  EnvMap env_map = {{"HOME", "/home/user"}, {"USER", "testuser"}};

  std::string input = "$USER lives in ${HOME}";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "testuser lives in /home/user");
}

// Test expanding variable that doesn't exist (should keep original)
TEST(EnvExpanderTest, Expand_NonExistentVariable) {
  EnvExpander expander;
  EnvMap env_map = {{"HOME", "/home/user"}};

  std::string input = "${NONEXISTENT} and $ALSO_NONEXISTENT";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "${NONEXISTENT} and $ALSO_NONEXISTENT");
}

// Test $ at end of string
TEST(EnvExpanderTest, Expand_DollarAtEnd) {
  EnvExpander expander;
  EnvMap env_map = {{"HOME", "/home/user"}};

  std::string input = "Price is 10$";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "Price is 10$");
}

// Test $ followed by non-alphanumeric character
TEST(EnvExpanderTest, Expand_DollarWithNonAlphanumeric) {
  EnvExpander expander;
  EnvMap env_map = {{"HOME", "/home/user"}};

  std::string input = "Cost: $100 dollars";
  std::string result = expander.Expand(input, env_map);

  // $1 should expand (if 1 is not in map, keep $1), then "00 dollars"
  // Since "1" is not in the map, it should keep "$1"
  EXPECT_EQ(result, "Cost: $100 dollars");
}

// Test ${} with missing closing brace
TEST(EnvExpanderTest, Expand_MissingClosingBrace) {
  EnvExpander expander;
  EnvMap env_map = {{"HOME", "/home/user"}};

  std::string input = "${HOME is incomplete";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "${HOME is incomplete");
}

// Test empty variable name
TEST(EnvExpanderTest, Expand_EmptyVariableName) {
  EnvExpander expander;
  EnvMap env_map = {{"HOME", "/home/user"}};

  std::string input = "${} and $ alone";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "${} and $ alone");
}

// Test variable with underscores
TEST(EnvExpanderTest, Expand_VariableWithUnderscores) {
  EnvExpander expander;
  EnvMap env_map = {{"MY_VAR", "value"}, {"MY_OTHER_VAR", "other"}};

  std::string input = "$MY_VAR and ${MY_OTHER_VAR}";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "value and other");
}

// Test variable with numbers
TEST(EnvExpanderTest, Expand_VariableWithNumbers) {
  EnvExpander expander;
  EnvMap env_map = {{"VAR123", "value123"}, {"ABC456", "test"}};

  std::string input = "$VAR123 and ${ABC456}";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "value123 and test");
}

// Test empty string
TEST(EnvExpanderTest, Expand_EmptyString) {
  EnvExpander expander;
  EnvMap env_map = {{"HOME", "/home/user"}};

  std::string input = "";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "");
}

// Test string with no variables
TEST(EnvExpanderTest, Expand_NoVariables) {
  EnvExpander expander;
  EnvMap env_map = {{"HOME", "/home/user"}};

  std::string input = "This is just plain text";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "This is just plain text");
}

// Test consecutive variables
TEST(EnvExpanderTest, Expand_ConsecutiveVariables) {
  EnvExpander expander;
  EnvMap env_map = {{"A", "hello"}, {"B", "world"}};

  std::string input = "$A$B";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "helloworld");
}

// Test variable at beginning, middle, and end
TEST(EnvExpanderTest, Expand_VariablePositions) {
  EnvExpander expander;
  EnvMap env_map = {{"START", "begin"}, {"MID", "middle"}, {"END", "finish"}};

  std::string input = "${START} text $MID text $END";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "begin text middle text finish");
}

// Test expanding JSON with string values
TEST(EnvExpanderTest, ExpandJson_StringValues) {
  EnvExpander expander;
  EnvMap env_map = {{"HOME", "/home/user"}, {"USER", "testuser"}};

  json input = {{"path", "${HOME}/data"}, {"username", "$USER"}};

  json result = expander.Expand(input, env_map);

  EXPECT_EQ(result["path"].get<std::string>(), "/home/user/data");
  EXPECT_EQ(result["username"].get<std::string>(), "testuser");
}

// Test expanding JSON with nested objects
TEST(EnvExpanderTest, ExpandJson_NestedObjects) {
  EnvExpander expander;
  EnvMap env_map = {{"HOME", "/home/user"}, {"PORT", "8080"}};

  json input = {{"config",
                 {{"home_dir", "${HOME}"}, {"server", {{"port", "$PORT"}}}}}};

  json result = expander.Expand(input, env_map);

  EXPECT_EQ(result["config"]["home_dir"].get<std::string>(), "/home/user");
  EXPECT_EQ(result["config"]["server"]["port"].get<std::string>(), "8080");
}

// Test expanding JSON with arrays
TEST(EnvExpanderTest, ExpandJson_Arrays) {
  EnvExpander expander;
  EnvMap env_map = {{"DIR1", "/path1"}, {"DIR2", "/path2"}};

  json input = {{"paths", {"${DIR1}", "$DIR2", "/path3"}}};

  json result = expander.Expand(input, env_map);

  EXPECT_EQ(result["paths"][0].get<std::string>(), "/path1");
  EXPECT_EQ(result["paths"][1].get<std::string>(), "/path2");
  EXPECT_EQ(result["paths"][2].get<std::string>(), "/path3");
}

// Test expanding JSON with non-string types (should remain unchanged)
TEST(EnvExpanderTest, ExpandJson_NonStringTypes) {
  EnvExpander expander;
  EnvMap env_map = {{"VAR", "value"}};

  json input = {{"number", 42},
                {"boolean", true},
                {"null_value", nullptr},
                {"float", 3.14}};

  json result = expander.Expand(input, env_map);

  EXPECT_EQ(result["number"].get<int>(), 42);
  EXPECT_EQ(result["boolean"].get<bool>(), true);
  EXPECT_TRUE(result["null_value"].is_null());
  EXPECT_DOUBLE_EQ(result["float"].get<double>(), 3.14);
}

// Test expanding JSON with mixed types
TEST(EnvExpanderTest, ExpandJson_MixedTypes) {
  EnvExpander expander;
  EnvMap env_map = {{"HOST", "localhost"}, {"PORT", "8080"}};

  json input = {{"server", {{"host", "${HOST}"}, {"port", 8080}}},
                {"enabled", true}};

  json result = expander.Expand(input, env_map);

  EXPECT_EQ(result["server"]["host"].get<std::string>(), "localhost");
  EXPECT_EQ(result["server"]["port"].get<int>(), 8080);
  EXPECT_EQ(result["enabled"].get<bool>(), true);
}

// Test expanding empty JSON object
TEST(EnvExpanderTest, ExpandJson_EmptyObject) {
  EnvExpander expander;
  EnvMap env_map = {{"VAR", "value"}};

  json input = json::object();
  json result = expander.Expand(input, env_map);

  EXPECT_TRUE(result.is_object());
  EXPECT_TRUE(result.empty());
}

// Test expanding empty JSON array
TEST(EnvExpanderTest, ExpandJson_EmptyArray) {
  EnvExpander expander;
  EnvMap env_map = {{"VAR", "value"}};

  json input = json::array();
  json result = expander.Expand(input, env_map);

  EXPECT_TRUE(result.is_array());
  EXPECT_TRUE(result.empty());
}

// Test expanding with empty environment map
TEST(EnvExpanderTest, Expand_EmptyEnvMap) {
  EnvExpander expander;
  EnvMap env_map = {};

  std::string input = "${HOME} and $USER";
  std::string result = expander.Expand(input, env_map);

  // Variables not found, should keep original
  EXPECT_EQ(result, "${HOME} and $USER");
}

// Test expanding with nullopt (should use system environment)
TEST(EnvExpanderTest, Expand_WithSystemEnvironment) {
  EnvExpander expander;

  // Set a test environment variable
#ifdef _WIN32
  _putenv_s("TEST_ENV_VAR_12345", "test_value");
#else
  setenv("TEST_ENV_VAR_12345", "test_value", 1);
#endif

  std::string input = "${TEST_ENV_VAR_12345}";
  std::string result = expander.Expand(input, std::nullopt);

  EXPECT_EQ(result, "test_value");

  // Clean up
#ifdef _WIN32
  _putenv_s("TEST_ENV_VAR_12345", "");
#else
  unsetenv("TEST_ENV_VAR_12345");
#endif
}

// Test expanding JSON with nullopt (should use system environment)
TEST(EnvExpanderTest, ExpandJson_WithSystemEnvironment) {
  EnvExpander expander;

  // Set a test environment variable
#ifdef _WIN32
  _putenv_s("TEST_JSON_VAR_54321", "json_value");
#else
  setenv("TEST_JSON_VAR_54321", "json_value", 1);
#endif

  json input = {{"key", "${TEST_JSON_VAR_54321}"}};
  json result = expander.Expand(input, std::nullopt);

  EXPECT_EQ(result["key"].get<std::string>(), "json_value");

  // Clean up
#ifdef _WIN32
  _putenv_s("TEST_JSON_VAR_54321", "");
#else
  unsetenv("TEST_JSON_VAR_54321");
#endif
}

// Test variable value containing special characters
TEST(EnvExpanderTest, Expand_VariableWithSpecialChars) {
  EnvExpander expander;
  EnvMap env_map = {{"PATH", "/usr/bin:/usr/local/bin"},
                    {"SPECIAL", "value with $pecial ch@rs!"}};

  std::string input = "Path is ${PATH} and ${SPECIAL}";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "Path is /usr/bin:/usr/local/bin and value with $pecial "
                    "ch@rs!");
}

// Test variable value that itself looks like a variable (no recursive
// expansion)
TEST(EnvExpanderTest, Expand_NoRecursiveExpansion) {
  EnvExpander expander;
  EnvMap env_map = {{"VAR1", "$VAR2"}, {"VAR2", "value"}};

  std::string input = "${VAR1}";
  std::string result = expander.Expand(input, env_map);

  // Should expand to "$VAR2", not "value" (no recursive expansion)
  EXPECT_EQ(result, "$VAR2");
}

// Test complex JSON structure
TEST(EnvExpanderTest, ExpandJson_ComplexStructure) {
  EnvExpander expander;
  EnvMap env_map = {{"DB_HOST", "db.example.com"},
                    {"DB_PORT", "5432"},
                    {"DB_USER", "admin"},
                    {"API_KEY", "secret123"}};

  json input = {
      {"database",
       {{"host", "${DB_HOST}"},
        {"port", "${DB_PORT}"},
        {"credentials", {{"username", "$DB_USER"}, {"password", "hardcoded"}}},
        {"pools", {{"min", 5}, {"max", 20}}}}},
      {"api", {{"key", "${API_KEY}"}, {"endpoint", "/api/v1"}}},
      {"features", {"feature1", "feature2"}}};

  json result = expander.Expand(input, env_map);

  EXPECT_EQ(result["database"]["host"].get<std::string>(), "db.example.com");
  EXPECT_EQ(result["database"]["port"].get<std::string>(), "5432");
  EXPECT_EQ(result["database"]["credentials"]["username"].get<std::string>(),
            "admin");
  EXPECT_EQ(result["database"]["credentials"]["password"].get<std::string>(),
            "hardcoded");
  EXPECT_EQ(result["database"]["pools"]["min"].get<int>(), 5);
  EXPECT_EQ(result["database"]["pools"]["max"].get<int>(), 20);
  EXPECT_EQ(result["api"]["key"].get<std::string>(), "secret123");
  EXPECT_EQ(result["api"]["endpoint"].get<std::string>(), "/api/v1");
  EXPECT_EQ(result["features"][0].get<std::string>(), "feature1");
  EXPECT_EQ(result["features"][1].get<std::string>(), "feature2");
}

// Test variable name with only underscores
TEST(EnvExpanderTest, Expand_UnderscoreOnlyVariable) {
  EnvExpander expander;
  EnvMap env_map = {{"_", "underscore_value"}, {"__", "double_underscore"}};

  std::string input = "$_ and $__";
  std::string result = expander.Expand(input, env_map);

  EXPECT_EQ(result, "underscore_value and double_underscore");
}

// Test escaped dollar signs (note: this implementation doesn't support
// escaping)
TEST(EnvExpanderTest, Expand_MultipleDollars) {
  EnvExpander expander;
  EnvMap env_map = {{"VAR", "value"}};

  std::string input = "$$VAR";
  std::string result = expander.Expand(input, env_map);

  // First $ is treated as variable with empty name (kept as $), then $VAR is
  // expanded This will result in "$value"
  EXPECT_EQ(result, "$value");
}

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
