// @author ssrjkk | cppload
#include <gtest/gtest.h>
#include "cppload/result.hpp"
#include "cppload/error.hpp"
#include <string>
#include <system_error>

using namespace cppload;

TEST(ResultTest, OkValue) {
    Result<int, Err> r = Result<int, Err>::ok(42);
    EXPECT_TRUE(r.has_value());
    EXPECT_EQ(r.value(), 42);
}

TEST(ResultTest, ErrorValue) {
    Result<int, Err> r = Result<int, Err>::err(Err::timeout);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), Err::timeout);
}

TEST(ResultTest, ValueOrDefault) {
    Result<int, Err> ok = Result<int, Err>::ok(10);
    Result<int, Err> err = Result<int, Err>::err(Err::timeout);
    EXPECT_EQ(ok.value_or(0), 10);
    EXPECT_EQ(err.value_or(0), 0);
}

TEST(ResultTest, MapOk) {
    Result<int, Err> r = Result<int, Err>::ok(5);
    auto mapped = r.map([](int v) { return v * 2; });
    EXPECT_TRUE(mapped.has_value());
    EXPECT_EQ(mapped.value(), 10);
}

TEST(ResultTest, MapError) {
    Result<int, Err> r = Result<int, Err>::err(Err::timeout);
    auto mapped = r.map([](int v) { return v * 2; });
    EXPECT_FALSE(mapped.has_value());
    EXPECT_EQ(mapped.error(), Err::timeout);
}

TEST(ResultTest, TransformErrorOk) {
    Result<int, Err> r = Result<int, Err>::ok(42);
    auto transformed = r.transform_error([](Err e) -> std::string {
        return "error: " + std::to_string(static_cast<int>(e));
    });
    EXPECT_TRUE(transformed.has_value());
    EXPECT_EQ(transformed.value(), 42);
}

TEST(ResultTest, TransformErrorErr) {
    Result<int, Err> r = Result<int, Err>::err(Err::timeout);
    auto transformed = r.transform_error([](Err e) -> std::string {
        return "error: " + std::to_string(static_cast<int>(e));
    });
    EXPECT_FALSE(transformed.has_value());
    EXPECT_EQ(transformed.error(), "error: 5");
}

TEST(ResultTest, AndThenOk) {
    Result<int, Err> r = Result<int, Err>::ok(5);
    auto result = r.and_then([](int v) -> Result<std::string, Err> {
        return Result<std::string, Err>::ok(std::to_string(v));
    });
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), "5");
}

TEST(ResultTest, AndThenError) {
    Result<int, Err> r = Result<int, Err>::err(Err::timeout);
    auto result = r.and_then([](int v) -> Result<std::string, Err> {
        return Result<std::string, Err>::ok(std::to_string(v));
    });
    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), Err::timeout);
}

TEST(ResultTest, OrElseOk) {
    Result<int, Err> r = Result<int, Err>::ok(42);
    auto result = r.or_else([](Err) -> Result<int, Err> {
        return Result<int, Err>::ok(0);
    });
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 42);
}

TEST(ResultTest, OrElseError) {
    Result<int, Err> r = Result<int, Err>::err(Err::timeout);
    auto result = r.or_else([](Err) -> Result<int, Err> {
        return Result<int, Err>::ok(99);
    });
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 99);
}

TEST(ResultVoidTest, Ok) {
    Result<void, Err> r = Result<void, Err>::ok();
    EXPECT_TRUE(r.has_value());
}

TEST(ResultVoidTest, Error) {
    Result<void, Err> r = Result<void, Err>::err(Err::timeout);
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(r.error(), Err::timeout);
}

TEST(ResultVoidTest, TransformErrorOk) {
    Result<void, Err> r = Result<void, Err>::ok();
    auto transformed = r.transform_error([](Err e) -> std::string {
        return "error";
    });
    EXPECT_TRUE(transformed.has_value());
}

TEST(ResultVoidTest, TransformErrorErr) {
    Result<void, Err> r = Result<void, Err>::err(Err::timeout);
    auto transformed = r.transform_error([](Err e) -> std::string {
        return "timeout";
    });
    EXPECT_FALSE(transformed.has_value());
    EXPECT_EQ(transformed.error(), "timeout");
}

TEST(ResultTest, MoveSemantics) {
    Result<std::string, Err> r = Result<std::string, Err>::ok("hello");
    auto moved = std::move(r);
    EXPECT_TRUE(moved.has_value());
    EXPECT_EQ(moved.value(), "hello");
}