# Module 09 — Deterministic Replay: Topological Sort and Replay Engines

## Learning Objectives

- Understand what deterministic replay means and why it matters
- Know how to build a causal graph from a set of vector-clocked events
- Implement topological sort (Kahn's algorithm) on a causal graph
- Understand tie-breaking with wall-clock timestamps
- Read and extend the CauseDeb `replay_engine.cpp`

---

## 1. What Is Deterministic Replay?

Imagine recording a concert and being able to replay it perfectly — every
note in exactly the right order, at exactly the right time relative to every
other note.

Deterministic replay for distributed systems means: given a trace of all
events, reconstruct the exact execution order that produced an observed bug.

The challenge is that the trace was collected on multiple machines with
unreliable clocks. We cannot sort events by wall-clock timestamp. We must
use causal ordering — the only reliable ordering we have.

---

## 2. Building the Causal Graph

The causal graph is a directed acyclic graph (DAG) where:
- **Nodes** are events
- **Edges** point from cause to effect: an edge A → B means "A happened
  before B"

To build it from vector clocks:
```
for each pair of events (i, j):
    if happens_before(VC[i], VC[j]):
        add edge i → j
```

This is O(n²) in the number of events. For a typical debugging trace of a
few thousand events, this is fast. For very large traces, optimisations exist
(e.g., process events per-service in sequence, then merge).

---

## 3. Topological Sort

A topological sort of a DAG produces a linear order where every cause appears
before all its effects. It is a fundamental algorithm in distributed systems.

**Kahn's Algorithm:**
1. Compute in-degree (number of incoming edges) for each node
2. Initialise a queue with all nodes of in-degree 0 (no causes yet seen)
3. While the queue is non-empty:
   a. Remove a node u from the queue — this is the next event in the replay
   b. For each successor v of u: decrement `in_degree[v]`; if now 0, add v to queue
4. If all nodes were processed, the result is a valid topological order
   (otherwise the graph has a cycle — which would be a bug in the VC logic)

---

## 4. Tie-Breaking with Wall-Clock Timestamps

When two events are concurrent (neither happens before the other), both may
be added to the queue simultaneously. We break the tie by preferring the
event with the earlier wall-clock timestamp.

This is not required for correctness — any valid topological order is a
legitimate replay. But it produces a more intuitive timeline for the engineer.

In CauseDeb's replay engine:
```cpp
// Priority queue with smallest wall_ns first
using Pair = std::pair<uint64_t, std::size_t>;
std::priority_queue<Pair, std::vector<Pair>, std::greater<Pair>> ready;
```

---

## 5. The Replay Timeline

After sorting, events are rendered as a timeline showing:
- Step number (causal order)
- Wall-clock offset from the first event (in milliseconds)
- Which service produced the event (shown in its column)
- The full vector clock

```
Step  Wall(ms)      PaymentService        FraudService          VectorClock
───────────────────────────────────────────────────────────────────────────
1     0.000ms       order_received        |                      [1,0,0]
2     0.100ms       fraud_requested       |                      [2,0,0]
3     0.115ms       |                     fraud_check_started    [2,1,0]
4     2.400ms       |                     fraud_check_complete   [2,2,0]
5     2.405ms       fraud_result_received |                      [3,2,0]
```

The column layout immediately shows which service was responsible for each
step, and the vector clocks show the causal structure at a glance.

---

## 6. Detecting Cycles (Sanity Check)

If the topological sort does not process all events, the causal graph
contains a cycle — meaning two events each claim to happen before the other.
This indicates a bug in the vector clock logic.

```cpp
if (order.size() != events.size()) {
    std::cerr << "ERROR: causal graph has a cycle — vector clock logic is broken\n";
}
```

---

## Mini Project

Implement Kahn's algorithm from scratch:
1. Build an adjacency list from a set of `(from, to)` dependency pairs
2. Compute in-degrees
3. Process nodes in order using a min-heap keyed by a "priority" value
4. Detect and report any cycles

## Exercises

1. What does it mean if the causal graph has a cycle?
2. Two events A and B are concurrent. A valid topological order places B
   before A. Is this still a "correct" replay? Why?
3. How does the replay engine determine that one event "directly" caused
   another (rather than being a transitive cause)?
