// replay_engine.cpp — CauseDeb Replay Engine
//
// Reads a trace file, builds a causal graph using vector clock comparisons,
// performs a topological sort so every cause appears before its effect, and
// renders a step-by-step human-readable timeline.
//
// Usage:
//   causedeb_replay <trace_file>

#include <algorithm>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include "trace_io.h"

using namespace causedeb;

// ---------------------------------------------------------------------------
// Causal graph + topological sort (Kahn's algorithm)
// ---------------------------------------------------------------------------

// Build a dependency list: deps[i] = set of event indices that event i causally
// depends on (i.e. that happen-before event i).
static std::vector<std::vector<std::size_t>>
build_causal_graph(const std::vector<TraceEvent>& events) {
    std::size_t n = events.size();
    std::vector<std::vector<std::size_t>> deps(n);
    // For each pair (j, i) check if j happens-before i.
    // O(n²) — acceptable for debugging trace sizes.
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            if (i == j) continue;
            if (happens_before(events[j].vc, events[i].vc)) {
                deps[i].push_back(j);
            }
        }
    }
    return deps;
}

// Topological sort using Kahn's BFS algorithm.
// Returns events in causal order (every cause before its effects).
// Ties (concurrent events) are broken by wall-clock timestamp.
static std::vector<std::size_t>
topological_sort(const std::vector<TraceEvent>& events,
                 const std::vector<std::vector<std::size_t>>& deps) {
    std::size_t n = events.size();
    std::vector<std::size_t> in_degree(n, 0);

    // Build reverse adjacency: edges[j] = events that depend on j.
    std::vector<std::vector<std::size_t>> edges(n);
    for (std::size_t i = 0; i < n; ++i) {
        // Only keep direct (non-transitive) dependencies to avoid O(n³).
        // A dependency j→i is direct if there's no intermediate k such that
        // j < k < i in causal order (Hasse diagram reduction).
        // For simplicity here we use all dependencies; correctness is guaranteed.
        for (std::size_t j : deps[i]) {
            edges[j].push_back(i);
            in_degree[i]++;
        }
    }

    // Priority queue: prefer lower wall_ns to break ties among concurrent events.
    using Pair = std::pair<uint64_t, std::size_t>;  // (wall_ns, index)
    std::priority_queue<Pair, std::vector<Pair>, std::greater<Pair>> ready;

    for (std::size_t i = 0; i < n; ++i) {
        if (in_degree[i] == 0) ready.push({events[i].wall_ns, i});
    }

    std::vector<std::size_t> order;
    order.reserve(n);
    while (!ready.empty()) {
        auto [wall_ns_unused, u] = ready.top(); ready.pop();
        (void)wall_ns_unused;  // used only as priority-queue key, not in body
        order.push_back(u);
        for (std::size_t v : edges[u]) {
            if (--in_degree[v] == 0) ready.push({events[v].wall_ns, v});
        }
    }
    return order;
}

// ---------------------------------------------------------------------------
// Rendering helpers
// ---------------------------------------------------------------------------

static std::string ns_to_ms_str(uint64_t ns) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(3)
        << (static_cast<double>(ns) / 1e6) << "ms";
    return oss.str();
}

// Assign a display column to each service (stable order = service_names order).
static void render_timeline(const std::vector<TraceEvent>& events,
                            const std::vector<std::size_t>& order,
                            const std::vector<std::string>& service_names) {
    const int COL_WIDTH = 22;
    int num_svc = static_cast<int>(service_names.size());

    // Header
    std::cout << "\n=== CauseDeb Causal Replay Timeline ===\n\n";
    std::cout << std::left << std::setw(12) << "Step"
              << std::setw(14) << "Wall(ms)";
    for (const auto& s : service_names) {
        std::cout << std::setw(COL_WIDTH) << s;
    }
    std::cout << "VectorClock\n";
    std::cout << std::string(12 + 14 + num_svc * COL_WIDTH + 30, '-') << '\n';

    uint64_t base_ns = 0;
    if (!order.empty()) base_ns = events[order[0]].wall_ns;

    for (std::size_t step = 0; step < order.size(); ++step) {
        const TraceEvent& e = events[order[step]];
        uint64_t rel_ns = (e.wall_ns >= base_ns) ? e.wall_ns - base_ns : 0;

        // Find service column.
        int col = -1;
        for (int s = 0; s < num_svc; ++s) {
            if (service_names[static_cast<std::size_t>(s)] == e.service) { col = s; break; }
        }

        std::cout << std::left << std::setw(12) << (step + 1)
                  << std::setw(14) << ns_to_ms_str(rel_ns);
        for (int s = 0; s < num_svc; ++s) {
            if (s == col) {
                std::string cell = e.event_name;
                if (cell.size() > static_cast<std::size_t>(COL_WIDTH - 2))
                    cell = cell.substr(0, COL_WIDTH - 5) + "...";
                std::cout << std::setw(COL_WIDTH) << cell;
            } else {
                std::cout << std::setw(COL_WIDTH) << "|";
            }
        }
        std::cout << format_vc(e.vc);
        if (!e.metadata.empty()) std::cout << "  " << e.metadata;
        std::cout << '\n';
    }
    std::cout << '\n';
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: causedeb_replay <trace_file>\n";
        return 1;
    }

    TraceReader reader;
    if (!reader.load(argv[1])) {
        std::cerr << "Failed to load trace file: " << argv[1] << '\n';
        return 1;
    }

    std::cout << "Loaded " << reader.events.size() << " events from " << argv[1] << '\n';
    std::cout << "Services (" << reader.service_names.size() << "): ";
    for (std::size_t i = 0; i < reader.service_names.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << reader.service_names[i];
    }
    std::cout << '\n';

    if (reader.events.empty()) {
        std::cout << "No events to replay.\n";
        return 0;
    }

    std::cout << "Building causal graph...\n";
    auto deps  = build_causal_graph(reader.events);
    auto order = topological_sort(reader.events, deps);

    render_timeline(reader.events, order, reader.service_names);
    return 0;
}
