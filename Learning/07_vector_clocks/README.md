# Module 07 — Vector Clocks: The Heart of Causal Ordering

## Learning Objectives

- Understand the happens-before (→) relation formally
- Know the three rules of vector clock maintenance
- Determine causal relationships from vector clocks: precedes, concurrent, equal
- Understand why vector clocks are the right mechanism for CauseDeb
- Read and write the CauseDeb `VectorClock` implementation

---

## 1. The Happens-Before Relation

Lamport (1978) defined happens-before (→) with three rules:
1. If A and B are events in the same process and A occurs before B, then A → B
2. If A is the send of a message and B is the receipt of that message, then A → B
3. If A → B and B → C, then A → C (transitivity)

If neither A → B nor B → A, then A and B are **concurrent** (written A ∥ B).

---

## 2. Vector Clock Rules

A vector clock for a system of *n* services is an array of *n* integers.
Service *i* maintains `VC[i]`.

**Rule 1 — Local event:** Increment own entry.
```
VC[i]++
```

**Rule 2 — Send:** Increment own entry, attach full vector clock to message.
```
VC[i]++
send(message, VC)
```

**Rule 3 — Receive:** Take component-wise maximum, then increment own entry.
```
VC = component_max(VC, received_VC)
VC[i]++
```

---

## 3. Worked Example

System: PaymentService (P, index 0), FraudService (F, index 1), NotifyService (N, index 2).

```
Initial state: P=[0,0,0]  F=[0,0,0]  N=[0,0,0]

1. P: local event "order_received"
   P.VC[0]++  →  P=[1,0,0]
   Record event with VC=[1,0,0]

2. P: send fraud_check_request to F
   P.VC[0]++  →  P=[2,0,0]
   Message carries VC=[2,0,0]

3. F: receive fraud_check_request from P
   F.VC = max([0,0,0], [2,0,0]) = [2,0,0]
   F.VC[1]++  →  F=[2,1,0]
   Record event "fraud_check_started" with VC=[2,1,0]

4. F: local event "fraud_score_computed"
   F.VC[1]++  →  F=[2,2,0]
   Record event with VC=[2,2,0]

5. F: send fraud_result to P
   F.VC[1]++  →  F=[2,3,0]
   Message carries VC=[2,3,0]

6. P: receive fraud_result from F
   P.VC = max([2,0,0], [2,3,0]) = [2,3,0]
   P.VC[0]++  →  P=[3,3,0]
   Record event "payment_authorised" with VC=[3,3,0]
```

---

## 4. Comparing Vector Clocks

Given two vector clocks A and B of the same length:

| Condition | Meaning |
|-----------|---------|
| A == B | Same event |
| ∀i: A[i] ≤ B[i] and ∃i: A[i] < B[i] | A → B (A happens-before B) |
| ∀i: B[i] ≤ A[i] and ∃i: B[i] < A[i] | B → A (B happens-before A) |
| Otherwise | A ∥ B (concurrent) |

CauseDeb's `happens_before(a, b)` function implements this exactly.

---

## 5. CauseDeb Implementation

```cpp
// Returns true if a happens-before b.
inline bool happens_before(const VectorClock& a, const VectorClock& b) {
    if (a.size() != b.size()) return false;
    bool strictly_less = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] > b[i]) return false;         // a is NOT ≤ b in component i
        if (a[i] < b[i]) strictly_less = true; // strict inequality found
    }
    return strictly_less;
}
```

---

## 6. What Vector Clocks Cannot Do

- **Size grows with the number of services.** A system with 1000 services
  needs 1000-entry vector clocks. For very large systems, alternative
  structures (plausible clocks, interval tree clocks) exist.
- **They don't identify which specific message caused a relationship.**
  They only answer: "did A happen before B?"
- **Dynamic membership** (services joining mid-run) requires extending all
  existing clocks, which is non-trivial in a live system.

---

## Mini Project

On paper, trace through a scenario with three services:
1. Service A sends a message to B
2. B sends a message to C
3. Independently, A sends another message directly to C
4. Compare the vector clocks of the two events at C — are they concurrent
   or causally related?

## Exercises

1. If `VC_A = [3, 2, 0]` and `VC_B = [2, 3, 0]`, what is their relationship?
2. If Service A processes an event with `VC = [5, 0, 0]` and then sends a
   message to Service B (currently at `[0, 4, 0]`), what is Service B's
   vector clock after receiving the message?
3. Why is the component-wise maximum used when merging clocks?
