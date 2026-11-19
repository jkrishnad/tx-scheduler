#pragma once
#include <format>
#include <string>

#include <unistd.h>

// ANSI terminal colours.
namespace color {
inline constexpr const char *BOLD = "\x1b[1m";
inline constexpr const char *RED = "\x1b[31m";
inline constexpr const char *RESET = "\x1b[0m";

// Line labels say what kind of line it is; keys recede; values stay plain.
inline constexpr const char *SLOT = "\x1b[1;34m";    // bold blue
inline constexpr const char *TX = "\x1b[1;32m";      // bold green
inline constexpr const char *KEY = "\x1b[38;5;240m"; // dark grey

// Colour only when writing to a terminal, so piped output stays plain text.
inline const bool out_is_tty = isatty(STDOUT_FILENO);
inline const bool err_is_tty = isatty(STDERR_FILENO);

inline const char *out(const char *code) { return out_is_tty ? code : ""; }
inline const char *err(const char *code) { return err_is_tty ? code : ""; }

template <typename T> std::string paint(const char *code, const T &value) {
    return std::format("{}{}{}", out(code), value, out(RESET));
}
} // namespace color
