#include "src/clock.hpp"
#include "src/guard.hpp"


Verdict Guard::admit(Command command, Time now){
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

    // explicit stop
    if (command.is_stop){
        return Verdict::accepted;
    }
    
    // stale check
    if (command.press_id == current_command_.press_id){
        if (now - command_time_stamp_ >= lease_){
            return Verdict::refused_stale;
        } else {
            return Verdict::accepted;
        }
    } else {
        // stray invalid press_id check
        if (lapsed_press_ids_.contains(command.press_id)){
            return Verdict::refused_spent_press;
        // latched and not centered check
        } else if (latched_ && (std::abs(command.x) > center_band_ || std::abs(command.y) > center_band_)){
            return Verdict::refused_not_centered;
        } else {
            return Verdict::accepted;
        }
    }
}


// TickOutput Guard::tick(std::optional<Command> command, Time now){



// }
