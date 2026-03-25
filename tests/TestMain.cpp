#include <iostream>
#include <string>

#include "tests/TestFramework.hpp"

int main() {
  int failures = 0;
  for (const auto& [name, test] : autoequalizer::tests::registry()) {
    try {
      test();
      std::cout << "[PASS] " << name << "\n";
    } catch (const std::exception& error) {
      ++failures;
      std::cout << "[FAIL] " << name << ": " << error.what() << "\n";
    }
  }

  std::cout << autoequalizer::tests::registry().size() << " tests run, " << failures
            << " failures\n";
  return failures == 0 ? 0 : 1;
}

