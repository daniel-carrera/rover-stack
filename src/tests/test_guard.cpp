#include <iostream>
#include <chrono>
#include <string>
#include <cstddef>
#include <cmath>
#include <limits>

#include "src/clock.hpp"
#include "src/guard.hpp"
#include "src/tests/check.hpp"

constexpr std::uint64_t PAGE_SENDER_START = 1;

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
    // bool same_start = test.sender_start == expected.sender_start;
    // CHECK_EQ_L(same_start, true, label);
    // if (!same_start){
    //     std::cout << "Invalid sender_start value";
    // }
    // bool same_counter = test.sender_counter == expected.sender_counter;
    // CHECK_EQ_L(same_counter, true, label);
    // if (!same_counter) {
    //     std::cout << "Invalid sender_counter value";
    // }
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

// Factory Helpers for the four shapes
// Creates a drive command, is_stop false, x,y,id are passed in
std::uint64_t get_sender_counter(){
    static std::uint64_t counter = 0;
    return ++counter;
}

Command drive(const std::string& id, double x, double y){
    return {
        .x = x,
        .y = y,
        .press_id = id,
        .is_stop = false,
        .sender_start = PAGE_SENDER_START,
        .sender_counter = get_sender_counter()
    };
}
// Creates a stop command, stick zeroes, is_stop is true, same id
Command stop(const std::optional<std::string>& id){
    return {
        .x = 0.0,
        .y = 0.0,
        .press_id = id,
        .is_stop = true,
        .sender_start = PAGE_SENDER_START,
        .sender_counter = get_sender_counter()
    };
}
// Covers echo cases like regular_command and repeat_command
// where output is the command as sent
TickOutput expect(TickOutputType kind, const Command& command){
    return {
        .command = command,
        .kind = kind
    };
}
// For ended cases: stop_command, lapse, latched_stop, latched_burst
// The output command is always zeros with the current id, is_stop is true
TickOutput ended(TickOutputType kind, const std::optional<std::string>& id){
    return expect(kind, stop(id));
}

// 1. Healthy command every 50ms for 10s, never a stop 
// tick period is 25ms
void test_healthy_command(Duration lease){
    kit::current_test = __func__;

    const std::string p0 = "press_id_0";
    Command p0_command;

    Guard guard(lease);
    Time start_time = Time{};
    Time current_time = Time{};
    while (current_time - start_time < std::chrono::milliseconds{10000}){

        // deflect commands sent after 50ms
        if (current_time - start_time >= std::chrono::milliseconds{50}){
            p0_command = drive(p0, 0.5, 0.5);
        } else {
            p0_command = drive(p0, 0, 0);
        }

        // new tick
        Verdict admit_verdict = guard.admit(p0_command, current_time);
        CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "p0 New Command"));

        TickOutput tick_output = guard.tick(p0_command, current_time);
        compare_tick_output(tick_output, expect(TickOutputType::regular_command, p0_command), at_ms(current_time, start_time, "p0 New Command"));
        current_time += std::chrono::milliseconds{25};

        // no command, empty tick
        tick_output = guard.tick(std::nullopt, current_time);
        compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p0_command), at_ms(current_time, start_time, "p0 Repeat Command"));
        current_time += std::chrono::milliseconds{25};
    }

    return;
}


