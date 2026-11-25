# The driver

Status: design only. Nothing here is built yet. Numbers marked "unmeasured" come from reading or from arithmetic and still need the bench.

## What it is for

The driver is the only part of the stack that talks to the ESP32 motor board. The core loop hands it a narrow request, drive or stop, and the driver writes one frame to the serial line. It also reads the feedback the board sends and hands it back to the core loop.

The board keeps the vendor firmware, so the bytes on the wire are whatever that firmware reads. That format lives inside the driver and nowhere else.

The driver knows nothing about the guard. It never sees a press id, a counter or a kind of tick output.

## The interface

There are two calls.

| Call | Takes | Writes |
|---|---|---|
| `drive` | Two values. Open: stick values or wheel speeds, decided by what the board reads. | One frame |
| `stop` | Nothing | One frame |

A call gives back one of three answers: the frame was written, the write timed out, or the request was refused because the driver is faulted.

Every write has a wait limit of 10 ms, so a blocked port cannot hold up the core loop. The number is provisional until bench check 7. When a write times out, the rest of that tick's writes are skipped.

A call does not say whether the board took the frame. A serial write can succeed with nobody listening. Whether the board answers a motion command at all is unmeasured (bench check 1), and the design does not depend on it. The driver learns that the board is alive from the feedback.

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

The driver starts faulted. It becomes faulted again in three cases:

1. A write timed out.
2. The port answered with an error, for example because the cable came out.
3. No feedback has arrived for 165 ms.

While faulted the driver refuses a `drive` and writes nothing. A `stop` is always written. The core loop also stops passing drive requests and tells the operator that the motor board is lost.

A fault latches. It clears only when the operator presses Re-arm, and Re-arm is accepted only after 20 feedback messages in a row have arrived on time, which is about one second. On time means no gap longer than the 165 ms lease. A late message restarts the count.

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

The last row is the known weakness. While the Pi cannot write, the board's own timeout is the only thing that stops the wheels. The 3 s is from the published firmware source and is unmeasured on the installed firmware (bench checks 2 and 3).

The model has been checked once: it predicted 8 cm at 0.18 m/s and 7 cm was measured on the stand.

## Tests

The driver takes its clock and its port from outside. Every test runs on made-up time with a fake port that records what was written and can be told to block, fail or go silent. A test that needs a healthy driver arms it first: 20 on-time messages, then Re-arm.

1. `test_healthy_command`: a drive on every tick for 10 s. The port holds 400 frames and there is no fault.
2. `test_healthy_stop_command`: drives and stops interleaved on every tick for 10 s. One frame per call and no fault.
3. `test_timeout`: healthy ticks, then a burst of three stops whose first write times out. One write is attempted and the fault is raised. The next tick still sends a stop.
4. `test_feedback_lease`: with the last message 164 ms old there is no fault. At 165 ms there is.
5. `test_latching`: a fault, then feedback returns on time. The driver stays faulted.
6. `test_re_arm`: after a fault a drive is refused and nothing is written. Re-arm is refused at 19 on-time messages and accepted at 20.
7. `test_re_arm_restart`: a late message during the count sets it back to zero.
8. `test_port_disappear`: the port answers every call with an error and the fault is raised at once. The port opens again, and Re-arm is accepted after 20 on-time messages.
9. `test_feedback_pieces`: a message split across two reads counts once. Bytes that are not a message are skipped and do not renew the lease.
10. `test_startup`: before any feedback a drive is refused. Re-arm is refused before 20 on-time messages.
11. `test_stop_while_faulted`: a stop is written while the driver is faulted.

The exact bytes of a frame wait for the board's format.

## Open

- `drive` takes stick values or wheel speeds.
- Which of the board's messages the driver reads, and their fields.

## Bench checks

1. Does the installed firmware reply to a motion command, or only stream feedback?
2. What is the board's timeout on the installed firmware, measured on the stand?
3. Is there a command that sets that timeout, and does it survive a power cycle?
4. Can the board take 40 commands a second without falling behind or dropping feedback?
5. How much do the gaps between feedback messages vary, on the stand and while driving?
6. What frame rate does the board take comfortably, and what is the most it takes before it falls behind?
7. What is the line's speed, and does a burst of three stops fit inside the 5 ms budgeted for a write?