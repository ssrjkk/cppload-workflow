// @author ssrjkk | cppload
#pragma once

#include <variant>
#include <type_traits>
#include <utility>
#include <cassert>
#include <functional>

namespace cppload {

template <typename T, typename E>
class [[nodiscard]] Result {
public:
    explicit Result(const T& value) : storage_(value) {}
    explicit Result(T&& value) : storage_(std::move(value)) {}
    explicit Result(const E& error) : storage_(error) {}
    explicit Result(E&& error) : storage_(std::move(error)) {}

    Result(const Result&) = default;
    Result& operator=(const Result&) = default;
    Result(Result&&) noexcept = default;
    Result& operator=(Result&&) noexcept = default;

    static Result ok(T value) { return Result(std::move(value)); }
    static Result err(E error) { return Result(std::move(error)); }

    [[nodiscard]] constexpr bool has_value() const noexcept { return storage_.index() == 0; }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    T& value() & { assert(has_value()); return std::get<0>(storage_); }
    const T& value() const& { assert(has_value()); return std::get<0>(storage_); }
    T&& value() && { assert(has_value()); return std::get<0>(std::move(storage_)); }

    E& error() & { assert(!has_value()); return std::get<1>(storage_); }
    const E& error() const& { assert(!has_value()); return std::get<1>(storage_); }
    E&& error() && { assert(!has_value()); return std::get<1>(std::move(storage_)); }

    T value_or(const T& default_value) const& {
        return has_value() ? std::get<0>(storage_) : default_value;
    }

    T value_or(T&& default_value) && {
        return has_value() ? std::get<0>(std::move(storage_)) : std::forward<T>(default_value);
    }

    template <typename F>
    auto map(F&& f) const& -> Result<std::invoke_result_t<F, const T&>, E> {
        if (has_value()) return Result<std::invoke_result_t<F, const T&>, E>::ok(f(std::get<0>(storage_)));
        return Result<std::invoke_result_t<F, const T&>, E>::err(std::get<1>(storage_));
    }

    template <typename F>
    auto map(F&& f) && -> Result<std::invoke_result_t<F, T&&>, E> {
        if (has_value()) return Result<std::invoke_result_t<F, T&&>, E>::ok(f(std::get<0>(std::move(storage_))));
        return Result<std::invoke_result_t<F, T&&>, E>::err(std::get<1>(std::move(storage_)));
    }

    template <typename F>
    auto and_then(F&& f) const& -> decltype(f(std::declval<const T&>())) {
        if (has_value()) return f(std::get<0>(storage_));
        using R = decltype(f(std::declval<const T&>()));
        return R::err(std::get<1>(storage_));
    }

    template <typename F>
    auto and_then(F&& f) && -> decltype(f(std::declval<T&&>())) {
        if (has_value()) return f(std::get<0>(std::move(storage_)));
        using R = decltype(f(std::declval<T&&>()));
        return R::err(std::get<1>(std::move(storage_)));
    }

    template <typename F,
              typename = std::enable_if_t<!std::is_void_v<std::invoke_result_t<F, const E&>>>>
    auto or_else(F&& f) const& -> Result {
        if (has_value()) return Result::ok(std::get<0>(storage_));
        return f(std::get<1>(storage_));
    }

    template <typename F,
              typename = std::enable_if_t<std::is_void_v<std::invoke_result_t<F, const E&>>>>
    void or_else(F&& f) const& {
        if (!has_value()) f(std::get<1>(storage_));
    }

    template <typename F,
              typename = std::enable_if_t<!std::is_void_v<std::invoke_result_t<F, E&&>>>>
    auto or_else(F&& f) && -> Result {
        if (has_value()) return Result::ok(std::get<0>(std::move(storage_)));
        return f(std::get<1>(std::move(storage_)));
    }

    template <typename F,
              typename = std::enable_if_t<std::is_void_v<std::invoke_result_t<F, E&&>>>>
    void or_else(F&& f) && {
        if (!has_value()) f(std::get<1>(std::move(storage_)));
    }

    template <typename F>
    auto transform_error(F&& f) const& -> Result<T, std::invoke_result_t<F, const E&>> {
        if (has_value()) return Result<T, std::invoke_result_t<F, const E&>>::ok(std::get<0>(storage_));
        return Result<T, std::invoke_result_t<F, const E&>>::err(f(std::get<1>(storage_)));
    }

    template <typename F>
    auto transform_error(F&& f) && -> Result<T, std::invoke_result_t<F, E&&>> {
        if (has_value()) return Result<T, std::invoke_result_t<F, E&&>>::ok(std::get<0>(std::move(storage_)));
        return Result<T, std::invoke_result_t<F, E&&>>::err(f(std::get<1>(std::move(storage_))));
    }

    void swap(Result& other) noexcept {
        storage_.swap(other.storage_);
    }

private:
    explicit Result(std::variant<T, E> v) : storage_(std::move(v)) {}
    std::variant<T, E> storage_;
};

template <typename T, typename E>
void swap(Result<T, E>& a, Result<T, E>& b) noexcept {
    a.swap(b);
}

template <typename E>
class [[nodiscard]] Result<void, E> {
public:
    Result() : storage_(std::monostate{}) {}
    explicit Result(const E& error) : storage_(error) {}
    explicit Result(E&& error) : storage_(std::move(error)) {}

    Result(const Result&) = default;
    Result& operator=(const Result&) = default;
    Result(Result&&) noexcept = default;
    Result& operator=(Result&&) noexcept = default;

    static Result ok() { return Result(); }
    static Result err(E error) { return Result(std::move(error)); }

    [[nodiscard]] constexpr bool has_value() const noexcept { return storage_.index() == 0; }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    E& error() & { assert(!has_value()); return std::get<1>(storage_); }
    const E& error() const& { assert(!has_value()); return std::get<1>(storage_); }
    E&& error() && { assert(!has_value()); return std::get<1>(std::move(storage_)); }

    template <typename F,
              typename = std::enable_if_t<std::is_void_v<std::invoke_result_t<F, const E&>>>>
    void or_else(F&& f) const& {
        if (!has_value()) f(std::get<1>(storage_));
    }

    template <typename F,
              typename = std::enable_if_t<std::is_void_v<std::invoke_result_t<F, E&&>>>>
    void or_else(F&& f) && {
        if (!has_value()) f(std::get<1>(std::move(storage_)));
    }

    template <typename F>
    auto transform_error(F&& f) const& -> Result<void, std::invoke_result_t<F, const E&>> {
        if (has_value()) return Result<void, std::invoke_result_t<F, const E&>>::ok();
        return Result<void, std::invoke_result_t<F, const E&>>::err(f(std::get<1>(storage_)));
    }

    template <typename F>
    auto transform_error(F&& f) && -> Result<void, std::invoke_result_t<F, E&&>> {
        if (has_value()) return Result<void, std::invoke_result_t<F, E&&>>::ok();
        return Result<void, std::invoke_result_t<F, E&&>>::err(f(std::get<1>(std::move(storage_))));
    }

    void swap(Result& other) noexcept {
        storage_.swap(other.storage_);
    }

private:
    std::variant<std::monostate, E> storage_;
};

template <typename E>
void swap(Result<void, E>& a, Result<void, E>& b) noexcept {
    a.swap(b);
}

} // namespace cppload