// 2. Silence: last command at t
void test_silence(Duration lease, Duration tick_period){
    kit::current_test = __func__;
    const std::string p0 = "press_id_0";

    // when the tick processes a command and it produces a stop
    // the returned command in TickOutput will carry the press id 
    // of the last command it saw
    Command p0_command = drive(p0, 0, 0);
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
            Verdict admit_verdict = guard.admit(p0_command, current_time);
            CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "p0 First Command"));
            TickOutput tick_output = guard.tick(p0_command, current_time);
            compare_tick_output(tick_output, expect(TickOutputType::regular_command, p0_command), at_ms(current_time, start_time, "p0 First Command")); 
            current_time += tick_period;
            continue;
        }
        
        // No command between t = start and t < lapse_time
        if (time_diff > std::chrono::milliseconds{0} && time_diff < lapse_time) {
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p0_command), at_ms(current_time, start_time, "p0 Repeat Command"));
            current_time += tick_period;
            continue;
        }
        
        // if time is lapsed, check expected burst
        if (time_diff == lapse_time){
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, ended(TickOutputType::lapse, p0), at_ms(current_time, start_time, "p0 Lapsed Command"));
            current_time += tick_period;
            continue;
        }
        
        // if time is after lapsed, check expected latched stop
        if (time_diff > lapse_time && time_diff < lapse_time_twice) {
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, ended(TickOutputType::latched_stop, p0), at_ms(current_time, start_time, "p0 Latched Stop Command"));
            current_time += tick_period;
            continue;
        }

        // if time is lapsed 2x, check expected burst
        if (time_diff == lapse_time_twice){
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, ended(TickOutputType::latched_burst, p0), at_ms(current_time, start_time, "p0 Latched Burst Command"));
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
    const std::string p0 = "press_id_0";

    Command p0_command = drive(p0, 0, 0);
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

        // Command recieved after lapse
        if (time_diff > std::chrono::milliseconds{300}){
            Verdict admit_verdict = guard.admit(drive(p0, 0, 0), current_time);
            CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_stale), at_ms(current_time, start_time, "Latched Stop Command"));
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, ended(TickOutputType::latched_stop, p0), "p0 Latched Stop Command");
            current_time += std::chrono::milliseconds{25};
            continue;
        }

        // Command recieved at lapse time
        if (time_diff == std::chrono::milliseconds{300}){
            Verdict admit_verdict = guard.admit(drive(p0, 0, 0), current_time);
            CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_stale), at_ms(current_time, start_time, "p0 Lapsed Command"));
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, ended(TickOutputType::lapse, p0), "p0 Lapsed Command");
            current_time += std::chrono::milliseconds{25};
            continue;
        }

        // First command at t = 0
        if (time_diff == std::chrono::milliseconds{0}){
            Verdict admit_verdict = guard.admit(p0_command, current_time);
            CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "p0 First Command"));
            TickOutput tick_output = guard.tick(p0_command, current_time);
            compare_tick_output(tick_output, expect(TickOutputType::regular_command, p0_command), "p0 First Command");
            current_time += std::chrono::milliseconds{25};
            continue;
        }

        // 1 tick without an arrival
        TickOutput tick_output = guard.tick(std::nullopt, current_time);
        compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p0_command), "p0 Repeat Command");
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
            p0_command = drive(p0, 0, 0);
            Verdict admit_verdict = guard.admit(p0_command, current_time);
            CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "p0 New Command"));
            TickOutput tick_output = guard.tick(p0_command, current_time);
            compare_tick_output(tick_output, expect(TickOutputType::regular_command, p0_command), "p0 New Command");
            current_time += std::chrono::milliseconds{25};
            continue;
        }
        // 1 tick of the same repeated command
        TickOutput tick_output = guard.tick(std::nullopt, current_time);
        compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p0_command), "p0 Repeat Command");
        current_time += std::chrono::milliseconds{25};
        continue;
    }
}

