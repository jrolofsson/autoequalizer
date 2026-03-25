#pragma once

#include <cmath>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace autoequalizer::tests {

using TestFunction = std::function<void()>;

inline std::vector<std::pair<std::string, TestFunction>>& registry() {
  static std::vector<std::pair<std::string, TestFunction>> tests;
  return tests;
}

inline void registerTest(const std::string& name, TestFunction test) {
  registry().emplace_back(name, std::move(test));
}

}  // namespace autoequalizer::tests

#define AUTOEQUALIZER_CONCAT_INNER(left, right) left##right
#define AUTOEQUALIZER_CONCAT(left, right) AUTOEQUALIZER_CONCAT_INNER(left, right)

#define TEST_CASE(name)                                                        \
  static void AUTOEQUALIZER_CONCAT(test_, __LINE__)();                                \
  namespace {                                                                  \
  const bool AUTOEQUALIZER_CONCAT(registered_, __LINE__) = []() {                     \
    autoequalizer::tests::registerTest(name, AUTOEQUALIZER_CONCAT(test_, __LINE__));         \
    return true;                                                               \
  }();                                                                         \
  }                                                                            \
  static void AUTOEQUALIZER_CONCAT(test_, __LINE__)()

#define EXPECT_TRUE(condition)                                                 \
  do {                                                                         \
    if (!(condition)) {                                                        \
      throw std::runtime_error(std::string("Expectation failed: ") +           \
                               #condition);                                    \
    }                                                                          \
  } while (false)

#define EXPECT_NEAR(actual, expected, tolerance)                               \
  do {                                                                         \
    if (std::fabs((actual) - (expected)) > (tolerance)) {                      \
      std::ostringstream message;                                              \
      message << "Expected " << #actual << " ~= " << #expected                 \
              << " within " << tolerance << ", but got " << (actual)          \
              << " and " << (expected);                                        \
      throw std::runtime_error(message.str());                                 \
    }                                                                          \
  } while (false)

