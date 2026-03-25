# Module 02 — C++ Systems Programming: Memory, RAII, and the STL

## Learning Objectives

- Understand stack vs heap memory and when to use each
- Apply RAII to manage resources safely
- Use key STL containers: `vector`, `map`, `queue`, `string`
- Understand move semantics and why they matter for performance
- Avoid common pitfalls: use-after-free, dangling references, memory leaks

---

## 1. Stack vs Heap

```cpp
// Stack allocation — automatic lifetime, very fast
int x = 42;
TraceEvent e;              // destroyed when the enclosing scope exits

// Heap allocation — manual lifetime, flexible
TraceEvent* p = new TraceEvent();   // AVOID raw new in modern C++
delete p;                           // easy to forget → memory leak

// Preferred: smart pointers manage lifetime automatically
#include <memory>
auto p2 = std::make_unique<TraceEvent>();  // freed when p2 goes out of scope
```

**Rule of thumb:** prefer stack allocation and value semantics. Use `unique_ptr`
or `shared_ptr` only when you genuinely need heap allocation.

---

## 2. RAII (Resource Acquisition Is Initialization)

RAII ties a resource's lifetime to an object's lifetime:

```cpp
class FileGuard {
public:
    explicit FileGuard(const std::string& path)
        : file_(path, std::ios::out) {
        if (!file_.is_open()) throw std::runtime_error("cannot open " + path);
    }
    ~FileGuard() { file_.close(); }   // guaranteed to run even on exception

    std::ofstream& get() { return file_; }
private:
    std::ofstream file_;
};
```

CauseDeb's `TraceWriter` follows this pattern. The destructor cleans up the
file handle regardless of how the function exits.

---

## 3. Essential STL Containers

### `std::vector<T>` — dynamic array

```cpp
std::vector<uint64_t> vc(3, 0);   // [0, 0, 0]
vc.push_back(1);                   // [0, 0, 0, 1]
vc[2] = 5;                         // [0, 0, 5, 1]
vc.size();                         // 4
```

### `std::map<K,V>` — sorted associative container

```cpp
std::map<std::string, std::size_t> service_index;
service_index["PaymentService"] = 0;
service_index["FraudService"]   = 1;
```

### `std::queue<T>` — FIFO buffer (used in the instrumentation library)

```cpp
std::queue<TraceEvent> q;
q.push(event1);
q.push(event2);
auto front = q.front(); q.pop();   // process in order
```

---

## 4. Move Semantics

Copying a large `std::string` or `std::vector` allocates a new buffer.
*Moving* transfers ownership in O(1) without copying:

```cpp
std::string big_string = "...many kilobytes...";
std::string moved = std::move(big_string);  // big_string is now empty; no copy
```

In CauseDeb's sender queue, events are moved — not copied — into the queue
to avoid unnecessary allocations on the hot path.

---

## 5. Common Pitfalls

| Pitfall | Symptom | Fix |
|---------|---------|-----|
| Use-after-free | Crash / garbage data | Use RAII / smart pointers |
| Iterator invalidation | Crash after `vector::push_back` | Take copies of iterators before modifying |
| Dangling reference | Undefined behaviour | Never return references to locals |
| Forgetting `virtual` destructor | Resource leak in polymorphic class | Add `virtual ~Base() = default;` |

---

## Mini Project

Implement a minimal `EventQueue` class that:
1. Holds a `std::queue<TraceEvent>` internally
2. Provides thread-safe `push` and `pop` methods using `std::mutex`
3. Destroys all remaining events cleanly in its destructor

## Exercises

1. What is the difference between `std::unique_ptr` and `std::shared_ptr`?
2. Why does `std::vector::push_back` sometimes invalidate existing iterators?
3. When does a move constructor leave the moved-from object in a valid but
   unspecified state?
