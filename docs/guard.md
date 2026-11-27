# The guard

Status: built and tested. The code is in `src/guard.hpp` and `src/guard.cpp`, and the tests are in `src/tests/test_guard.cpp`. The suite passes 2,847 of 2,847 checks. The parts around the guard (the request handler, the core loop and the operator's page) are not in this repository yet. Two known gaps and the design decisions that are not built are listed at the end.

## What it is for

The operator drives by holding a control on a web page. While it is held, the page posts a command every 50 ms.

The guard decides, for every command, whether the rover may move. It also stops the rover when commands stop arriving.

The guard has no thread, no port and no clock of its own. Every call is handed the time, so every test runs on made-up time.

The guard knows nothing about the motor board or the serial line. It gives the core loop one output per tick, and the core loop turns that output into calls to the driver. The mapping is in `driver.md`.

## Words

| Word | Meaning |
|---|---|
| Press | One hold of the drive control, from press to release. The page gives each press an id. The guard only compares ids for equality. |
| Lease | How long the last accepted command stays valid. It is 300 ms. |
| Latched | Stopped, and staying stopped until a new press starts with the stick centered. |
| Spent | A press that has ended. A drive command with its id is never accepted again. |

## The command

| Field | Holds |
|---|---|
| `x`, `y` | The two stick values, each from -1 to 1 |
| `press_id` | The id of the press, or nothing |
| `is_stop` | True for an explicit stop |
| `sender_start` | A number the page sets once when it loads, higher on each load |
| `sender_counter` | A number the page raises by one on every post |

## The two calls

| Call | Takes | Gives | Changes the guard |
|---|---|---|---|
| `admit` | A command and the time | A verdict | No |
| `tick` | A command or nothing, and the time | One tick output | Yes |

The request handler calls `admit` so it can answer the page at once. If the verdict is accepted, it leaves the command in a slot for the core loop.

Every 25 ms the core loop takes whatever is in the slot, which may be nothing, and calls `tick`. `tick` runs `admit` again with its own time, because the lease can run out between the two calls.

## What `admit` checks

The checks run in this order. The first one that fails is the verdict.

| # | Check | Verdict when it fails |
|---|---|---|
| 1 | Is it a stop with a press id? Then it is accepted here, in every state, whatever its sticks and numbers. | |
| 2 | Is there a press id, and is each stick a finite number from -1 to 1? | Refused, range |
| 3 | Is the start number higher than the last accepted drive's, or equal with a higher counter? | Refused, out of order |
| 4 | If the command is from the live press, is the last accepted command less than a lease old? | Refused, stale |
| 5 | Is the press still unspent? | Refused, spent press |
| 6 | If the guard is latched, are both sticks within 0.1 of zero? | Refused, not centered |
| 7 | Otherwise | Accepted |

The stick values on a stop never reach the output. Ending the press sets them to zero.

A lease that is exactly 300 ms old has run out. The comparison is "at or over".

## What `tick` does

When the command is accepted:

- The time of the last accepted command is set to now.
- If the command is from a different press than the live one, the live press is spent first. The current press is the only live one, and every press accepted before it is spent.
- A drive becomes the current command. The guard is no longer latched, and it keeps the command's start number and counter for the order check.
- A stop ends the press. The sticks go to zero, the guard latches, the press is spent and the burst time is set to now.

When nothing is waiting, or the waiting command is refused, the guard's state decides:

| State | Condition | Tick output | What changes |
|---|---|---|---|
| Driving | The last accepted command is less than a lease old | Repeat command | Nothing |
| Driving | It is a lease old or more | Lapse | The press ends, as for a stop |
| Latched | No burst has been sent yet, or a lease has passed since the last one | Latched burst | The burst time is set to now |
| Latched | Otherwise | Latched stop | Nothing |

A refused command changes nothing in the guard.

A tick output is the current command and one of six kinds: regular command, repeat command, stop command, lapse, latched burst or latched stop. For the last four the command has zero sticks and is marked as a stop.

The guard starts latched with no live press. The first tick gives a latched burst, and the first drive has to arrive with the stick centered.

## Timing

The lease is given to the guard when it is built. The tests use 300 ms, posts every 50 ms and a tick every 25 ms.

- Four missing posts in a row survive. Five trip the lease.
- After the last command at time t, the lapse comes on the first tick at or past t + 300 ms. The latched burst repeats at t + 600 ms.

The time handed to the guard must never run backwards. The core loop reads the steady clock, and the tests only move time forwards. A negative difference would read as a fresh lease.

## Why the press id and the counter

After a lapse, late posts from the old press can still arrive. They can start centered and then deflect, which would pass the centered check and drive the rover. The press is spent at the lapse, so every one of them is refused.

Two posts from one press can also arrive in the wrong order. The counter refuses the older one. The start number does the same for a late post from before the page was reloaded.

## Tests

Every test runs on made-up time with a lease of 300 ms.

1. `test_healthy_command`: a command every 50 ms for 10 s. There is never a stop.
2. `test_silence`: the last command at t. The lapse comes on the first tick at or past t + 300 ms, every tick after it gives a latched stop, and the latched burst comes at t + 600 ms.
3. `test_loss_tolerance`: four missing posts in a row survive and five trip the lease.
4. `test_late_command`: a command that arrives 300 ms or more after the last accepted one is refused as stale, and the guard stays stopped.
5. `test_handler_tick_race`: a command is admitted just inside the lease and reaches the tick after the lease has run out. The tick finds the lapse and does not send it.
6. `test_reset_after_lapse`: after a lapse, a centered command on a new press is accepted and a deflected one is refused.
7. `test_the_hole`: after a lapse, late posts from the old press that start centered and then deflect do not drive. Once a new press is accepted over a live one, the old press is refused as spent.
8. `test_range`: a stick that is not a number, is infinite or is outside -1 to 1 is refused, and so is an empty press id.
9. `test_explicit_stop`: a stop is accepted at startup, while driving and while latched, and from a press that is already spent.
10. `test_swapped_posts`: two posts from one press arrive swapped, and the older one is refused as out of order.

## Known gaps

1. `admit` reads two press ids without first checking that they are present: the command's id on the stop check, and the live press's id on the stale check, which is empty until the first press is accepted. Both reads have to be made safe before the guard is called done.
2. A stop with an empty press id is refused as out of range. The design says a stop is obeyed whatever its id.

## Decided and not built

- A stop is obeyed with any press id or none.
- A press id is 1 to 64 characters long.
- One spent press is kept in place of the set of all spent ids. The set grows by one id for every press and is never cleared.
- The guard takes one more input that says a fault happened outside it, and latches as it does after a lapse. The driver note describes it under "What the guard learns".
- The request handler, the slot and its lock, the core loop, and a page that sends the start number and the counter.

## Open

- Whether a new press over a live one must also start with the stick centered. Today the centered check applies only while the guard is latched.
