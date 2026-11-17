#include <iostream>
#include <chrono>
#include <string>
#include <cstddef>
#include <cmath>

#include "src/clock.hpp"
#include "src/guard.hpp"
#include "src/tests/check.hpp"


bool check_double_equal(const double test, const double correct) {
    double abs_diff = std::abs(test - correct);
    bool same = abs_diff < 1e-9;
    CHECK_EQ(same, true);
    if (!same){
        std::printf("Doubles are not equal: %.17g doesn't match %.17g by %.17g\n", test, correct, abs_diff);
        return false;
    }
    return true;
}

void compare_command(Command test, Command expected) {
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

void compare_tick_output(TickOutput test, TickOutput expected) {
    compare_command(test.command, expected.command);
    CHECK_EQ(static_cast<int>(test.kind), static_cast<int>(expected.kind));
    return;
}


// 1. Healthy command every 50ms for 10s, never a stop 
void test_healthy_command(Duration lease, Duration tick_period){
    Command command {
        .x = 0,
        .y = 0,
        .press_id = "press_id_0",
        .is_stop = false
    };

    TickOutput expected_tick_output {
        .command = {
            .x = 0,
            .y = 0,
            .press_id = "press_id_0",
            .is_stop = false
        },
        .kind = TickOutputType::regular_command 
    }; 

    Guard guard(lease, tick_period);

    Time start_time = Time{};
    Time curr_time = Time{};
    while (curr_time - start_time < std::chrono::milliseconds{10000}){

        if (curr_time - start_time >= std::chrono::milliseconds{50}){
            command.x = 0.5;
            command.y = 0.5;
            expected_tick_output.command.x = 0.5;
            expected_tick_output.command.y = 0.5;
            expected_tick_output.kind = TickOutputType::regular_command;
        }

        Verdict admit_verdict = guard.admit(command, curr_time);
        CHECK_EQ(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted));

        TickOutput tick_output = guard.tick(command, curr_time);
        compare_tick_output(tick_output, expected_tick_output);
        curr_time += std::chrono::milliseconds{25};

        expected_tick_output.kind = TickOutputType::repeat_command;
        tick_output = guard.tick(std::nullopt, curr_time);
        compare_tick_output(tick_output, expected_tick_output);
        curr_time += std::chrono::milliseconds{25};
    }

    return;
}

// 2. Silence: last command at t
void test_silence(Duration lease, Duration tick_period){
    // when the tick processes a command and it produces a stop
    // the returned command in TickOutput will carry the press id 
    // of the last command it saw
    Command command {
        .x = 0,
        .y = 0,
        .press_id = "press_id_0",
        .is_stop = false
    };

    Guard guard(lease, tick_period);

    Time start_time = Time{};
    Time curr_time = Time{};

    // first command is sent at time = 0, passed into the loop
    // we tick, after x ticks we hit lapse_time at that point the lease will lapse
    // we should do another round of lapse_time, for another x ticks, and at the last tick, we should get a burst
    // then we exit the loop

    // The lease expires when we are at or over the lease time
    Duration lapse_time = lease;
    if (lease % tick_period != tick_period.zero()){
        Duration remainder = lease % tick_period;
        lapse_time = lapse_time + (tick_period - remainder);
    }
    Duration lapse_time_twice = lapse_time * 2;

    // stop when we are over the lapse time * 2
    while (curr_time - start_time <= lapse_time_twice){
        Duration time_diff = curr_time - start_time;
        
        // we only ever recieve one command at t = 0 so we only admit it once in the loop
        if (time_diff == std::chrono::milliseconds{0}){
            TickOutput regular_tick_output {
                .command = {
                    .x = 0,
                    .y = 0,
                    .press_id = "press_id_0",
                    .is_stop = false
                },
                .kind = TickOutputType::regular_command 
            }; 

            Verdict admit_verdict = guard.admit(command, curr_time);
            CHECK_EQ(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted));

            TickOutput tick_output = guard.tick(command, curr_time);
            compare_tick_output(tick_output, regular_tick_output); 
            
            curr_time += tick_period;
            continue;
        }

        if (time_diff > std::chrono::milliseconds{0} && time_diff < lapse_time) {
            TickOutput repeat_tick_output {
                .command = {
                    .x = 0,
                    .y = 0,
                    .press_id = "press_id_0",
                    .is_stop = false
                },
                .kind = TickOutputType::repeat_command 
            }; 
            TickOutput tick_output = guard.tick(std::nullopt, curr_time);
            compare_tick_output(tick_output, repeat_tick_output);
            curr_time += tick_period;
            continue;
        }
        
        // if time is lapsed, check expected burst
        if (time_diff == lapse_time){
            TickOutput lapse_tick_output {
                .command = {
                    .x = 0,
                    .y = 0,
                    .press_id = "press_id_0",
                    .is_stop = true
                },
                .kind = TickOutputType::lapse 
            }; 
            TickOutput tick_output = guard.tick(std::nullopt, curr_time);
            compare_tick_output(tick_output, lapse_tick_output);
            curr_time += tick_period;
            continue;
        }
        
        // if time is after lapsed, check expected latched stop
        if (time_diff > lapse_time && time_diff < lapse_time_twice) {
            TickOutput latched_stop_tick_output {
                .command = {
                    .x = 0,
                    .y = 0,
                    .press_id = "press_id_0",
                    .is_stop = true
                },
                .kind = TickOutputType::latched_stop     
            };
            TickOutput tick_output = guard.tick(std::nullopt, curr_time);
            compare_tick_output(tick_output, latched_stop_tick_output);
            curr_time += tick_period;
            continue;
        }

        // if time is lapsed 2x, check expected burst
        if (time_diff == lapse_time_twice){
            TickOutput latched_burst_tick_output {
                .command = {
                    .x = 0,
                    .y = 0,
                    .press_id = "press_id_0",
                    .is_stop = true
                },
                .kind = TickOutputType::latched_burst     
            };
            TickOutput tick_output = guard.tick(std::nullopt, curr_time);
            compare_tick_output(tick_output, latched_burst_tick_output);
            curr_time += tick_period;
            continue;
        }
    }
}

