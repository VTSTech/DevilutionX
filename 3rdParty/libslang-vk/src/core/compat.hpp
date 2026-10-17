#pragma once

// Internal compatibility shims for the port of the reference host's preset
// layer (see PROVENANCE.md). These replace three host utilities the ported
// sources used:
//
//   1. StrCat            — fork: Source/utils/str_cat.hpp ( DevilutionX).
//      Only string/integral overloads are exercised by the ported code.
//   2. ParseInt<T>       — fork: Source/utils/parse_int.hpp template (the
//      std::from_chars-based core; fixed-point helpers are not used here).
//   3. utils/log.hpp     — replaced by a caller-provided log callback; the
//      library never writes to stdout/stderr on its own (plan §5.2).

#include <charconv>
#include <concepts>
#include <expected>
#include <limits>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace slv::preset {

// ---------------------------------------------------------------------------
// Logging
// ---------------------------------------------------------------------------

using LogCallback = void (*)(void *userData, const char *message);

/** @brief The process-wide log sink pair (callback + user data). */
inline std::pair<LogCallback, void *> &GetLogSink()
{
        static std::pair<LogCallback, void *> sink = { nullptr, nullptr };
        return sink;
}

/** @brief Installs the library-wide log sink (call once at startup). Passing
 * nullptr disables logging (the default). Not thread-safe with concurrent
 * log calls; see the threading contract in the plan. */
inline void SetLogCallback(LogCallback callback, void *userData)
{
        GetLogSink().first = callback;
        GetLogSink().second = userData;
}

inline void Log(const std::string &message)
{
        auto &[callback, userData] = GetLogSink();
        if (callback != nullptr)
                callback(userData, message.c_str());
}

// ---------------------------------------------------------------------------
// StrCat (minimal fork shim)
// ---------------------------------------------------------------------------

namespace detail {

template <typename T>
        requires std::convertible_to<T, std::string_view>
inline std::string StrCatPiece(const T &value)
{
        return std::string(static_cast<std::string_view>(value));
}

template <typename T>
        requires std::same_as<T, char>
inline std::string StrCatPiece(const T &value)
{
        return std::string(1, value);
}

template <typename T>
        requires(!std::convertible_to<T, std::string_view> && !std::same_as<T, char>)
inline std::string StrCatPiece(const T &value)
{
        return std::to_string(value);
}

} // namespace detail

/** @brief Concatenates strings, string literals and integral values.
 * Matches the reference host's usage surface (paths, counts, indices). */
template <typename... Ts>
inline std::string StrCat(const Ts &...values)
{
        std::string out;
        out.reserve(sizeof...(Ts) * 8);
        ([&] { out += detail::StrCatPiece(values); }(), ...);
        return out;
}

// ---------------------------------------------------------------------------
// ParseInt (fork parse_int.hpp core, self-contained here)
// ---------------------------------------------------------------------------

enum class ParseIntError {
        ParseError = 1,
        OutOfRange
};

template <typename IntT>
using ParseIntResult = std::expected<IntT, ParseIntError>;

template <typename IntT>
ParseIntResult<IntT> ParseInt(
    std::string_view str, IntT min = std::numeric_limits<IntT>::min(),
    IntT max = std::numeric_limits<IntT>::max(), const char **endOfParse = nullptr)
{
        IntT value;
        const std::from_chars_result result = std::from_chars(str.data(), str.data() + str.size(), value);
        if (endOfParse != nullptr) {
                *endOfParse = result.ptr;
        }
        if (result.ec == std::errc::invalid_argument)
                return std::unexpected(ParseIntError::ParseError);
        if (result.ec == std::errc::result_out_of_range || value < min || value > max)
                return std::unexpected(ParseIntError::OutOfRange);
        if (result.ec != std::errc())
                return std::unexpected(ParseIntError::ParseError);
        return value;
}

} // namespace slv::preset
