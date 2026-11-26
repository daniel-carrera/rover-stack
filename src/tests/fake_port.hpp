#pragma once

#include "src/port.hpp"

class FakePort : public Port {
public:
    PortStatus write(std::string_view) override {
        return PortStatus::written;
    }
    std::optional<std::string> read() override {
        return std::string{};
    }
};