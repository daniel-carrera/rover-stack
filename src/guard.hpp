#pragma once
#include <cstddef>
#include <vector>
#include <string>
#include <chrono>
#include <optional>

#include "src/clock.hpp"

// The handler builds the command, stamps it, calls admit for the verdict, answers the request
// if the command is accepted, it the command is added to the loop's slot under a lock
// each tick, the loop takes whatever is in the slot, could be nothing, and the time now
// the guard processes tick and does the second "admit" itself and returns a TickOutput: command to write, 
// if its a repeat command (for logging), if its a lapse and how many stops to send if any
// the loop then builds the frame based on the TickOutput and sends that to the port.
// That explains why tick requires a now and an optional command

struct Command{
    double x;
    double y;
    string press_id;
    bool is_stop;
};

// command is a full copy, string included, can be optimized for later
// repeat_command tells the loop whether the frame is new or a re-send
// Loop writes the same frame anyways, but this is information for the log, not the port
// stops_to_send is 0 while driving, 3 at the lapse and repeat burst, 1 on every other latched tick 
struct TickOutput{
    Command command;
    bool repeat_command;
    std::uint8_t stops_to_send;
    bool lapse_found;
};

class Guard {
public:
    Guard(
        std::chrono::steady_clock::duration lease,
        std::chrono::steady_clock::duration tick_period,
        Clock& clock
    ): lease_(lease), tick_(tick), clock_(clock){}

    // returns true (command accepted) or false (command rejected)
    bool admit(Command command, std::chrono::steady_clock now);
    
    // 
    TickOutput tick(std::optional<Command> command, std::chrono::steady_clock now);

private: 
    const unsigned int STOP_BURST = 3;
    const unsigned int BURST_REPEAT = 1;

    std::chrono::steady_clock::duration lease_;
    std::chrono::steady_clock::duration tick_period_;
    Clock& clock_;

    Command current_command;
    std::chrono::time_point command_time_stamp;
    std::vector<string> lapsed_press_ids;
    bool latched;

    double center_band = 0.1;
    double max_stick_value = 1;
    double min_stick_value = -1;

};