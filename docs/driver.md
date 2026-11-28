# The driver

Status: design only. Nothing here is built yet. Numbers marked "unmeasured" come from reading or from arithmetic and still need the bench. The wire format is read from the first version's code and is unconfirmed until bench check 8.

## What it is for

The driver is the only part of the stack that talks to the ESP32 motor board. The core loop hands it a narrow request, drive or stop, and the driver writes one frame to the serial line. It also reads the feedback the board sends and hands it back to the core loop.

The board keeps the vendor firmware, so the bytes on the wire are whatever that firmware reads. That format lives inside the driver and nowhere else.

The driver knows nothing about the guard. It never sees a press id, a counter or a kind of tick output.

## The interface

There are two calls.

| Call | Takes | Writes |
|---|---|---|
| `drive` | A left and a right wheel speed | One frame |
| `stop` | Nothing | One frame |

The core loop converts stick values to wheel speeds before it calls `drive`. The driver never sees a stick value.

A call gives back one of four answers: the frame was written, the write timed out, the port answered with an error, or the request was refused. After a timed out or an error answer, the rest of that tick's writes are skipped.

A `drive` is refused while the driver is faulted. A `drive` with either wheel speed at or above 2.0 in either direction is refused too, and it raises a fault. The first version's code says the firmware ignores such a speed, and a request for one means the core loop's conversion is wrong. Nothing is written for a refused drive.

Every write has a wait limit of 10 ms, so a blocked port cannot hold up the core loop. The number is provisional until bench check 7. When a write times out or answers with an error, the rest of that tick's writes are skipped.

A call does not say whether the board took the frame. A serial write can succeed with nobody listening. Whether the board answers a motion command at all is unmeasured (bench check 1), and the design does not depend on it. The driver learns that the board is alive from the feedback.

## The wire

This section is what the first version's code says. None of it is confirmed on the bench yet (bench check 8), and it changes if the bench says otherwise.

The line runs at 115200 baud. Each message is one JSON object on one line, ended by a newline.

A drive is `{"T": 1, "L": left, "R": right}`. A stop is the same command with both speeds at zero. There is no separate stop message.

A feedback line carries 14 fields: the two wheel speeds (`L`, `R`), two odometry counts (`odl`, `odr`), a voltage (`v`) and nine IMU values (`ax`, `ay`, `az`, `gx`, `gy`, `gz`, `mx`, `my`, `mz`). The driver hands these to the core loop as it reads them.

Only a whole line that parses and carries the feedback fields renews the feedback lease and counts toward Re-arm. Any other line is skipped and renews nothing.

By arithmetic, a byte takes about 87 microseconds at this speed. A stop frame of about 25 bytes takes about 2.2 ms, and a burst of three about 6.5 ms. Unmeasured (bench check 7).

## The port

The driver reaches the serial line through a port it is given. The port carries bytes, held as a `std::string`. It knows nothing about frames, lines or JSON. There are two ports: the real serial port, and a fake one for the tests.

| Call | Does |
|---|---|
| `write` | Takes the bytes of one frame. Answers written, timed out or error. |
| `read` | Returns at once with the bytes that have arrived, possibly none, or says that the port failed. It never waits. |

A failed read is not the same as a read with no bytes. "Nothing arrived" is an empty string. "The port failed" is an empty `std::optional`, and it raises the fault with the reason port error. The tick's writes are not skipped for it: drives are refused and the stop is still written.

Written means every byte of the frame went out. Anything less is timed out or error, and the driver never resumes a frame. Half a frame may be left on the wire. What the board does with it is unmeasured (bench check 9), and the burst of three stops is the protection.

The port does the waiting on a write. The limit is the 10 ms, handed to the real port when it is built. A healthy write hands the bytes to the operating system and should return almost at once, with the time on the wire passing afterwards (unmeasured, bench check 7). So the driver's work in a tick is bounded by one wait limit and a read that never waits.

The driver encodes frames and assembles lines. It keeps the unfinished line between reads, splits on the newline and parses.

Wall time is read in two places only: the core loop, once per tick, and the real port's wait. The guard, the driver and the fake port never read it. Every driver call that depends on time is handed the time by the core loop.

## What the core loop sends

The guard gives the core loop one tick output every 25 ms. The core loop turns it into calls. The driver never decides how many.

| Tick output from the guard | Calls to the driver |
|---|---|
| Regular command | One `drive` |
| Repeat command | One `drive` with the same values again |
| Stop command | A burst of `stop` |
| Lapse | A burst of `stop` |
| Latched burst | A burst of `stop` |
| Latched stop | One `stop` |

A burst is three stops written back to back. A frame can be lost on the wire, and the stop is the one that has to arrive. The core loop holds the count.

## Timing

The core loop writes on every tick, 40 times a second, repeats included. The board has its own timeout and stops the wheels when no command arrives in time. The Pi does not need to know how long that timeout is. A write on every tick renews it, and silence from the Pi is the only thing the board has to detect.

The board sends feedback about 20 times a second, one message every 50 ms. The driver forgives two missed messages:

    feedback lease = 3 × 50 ms + 15 ms of jitter = 165 ms

The lease is checked on a tick, so silence is noticed within 190 ms. The 15 ms is a guess until bench check 5.

## Faults

The driver starts faulted. It becomes faulted again in four cases:

1. A write timed out.
2. The port answered with an error, for example because the cable came out.
3. No feedback has arrived for 165 ms.
4. A `drive` asked for a wheel speed at or above 2.0.

