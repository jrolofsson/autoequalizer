#pragma once

#include <string>
#include <utility>
#include <variant>

namespace autoequalizer::core {

struct Error {
  std::string code;
  std::string message;
};

template <typename T>
class Result {
 public:
  Result(T value) : storage_(std::move(value)) {}
  Result(Error error) : storage_(std::move(error)) {}

  [[nodiscard]] bool ok() const noexcept {
    return std::holds_alternative<T>(storage_);
  }

  [[nodiscard]] const T& value() const& {
    return std::get<T>(storage_);
  }

  [[nodiscard]] T& value() & {
    return std::get<T>(storage_);
  }

  [[nodiscard]] T&& value() && {
    return std::get<T>(std::move(storage_));
  }

  [[nodiscard]] const Error& error() const {
    return std::get<Error>(storage_);
  }

 private:
  std::variant<T, Error> storage_;
};

template <>
class Result<void> {
 public:
  Result() : ok_(true) {}
  Result(Error error) : ok_(false), error_(std::move(error)) {}

  [[nodiscard]] bool ok() const noexcept { return ok_; }
  [[nodiscard]] const Error& error() const { return error_; }

 private:
  bool ok_{true};
  Error error_{};
};

}  // namespace autoequalizer::core

