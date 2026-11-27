#pragma once

#include "src/port.hpp"
#include <vector>

class FakePort : public Port {
public:
    PortStatus write(std::string_view bytes) override {
        bytes_attempted.push_back(std::string(bytes));
        return next_write_status;
    }
    std::optional<std::string> read() override {
        std::optional<std::string> out = next_read_string;
        if (next_read_string.has_value()){
            next_read_string = "";
        }
        return out;
    }
    PortStatus next_write_status = PortStatus::written;
    std::optional<std::string> next_read_string = "";
    std::vector<std::string> bytes_attempted;
};