// 3. Loss tolerance
// 4 consecutive missing posts survive, 5 trip
// Posts every 50ms, lease is 300ms, tick period is 25ms
void test_loss_tolerance(){
    Command command {
        .x = 0,
        .y = 0,
        .press_id = "press_id_0",
        .is_stop = false
    };
    Guard guard(std::chrono::milliseconds{300}, std::chrono::milliseconds{25});

    Time start_time = Time{};
    Time curr_time = Time{};

    // Trip run: arrival at 0 is accepted
    // ticks 25 -> 275 repeat
    // arrival at 300, refused stale, dropped
    // tick at 300 nothing, lapse triggered
    // tick at 325 latched stop
    // end
    while (curr_time - start_time <= std::chrono::milliseconds{350}){
        Duration time_diff = curr_time - start_time;

        if (time_diff > std::chrono::milliseconds{300}){
            TickOutput latched_stop_tick_output = {
                .command = {
                    .x = 0,
                    .y = 0,
                    .press_id = "press_id_0",
                    .is_stop = true
                },
                .kind = TickOutputType::latched_stop
            };
            Verdict admit_verdict = guard.admit(command, curr_time);
            CHECK_EQ(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_stale));
            TickOutput tick_output = guard.tick(std::nullopt, curr_time);
            compare_tick_output(tick_output, latched_stop_tick_output);
            curr_time += std::chrono::milliseconds{25};
            continue;
        }

        if (time_diff == std::chrono::milliseconds{300}){
            TickOutput refused_tick_output = {
                .command = {
                    .x = 0,
                    .y = 0,
                    .press_id = "press_id_0",
                    .is_stop = true
                },
                .kind = TickOutputType::lapse
            };
            Verdict admit_verdict = guard.admit(command, curr_time);
            CHECK_EQ(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_stale));
            TickOutput tick_output = guard.tick(std::nullopt, curr_time);
            compare_tick_output(tick_output, refused_tick_output);
            curr_time += std::chrono::milliseconds{25};
            continue;
        }
        if (time_diff == std::chrono::milliseconds{0}){
            TickOutput regular_tick_output = {
                .command = {
                    .x = 0,
                    .y = 0,
                    .press_id = "press_id_0",
                    .is_stop = false
                },
                .kind = TickOutputType::regular_command
            };
            Verdict admit_verdict = guard.admit(command, curr_time);
            CHECK_EQ(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted));
            TickOutput tick_output = guard.tick(command, curr_time);
            compare_tick_output(tick_output, regular_tick_output);
            curr_time += std::chrono::milliseconds{25};
            continue;
        }
        TickOutput repeat_tick_output = {
            .command = {
                .x = 0,
                .y = 0,
                .press_id = "press_id_0",
                .is_stop = false
            },
            .kind = TickOutputType::repeat_command
        };
        // 1 tick without an arrival
        TickOutput tick_output = guard.tick(std::nullopt, curr_time);
        compare_tick_output(tick_output, repeat_tick_output);
        curr_time += std::chrono::milliseconds{25};
        continue;
    }


    // Survive run
    // arrival at 0 accepted, ticks 25 -> 225 repeat command
    // arrival at 250 accepted, new frame
    // ticks 275 -> 300 repeat, last repeat at 300 accepted repeat, end.
    while (curr_time - start_time <= std::chrono::milliseconds{300}){
        Duration time_diff = curr_time - start_time;

        if (time_diff == std::chrono::milliseconds{0} || time_diff == std::chrono::milliseconds{250}){
            TickOutput regular_tick_output = {
                .command = {
                    .x = 0,
                    .y = 0,
                    .press_id = "press_id_0",
                    .is_stop = false
                },
                .kind = TickOutputType::regular_command
            };
            Verdict admit_verdict = guard.admit(command, curr_time);
            CHECK_EQ(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted));
            TickOutput tick_output = guard.tick(command, curr_time);
            compare_tick_output(tick_output, regular_tick_output);
            curr_time += std::chrono::milliseconds{25};
            continue;
        }
        TickOutput repeat_tick_output = {
            .command = {
                .x = 0,
                .y = 0,
                .press_id = "press_id_0",
                .is_stop = false
            },
            .kind = TickOutputType::repeat_command
        };
        // 1 tick of the same repeated command
        TickOutput tick_output = guard.tick(std::nullopt, curr_time);
        compare_tick_output(tick_output, repeat_tick_output);
        curr_time += std::chrono::milliseconds{25};
        continue;
    }


}

