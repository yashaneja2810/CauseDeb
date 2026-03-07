# CauseDeb — Distributed Deterministic Replay Debugger

> **Causality + Debugging** — Record every event across every service. Replay the exact execution that caused your bug.

---

## The Problem

Imagine your payment system processes thousands of orders per day. One morning, a customer's order gets stuck — the payment was charged but the order never confirmed. You look at the logs, restart the services, and try to reproduce the issue. It never happens again.

This is the most frustrating class of bug in distributed systems: **the Heisenbug** — it disappears the moment you look for it.

The root cause is the nature of distributed execution itself. Three or more services run simultaneously on different machines. Messages travel over a network. The order in which those messages arrive, the timing of thread switches, and the load on each machine all vary from run to run. The exact combination of events that caused the bug will almost certainly never repeat in the same way.

Traditional logging does not solve this. Even when every service writes detailed logs, those logs sit on different machines with different clocks. Wall-clock timestamps cannot be trusted across machines — clocks drift, network latency varies, and two events stamped at the "same millisecond" on different servers may still have a strict causal relationship that the timestamps completely hide.

**CauseDeb was built to solve this.**

---

## What Is CauseDeb?

CauseDeb (**Cause**lity + **Deb**ugger) is a **distributed deterministic replay debugging system** written in C++17 on Linux.

It gives engineers the ability to record a distributed execution in full detail, then replay that exact execution later — reproducing the bug on demand, every time, regardless of how rarely it originally occurred.

CauseDeb is built around one core insight:

> *Wall-clock time is unreliable across machines. Causal ordering — which event caused which — is not.*

By attaching a **vector clock** to every event, CauseDeb captures the causal structure of the entire execution. During replay, it uses that causal structure — not timestamps — to reconstruct the correct event ordering. The result is a faithful, deterministic replay of exactly what happened.

---

## How It Works — The Three Phases

### Phase 1 — Record

Every service includes the CauseDeb instrumentation library. Whenever something noteworthy happens — a request received, a decision made, a message sent — the service records that event and forwards it to a central **Collector** process over TCP.

The Collector is a multi-threaded TCP server that accepts connections from all services simultaneously. It deserializes each incoming event and appends it to a **trace file** on disk — a plain text record of the complete history of everything every service did, in the order events arrived at the Collector.

Each recorded event carries: who generated it (service name and process ID), what happened (a descriptive event name like `fraud_check_started`), when it happened (wall-clock timestamp, for human readability only), and most importantly, **its causal position** expressed as a vector clock.

### Phase 2 — Replay

After a bug is observed, an engineer runs the **replay engine** against the saved trace file.

The engine reads all events, builds a causal graph by comparing vector clocks, and sorts events into a topological order where every cause appears before its effect. It then renders this as a human-readable timeline showing which event triggered which, across which services, in the exact causal sequence that produced the bug.

### Phase 3 — Analysis

For deeper investigation, the **analysis tool** lets engineers query the trace like a database. They can ask: *"What events causally preceded this timeout?"*, *"How long did the fraud check take?"*, or *"Show me only what NotifyService did."* The tool reconstructs causal chains and computes latencies between causally related events — turning a wall of log lines into a structured root cause investigation.

---

## The Role of Vector Clocks

The core mechanism in CauseDeb is the **vector clock** — a small array of integers, one entry per participating service.

Each service maintains its own vector clock and follows three rules:

- **On any local event:** increment your own entry
- **When sending a message:** attach your current vector clock to the message
- **When receiving a message:** take the component-wise maximum of your clock and the sender's clock, then increment your own entry

The result is a clock that accumulates causal history. If ServiceA sends a message to ServiceB, then every event at ServiceB after that receive has a vector clock that is mathematically "greater than" ServiceA's clock at the moment of sending. This relationship holds transitively through any chain of communication — no matter how many services the message passed through.

**Example:** PaymentService starts an order (vector clock: `[1,0,0]`). It sends a fraud check request — attaching `[2,0,0]` to the message. FraudService receives this, merges and increments its own clock to `[2,1,0]`, and records `fraud_check_started`. Even if the network was congested that day and the message arrived 500ms late, the vector clocks still correctly record that `fraud_check_started` causally followed `fraud_check_requested`. Every event downstream of the fraud check inherits this causal ordering.

No wall clock needs to be trusted. No network timing needs to be consistent. Causality is preserved exactly.

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                        PRODUCTION SYSTEM                             │
│                                                                      │
│   PaymentService   ──────┐                                          │
│   FraudService     ──────┼──► CauseDeb Collector ──► trace.log     │
│   NotifyService    ──────┘       (TCP port 9000)                    │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
                                        │
                    ┌───────────────────┘
                    ▼
         causedeb_replay trace.log      ← visualize causal sequence
         causedeb_analyze trace.log     ← query and find root cause
