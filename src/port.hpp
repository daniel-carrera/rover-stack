#pragma once
#include <string_view>
#include <string>
#include <optional>

enum class PortStatus {
    written,
    timed_out,
    error
};

class Port {
public: 
    // destroying through Port& should be safe
    virtual ~Port() = default;
    // no body here, every port supplies one
    virtual PortStatus write(std::string_view) = 0; 
    virtual std::optional<std::string> read() = 0;
};