// 4. arrives 0.3 s or more after the last accepted; refused stale; the stops go out.
// Lease is 300ms, posts every 50ms, tick period is 25ms
void test_late_command(){
    kit::current_test = __func__;
    const std::string p0 = "press_id_0";

    Command p0_command = drive(p0, 0, 0);
    Guard guard(std::chrono::milliseconds{300});
    Time start_time = Time{};
    Time current_time = Time{};

    // Last accepted command at t = 0
    // no commands arrive until t = 400, past the lease of 300.
    // the 400 arrival has same press_id as t = 0, it is stale and dropped
    // past 400, latched_stop continues
    while (current_time - start_time <= std::chrono::milliseconds{500}){
        Duration time_diff = current_time - start_time;
        
        // Latched stops past t > 300
        if (time_diff > std::chrono::milliseconds{300}){
            // New command at t = 400, refused stale
            if (time_diff == std::chrono::milliseconds{400}){
                Verdict admit_verdict = guard.admit(drive(p0, 0, 0), current_time);
                CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_stale), at_ms(current_time, start_time, "p0 New Stale Command"));
            }
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            compare_tick_output(tick_output, expect(TickOutputType::latched_stop, p0_command), at_ms(current_time, start_time, "p0 Latched Stop"));
            current_time += std::chrono::milliseconds{25};
            continue;
        }

        // t = 300 lapsed, no command recieved
        if (time_diff == std::chrono::milliseconds{300}){
            TickOutput tick_output = guard.tick(std::nullopt, current_time);
            p0_command = stop(p0);
            compare_tick_output(tick_output, ended(TickOutputType::lapse, p0), at_ms(current_time, start_time, "p0 Lapsed Command"));
            current_time += std::chrono::milliseconds{25};
            continue;
        }

        // First Command Accepted at t = 0
        if (time_diff == std::chrono::milliseconds{0}){
            Verdict admit_verdict = guard.admit(p0_command, current_time);
            CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "p0 First Command"));
            TickOutput tick_output = guard.tick(p0_command, current_time);
            compare_tick_output(tick_output, expect(TickOutputType::regular_command, p0_command), "p0 First Command");
            current_time += std::chrono::milliseconds{25};
            continue;
        }

        // 1 tick without an arrival
        TickOutput tick_output = guard.tick(std::nullopt, current_time);
        compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p0_command), "Repeat Command");
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
    const std::string p0 = "press_id_0";
    TickOutput tick_output;
    Guard guard(std::chrono::milliseconds{300});
    Command p0_command = drive(p0, 0, 0);
    Command p0_drive_command = drive(p0, 0.5, 0.5);
    Command p0_stale_command = drive(p0, 0.5, 0.5);

    Time current_time {};
    Time start_time {};
    Verdict admit_verdict = guard.admit(p0_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "p0 First Command"));
    tick_output = guard.tick(p0_command, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::regular_command, p0_command), at_ms(current_time, start_time, "p0 First Command"));

    current_time += std::chrono::milliseconds{299};
    admit_verdict = guard.admit(p0_drive_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted),  at_ms(current_time, start_time, "p0 Drive Command"));
    current_time += std::chrono::milliseconds{1};
    tick_output = guard.tick(p0_drive_command, current_time);
    p0_drive_command = stop(p0);
    compare_tick_output(tick_output, ended(TickOutputType::lapse, p0), at_ms(current_time, start_time, "p0 Drive Lapsed Command"));

    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p0_stale_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_stale), "p0 Stale Command");
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::latched_stop, p0_drive_command), at_ms(current_time, start_time, "Latched Stop Command"));

    return;
}

// 6. reset: after a lapse, a centered command lands on a new press id and is admitted, deflected is refused
// t = 0 command sent, t = 300 second command sent, lapsed
// t = 325 second command sent again latched stop 
// t = 350 centered command lands admitted
// second branch sends a non-centered command at t=350 refused latched stop
void test_reset_after_lapse(){
    kit::current_test = __func__;
    const std::string p0 = "press_id_0";
    const std::string p1 = "press_id_1";

    Command p0_command = drive(p0, 0, 0);
    Command p0_drive_command = drive(p0, 0.5, 0.5);
    Command p1_command = drive(p1, 0, 0);
    TickOutput tick_output;
    Guard guard(std::chrono::milliseconds{300});

    // first branch, regular, stale, then reset accepted
    Time first_current_time{};
    Time first_start_time{};

    Verdict guard_verdict = guard.admit(p0_command, first_current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::accepted), at_ms(first_current_time, first_start_time, "p0 First Command"));
    tick_output = guard.tick(p0_command, first_current_time);
    compare_tick_output(tick_output, expect(TickOutputType::regular_command, p0_command), at_ms(first_current_time, first_start_time, "p0 First Command"));

    first_current_time += std::chrono::milliseconds{300};
    guard_verdict = guard.admit(p0_drive_command, first_current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_stale), at_ms(first_current_time, first_start_time, "p0 Lapsed Command"));
    tick_output = guard.tick(std::nullopt, first_current_time);
    p0_drive_command = stop(p0);
    compare_tick_output(tick_output, ended(TickOutputType::lapse, p0), at_ms(first_current_time, first_start_time, "p0 Lapsed Command"));

    first_current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(p1_command, first_current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::accepted), at_ms(first_current_time, first_start_time, "p1 Stick Reset Command"));
    tick_output = guard.tick(p1_command, first_current_time);
    compare_tick_output(tick_output, expect(TickOutputType::regular_command, p1_command), at_ms(first_current_time, first_start_time, "p1 Stick Reset Command"));

    // second branch, regular, stale, then reset refused
    p0_command = drive(p0, 0, 0);
    p0_drive_command = drive(p0, 0.5, 0.5);
    Command p1_drive_command = drive(p1, 0.5, 0.5);
    Time second_current_time{};
    Time second_start_time{};

    Guard second_guard(std::chrono::milliseconds{300});
    guard_verdict = second_guard.admit(p0_command, second_current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::accepted), at_ms(second_current_time, second_start_time, "p0 First Command"));
    tick_output = second_guard.tick(p0_command, second_current_time);
    compare_tick_output(tick_output, expect(TickOutputType::regular_command, p0_command), at_ms(second_current_time, second_start_time, "p0 First Command"));

    second_current_time += std::chrono::milliseconds{300};
    guard_verdict = second_guard.admit(p0_drive_command, second_current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_stale), at_ms(second_current_time, second_start_time, "p0 Lapsed Waiting Command"));
    tick_output = second_guard.tick(std::nullopt, second_current_time);
    p0_drive_command = stop(p0);
    compare_tick_output(tick_output, ended(TickOutputType::lapse, p0), at_ms(second_current_time, second_start_time, "p0 Lapsed Command"));

    second_current_time += std::chrono::milliseconds{25};
    guard_verdict = second_guard.admit(p1_drive_command, second_current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_not_centered), at_ms(second_current_time, second_start_time, "p1 Stick Not Centered Command"));
    tick_output = second_guard.tick(std::nullopt, second_current_time);
    compare_tick_output(tick_output, expect(TickOutputType::latched_stop, p0_drive_command), at_ms(second_current_time, second_start_time, "p1 Latched Stop Command"));

    return;
}

