#pragma once

#include <string>
#include <optional>

#include "src/port.hpp"
#include "src/clock.hpp"

enum class DriveStatus {
    frame_written,
    write_timed_out,
    write_error,
    refused
};

enum class FaultReason {
    not_armed_yet,
    feedback_silent,
    bad_speed,
    write_timed_out,
    port_error
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
    ): port_(port) {}
    
    DriveStatus drive(WheelSpeeds wheel_speeds);
    DriveStatus stop();
    std::optional<BoardFeedback> tick(Time now); 
    std::optional<FaultReason> reason() const;
    bool rearm();


private:
    Port& port_;
    std::string unfinished_line_;
    std::optional<FaultReason> reason_ = FaultReason::not_armed_yet;
    std::optional<Time> last_good_feedback_;
    int rearm_count_ = 0;

    static constexpr double max_wheel_speed = 2.0;
    static constexpr int rearm_count_threshold = 20;
    static constexpr Duration feedback_lease = std::chrono::milliseconds{165};
};