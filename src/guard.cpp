#include "src/clock.hpp"
#include "src/guard.hpp"


void Guard::end_press(Time now){
    latched_ = true;
    last_burst_time_stamp_ = now;
    current_command_.x = 0.0;
    current_command_.y = 0.0;
    current_command_.is_stop = true;
    spent_press_ids_.insert(current_command_.press_id);
}

Verdict Guard::admit(Command command, Time now) const {
    // explicit stop
    if (command.is_stop){
        return Verdict::accepted;
    }
        
    // stick check: valid numbers, in range?
    if (!std::isfinite(command.x) || !std::isfinite(command.y)) {
        return Verdict::refused_range;
    }
    if (
        command.x > max_stick_value_ || 
        command.x < min_stick_value_ ||
        command.y > max_stick_value_ ||
        command.y < min_stick_value_
    ){
        return Verdict::refused_range;
    }

    // stale check
    if (command.press_id == current_command_.press_id && now - command_time_stamp_ >= lease_){
        return Verdict::refused_stale;
    // spent press id
    } else if (spent_press_ids_.contains(command.press_id)){
        return Verdict::refused_spent_press;
    // latched and not centered
    // A same-id command while latched is always caught earlier, stale after a lapse, spent after a stop, 
    // so by the time the latch is consulted the id is new
    } else if (latched_ && (std::abs(command.x) > center_band_ || std::abs(command.y) > center_band_)){
        return Verdict::refused_not_centered;
    // accept
    } else {
        return Verdict::accepted;
    }
}


// tick keeps the ended press’s id on the stop command it makes current, 
// and puts that same id in the spent set. No invented id.
TickOutput Guard::tick(std::optional<Command> command, Time now){
    TickOutput tick_output;

    if (command.has_value() && admit(*command, now) == Verdict::accepted){
        // apply: drive or stop
        command_time_stamp_ = now;
        current_command_ = *command;
        if (current_command_.is_stop){
            end_press(now);
            tick_output.kind = TickOutputType::stop_command;
        } else {
            latched_ = false;
            tick_output.kind = TickOutputType::regular_command;
        }
        tick_output.command = current_command_;
        return tick_output;
    }
    
    // 4 idle branches
    if (latched_){
        if (!last_burst_time_stamp_.has_value() || now - *last_burst_time_stamp_ >= lease_){
            // burst stop
            last_burst_time_stamp_ = now;
            tick_output.kind = TickOutputType::latched_burst;
        } else {
            // latched 
            tick_output.kind = TickOutputType::latched_stop;
        }
    } else if (now - command_time_stamp_ >= lease_){
        // lapse
        end_press(now);
        tick_output.kind = TickOutputType::lapse;
    } else {
        // repeat command
        tick_output.kind = TickOutputType::repeat_command;
    }
    tick_output.command = current_command_;
    return tick_output;
}
