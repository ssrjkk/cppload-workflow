// @author ssrjkk | volley
#pragma once

#include <cstdlib>
#include <string>
#include <string_view>
#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#include <io.h>
#define ISATTY _isatty
#define FILENO _fileno
#else
#include <unistd.h>
#define ISATTY isatty
#define FILENO fileno
#endif

namespace cppload::core::term {

enum class Color : std::uint8_t {
    Reset = 0,
    Bold = 1,
    FgBlack = 30,
    FgRed = 31,
    FgGreen = 32,
    FgYellow = 33,
    FgBlue = 34,
    FgMagenta = 35,
    FgCyan = 36,
    FgWhite = 37,
    FgBrightBlack = 90,
    FgBrightRed = 91,
    FgBrightGreen = 92,
    FgBrightYellow = 93,
    FgBrightBlue = 94,
    FgBrightMagenta = 95,
    FgBrightCyan = 96,
    FgBrightWhite = 97,
};

inline bool stdout_is_tty() {
    return ISATTY(FILENO(stdout)) != 0;
}

inline bool no_color_env() {
    const char* nc = std::getenv("NO_COLOR");
    return nc && nc[0] != '\0';
}

inline bool use_color() {
    return stdout_is_tty() && !no_color_env();
}

inline std::string ansi(Color c) {
    if (!use_color()) return {};
    return "\x1b[" + std::to_string(static_cast<int>(c)) + "m";
}

inline std::string reset() { return ansi(Color::Reset); }
inline std::string bold() { return ansi(Color::Bold); }
inline std::string red() { return ansi(Color::FgRed); }
inline std::string green() { return ansi(Color::FgGreen); }
inline std::string yellow() { return ansi(Color::FgYellow); }
inline std::string blue() { return ansi(Color::FgBlue); }
inline std::string magenta() { return ansi(Color::FgMagenta); }
inline std::string cyan() { return ansi(Color::FgCyan); }
inline std::string bright_red() { return ansi(Color::FgBrightRed); }
inline std::string bright_green() { return ansi(Color::FgBrightGreen); }
inline std::string bright_yellow() { return ansi(Color::FgBrightYellow); }
inline std::string bright_blue() { return ansi(Color::FgBrightBlue); }
inline std::string bright_cyan() { return ansi(Color::FgBrightCyan); }

inline std::string wrap(const std::string& text, const std::string& color_code) {
    if (color_code.empty()) return text;
    return color_code + text + reset();
}

inline std::string ok_label() { return wrap("[PASS] ", green() + bold()); }
inline std::string fail_label() { return wrap("[FAIL] ", bright_red() + bold()); }
inline std::string info_label() { return wrap("[INFO] ", bright_blue() + bold()); }
inline std::string warn_label() { return wrap("[WARN] ", bright_yellow() + bold()); }
inline std::string error_label() { return wrap("[ERR ] ", bright_red() + bold()); }
inline std::string sla_pass_label() { return wrap("SLA PASSED", bright_green() + bold()); }
inline std::string sla_fail_label() { return wrap("SLA FAILED", bright_red() + bold()); }

} // namespace cppload::core::term

#undef ISATTY
#undef FILENO