```

The design is intentionally minimal: services connect to one Collector, the Collector writes one file, and standalone tools read that file. There is no distributed infrastructure in the debugger itself. When your system is broken, the last thing you want is a complex debugging system that can also break.

---

## Project Modules

| Module | Purpose |
|--------|---------|
| **Instrumentation Library** (`causedeb.h`) | A single header file that any service includes to participate in CauseDeb. Maintains the service's vector clock, serializes events, and ships them to the Collector on a background thread — never blocking the service. |
| **Collector Server** (`collector_server.cpp`) | A multi-threaded TCP server. Accepts simultaneous connections from all services, reads length-prefixed event messages, and appends them to the trace file with durability guarantees. |
| **Replay Engine** (`replay_engine.cpp`) | Reads the trace, builds the causal graph via vector clock comparisons, topologically sorts all events, and renders a step-by-step timeline showing the causal chain of the recorded execution. |
| **Trace Analyzer** (`causedeb_analyze.cpp`) | A command-line tool for querying the trace. Supports filtering by service, event type, and time range; causal ancestor and descendant lookup; inter-event latency computation; and automatic anomaly detection. |
| **Trace I/O Library** (`trace_io.h`) | Shared utilities for reading and writing the trace file format, used by the Collector and all tools to guarantee format consistency. |

---

## A Real Debugging Session — Example

An engineer receives a report: *"Order ORD-942 was charged but never fulfilled — the customer got an error page."*

**Step 1 — Find the failure event.** The engineer queries the trace for anything related to `ORD-942` and finds a `fulfillment_timeout` event in OrderService at 14,200ms.

**Step 2 — Reconstruct the causal chain.** They ask: *"What events causally led to this timeout?"* The tool walks backward through the vector clocks and prints the ancestral chain:

```
t=100ms    OrderService      order_received
t=110ms    OrderService      inventory_check_requested
t=115ms    InventoryService  inventory_check_started
t=2,400ms  InventoryService  inventory_check_complete    ← 2,285ms gap!
t=2,405ms  OrderService      inventory_result_received
t=2,410ms  OrderService      payment_requested
t=14,000ms OrderService      payment_timeout
t=14,200ms OrderService      fulfillment_timeout
```

**Step 3 — Identify the root cause.** The gap between `inventory_check_started` and `inventory_check_complete` is 2,285ms — far beyond normal. Every event downstream inherited this delay, eventually causing the payment request to miss its deadline.

Without CauseDeb, this looked like a payment timeout. With CauseDeb, the root cause — a slow inventory check — is immediately visible in the causal chain.

---

## Trace File Design

The trace file is stored as plain tab-separated text. This is a deliberate design choice:

- Any engineer can open it immediately in any text editor
- Standard Unix tools like `grep`, `awk`, and `sort` work on it directly without any special parser
- It is portable and stable — no database engine, no binary decoder, no version-locked format
- It survives indefinitely — readable by any future tool regardless of how CauseDeb itself evolves

Each line in the trace represents one event, with fields for the service name, event type, wall-clock timestamp, monotonic timestamp, vector clock, sequence number, process ID, and optional metadata. A header at the top of the file maps each service to its position in the vector clock array — this mapping is the key that makes all causal comparisons possible.

---

## Design Philosophy

**Minimal overhead.** The instrumentation library never blocks a service. Events are queued internally and forwarded to the Collector on a dedicated background thread. If the network is momentarily slow, events buffer locally. The service itself is unaffected.

**No trust in clocks.** Wall-clock timestamps in the trace serve only as human-readable context. All causal reasoning, all replay ordering, and all root cause analysis operates exclusively on vector clocks. A machine with a misconfigured system clock does not corrupt the analysis.

**Plain data over clever infrastructure.** The trace file is the contract between all components. It is text. It is stable. Any tool — including tools not yet written — can read it without a special library, a running server, or a specific version of CauseDeb.

---

## Learning Curriculum

The `Learning/` directory contains a complete 10-module self-study curriculum that teaches every concept needed to understand and build CauseDeb — starting from Linux basics and building up through distributed systems theory.

| Module | Topic |
|--------|-------|
| 01 | Linux environment setup and development tools |
| 02 | C++ systems programming — memory, RAII, STL |
| 03 | C++ concurrency — threads, mutexes, condition variables |
| 04 | Linux POSIX APIs — files, processes, signals, clocks |
| 05 | TCP networking and socket programming |
| 06 | Distributed systems theory — CAP theorem, failure models, clocks |
| 07 | Vector clocks — the heart of causal ordering |
| 08 | Distributed logging and the instrumentation library |
| 09 | Deterministic replay — topological sort and replay engines |
| 10 | Debugging infrastructure — analysis tools and root cause workflows |

Each module includes plain-language explanations, worked examples with diagrams, a mini project, and practice exercises. No prior distributed systems knowledge is assumed.

---

## Tech Stack

- **Language:** C++17 — for precise control over memory, threads, and system calls
- **OS:** Linux (Ubuntu 22.04)
- **Transport:** Raw TCP sockets via POSIX APIs — no middleware or message brokers
- **Causality mechanism:** Vector clocks
- **Storage:** Plain-text tab-separated trace files
- **Build system:** GNU Make / CMake
- **External dependencies:** None

---

## License

MIT License — see [LICENSE](LICENSE) for details.
