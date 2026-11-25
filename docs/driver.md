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

A call gives back one of two answers: the frame was written, or the write timed out. Every write has a wait limit so a blocked port cannot hold up the core loop.

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

The driver raises a fault in two cases:

1. A write timed out.
2. No feedback has arrived for 165 ms.

On a fault the core loop stops passing drive requests, sends stops, and tells the operator that the motor board is lost.

A fault latches. It clears only when the operator presses Re-arm, and Re-arm is accepted only after several feedback messages in a row have arrived on time. The count is open.

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

## Open

- `drive` takes stick values or wheel speeds.
- Which of the board's messages the driver reads, and their fields.
- The wait limit on a write.
- How many on-time feedback messages count as "back".
- What state the driver is in when it gives up waiting for the board.
- The test list, written before any code.

## Bench checks

1. Does the installed firmware reply to a motion command, or only stream feedback?
2. What is the board's timeout on the installed firmware, measured on the stand?
3. Is there a command that sets that timeout, and does it survive a power cycle?
4. Can the board take 40 commands a second without falling behind or dropping feedback?
5. How much do the gaps between feedback messages vary, on the stand and while driving?
6. What frame rate does the board take comfortably, and what is the most it takes before it falls behind?
7. What is the line's speed, and does a burst of three stops fit inside the 5 ms budgeted for a write?