// 7. The hole: after a lapse, a late burst from the old press that starts centred then deflects must not drive. 
// The press id is what makes this pass.
void test_the_hole(){
    kit::current_test = __func__;
    const std::string p0 = "press_id_0";
    const std::string p1 = "press_id_1";
    const std::string p2 = "press_id_2";
    const std::string p3 = "press_id_3";

    
    TickOutput tick_output;
    Guard guard(std::chrono::milliseconds{300});

    Command p0_reset_sticks_command = drive(p0, 0, 0);
    Command p0_drive_command = drive(p0, 0.5, 0);
    Command p1_reset_sticks_command = drive(p1, 0, 0);
    Command p1_drive_command = drive(p1, 0.5, 0.5);
    Command p2_drive_command = drive(p2, 0.5, 0.5);
    Command p3_stop_command = stop(p3);
    Command p2_reset_sticks_command = drive(p2, 0, 0);

    Time current_time{};
    Time start_time{};

    Verdict guard_verdict = guard.admit(p0_reset_sticks_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "p0 centred, first press"));
    tick_output = guard.tick(p0_reset_sticks_command, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::regular_command, p0_reset_sticks_command), at_ms(current_time, start_time, "p0 Drives"));

    current_time += std::chrono::milliseconds{300};
    // no admit since no command arrived at 300ms
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::lapse, p0), at_ms(current_time, start_time, "Lapsed Command, p0 ends"));

    current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(p0_reset_sticks_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_stale), at_ms(current_time, start_time, "p0 reset sticks after lapse, stale"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::latched_stop, p0), at_ms(current_time, start_time, "Latched Stop Command p0"));

    current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(p0_drive_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_stale), at_ms(current_time, start_time, "p0 deflected after lapse, stale"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::latched_stop, p0), at_ms(current_time, start_time, "Latched Stop Command p0"));

    current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(p1_reset_sticks_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "p1 reset sticks, accepted"));
    tick_output = guard.tick(p1_reset_sticks_command, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::regular_command, p1_reset_sticks_command), at_ms(current_time, start_time, "p1 drives"));

    current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(p0_drive_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_spent_press), at_ms(current_time, start_time, "p0 deflected, spent press"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p1_reset_sticks_command), at_ms(current_time, start_time, "repeat p1"));

    // a press 2 drive, a stop from press 3, then press 2 centred, expected refused_spent_press, and press 2 deflected also spent
    current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(p2_drive_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "p2 deflected, takeover from p1"));
    tick_output = guard.tick(p2_drive_command, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::regular_command, p2_drive_command), at_ms(current_time, start_time, "p2 drives"));

    current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(p1_reset_sticks_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_spent_press), at_ms(current_time, start_time, "p1 centered, spent press"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p2_drive_command), at_ms(current_time, start_time, "p2 repeats"));

    current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(p1_drive_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_spent_press), at_ms(current_time, start_time, "p1 deflected, spent press"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p2_drive_command), at_ms(current_time, start_time, "p2 repeats"));
    
    current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(p3_stop_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "p3 stop, accepted"));
    tick_output = guard.tick(p3_stop_command, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::stop_command, p3_stop_command), at_ms(current_time, start_time, "p3 stops, p2 spent"));

    current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(p2_reset_sticks_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_spent_press), at_ms(current_time, start_time, "p2 reset sticks, spent press"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::latched_stop, p3_stop_command), at_ms(current_time, start_time, "p3 latched stop"));

    current_time += std::chrono::milliseconds{25};
    guard_verdict = guard.admit(p2_drive_command, current_time);
    CHECK_EQ_L(static_cast<int>(guard_verdict), static_cast<int>(Verdict::refused_spent_press), at_ms(current_time, start_time, "p2 drive command, spent press"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::latched_stop, p3_stop_command), at_ms(current_time, start_time, "p3 latched stop"));
}

