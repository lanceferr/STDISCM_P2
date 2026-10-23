# Dungeon queue simulation

A portable C++17 console implementation of `specs.md`, using one worker thread per
dungeon instance and a custom counting semaphore. No external libraries are needed.

## Build and run

Use CMake 3.20 or newer and a compiler with a complete C++17 standard library,
including `<charconv>`, `<optional>`, and standard threading support.

### GCC (Linux or a modern MinGW-w64 toolchain)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/dungeon_queue
```

On Windows, run `.\build\dungeon_queue.exe` for a single-configuration generator.
With GCC on Linux, a direct build without CMake is also possible:

```sh
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -pthread -Iinclude src/main.cpp src/simulation.cpp -o dungeon_queue
./dungeon_queue
```

### MSVC (Visual Studio Developer PowerShell)

Install the Visual Studio Desktop development with C++ workload and CMake tools.
From a Developer PowerShell, with CMake on PATH:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
.\build\Release\dungeon_queue.exe
```

Use separate build directories when switching compilers or CMake generators.

## Input and output

Enter six whitespace-separated integers, on one line or across several lines:

```text
n t h d t1 t2
```

| Input | Meaning | Allowed values |
| --- | --- | --- |
| `n` | Maximum concurrent dungeon instances | At least 1 |
| `t` | Tank players | Nonnegative |
| `h` | Healer players | Nonnegative |
| `d` | DPS players | Nonnegative |
| `t1`, `t2` | Inclusive dungeon duration bounds, in seconds | `0 <= t1 <= t2 <= 15` |

The specification's reference to “three inputs” is treated as a typo. Inputs must
fit a signed C++ `int`. Missing input at EOF, malformed tokens, overflow, and invalid
bounds produce an error on stderr and exit code 1. The program reads the first six
tokens; no EOF is required after valid interactive input. Zero-second runs are valid.
Extremely large valid queues or instance counts remain subject to available memory
and OS thread resources; resource failures are reported as errors.

For example, enter:

```text
2 3 2 8 1 1
```

This runs two parties, each lasting one second, with two instances available.
Party 1 contains T1, H1, D1, D2, D3; party 2 contains T2, H2, D4, D5, D6.
The final totals include:

```text
Total parties completed: 2
Unassigned players: tanks = 1, healers = 0, DPS = 2
```

Every status snapshot lists **all** instances as `active` or `empty`. Active entries
also list party ID, role-qualified player IDs, and assigned duration. Snapshots appear
initially, on every start and finish, and after all workers have joined.

The final summary includes every instance, even unused ones. Its total time served
is the sum of that instance's assigned dungeon durations, in seconds, excluding idle
time. This is simulated service time, not a measured wall-clock runtime. Each duration
is drawn separately using `std::uniform_int_distribution<int>` and `std::mt19937`.
The executable uses real `sleep_for` calls, which can return later than requested
because of OS scheduling. Scheduling determines which instance claims each party;
instances need not serve equal numbers of parties.

## Synchronization and fairness

All players arrive before processing begins. Sequential IDs represent implicit FIFO
queues for each role, avoiding allocations for players who cannot join any party.
The main thread forms exactly `min(t, h, d / 3)` parties by consuming the next tank,
healer, and three DPS IDs. It puts those parties in a FIFO job queue, followed by one
shutdown marker per instance, then publishes one semaphore permit for every item.
Each instance has one worker, which acquires a permit, removes one job under the queue
mutex, marks itself active, sleeps, updates its statistics, and repeats. A marker
ends that worker. The main thread joins all workers before the final summary.

The semaphore uses a mutex, condition variable, and permit count. `acquire()` waits
with a predicate, so spurious wakeups cannot consume nonexistent permits. `release()`
adds permits under the mutex before notifying waiters; releases made before a waiter
arrives are retained. A separate queue mutex makes each job removal exclusive. A
status mutex serializes state changes and snapshot output, preventing mixed lines or
partially updated snapshots. The locks are not nested, and no application mutex is
held while waiting on the semaphore or sleeping through a dungeon run.

There is no circular lock dependency. Every queued item has a permit, and shutdown
markers cannot overtake pending party jobs. Workers that are still running parties
finish before they can consume their own markers, and all workers are joined. Thus
normal execution terminates without deadlock. One worker per instance guarantees
at most `n` active parties.

Player fairness follows from FIFO role selection and a finite FIFO job queue: every
eligible party is eventually processed, assuming runnable threads eventually receive
CPU time and synchronization operations make progress. Mutexes and condition
variables do not promise FIFO worker wakeup; worker assignment and the order of printed
start events can vary even though job removal is FIFO. Leftover players cannot form a
complete party from the available roles; they are reported rather than left waiting.
This is an unmatched queue, not starvation. Fair allocation of work among instances
is not a requirement.

Worker exceptions are saved and rethrown on the caller's thread after joining workers.
If creating a thread fails partway through, existing workers are cancelled and joined.
An already running dungeon may finish before error cleanup completes.

## Tests and internal API

`include/simulation.hpp` exposes input validation, simulation, snapshots, summaries,
and output formatting. `Options` accepts an optional seed, a custom sleep function,
and an observer. These are internal testing hooks; the console has no extra required
inputs. Fixed seeds reproduce per-party durations within a standard-library
implementation; scheduling and cross-library distribution sequences need not match.
Custom sleepers run concurrently and must be thread-safe. Observers run serially
under the status mutex and must not wait for worker progress.

CTest runs a C++ suite and CLI integration checks. Coverage includes party composition,
FIFO allocation, unique player assignments, leftovers, statistics, coherent snapshots,
invalid input, duration bounds and seeding, semaphore blocking and exact permit
accounting, worker exception cleanup, and shutdown with idle instances. An injected
sleeper holds the first wave until all available instances overlap, checking both
real concurrency and the capacity bound without waiting through dungeon durations.
Blocking test waits have deadlines, with a separate watchdog and CTest timeouts.

For a short real-time smoke test in PowerShell:

```powershell
"2 2 2 6 1 1" | .\build\Release\dungeon_queue.exe
```

Both parties should complete, each instance should finish empty, and the summed
service time should be two seconds. Wall time is normally around one second when
both workers overlap, with scheduling and startup overhead.
