#include <iostream>
#include <chrono>
#include <string>
#include <cstddef>
#include <cmath>

#include "src/clock.hpp"
#include "src/guard.hpp"
#include "src/tests/check.hpp"


bool check_double_equal(const double test, const double correct, const std::string& label) {
    double abs_diff = std::abs(test - correct);
    bool same = abs_diff < 1e-9;
    CHECK_EQ_L(same, true, label);
    if (!same){
        std::printf("Doubles are not equal: %.17g doesn't match %.17g by %.17g\n", test, correct, abs_diff);
        return false;
    }
    return true;
}

void compare_command(Command test, Command expected, const std::string& label) {
    if (!check_double_equal(test.x, expected.x, label)){
        std::cout << "Invalid x value found for Command\n";
    }
    if (!check_double_equal(test.y, expected.y, label)){
        std::cout << "Invalid y value found for Command\n";
    }
    CHECK_EQ_L(test.press_id, expected.press_id, label);
    CHECK_EQ_L(test.is_stop, expected.is_stop, label);
    return;
}

void compare_tick_output(TickOutput test, TickOutput expected, const std::string& label) {
    compare_command(test.command, expected.command, label);
    CHECK_EQ_L(static_cast<int>(test.kind), static_cast<int>(expected.kind), label);
    return;
}

// Taking the time point, the start, and the reason, returning the string
std::string at_ms(Time now, Time start, const std::string& what){
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    return "tick " + std::to_string(ms) + " ms. " + what;
}

// 1. Healthy command every 50ms for 10s, never a stop 
// tick period is 25ms
void test_healthy_command(Duration lease){
    kit::current_test = __func__;

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

    Guard guard(lease);

    Time start_time = Time{};
    Time current_time = Time{};
    while (current_time - start_time < std::chrono::milliseconds{10000}){

        if (current_time - start_time >= std::chrono::milliseconds{50}){
            command.x = 0.5;
            command.y = 0.5;
            expected_tick_output.command.x = 0.5;
            expected_tick_output.command.y = 0.5;
            expected_tick_output.kind = TickOutputType::regular_command;
        }

        Verdict admit_verdict = guard.admit(command, current_time);
        CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "Command waiting."));

        TickOutput tick_output = guard.tick(command, current_time);
        compare_tick_output(tick_output, expected_tick_output, at_ms(current_time, start_time, "Command waiting."));
        current_time += std::chrono::milliseconds{25};

        expected_tick_output.kind = TickOutputType::repeat_command;
        tick_output = guard.tick(std::nullopt, current_time);
        compare_tick_output(tick_output, expected_tick_output, at_ms(current_time, start_time, "Command repeat."));
        current_time += std::chrono::milliseconds{25};
    }

    return;
}

// 2. Silence: last command at t
void test_silence(Duration lease, Duration tick_period){
    kit::current_test = __func__;

    // when the tick processes a command and it produces a stop
    // the returned command in TickOutput will carry the press id 
    // of the last command it saw
    Command command {
        .x = 0,
        .y = 0,
        .press_id = "press_id_0",
        .is_stop = false
    };

    Guard guard(lease);

    Time start_time = Time{};
    Time current_time = Time{};

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
    while (current_time - start_time <= lapse_time_twice){
        Duration time_diff = current_time - start_time;
        
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

            Verdict admit_verdict = guard.admit(command, current_time);
            CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "First Command waiting"));

            TickOutput tick_output = guard.tick(command, current_time);
            compare_tick_output(tick_output, regular_tick_output, at_ms(current_time, start_time, "First Command waiting")); 
            
            current_time += tick_period;
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
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, repeat_tick_output, at_ms(current_time, start_time, "Repeat Command"));
            current_time += tick_period;
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
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, lapse_tick_output, at_ms(current_time, start_time, "Lapsed Command"));
            current_time += tick_period;
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
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, latched_stop_tick_output, at_ms(current_time, start_time, "Latched Stop Command"));
            current_time += tick_period;
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
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, latched_burst_tick_output, at_ms(current_time, start_time, "Latched Burst Command"));
            current_time += tick_period;
            continue;
        }
    }
}

