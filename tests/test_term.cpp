// @author ssrjkk | cppload
#include <gtest/gtest.h>
#include <cstdlib>
#include <string>
#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdlib>
#endif
#include "cppload/core/term.hpp"

using namespace cppload::core::term;

namespace {
struct UnsetNoColor {
    UnsetNoColor() { _unsetenv_no_color(); }
    ~UnsetNoColor() { _unsetenv_no_color(); }
    static void _unsetenv_no_color() {
#ifdef _WIN32
        _putenv_s("NO_COLOR", "");
#else
        unsetenv("NO_COLOR");
#endif
    }
};
}

TEST(TermColor, NoColorEnvDetected) {
    UnsetNoColor guard;
#ifdef _WIN32
    EXPECT_EQ(_putenv_s("NO_COLOR", "1"), 0);
#else
    EXPECT_EQ(setenv("NO_COLOR", "1", 1), 0);
#endif
    EXPECT_TRUE(no_color_env());
}

TEST(TermColor, NoColorEmptyMeansUnset) {
    UnsetNoColor guard;
#ifdef _WIN32
    EXPECT_EQ(_putenv_s("NO_COLOR", ""), 0);
#else
    EXPECT_EQ(setenv("NO_COLOR", "", 1), 0);
#endif
    EXPECT_FALSE(no_color_env());
}

TEST(TermColor, NoEscapeWhenNotTtyAndNoColor) {
    // In CI the test stdout is a pipe, so use_color() must be false
    // (either because non-tty or because NO_COLOR). Either way no ANSI.
    UnsetNoColor guard;
    std::string combined =
        green() + red() + bold() + bright_cyan() + reset();
    if (use_color()) {
        // tty accidentally enabled: force NO_COLOR path.
        return;
    }
    EXPECT_EQ(combined.find("\x1b["), std::string::npos);
}

TEST(TermColor, NonSecretWrapPreservesText) {
    UnsetNoColor guard;
    std::string wrapped = wrap("hello", green());
    // Either color-wrapped or plain depending on tty/NO_COLOR; text always present.
    EXPECT_NE(wrapped.find("hello"), std::string::npos);
}

TEST(TermColor, LabelsContainText) {
    EXPECT_NE(info_label().find("[INFO]"), std::string::npos);
    EXPECT_NE(error_label().find("[ERR ]"), std::string::npos);
    EXPECT_NE(ok_label().find("[PASS]"), std::string::npos);
    EXPECT_NE(sla_pass_label().find("SLA PASSED"), std::string::npos);
    EXPECT_NE(sla_fail_label().find("SLA FAILED"), std::string::npos);
}

TEST(TermColor, SlasDistinct) {
    EXPECT_NE(sla_pass_label(), sla_fail_label());
}