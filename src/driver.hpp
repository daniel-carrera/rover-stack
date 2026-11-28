#pragma once

#include <stddef>
#include <optional>

#include "src/port.hpp"
#include "src/clock.hpp"

enum class DriveStatus {
    frame_written,
    write_timed_out,
    refused
};

struct WheelSpeeds {
    double left;
    double right;
};

struct BoardFeedback{
    double L;
    double R;
};

class Driver {
public:
    Driver(
        Port& port
    ): port_(port_) {}
    
    std::optional<BoardFeedback> tick(Time now); 
    DriveStatus drive(WheelSpeeds wheel_speeds);
    DriveStatus stop();
    bool rearm();
    bool is_faulted();

private:
    Port& port_;
    std::string unfinished_line;
    bool faulted_ = true;
    Time last_good_feedback_;
    int rearm_count = 0;
    constexpr Duration max_wait_time_ = std::chrono::milliseconds{165};
};