// 4. arrives 0.3 s or more after the last accepted; refused stale; the stops go out.
// Lease is 300ms, posts every 50ms, tick period is 25ms
void test_late_command(){
    Command command {
        .x = 0,
        .y = 0,
        .press_id = "press_id_0",
        .is_stop = false
    };
    Guard guard(std::chrono::milliseconds{300}, std::chrono::milliseconds{25});
    Time start_time = Time{};
    Time curr_time = Time{};

    // Last accepted command at t = 0
    // no commands arrive until t = 400, past the lease of 300.
    // the 400 arrival has same press_id as t = 0, it is stale and dropped
    // past 400, latched_stop continues
    while (curr_time - start_time <= std::chrono::milliseconds{500}){
        Duration time_diff = curr_time - start_time;
        
        if (time_diff > std::chrono::milliseconds{300}){
            TickOutput latched_stop_tick_output = {
                .command = {
                    .x = 0,
                    .y = 0,
                    .press_id = "press_id_0",
                    .is_stop = true
                },
                .kind = TickOutputType::latched_stop
            };
            if (time_diff == std::chrono::milliseconds{400}){
                Verdict admit_verdict = guard.admit(command, curr_time);
                CHECK_EQ(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_stale));
            }
            TickOutput tick_output = guard.tick(std::nullopt, curr_time);
            compare_tick_output(tick_output, latched_stop_tick_output);
            curr_time += std::chrono::milliseconds{25};
            continue;
        }

        if (time_diff == std::chrono::milliseconds{300}){
            TickOutput lapsed_tick_output = {
                .command = {
                    .x = 0,
                    .y = 0,
                    .press_id = "press_id_0",
                    .is_stop = true
                },
                .kind = TickOutputType::lapse
            };
            TickOutput tick_output = guard.tick(std::nullopt, curr_time);
            compare_tick_output(tick_output, lapsed_tick_output);
            curr_time += std::chrono::milliseconds{25};
            continue;
        }

        if (time_diff == std::chrono::milliseconds{0}){
            TickOutput regular_tick_output = {
                .command = {
                    .x = 0,
                    .y = 0,
                    .press_id = "press_id_0",
                    .is_stop = false
                },
                .kind = TickOutputType::regular_command
            };
            Verdict admit_verdict = guard.admit(command, curr_time);
            CHECK_EQ(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted));
            TickOutput tick_output = guard.tick(command, curr_time);
            compare_tick_output(tick_output, regular_tick_output);
            curr_time += std::chrono::milliseconds{25};
            continue;
        }

        TickOutput repeat_tick_output = {
            .command = {
                .x = 0,
                .y = 0,
                .press_id = "press_id_0",
                .is_stop = false
            },
            .kind = TickOutputType::repeat_command
        };
        // 1 tick without an arrival
        TickOutput tick_output = guard.tick(std::nullopt, curr_time);
        compare_tick_output(tick_output, repeat_tick_output);
        curr_time += std::chrono::milliseconds{25};
        continue;
    }
}

int main(){
    test_healthy_command(std::chrono::milliseconds{300}, std::chrono::milliseconds{25});
    return kit::finish();
}