// 3. Loss tolerance
// 4 consecutive missing posts survive, 5 trip
// Posts every 50ms, lease is 300ms, tick period is 25ms
void test_loss_tolerance(){
    kit::current_test = __func__;

    Command command {
        .x = 0,
        .y = 0,
        .press_id = "press_id_0",
        .is_stop = false
    };
    Guard guard(std::chrono::milliseconds{300});

    Time start_time = Time{};
    Time current_time = Time{};

    // Trip run: arrival at 0 is accepted
    // ticks 25 -> 275 repeat
    // arrival at 300, refused stale, dropped
    // tick at 300 nothing, lapse triggered
    // tick at 325 latched stop
    // end
    while (current_time - start_time <= std::chrono::milliseconds{350}){
        Duration time_diff = current_time - start_time;

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
            Verdict admit_verdict = guard.admit(command, current_time);
            CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_stale), at_ms(current_time, start_time, "Latched Stop Command"));
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, latched_stop_tick_output, "Latched Stop Command");
            current_time += std::chrono::milliseconds{25};
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
            Verdict admit_verdict = guard.admit(command, current_time);
            CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_stale), at_ms(current_time, start_time, "Lapsed Command"));
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, refused_tick_output, "Lapsed Command");
            current_time += std::chrono::milliseconds{25};
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
            Verdict admit_verdict = guard.admit(command, current_time);
            CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "First Command"));
            TickOutput tick_output = guard.tick(command, current_time);
            compare_tick_output(tick_output, regular_tick_output, "First Command");
            current_time += std::chrono::milliseconds{25};
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
        TickOutput tick_output = guard.tick(std::nullopt, current_time);
        compare_tick_output(tick_output, repeat_tick_output, "Repeat Command");
        current_time += std::chrono::milliseconds{25};
        continue;
    }


    // Survive run
    // arrival at 0 accepted, ticks 25 -> 225 repeat command
    // arrival at 250 accepted, new frame
    // ticks 275 -> 300 repeat, last repeat at 300 accepted repeat, end.
    while (current_time - start_time <= std::chrono::milliseconds{300}){
        Duration time_diff = current_time - start_time;

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
            Verdict admit_verdict = guard.admit(command, current_time);
            CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "Command Waiting"));
            TickOutput tick_output = guard.tick(command, current_time);
            compare_tick_output(tick_output, regular_tick_output, "Command Waiting");
            current_time += std::chrono::milliseconds{25};
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
        TickOutput tick_output = guard.tick(std::nullopt, current_time);
        compare_tick_output(tick_output, repeat_tick_output, "Repeat Command");
        current_time += std::chrono::milliseconds{25};
        continue;
    }
}

// 4. arrives 0.3 s or more after the last accepted; refused stale; the stops go out.
// Lease is 300ms, posts every 50ms, tick period is 25ms
void test_late_command(){
    kit::current_test = __func__;

    Command command {
        .x = 0,
        .y = 0,
        .press_id = "press_id_0",
        .is_stop = false
    };
    Guard guard(std::chrono::milliseconds{300});
    Time start_time = Time{};
    Time current_time = Time{};

    // Last accepted command at t = 0
    // no commands arrive until t = 400, past the lease of 300.
    // the 400 arrival has same press_id as t = 0, it is stale and dropped
    // past 400, latched_stop continues
    while (current_time - start_time <= std::chrono::milliseconds{500}){
        Duration time_diff = current_time - start_time;
        
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
                Verdict admit_verdict = guard.admit(command, current_time);
                CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_stale), at_ms(current_time, start_time, "Stale Command Waiting"));
            }
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, latched_stop_tick_output, at_ms(current_time, start_time, "Latched Stop"));
            current_time += std::chrono::milliseconds{25};
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
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, lapsed_tick_output, at_ms(current_time, start_time, "Lapsed Command"));
            current_time += std::chrono::milliseconds{25};
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
            Verdict admit_verdict = guard.admit(command, current_time);
            CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "First Waiting Command"));
            TickOutput tick_output = guard.tick(command, current_time);
            compare_tick_output(tick_output, regular_tick_output, "First Waiting Command");
            current_time += std::chrono::milliseconds{25};
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
        TickOutput tick_output = guard.tick(std::nullopt, current_time);
        compare_tick_output(tick_output, repeat_tick_output, "Repeat Command");
        current_time += std::chrono::milliseconds{25};
        continue;
    }
}

