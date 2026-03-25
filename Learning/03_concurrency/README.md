# Module 03 — C++ Concurrency: Threads, Mutexes, Condition Variables

## Learning Objectives

- Create and join threads with `std::thread`
- Protect shared state with `std::mutex` and `std::lock_guard`
- Coordinate threads with `std::condition_variable`
- Understand data races and how to avoid them
- See how CauseDeb's background sender thread works

---

## 1. Creating Threads

```cpp
#include <thread>
#include <iostream>

void worker(int id) {
    std::cout << "Thread " << id << " running\n";
}

int main() {
    std::thread t1(worker, 1);
    std::thread t2(worker, 2);
    t1.join();   // wait for t1 to finish
    t2.join();   // wait for t2 to finish
}
```

If you forget to `join` (or `detach`), the destructor of `std::thread` calls
`std::terminate()` — this is a hard crash, not a graceful exit.

---

## 2. Mutexes

A **mutex** (mutual exclusion lock) ensures only one thread executes a
critical section at a time:

```cpp
#include <mutex>

std::mutex mtx;
int shared_counter = 0;

void increment() {
    std::lock_guard<std::mutex> lk(mtx);   // RAII lock — released on scope exit
    shared_counter++;
}
```

Never access shared state without holding the appropriate mutex. A **data
race** (concurrent access where at least one is a write) is undefined
behaviour in C++.

---

## 3. Condition Variables

A **condition variable** lets a thread sleep until a condition becomes true,
without busy-waiting:

```cpp
#include <condition_variable>
#include <queue>

std::mutex          q_mtx;
std::condition_variable q_cv;
std::queue<int>     q;

// Producer
void produce(int value) {
    {
        std::lock_guard<std::mutex> lk(q_mtx);
        q.push(value);
    }
    q_cv.notify_one();   // wake one waiting consumer
}

// Consumer
void consume() {
    std::unique_lock<std::mutex> lk(q_mtx);
    q_cv.wait(lk, []{ return !q.empty(); });  // atomically releases lock and sleeps
    int v = q.front(); q.pop();
    // process v...
}
```

CauseDeb's `Tracer::sender_loop` uses exactly this pattern: the main thread
calls `record()` which pushes an event and calls `notify_one()`; the sender
thread wakes up, drains the queue, and sends each event to the Collector.

---

## 4. CauseDeb's Sender Thread (annotated)

```cpp
// In Tracer constructor:
sender_thread_ = std::thread(&Tracer::sender_loop, this);

// The background thread:
void sender_loop() {
    while (running_.load() || !queue_empty()) {
        std::unique_lock<std::mutex> lk(queue_mutex_);
        queue_cv_.wait(lk, [this]{
            return !queue_.empty() || !running_.load();
        });
        while (!queue_.empty()) {
            TraceEvent e = std::move(queue_.front());
            queue_.pop();
            lk.unlock();      // release lock while doing I/O
            send_event(e);    // network send — may block briefly
            lk.lock();        // reacquire before checking queue again
        }
    }
}
```

Key points:
- The lock is released while `send_event` does network I/O, so the main
  thread is never blocked by a slow network.
- `running_.load()` is an `std::atomic<bool>` — reads/writes are safe without
  a mutex.
- The destructor sets `running_ = false`, notifies the CV, and joins the
  thread — ensuring all queued events are sent before shutdown.

---

## 5. `std::atomic` for Simple Flags

For a single boolean or integer that only needs increment/read, `std::atomic`
is cheaper than a mutex:

```cpp
std::atomic<bool> running{true};
running.store(false);            // signal stop
if (running.load()) { ... }      // check flag
```

---

## 6. Avoiding Deadlocks

- **Always lock mutexes in the same order** across all threads.
- **Never hold a mutex while calling code that might acquire another mutex.**
- Prefer `std::scoped_lock(mtx1, mtx2)` when you need two locks simultaneously
  — it uses deadlock-avoidance internally.

---

## Mini Project

Build a thread-safe `BoundedQueue<T>` with:
- A maximum capacity specified at construction
- `push(T)` that blocks when the queue is full
- `pop()` that blocks when the queue is empty
- A `shutdown()` method that unblocks all waiting threads

## Exercises

1. What is a spurious wakeup, and why must condition variable waits use a
   predicate loop?
2. What is the difference between `notify_one()` and `notify_all()`?
3. In CauseDeb's sender loop, why is the lock released before calling
   `send_event()`?