// 8. 
// TODO: An empty press id. At startup the current id is empty, so a post with an empty id reads as “the same press” 
// and can be accepted if centred. The page should never send one, so we refuse it where the bool is refused, in the parser, 
// and note it in test 8’s comment with the bool.
void test_range(){
    kit::current_test = __func__;

    const std::string p0 = "press_id_0";
    const std::string p1 = "press_id_1";
    const std::string p2 = "press_id_2";
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    
    Guard guard(std::chrono::milliseconds{300});
    
    Command p0_nan_x_command = drive(p0, nan, 0);
    Command p0_nan_y_command = drive(p0, 0, nan);
    Command p1_inf_x_command = drive(p1, inf, 0);
    Command p1_inf_y_command = drive(p1, 0, inf);
    Command p1_inf_command = drive(p1, inf, inf);
    Command p2_reset_sticks_command = drive(p2, 0, 0);
    Command p2_max_x_command = drive(p2, 1.5, 0);
    Command p2_max_y_command = drive(p2, 0, 1.5);
    Command p2_max_command = drive(p2, 2, 1.5);
    Command p2_x_min_command = drive(p2, -10, -0.5);
    Command p2_y_min_command = drive(p2, 0.8, -1.2);
    Command p2_min_command = drive(p2, -10, -3);
    Command empty_reset_sticks_command = drive("", 0, 0);
    // a stop with no press id names nothing to end, so the range check refuses it before the stop branch is reached
    Command empty_stop_command = stop("");

    Time current_time {};
    Time start_time {};

    // Nan stick tests
    // Latched start, expected burst at startup with an invalid first command
    Verdict admit_verdict = guard.admit(p0_nan_x_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(current_time, start_time, "NaN x Stick Command on Startup"));
    TickOutput tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output,  ended(TickOutputType::latched_burst, std::nullopt), at_ms(current_time, start_time, "NaN x Stick Command on Startup"));
    
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p0_nan_y_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(current_time, start_time, "NaN y Stick Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::latched_stop, std::nullopt), at_ms(current_time, start_time, "NaN y Stick Command"));
    
    // Infinite stick tests
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p1_inf_x_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(current_time, start_time, "Infinite x Stick Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::latched_stop, std::nullopt), at_ms(current_time, start_time, "Infinite x Stick Command"));
    
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p1_inf_y_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(current_time, start_time, "Infinite y Stick Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::latched_stop, std::nullopt), at_ms(current_time, start_time, "Infinite y Stick Command"));
    
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p1_inf_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(current_time, start_time, "Infinite Sticks Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::latched_stop, std::nullopt), at_ms(current_time, start_time, "Infinite Sticks Command"));
    
    // Sticks reset, command accepted, unlatched
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p2_reset_sticks_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "Reset Sticks Command"));
    tick_output = guard.tick(p2_reset_sticks_command, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::regular_command, p2_reset_sticks_command), at_ms(current_time, start_time, "Reset Sticks Command"));
    
    // Max and Min stick limits
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p2_max_x_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(current_time, start_time, "Max Limit x Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p2_reset_sticks_command), at_ms(current_time, start_time, "Max Limit x Command"));
    
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p2_max_y_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(current_time, start_time, "Max Limit y Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p2_reset_sticks_command), at_ms(current_time, start_time, "Max Limit y Command"));

    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p2_max_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(current_time, start_time, "Max Limit Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p2_reset_sticks_command), at_ms(current_time, start_time, "Max Limit Command"));
    
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p2_x_min_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(current_time, start_time, "Min Limit x Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p2_reset_sticks_command), at_ms(current_time, start_time, "Min Limit x Command"));
    
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p2_y_min_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(current_time, start_time, "Min Limit y Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p2_reset_sticks_command), at_ms(current_time, start_time, "Min Limit y Command"));

    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p2_min_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(current_time, start_time, "Min Limit Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p2_reset_sticks_command), at_ms(current_time, start_time, "Min Limit Command"));

    // Empty press_id
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(empty_reset_sticks_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(current_time, start_time, "Empty Press ID Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p2_reset_sticks_command), at_ms(current_time, start_time, "Empty Press ID Command"));
    
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(empty_stop_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(current_time, start_time, "Empty Press ID Stop Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::repeat_command, p2_reset_sticks_command), at_ms(current_time, start_time, "Empty Press ID Stop Command"));
    
    // Second Guard 
    Guard second_guard(std::chrono::milliseconds{300});
    Time second_current_time {};
    Time second_start_time {};

    // Empty press as first ID, expect latched burst
    admit_verdict = second_guard.admit(empty_stop_command, second_current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(second_current_time, second_start_time, "Empty Press ID as Stop Command on Startup"));
    tick_output = second_guard.tick(std::nullopt, second_current_time);
    compare_tick_output(tick_output, ended(TickOutputType::latched_burst, std::nullopt), at_ms(second_current_time, second_start_time, "Empty Press ID as Stop Command on Startup"));


    // Third Guard 
    Guard third_guard(std::chrono::milliseconds{300});
    Time third_current_time {};
    Time third_start_time {};

    // Empty press as first ID, expect latched burst
    admit_verdict = third_guard.admit(empty_reset_sticks_command, third_current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_range), at_ms(third_current_time, third_start_time, "Empty Press ID Command on Startup"));
    tick_output = third_guard.tick(std::nullopt, third_current_time);
    compare_tick_output(tick_output, ended(TickOutputType::latched_burst, std::nullopt), at_ms(third_current_time, third_start_time, "Empty Press ID Command on Startup"));
}

// 9. Explicit Stop: accepted while latched, while driving, in every state.
void test_explicit_stop(){
    kit::current_test = __func__;
    const std::string p0 = "press_id_0";
    const std::string p1 = "press_id_1";
    const std::string p2 = "press_id_2";
    const std::string p3 = "press_id_3";
    const std::string p4 = "press_id_4";
    const std::string p5 = "press_id_5";

    // deflected sticks, still stops so sticks never reach the output
    Command p0_stop_command = drive(p0, 0.7, 0.5);
    p0_stop_command.is_stop = true;
    
    Command p1_stop_command = stop(p1);
    Command p1_reset_sticks_command = drive(p1, 0.0, 0.0);
    Command p1_drive_command = drive(p1, 0.7, 0.5);
    Command p2_reset_sticks_command = drive(p2, 0.0, 0.0);
    Command p3_stop_command = stop(p3);
    Command p4_reset_sticks_command = drive(p4, 0.0, 0.0);
    Command p5_stop_command = stop(p5);

    Guard guard(std::chrono::milliseconds{300});
    TickOutput tick_output;

    Time current_time {};
    Time start_time {};

    // Startup latch, no new commands send, empty ticks
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::latched_burst, std::nullopt), at_ms(current_time, start_time, "Startup Latched Burst"));

    // Latched Stops until t = 75ms, no new commands, empty ticks
    current_time += std::chrono::milliseconds{25};
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::latched_stop, std::nullopt), at_ms(current_time, start_time, "Startup Latched Stop"));
    current_time += std::chrono::milliseconds{25};
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::latched_stop, std::nullopt), at_ms(current_time, start_time, "Startup Latched Stop"));
    
    // Stop command at t = 75
    current_time += std::chrono::milliseconds{25};
    Verdict admit_verdict = guard.admit(p0_stop_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "Stop Command after Latched Stop"));
    tick_output = guard.tick(p0_stop_command, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::stop_command, p0), at_ms(current_time, start_time, "Stop Command after Latched Stop"));

     // Centered command then Drive at t = 100
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p1_reset_sticks_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "Reset Sticks Command"));
    tick_output = guard.tick(p1_reset_sticks_command, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::regular_command, p1_reset_sticks_command), at_ms(current_time, start_time, "Reset Sticks Command"));
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p1_drive_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "Drive Command"));
    tick_output = guard.tick(p1_drive_command, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::regular_command, p1_drive_command), at_ms(current_time, start_time, "Drive Command"));
    
    // Stop command at t = 150
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p1_stop_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "Stop Command after Driving"));
    tick_output = guard.tick(p1_stop_command, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::stop_command, p1), at_ms(current_time, start_time, "Stop Command after Driving"));
    
    // Third Stop command at t = 175, with a spent press, still stops (accepted because the stop branch runs before the spent check)
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p0_stop_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "Stop Command with Spent Press, Accepted"));
    tick_output = guard.tick(p0_stop_command, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::stop_command, p0), at_ms(current_time, start_time, "Stop Command with Spent Press, Accepted"));
        
    // Reset recenter sticks at t = 200, then silence until t = 500 for lapse test (skipping eleven repeats, test 2 covers them)
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p2_reset_sticks_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "Reset Sticks Command after Stop"));
    tick_output = guard.tick(p2_reset_sticks_command, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::regular_command, p2_reset_sticks_command), at_ms(current_time, start_time, "Reset Sticks Command after Stop"));
    current_time += std::chrono::milliseconds{300};
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::lapse, p2), at_ms(current_time, start_time, "Lapsed Command after Silence"));
    
    // t = 525 latched stop, then a stop command at t = 550 with new press id
    current_time += std::chrono::milliseconds{25};
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::latched_stop, p2), at_ms(current_time, start_time, "Latched Stop after Lapse"));
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p3_stop_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "Stop Command after Lapse"));
    tick_output = guard.tick(p3_stop_command, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::stop_command, p3), at_ms(current_time, start_time, "Stop Command after Lapse"));

    // Silence until t = 850, then a latched burst
    current_time += std::chrono::milliseconds{300};
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::latched_burst, p3), at_ms(current_time, start_time, "Latched Burst"));

    // Reset sticks at t = 875, then a stop command p5 t = 900, then center p4 t=925, expected refused press
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p4_reset_sticks_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "Reset Sticks Command after Latched Burst"));
    tick_output = guard.tick(p4_reset_sticks_command, current_time);
    compare_tick_output(tick_output, expect(TickOutputType::regular_command, p4_reset_sticks_command), at_ms(current_time, start_time, "Reset Sticks Command after Latched Burst"));
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p5_stop_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::accepted), at_ms(current_time, start_time, "Stop Command after Drive"));
    tick_output = guard.tick(p5_stop_command, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::stop_command, p5), at_ms(current_time, start_time, "Stop Command after Drive"));
    current_time += std::chrono::milliseconds{25};
    admit_verdict = guard.admit(p4_reset_sticks_command, current_time);
    CHECK_EQ_L(static_cast<int>(admit_verdict), static_cast<int>(Verdict::refused_spent_press), at_ms(current_time, start_time, "Refused Spent Press Centered Command"));
    tick_output = guard.tick(std::nullopt, current_time);
    compare_tick_output(tick_output, ended(TickOutputType::latched_stop, p5), at_ms(current_time, start_time, "Latched Stop after foreign Stop"));
}   


int main(){

    test_healthy_command(std::chrono::milliseconds{300});
    test_silence(std::chrono::milliseconds{300}, std::chrono::milliseconds{25});
    test_loss_tolerance();
    test_late_command();
    test_handler_tick_race();
    test_reset_after_lapse();
    test_the_hole();
    test_range();
    test_explicit_stop();

    return kit::finish();
}
