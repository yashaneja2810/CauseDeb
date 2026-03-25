# Module 04 — Linux POSIX APIs: Files, Processes, Signals, Clocks

## Learning Objectives

- Open, read, write, and close files using POSIX file I/O
- Understand process creation with `fork` and `exec`
- Install signal handlers to enable graceful shutdown
- Use `clock_gettime` for high-resolution timing

---

## 1. POSIX File I/O

CauseDeb uses C++ `std::fstream` for trace files, but understanding the
underlying POSIX calls clarifies the semantics:

```cpp
#include <fcntl.h>
#include <unistd.h>

int fd = open("trace.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
if (fd < 0) { perror("open"); exit(1); }

const char* msg = "hello\n";
write(fd, msg, strlen(msg));

fsync(fd);   // flush to disk — important for durability guarantees
close(fd);
```

The Collector calls `file_.flush()` after every event to ensure the trace
is durable even if the process crashes.

---

## 2. Process Concepts

| Concept | Description |
|---------|-------------|
| PID | Unique integer identifying a process |
| PPID | Parent process ID |
| `fork()` | Creates an identical child process |
| `exec()` | Replaces the current process image with a new programme |
| `wait()` | Parent waits for child to exit |
| `getpid()` | Returns the calling process's PID |

CauseDeb records the PID of each event so you can distinguish between
multiple instances of the same service running simultaneously.

```cpp
pid_t pid = getpid();
```

---

## 3. Signal Handling

The Collector installs signal handlers so it shuts down gracefully on
Ctrl-C or `kill`:

```cpp
#include <csignal>

static std::atomic<bool> g_running{true};

static void on_signal(int /*signum*/) {
    g_running.store(false);
}

// In main():
signal(SIGINT,  on_signal);   // Ctrl-C
signal(SIGTERM, on_signal);   // kill <PID>
```

Important: signal handlers run in an interrupt context. Only **async-signal-
safe** functions are safe to call from a signal handler. Writing to an
`std::atomic<bool>` is safe; calling `printf` or `malloc` is not.

---

## 4. High-Resolution Clocks

```cpp
#include <time.h>

struct timespec ts;

// Wall-clock time (can jump backward if NTP adjusts the clock)
clock_gettime(CLOCK_REALTIME, &ts);
uint64_t wall_ns = (uint64_t)ts.tv_sec * 1'000'000'000ULL + ts.tv_nsec;

// Monotonic time (always increases; no relation to wall clock)
clock_gettime(CLOCK_MONOTONIC, &ts);
uint64_t mono_ns = (uint64_t)ts.tv_sec * 1'000'000'000ULL + ts.tv_nsec;
```

CauseDeb records **both**:
- `wall_ns` for human-readable timestamps in the trace
- `mono_ns` for accurate latency computation on a single machine

Across machines, **neither clock is reliable** for ordering. That is exactly
why CauseDeb uses vector clocks for causal ordering.

---

## 5. File Descriptors and `close-on-exec`

When the Collector spawns child processes (for analysis tools), it should
not inherit the trace file descriptor. Set `O_CLOEXEC` on `open()` or
`FD_CLOEXEC` via `fcntl` to automatically close FDs on `exec`.

---

## Mini Project

Write a programme that:
1. Opens a file for writing
2. Installs a `SIGINT` handler that writes "shutting down\n" to the file
   and then closes it gracefully
3. Loops printing the current monotonic timestamp every 100ms until
   interrupted

## Exercises

1. What is the difference between `CLOCK_REALTIME` and `CLOCK_MONOTONIC`?
2. Why is it unsafe to call `printf` from a signal handler?
3. What does `fsync` do, and when is it necessary?