// 5. Handler admits a command before lease expires, but when tick recieves command, lease expired in the gap
// lease is 300ms, posts every 50ms, tick period 25ms
// first command lands t=0, 
// second command lands t=299ms, but reaches the tick by 300ms when the lease lapses
// then sending the same second command at t=325, confirming we get a latched stop
void test_handler_tick_race(){
    kit::current_test = __func__;
    
    TickOutput tick_output;
    Guard guard(std::chrono::milliseconds{300});
    Command first_command = {
        .x = 0.0,
        .y = 0.0,
        .press_id = "press_id_0",
        .is_stop = false
    };
    Command second_command = {
        .x = 0.5,
        .y = 0.5,
        .press_id = "press_id_0",
        .is_stop = false
    };
    TickOutput regular_tick_output = {
        .command = {
            .x = 0.0,
            .y = 0.0,
            .press_id = "press_id_0",
            .is_stop = false
        },
        .kind = TickOutputType::regular_command
    };
    TickOutput lapsed_tick_output = {
        .command = {
            .x = 0.0,
            .y = 0.0,
            .press_id = "press_id_0",
            .is_stop = true
        },
        .kind = TickOutputType::lapse
    };
    TickOutput latched_stop_tick_output = {
        .command = {
            .x = 0.0,
            .y = 0.0,
            .press_id = "press_id_0",
            .is_stop = true
        },
        .kind = TickOutputType::latched_stop
    };

    Time current_time {};
    Time start_time {};
    Verdict admit_verdict = guard.admit(first_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "First Waiting Command"));
    tick_output = guard.tick(first_command, current_time);
    compare_tick_output(tick_output, regular_tick_output, at_ms(current_time, start_time, "First Waiting Command"));

    current_time += std::chrono::milliseconds{299};
    admit_verdict = guard.admit(second_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted),  at_ms(current_time, start_time, "Second Waiting Command"));

    current_time += std::chrono::milliseconds{1};
    tick_output = guard.tick(second_command, current_time);
    compare_tick_output(tick_output, lapsed_tick_output, at_ms(current_time, start_time, "Lapsed Waiting Command"));

    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(second_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_stale), "Lapsed Waiting Command");
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, latched_stop_tick_output, at_ms(current_time, start_time, "Latched Stop Command"));

    return;
}

// 6. reset: after a lapse, a centered command lands on a new press id and is admitted, deflected is refused
// t = 0 command sent, t = 300 second command sent, lapsed
// t = 325 second command sent again latched stop 
// t = 350 centered command lands admitted
// second branch sends a non-centered command at t=350 refused latched stop
void test_reset_after_lapse(){
    kit::current_test = __func__;

    TickOutput tick_output;
    Guard guard(std::chrono::milliseconds{300});
    Command first_command = {
        .x = 0.0,
        .y = 0.0,
        .press_id = "press_id_0",
        .is_stop = false
    };
    Command second_command = {
        .x = 0.5,
        .y = 0.5,
        .press_id = "press_id_0",
        .is_stop = false
    };
    Command third_command = {
        .x = 0.0,
        .y = 0.0,
        .press_id = "press_id_1",
        .is_stop = false
    };
    Command fourth_command = {
        .x = 0.5,
        .y = 0.0,
        .press_id = "press_id_1",
        .is_stop = false
    };
    TickOutput regular_tick_output = {
        .command = first_command,
        .kind = TickOutputType::regular_command
    };
    TickOutput lapsed_tick_output = {
        .command = {
            .x = 0.0,
            .y = 0.0,
            .press_id = "press_id_0",
            .is_stop = true
        },
        .kind = TickOutputType::lapse
    };
    TickOutput latched_tick_output = {
        .command = {
            .x = 0.0,
            .y = 0.0,
            .press_id = "press_id_0",
            .is_stop = true
        },
        .kind = TickOutputType::latched_stop
    };
    TickOutput reset_tick_output = {
        .command = {
            .x = 0.0,
            .y = 0.0,
            .press_id = "press_id_1",
            .is_stop = false
        },
        .kind = TickOutputType::regular_command
    };

    // first branch, regular, stale, then reset accepted
    Time first_current_time{};
    Time first_start_time{};

    Verdict guard_verdict = guard.admit(first_command, first_current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::accepted), at_ms(first_current_time, first_start_time, "First Waiting Command"));
    tick_output = guard.tick(first_command, first_current_time);
    compare_tick_output(tick_output, regular_tick_output, at_ms(first_current_time, first_start_time, "First Waiting Command"));

    first_current_time += std::chrono::milliseconds{300};
    guard_verdict = guard.admit(second_command, first_current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_stale), at_ms(first_current_time, first_start_time, "Lapsed Command"));
    tick_output = guard.tick(std::nullopt, first_current_time);
    compare_tick_output(tick_output, lapsed_tick_output, at_ms(first_current_time, first_start_time, "Lapsed Command"));

    first_current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(third_command, first_current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::accepted), at_ms(first_current_time, first_start_time, "Stick Reset Waiting Command"));
    tick_output = guard.tick(third_command, first_current_time);
    compare_tick_output(tick_output, reset_tick_output, at_ms(first_current_time, first_start_time, "Stick Reset Waiting Command"));

    
    // second branch, regular, stale, then reset refused
    Time second_current_time{};
    Time second_start_time{};

    Guard second_guard(std::chrono::milliseconds{300});
    guard_verdict = second_guard.admit(first_command, second_current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::accepted), at_ms(second_current_time, second_start_time, "First Waiting Command"));
    tick_output = second_guard.tick(first_command, second_current_time);
    compare_tick_output(tick_output, regular_tick_output, at_ms(second_current_time, second_start_time, "First Waiting Command"));

    second_current_time += std::chrono::milliseconds{300};
    guard_verdict = second_guard.admit(second_command, second_current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_stale), at_ms(second_current_time, second_start_time, "Lapsed Waiting Command"));
    tick_output = second_guard.tick(std::nullopt, second_current_time);
    compare_tick_output(tick_output, lapsed_tick_output, at_ms(second_current_time, second_start_time, "Lapsed Command"));

    second_current_time += std::chrono::milliseconds{25};
    guard_verdict = second_guard.admit(fourth_command, second_current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_not_centered), at_ms(second_current_time, second_start_time, "Stick Not Centered Waiting Command"));
    tick_output = second_guard.tick(std::nullopt, second_current_time);
    compare_tick_output(tick_output, latched_tick_output, at_ms(second_current_time, second_start_time, "Latched Stop Command"));

    return;
}

