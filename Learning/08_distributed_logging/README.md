# Module 08 — Distributed Logging and the Instrumentation Library

## Learning Objectives

- Understand the design goals of CauseDeb's instrumentation library
- Know how `causedeb.h` maintains and updates vector clocks
- Understand the non-blocking event queue and background sender thread
- Know how to instrument a real service with CauseDeb

---

## 1. Design Goals

The instrumentation library must:
1. **Never block** the service — logging must have near-zero latency impact
2. **Maintain a correct vector clock** — always up to date with the latest
   local and received events
3. **Deliver events reliably** — buffer internally if the network is slow
4. **Be easy to use** — one header file, a few simple calls

---

## 2. Architecture of `causedeb.h`

```
Service thread                    Sender thread
──────────────                    ─────────────
tracer.record("order_received")
  → increment VC[self]
  → snapshot VC
  → push TraceEvent to queue
  → notify condition variable   ──► wake up
                                    pop event from queue
                                    serialize_event(e)
                                    send to Collector (TCP)
```

The critical insight: **the queue decouples the service from the network.**
Even if the Collector is briefly unreachable or the network is congested,
the service continues at full speed. Events buffer in memory and are sent
when the connection is restored.

---

## 3. Vector Clock Maintenance in `record()`

```cpp
void record(const std::string& event_name, const std::string& metadata = "") {
    TraceEvent e;
    {
        std::lock_guard<std::mutex> lk(vc_mutex_);
        vc_[service_index_]++;   // Rule 1: increment own entry on local event
        e.vc  = vc_;             // snapshot full clock
        e.seq = vc_[service_index_];
    }
    e.service    = service_name_;
    e.event_name = event_name;
    e.wall_ns    = wall_ns_now();
    e.mono_ns    = mono_ns_now();
    e.pid        = ::getpid();
    e.metadata   = metadata;
    enqueue(std::move(e));
}
```

Note: the mutex protects the vector clock, but is released before enqueueing.
This keeps the critical section tiny.

---

## 4. Sending Messages Between Services

When ServiceA sends a message to ServiceB, it must piggyback its current
vector clock on the outgoing message so ServiceB can maintain causal ordering.

```cpp
// In ServiceA, before sending the message:
VectorClock vc = tracer.send_message("FraudService");
// Serialise vc into your message (e.g., as a header field)
// ... send the message with vc attached ...
```

```cpp
// In ServiceB, after receiving the message:
VectorClock sender_vc = /* deserialise from message */;
tracer.receive_message(sender_vc, "PaymentService");
// Now continue processing — the VC is correctly merged
```

---

## 5. Instrumenting a Real Service

Suppose you have an existing HTTP service. Add CauseDeb in three steps:

**Step 1 — Add the header and create a global Tracer:**
```cpp
#include "causedeb.h"

// At global or thread-local scope (one per service process):
causedeb::Tracer tracer("OrderService", 0, "collector.internal", 9000);
tracer.set_num_services(3);   // 3 services total: index 0, 1, 2
```

**Step 2 — Record events at interesting points:**
```cpp
void handle_order(const Request& req) {
    tracer.record("order_received", "order_id=" + req.order_id);

    auto result = call_inventory_service(req);  // outgoing call
    VectorClock vc = tracer.send_message("InventoryService");
    // Attach vc to the outgoing RPC headers

    tracer.record("inventory_check_requested", "order_id=" + req.order_id);
}
```

**Step 3 — Propagate the clock:**
```cpp
void on_inventory_response(const Response& resp) {
    VectorClock peer_vc = /* from RPC response headers */;
    tracer.receive_message(peer_vc, "InventoryService");
    tracer.record("inventory_result_received");
}
```

---

## 6. What to Record

Good events to record:
- Request received / response sent
- Outgoing calls to other services
- Important decisions (e.g., "fraud_check_passed", "payment_declined")
- Slow operations start/end (to compute latencies)
- Errors and retries

Avoid recording every line of code — the trace should capture the causal
skeleton of the execution, not a full instruction trace.

---

## Mini Project

Write a small simulation with two "services" as threads that exchange
messages using channels (e.g., `std::queue` protected by a mutex).
Instrument each thread with a `Tracer` and verify that the vector clocks
in the recorded events correctly reflect the causal order.

## Exercises

1. Why must the vector clock be incremented before snapshotting it for the
   event record, not after?
2. What would happen if the instrumentation library used a synchronous
   (blocking) send instead of the background queue?
3. How should you handle the case where a service restarts mid-trace?
