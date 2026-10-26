# Dungeon queue simulator

A C++20 process synchronization exercise implementing `specs.md` with the six-input clarification. All players arrive before execution. Each party uses exactly one tank, one healer, and three DPS; each player runs at most once.

## Build and run

Use CMake 3.16 or newer and a compiler **and standard library** supporting C++20 `<semaphore>` (for example, current Visual Studio, GCC, or Clang). The simulator has no third-party dependencies. CMake links `Threads::Threads`.

Windows, in a Visual Studio Developer PowerShell:

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\dungeon_queue.exe
```

Windows with MSYS2 UCRT64 GCC and MinGW Makefiles (both GCC and `mingw32-make` on `PATH`):

```powershell
cmake -S . -B build-mingw -G "MinGW Makefiles"
cmake --build build-mingw
"2 4 4 12 1 1" | .\build-mingw\dungeon_queue.exe
```

Linux:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
printf '2 4 4 12 1 1\n' | ./build/dungeon_queue
```

For a direct GCC build:

```sh
g++ -std=c++20 -Wall -Wextra -Wpedantic -pthread main.cpp -o dungeon_queue
```

## Input and output

Enter one configuration, preferably on one line:

```text
n t h d t1 t2
```

- `n`: positive number of individually identified, reusable instances.
- `t`, `h`, `d`: nonnegative tank, healer, and DPS counts.
- `t1`, `t2`: integer durations satisfying `0 <= t1 <= t2 <= 15`.

Inputs are signed 32-bit integers; `n` must also fit the standard library's semaphore capacity. Allocation or thread-creation failures are reported as errors. Whitespace and multiple lines are accepted. The program reads through the line containing the sixth value and rejects extra tokens on that line. It processes one configuration per invocation. Malformed tokens, overflow, invalid values, and EOF before all six integers produce an error on stderr and a nonzero exit code. EOF immediately after the sixth value is valid.

For `2 4 4 12 1 1`, four parties run on two instances in roughly two seconds. Each selects a duration of one second. `2 5 3 11 0 0` runs three parties immediately and reports two unmatched tanks and two unmatched DPS. `3 2 0 10 0 0` has no complete party and exits immediately with every instance unused.

Players have stable, role-qualified IDs: `T1..Tt`, `H1..Hh`, and `D1..Dd`. Party `k` contains `Tk`, `Hk`, and `D(3k-2)`, `D(3k-1)`, `D(3k)`. Contiguous ID ranges implement the initially populated FIFO role queues without creating a thread or object for each player.

The program prints all instances initially and after **every** start and finish, using the exact status words `active` and `empty`. Events include elapsed milliseconds, instance ID, party ID, composition, and selected duration. Each duration is drawn independently with `std::uniform_int_distribution<int>(t1, t2)` and simulated with `sleep_for(seconds(duration))`. Random draws can repeat; party completion order and instance assignment can vary with scheduling. Real elapsed time may exceed selected durations due to scheduling and output overhead.

The final summary includes every instance, even unused ones, with parties served and the sum of its parties' **selected durations** in seconds. It also reports total completed parties and unmatched players.

## Synchronization and fairness

There are `P = min(t, h, d / 3)` eligible parties. Unmatched players are exactly `t-P` tanks, `h-P` healers, and `d-3P` DPS; they cannot form another complete party. Eligible players awaiting admission are still scheduled to run. They are not counted as unmatched.

A single dispatcher admits parties in ascending party-ID order. It acquires a permit from `std::counting_semaphore<>`, initialized with exactly `n` permits, **before** assigning an empty instance. It puts that party into a FIFO work queue and prints the start and complete instance snapshot. Up to `min(n, P)` worker threads consume jobs, simulate durations outside the mutex, record completion, mark instances empty, and print another complete snapshot. Instance IDs are independent of worker threads. All `n` instance records exist even when fewer workers are needed.

The invariants are:

- Every admitted, unfinished party owns one move-only RAII permit, including while its job is queued. The permit moves from dispatcher to queued job to worker and is released once, after completion. There can be at most `n` such parties. An instance can be briefly empty while its completed job returns its permit.
- One mutex protects the work queue, all instance occupancy and counters, completion/error state, and all concurrent console output. An instance is assigned only while empty and becomes empty only when its current party finishes. Every printed snapshot is consistent with its event.
- Only the dispatcher assigns party IDs and players. Admission and role selection therefore follow FIFO order without depending on semaphore wake-up order. Workers may finish out of order. Fairness concerns players and parties; instances need not serve equal numbers of parties.
- Workers use a condition variable to sleep until work or shutdown is available. The dispatcher uses a completion condition variable after its last admission. Both waits use predicates. There is no busy waiting.

There is no circular wait: the dispatcher acquires the semaphore **without** holding the state mutex; workers never acquire the semaphore and never hold the mutex while simulating a dungeon. Workers can always finish and return permits while the dispatcher waits. There is only one state mutex, and condition-variable waits release it. A finite number of FIFO parties with bounded durations eventually finishes, assuming the OS schedules runnable threads; later parties cannot overtake or indefinitely bypass earlier ones. Unmatched players are reported without waiting for nonexistent later arrivals.

After all `P` completions, shutdown wakes idle workers and joins every thread before printing the summary. The exception path also requests shutdown, drains admitted work, and joins all threads, including after partial thread creation. RAII prevents permit leaks during exceptions. Worker failures are propagated to the main thread as errors.

## Verification

The optional black-box checker uses Python 3's standard library:

```powershell
python tests/check_simulator.py build/Release/dungeon_queue.exe
```

```sh
python3 tests/check_simulator.py build/dungeon_queue
```

Adjust the executable path for your generator (for MSYS2, `build-mingw/dungeon_queue.exe`). The checker runs no-party, one-party, excess-party, excess-instance, imbalanced, fixed-zero, fixed-one-second, random-duration, multiline-input, invalid-input, overflow, and EOF scenarios. Repeated rapid handoffs exercise concurrent reuse. It parses every transition and full status snapshot to check party composition, unique players, role FIFO, admission FIFO, active-party limits, instance exclusivity, completion, unmatched counts, and per-instance count/duration totals. Subprocess timeouts catch hangs. A final real-time run demonstrates two overlapping one-second instances over two batches and prints its trace.

These checks verify observed executions; they do not constitute an exhaustive scheduler exploration or a statistical proof of random uniformity. The synchronization argument above covers the design, and the standard distribution provides the inclusive uniform draws.
