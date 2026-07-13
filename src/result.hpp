// Result<T, E> — the C++ stand-in for Rust's `Result<T, E>`.
//
// We deliberately do NOT use std::expected so the whole codebase stays on C++20
// (std::expected is C++23). This is a small, explicit type: an ok value or an
// error, disambiguated by index so T and E may even be the same type.
//
// Pairs with std::optional<T> for Rust's Option<T>. Fallible functions that mirror
// a Rust `-> Result<..>` return this; infallible ones just return the value.
#pragma once

#include <string>
#include <utility>
#include <variant>

namespace snapback {

// Mirrors Rust's error-carrying types (thiserror). A message string is enough for
// the port; we can specialize into typed error kinds per subsystem later.
struct Error {
    std::string message;
    Error() = default;
    explicit Error(std::string msg) : message(std::move(msg)) {}
};

template <class T, class E = Error>
class [[nodiscard]] Result {
public:
    static Result ok(T value) { return Result(std::in_place_index<0>, std::move(value)); }
    static Result err(E error) { return Result(std::in_place_index<1>, std::move(error)); }

    bool is_ok() const noexcept { return data_.index() == 0; }
    bool is_err() const noexcept { return data_.index() == 1; }
    explicit operator bool() const noexcept { return is_ok(); }

    // Precondition: is_ok(). Mirrors Rust's `.unwrap()` contract.
    T& value() & { return std::get<0>(data_); }
    const T& value() const& { return std::get<0>(data_); }
    T&& value() && { return std::get<0>(std::move(data_)); }

    // Precondition: is_err().
    E& error() & { return std::get<1>(data_); }
    const E& error() const& { return std::get<1>(data_); }

    // Rust's `.unwrap_or(fallback)`.
    T value_or(T fallback) const& { return is_ok() ? std::get<0>(data_) : std::move(fallback); }

private:
    template <std::size_t I, class U>
    Result(std::in_place_index_t<I> idx, U&& u) : data_(idx, std::forward<U>(u)) {}
    std::variant<T, E> data_;
};

// Rust's `Result<(), E>`: success carries no value, failure carries an Error.
using Status = Result<std::monostate, Error>;
inline Status ok_status() { return Status::ok(std::monostate{}); }
inline Status err_status(std::string msg) { return Status::err(Error{std::move(msg)}); }

}  // namespace snapback
