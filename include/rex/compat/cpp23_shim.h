/**
 * C++23 compatibility shim for libc++ (llvm-mingw).
 * Provides missing features:
 *   - std::move_only_function (polyfill using std::function)
 *   - std::chrono::clock_time_conversion (primary template + clock_cast)
 */

#pragma once

#include <functional>
#include <chrono>
#include <type_traits>

// Polyfill std::move_only_function when not available (libc++ < 19)
#if !defined(__cpp_lib_move_only_function) || __cpp_lib_move_only_function < 202110L
namespace std {
template <typename Signature>
using move_only_function = std::function<Signature>;
}
#endif

// Polyfill std::chrono::clock_time_conversion and clock_cast
// when __cpp_lib_chrono doesn't include them (needs >= 201907L)
#if defined(__cpp_lib_chrono) && __cpp_lib_chrono < 201907L
namespace std::chrono {

template <typename Dest, typename Source>
struct clock_time_conversion {};

// identity conversion
template <typename Clock>
struct clock_time_conversion<Clock, Clock> {
    template <typename Duration>
    time_point<Clock, Duration> operator()(const time_point<Clock, Duration>& tp) const {
        return tp;
    }
};

template <typename Dest, typename Source, typename Duration>
auto clock_cast(const time_point<Source, Duration>& tp) {
    return clock_time_conversion<Dest, Source>{}(tp);
}

}  // namespace std::chrono
#endif