// 7. The hole: after a lapse, a late burst from the old press that starts centred then deflects must not drive. 
// The press id is what makes this pass.
void test_the_hole(){
    TickOutput tick_output;
    Guard guard(std::chrono::milliseconds{300});
    Command first_command = {
        .x = 0.0,
        .y = 0.0,
        .press_id = "press_id_0",
        .is_stop = false
    };
    Command second_command = {
        .x = 0.0,
        .y = 0.0,
        .press_id = "press_id_0",
        .is_stop = false
    };
    Command third_command = {
        .x = 0.5,
        .y = 0.0,
        .press_id = "press_id_0",
        .is_stop = false
    };
    Command fourth_command = {
        .x = 0.0,
        .y = 0.0,
        .press_id = "press_id_1",
        .is_stop = false
    };
    TickOutput regular_tick_output = {
        .command = first_command,
        .kind = TickOutputType::regular_command
    };
    TickOutput lapsed_tick_output = {
        .command = {
            .x = 0.0,
            .y = 0.0,
            .press_id = "press_id_0",
            .is_stop = true
        },
        .kind = TickOutputType::lapse
    };
    TickOutput latched_tick_output = {
        .command = {
            .x = 0.0,
            .y = 0.0,
            .press_id = "press_id_0",
            .is_stop = true
        },
        .kind = TickOutputType::latched_stop
    };
    TickOutput reset_tick_output = {
        .command = {
            .x = 0.0,
            .y = 0.0,
            .press_id = "press_id_1",
            .is_stop = false
        },
        .kind = TickOutputType::regular_command
    };
    TickOutput repeat_tick_output = {
        .command = {
            .x = 0.0,
            .y = 0.0,
            .press_id = "press_id_1",
            .is_stop = false
        },
        .kind = TickOutputType::repeat_command
    };

    Time current_time{};
    Time start_time{};

    Verdict guard_verdict = guard.admit(first_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "First Waiting Command"));
    tick_output = guard.tick(first_command, current_time);
    compare_tick_output(tick_output, regular_tick_output, at_ms(current_time, start_time, "First Waiting Command"));

    current_time += std::chrono::milliseconds{300};
    // no admit since no command arrived at 300ms
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, lapsed_tick_output, at_ms(current_time, start_time, "Lapsed Command"));

    current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(second_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_stale), at_ms(current_time, start_time, "Lapsed Waiting Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, latched_tick_output, at_ms(current_time, start_time, "Latched Stop Command"));

    current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(third_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_stale), at_ms(current_time, start_time, "Lapsed Waiting Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, latched_tick_output, at_ms(current_time, start_time, "Latched Stop Command"));

    current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(fourth_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "Stick Reset Waiting Command"));
    tick_output = guard.tick(fourth_command, current_time);
    compare_tick_output(tick_output, reset_tick_output, at_ms(current_time, start_time, "Stick Reset Command"));

    current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(third_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_spent_press), at_ms(current_time, start_time, "Spent Press Waiting Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, repeat_tick_output, at_ms(current_time, start_time, "Repeat Command"));
}

// 8. 
// TODO: An empty press id. At startup the current id is empty, so a post with an empty id reads as “the same press” 
// and can be accepted if centred. The page should never send one, so we refuse it where the bool is refused, in the parser, 
// and note it in test 8’s comment with the bool.


int main(){

    test_healthy_command(std::chrono::milliseconds{300});
    test_silence(std::chrono::milliseconds{300}, std::chrono::milliseconds{25});
    test_loss_tolerance();
    test_late_command();
    test_handler_tick_race();
    test_reset_after_lapse();
    test_the_hole();

    return kit::finish();
}
