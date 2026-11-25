#pragma once
#include <charconv>

namespace fast_float {

template <class T>
inline std::from_chars_result from_chars(char const* first, char const* last, T& value) {
    return std::from_chars(first, last, value);
}

} // namespace fast_float
