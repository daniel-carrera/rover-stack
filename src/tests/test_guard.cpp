#include <iostream>
#include <chrono>
#include <string>
#include <cstddef>
#include <cmath>

#include "src/clock.hpp"
#include "src/guard.hpp"
#include "src/tests/kit.hpp"


bool check_double_equal(const double test, const double correct){
    double abs_diff = std::abs(test - correct);
    bool same = abs_diff < 1e-9;
    CHECK_EQ(same, true);
    if (!same){
        std::printf("Doubles are not equal: %.17g doesn't match %.17g by %.17g\n", test, correct, abs_diff);
        return false;
    }
    return true;
}


void compare_command(Command test, Command expected){
    if (!check_double_equal(test.x, expected.x)){
        std::cout << "Invalid x value found for Command\n";
    }
    if (!check_double_equal(test.y, expected.y)){
        std::cout << "Invalid y value found for Command\n";
    }
    CHECK_EQ(test.press_id, expected.press_id);
    CHECK_EQ(test.is_stop, expected.is_stop);
    return;
}

void compare_tick_output(TickOutput test, TickOutput expected){
    compare_command(test.command, expected.command);
    CHECK_EQ(test.repeat_command, expected.repeat_command);
    CHECK_EQ(test.stops_to_send, expected.stops_to_send);
    CHECK_EQ(test.lapse_found, expected.lapse_found);
    return;
}

// 1. Healthy command every 50ms for 10s, never a stop
void test_healthy_command(Duration lease, Duration tick_period){
    Command command {
        .x = 0.05,
        .y = 0.05,
        .press_id = "press_id_0",
        .is_stop = false
    };

    TickOutput expected_tick_output {
        .command = {
            .x = 0.05,
            .y = 0.05,
            .press_id = "press_id_0",
            .is_stop = false
        },
        .repeat_command = false,
        .stops_to_send = 0,
        .lapse_found = false
    };


    Guard guard(lease, tick_period);

    Time curr_time = 0;
    while (curr_time < 10000){

        if (curr_time == 50){
            command.x = 0.5;
            command.y = 0.5;
            expected_tick_output.command.x = 0.5;
            expected_tick_output.command.y = 0.5;
        }
        expected_tick_output.repeat_command = false;

        // fix admit
        bool accepted = guard.admit(command, curr_time);
        CHECK_EQ(accepted, true);

        TickOutput tick_output = guard.tick(command, curr_time);
        compare_tick_output(tick_output, expected_tick_output);
        curr_time += 25;

        expected_tick_output.repeat_command = true;
        tick_output = guard.tick(std::nullopt, curr_time);
        compare_tick_output(tick_output, expected_tick_output);
        curr_time += 25;
    }

    return;
}


int main(){
    test_healthy_command(300, 25);
    return kit::finish();
}