There is one faulted state. The driver keeps the reason with it: not armed yet, write timed out, port error, feedback silent, or bad speed. The reason is for the operator and the log. It changes nothing about how the fault is cleared.

While faulted the driver refuses a `drive` and writes nothing. A `stop` is always written. The core loop also stops passing drive requests and tells the operator the reason.

A fault latches. It clears only when the operator presses Re-arm, and Re-arm is accepted only after 20 feedback messages in a row have arrived on time, which is about one second. On time means no gap longer than the 165 ms lease. A late message restarts the count. So does every fault, whatever its reason, so Re-arm always waits for 20 on-time messages after the last fault.

The same rule holds at power-on, so the first Re-arm is the arming step.

There is no give-up state. The driver keeps listening for as long as it takes. If the port itself disappears, the driver tries to reopen it while it is faulted.

Checking whether the wheels obey the commands is left out for now. Wheels ramp, slip and stall, so that rule needs a tolerance and a time window of its own.

## What the guard learns

The guard gets one input that says a fault happened outside it. It latches the same way it does after a lapse, so the next drive needs a new press with the stick centered. It does not learn what failed. This keeps the guard free of anything about motors or boards.

This input is not in the guard yet.

## Predicted stop distances at 0.30 m/s

Distance is speed times the total blind time. Coast is taken as 100 ms.

| Failure | Blind time | Distance |
|---|---|---|
| The Wi-Fi drops | 300 ms lease + 25 ms tick + 5 ms write + 100 ms coast = 430 ms | 12.9 cm |
| Feedback goes silent while writes still work | 190 ms + 5 ms write + 100 ms coast = 295 ms | 8.9 cm |
| The Pi dies, or the line dies both ways | 3,000 ms board timeout + 100 ms coast = 3,100 ms | 93 cm |

The Wi-Fi drop is the number to measure in January. It is the failure met in the field.

The 5 ms for a write covers the first stop frame, which is the one that ends the blind time: about 2.2 ms by arithmetic. The whole burst of three takes about 6.5 ms.

The last row is the known weakness. While the Pi cannot write, the board's own timeout is the only thing that stops the wheels. The 3 s is from the published firmware source. A comment in the first version's code says it was shown on this rover. This rebuild has not measured it, so it counts as unmeasured until bench checks 2 and 3.

The model has been checked once: it predicted 8 cm at 0.18 m/s and 7 cm was measured on the stand.

## Tests

The driver takes its clock and its port from outside. Every test runs on made-up time with a fake port that records what was written and can be told to block, fail, go silent or take only part of a frame. A fake port that blocks answers timed out at once. It never waits. A test that needs a healthy driver arms it first: 20 on-time messages, then Re-arm.

1. `test_healthy_command`: a drive on every tick for 10 s. The port holds 400 frames and there is no fault.
2. `test_healthy_stop_command`: drives and stops interleaved on every tick for 10 s. One frame per call and no fault.
3. `test_timeout`: healthy ticks, then a burst of three stops whose first write times out. One write is attempted and the fault is raised. The next tick still sends a stop. The same holds when the first write answers with an error.
4. `test_feedback_lease`: with the last message 164 ms old there is no fault. At 165 ms there is.
5. `test_latching`: a fault, then feedback returns on time. The driver stays faulted.
6. `test_re_arm`: after a fault a drive is refused and nothing is written. Re-arm is refused at 19 on-time messages and accepted at 20.
7. `test_re_arm_restart`: a late message during the count sets it back to zero.
8. `test_port_disappear`: the port answers every call with an error and the fault is raised at once. The port opens again, and Re-arm is accepted after 20 on-time messages.
9. `test_feedback_pieces`: a line split across two reads counts once, when its newline arrives. A line that does not parse, or that parses without the feedback fields, is skipped and does not renew the lease.
10. `test_startup`: before any feedback a drive is refused. Re-arm is refused before 20 on-time messages.
11. `test_stop_while_faulted`: a stop is written while the driver is faulted.
12. `test_speed_limit`: a speed just under 2.0 is written. A drive with a wheel speed of 2.0 or -2.0 on either side is refused, nothing is written and the fault is raised with the reason bad speed. The next stop is written. Re-arm is refused at 19 on-time messages after the fault and accepted at 20.
13. `test_partial_write`: the port takes half a frame and answers timed out. The fault is raised with the reason write timed out. The next write is a whole stop frame, not the rest of the old one.

Every test that raises a fault also checks its reason.

A test checks a frame by parsing it, not by its exact bytes, until bench check 8 confirms the format.

## Open

- Body velocity (forward speed in m/s, turn rate in rad/s) as the standard interface. It is added above the driver with the ROS 2 step and does not change the driver.
- The unit of a wheel speed on the wire.
- A cap on the length of an unfinished line.

## Bench checks

1. Does the installed firmware reply to a motion command, or only stream feedback?
2. What is the board's timeout on the installed firmware, measured on the stand?
3. Is there a command that sets that timeout, and does it survive a power cycle?
4. Can the board take 40 commands a second without falling behind or dropping feedback?
5. How much do the gaps between feedback messages vary, on the stand and while driving?
6. What frame rate does the board take comfortably, and what is the most it takes before it falls behind?
7. What is the line's speed, and how long does a burst of three stops take on the wire? Arithmetic says about 6.5 ms.
8. Is the wire what the first version's code says? The line's speed, one JSON object per line, the drive command's fields, and the 14 feedback fields, each read off the installed firmware on the stand. Does any other kind of line arrive?
9. What does the board do with a broken line followed by a good one? Is the good one taken?
