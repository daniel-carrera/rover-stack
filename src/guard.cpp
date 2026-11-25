#include "src/clock.hpp"
#include "src/guard.hpp"

void Guard::end_press(Time now){
    latched_ = true;
    last_burst_time_stamp_ = now;
    current_command_.x = 0.0;
    current_command_.y = 0.0;
    current_command_.is_stop = true;
    if (current_command_.press_id.has_value()){
        spent_press_ids_.insert(*current_command_.press_id);
    }
}

Verdict Guard::admit(Command command, Time now) const {
    // explicit stop
    // The sticks on the stop never reach the output. 
    // end_press zeroes them, so NaN in a stop’s x cannot leak into a frame
    if (!command.press_id->empty() && command.is_stop){
        return Verdict::accepted;
    }
        
    // stick check: valid press_id, numbers in range?
    if (!command.press_id.has_value() || command.press_id->empty() || !std::isfinite(command.x) || !std::isfinite(command.y)) {
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

    // check ordering: refuse if start is lower or the start is equal and counter is not strictly higher
    if (
        command.sender_start < sender_start_ || 
        (command.sender_start == sender_start_ && 
        command.sender_counter <= sender_counter_)
    ) {
        return Verdict::refused_out_of_order;
    }

    // stale check
    if ((*command.press_id) == (*current_command_.press_id) && now - command_time_stamp_ >= lease_){
        return Verdict::refused_stale;
    // spent press id
    } else if (spent_press_ids_.contains((*command.press_id))){
        return Verdict::refused_spent_press;
    // latched and not centered
    // any press the guard ever accepted is caught before here, 
    // stale or spent after a lapse, spent after any stop, its own or another page's, 
    // so a command that reaches the latch is from a press the guard has never driven on
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
        // an accepted command from a new press ends the live one, 
        // so spend the live id before it is replaced
        command_time_stamp_ = now;
        if (current_command_.press_id.has_value() && command->press_id != current_command_.press_id){
            spent_press_ids_.insert(*current_command_.press_id);
        }
        current_command_ = *command;
        if ((*command).is_stop){
            end_press(now);
            tick_output.kind = TickOutputType::stop_command;
        } else {
            latched_ = false;
            sender_start_ = current_command_.sender_start;
            sender_counter_ = current_command_.sender_counter;
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
