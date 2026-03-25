# Module 06 — Distributed Systems Theory: CAP Theorem, Failure Models, Clocks

## Learning Objectives

- Understand the fundamental challenges of distributed systems
- Know the CAP theorem and its practical implications
- Understand common failure models (crash, network partition, Byzantine)
- Understand why wall-clock time cannot be trusted across machines
- Know the alternatives: Lamport timestamps and vector clocks

---

## 1. What Makes Distributed Systems Hard?

A single-machine programme has one CPU, one clock, and shared memory. You
can reason about the order of operations because there is only one timeline.

A distributed system has:
- **Multiple machines** — each with its own CPU and clock
- **A network** — unreliable, with variable and unknown latency
- **No shared memory** — coordination requires message passing
- **Partial failures** — one machine can fail while others keep running

These properties make distributed systems fundamentally different from local
programming.

---

## 2. The CAP Theorem

A distributed system can guarantee at most **two** of:
- **C**onsistency — all nodes see the same data at the same time
- **A**vailability — every request receives a response (not an error)
- **P**artition tolerance — the system continues operating despite dropped
  network messages

In practice, network partitions are unavoidable (a cable can be cut), so
real systems choose between CP (consistent but may refuse requests) and AP
(always respond, possibly with stale data).

CauseDeb is an observability tool, not a data store — it doesn't need to
take a stance on CAP. But the systems it observes do, and understanding CAP
helps you interpret the bugs you find in their traces.

---

## 3. Failure Models

| Model | Description | Example |
|-------|-------------|---------|
| Crash-stop | Process halts and never recovers | OOM kill |
| Crash-recovery | Process crashes but can restart and recover state | Service restart after crash |
| Network omission | Messages are dropped or delayed | Packet loss, router failure |
| Network partition | Two groups cannot reach each other | Cable cut |
| Byzantine | Process behaves arbitrarily (lies, corrupts data) | Security attack, hardware fault |

Most distributed systems assume crash-recovery and network omission. CauseDeb
is designed for this model.

---

## 4. The Clock Problem

Suppose ServiceA sends a message at wall time 10:00:00.500, and ServiceB
receives it at wall time 10:00:00.490. This is impossible — the receive cannot
precede the send — but it happens routinely because:

- **Clock drift**: computer clocks run at slightly different rates
- **NTP corrections**: NTP can jump a clock backward to correct drift
- **Network latency**: messages take non-zero time to travel

The maximum typical drift without NTP correction is ~1 second per day.
With NTP, it can still be tens of milliseconds.

**Conclusion:** wall-clock comparisons across machines are unreliable.

---

## 5. Lamport Timestamps

Leslie Lamport (1978) proposed a simple logical clock:
1. Each process maintains a counter, initially 0
2. Before any event: increment counter
3. When sending a message: attach the current counter
4. When receiving a message: `counter = max(local, received) + 1`

Lamport clocks give a **total order** consistent with the "happens-before"
relation, but they lose information: if `ts(A) < ts(B)`, it does not
necessarily mean A happened before B.

---

## 6. Why Lamport Timestamps Are Not Enough

```
ServiceA: send(ts=5)
ServiceB: receive(ts=6)  ← we know A → B
ServiceC: local event(ts=7)
```

Did ServiceC's event happen after ServiceB's receive? We cannot tell from
Lamport timestamps alone — ServiceC's clock might have been running fast.

Vector clocks solve this.

---

## Mini Project

Simulate three processes exchanging messages on paper (or in code):
1. Use Lamport timestamps — show a scenario where two events have the same
   timestamp.
2. Use vector clocks — verify that the happens-before relationship is
   captured correctly.

## Exercises

1. Can Lamport timestamps be used to detect concurrent events?
2. A system stores data with Lamport timestamps and wants to answer: "did
   event A happen before event B?" What information is missing?
3. Name two real distributed databases and their CAP choices.
