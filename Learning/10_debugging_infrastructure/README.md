# Module 10 — Debugging Infrastructure: Analysis Tools and Root Cause Workflows

## Learning Objectives

- Know how to use `causedeb_analyze` to query a trace
- Understand causal ancestor/descendant queries and what they reveal
- Compute inter-event latencies to find bottlenecks
- Use anomaly detection to flag suspicious causal edges automatically
- Follow a complete root cause investigation workflow

---

## 1. The Analysis Tool in Brief

`causedeb_analyze <trace_file> [options]`

| Option | Purpose |
|--------|---------|
| (no options) | List all events with service, name, timestamp, VC |
| `--service PaymentService` | Show only events from PaymentService |
| `--event fraud` | Show events whose name contains "fraud" |
| `--after 100 --before 5000` | Show events between 100ms and 5000ms |
| `--ancestors 42` | Show all causal ancestors of event 42 |
| `--descendants 5` | Show all causal descendants of event 5 |
| `--latency 3 18` | Compute latency between events 3 and 18 |
| `--anomalies 500` | Flag causal edges with latency ≥ 500ms |

---

## 2. The Root Cause Workflow

This is the standard CauseDeb debugging procedure:

### Step 1 — Identify the failure event

```bash
causedeb_analyze trace.log --event timeout
```

Find the event that represents the observable failure (a timeout, an error
response, a crash). Note its event index (e.g., event 47).

### Step 2 — Find what caused it

```bash
causedeb_analyze trace.log --ancestors 47
```

This prints every event in the trace that causally preceded event 47 —
the complete causal history of the failure.

### Step 3 — Identify the bottleneck

```bash
causedeb_analyze trace.log --anomalies 200
```

This flags every direct causal edge (A immediately caused B) where the
wall-clock latency exceeded 200ms. The event with the largest latency is
your prime suspect.

### Step 4 — Measure the bottleneck

```bash
causedeb_analyze trace.log --latency 12 15
```

Compute the exact latency between two specific events (e.g., between
`inventory_check_started` and `inventory_check_complete`).

---

## 3. Example Root Cause Investigation

**Report:** *Order ORD-942 was charged but never fulfilled.*

```bash
# Step 1: find the failure event
causedeb_analyze trace.log --event fulfillment_timeout
# Output: event 44 at 14,200ms — OrderService/fulfillment_timeout

# Step 2: trace causes
causedeb_analyze trace.log --ancestors 44
# Output shows a chain ending with a slow inventory check

# Step 3: flag anomalies
causedeb_analyze trace.log --anomalies 500
# Output:
#  2285.000ms  InventoryService/inventory_check_started  →  InventoryService/inventory_check_complete

# Step 4: confirm
causedeb_analyze trace.log --latency 8 11
# Output: Latency from event 8 to event 11: 2285.000ms
```

Root cause: InventoryService took 2,285ms to complete an inventory check.
This delayed the payment request, which then missed its deadline, causing
the fulfillment timeout.

---

## 4. Reading the Trace Directly

Because the trace is plain text, you can also use standard Unix tools:

```bash
# Show all events from FraudService
grep '^FraudService' trace.log

# Show all events after the file header
awk 'NR > 2' trace.log

# Count events per service
awk 'NR > 2 {print $1}' trace.log | sort | uniq -c | sort -rn

# Show only payment-related events
grep -E 'payment|order' trace.log
```

---

## 5. Extending the Toolchain

Because the trace format is stable and documented, you can write your own
tools using `trace_io.h`:

```cpp
#include "trace_io.h"

int main() {
    causedeb::TraceReader reader;
    reader.load("trace.log");

    for (auto& e : reader.events) {
        // Your custom analysis here
        if (e.service == "FraudService") {
            std::cout << e.event_name << " at VC=" << causedeb::format_vc(e.vc) << '\n';
        }
    }
}
```

---

## 6. Operational Checklist

Use this checklist for every distributed system bug investigation:

- [ ] Collect the trace file from the Collector
- [ ] Run `causedeb_replay` to get the full causal timeline
- [ ] Identify the failure event (the last observable symptom)
- [ ] Run `--ancestors` to see what caused it
- [ ] Run `--anomalies` to find the slowest causal transitions
- [ ] Use `--latency` to measure specific bottlenecks precisely
- [ ] Identify the root cause (the first slow or erroneous event in the chain)
- [ ] File a bug with the exact event sequence and latency measurements

---

## Mini Project

Using the example trace data below (or generate your own with three services):
1. Load it with `TraceReader`
2. Implement a function that prints the "critical path" — the longest causal
   chain by wall-clock latency
3. Verify your critical path matches the output of `--ancestors` on the final
   event

## Exercises

1. Why does finding the *ancestors* of a failure event reveal the root cause
   more reliably than looking at the raw timeline?
2. What is the difference between a causal descendant and a causal successor
   (direct child in the Hasse diagram)?
3. If `--anomalies` reports no anomalies but you still have a bug, what
   other queries would you run?
