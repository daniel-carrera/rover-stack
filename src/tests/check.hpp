// CHECK_EQ(got, expected) counts a failure and prints both; kit::finish() prints the tally and gives the exit status.
#pragma once

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>

namespace kit {

inline int checks = 0;
inline int failures = 0;

template <typename T>
std::string show(const T& value) {
    std::ostringstream out;
    if constexpr (std::is_same_v<T, bool>) {
        out << (value ? "true" : "false");
    } else if constexpr (std::is_integral_v<T>) {
        const auto wide = static_cast<unsigned long long>(value);
        out << wide << " (0x" << std::hex << std::setw(static_cast<int>(2 * sizeof(T))) << std::setfill('0') << wide
            << ")";
    } else {
        out << value;
    }
    return out.str();
}

template <typename T>
std::string show(const std::optional<T>& value) {
    return value ? show(*value) : std::string("nothing");
}

template <typename T>
void equal(const T& got, const T& expected, const char* what, const char* file, int line) {
    ++checks;
    if (got == expected) {
        return;
    }
    ++failures;
    if (failures <= 6) {
        std::cout << "  " << what << "    (" << file << ":" << line << ")\n"
                  << "         got: " << show(got) << "\n"
                  << "    expected: " << show(expected) << "\n";
    }
}

inline int finish() {
    if (failures > 6) {
        std::cout << "  ... and " << failures - 6 << " more\n";
    }
    std::cout << checks - failures << " of " << checks << " checks pass\n";
    return failures == 0 ? 0 : 1;
}

}  // namespace kit

#define CHECK_EQ(got, expected) kit::equal((got), (expected), #got, __FILE__, __LINE__)
