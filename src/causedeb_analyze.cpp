// causedeb_analyze.cpp — CauseDeb Trace Analyzer
//
// A command-line tool for querying a CauseDeb trace file.
// Supports:
//   --service  <name>        — filter events by service
//   --event    <name>        — filter events by event name (substring match)
//   --after    <wall_ms>     — only events at or after this wall-clock offset (ms)
//   --before   <wall_ms>     — only events at or before this wall-clock offset (ms)
//   --ancestors <event_idx>  — show all causal ancestors of event N (1-based)
//   --descendants <event_idx>— show all causal descendants of event N (1-based)
//   --latency  <from> <to>   — compute latency between two event indices (1-based)
//   --anomalies <threshold>  — flag causal edges whose latency exceeds threshold ms
//
// Usage:
//   causedeb_analyze <trace_file> [options]

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "trace_io.h"

using namespace causedeb;

// ---------------------------------------------------------------------------
// Filtering helpers
// ---------------------------------------------------------------------------

static bool matches_filters(const TraceEvent& e,
                             const std::string& svc_filter,
                             const std::string& evt_filter,
                             uint64_t after_ns,
                             uint64_t before_ns,
                             uint64_t base_ns) {
    if (!svc_filter.empty() && e.service != svc_filter) return false;
    if (!evt_filter.empty() && e.event_name.find(evt_filter) == std::string::npos) return false;
    uint64_t rel = (e.wall_ns >= base_ns) ? e.wall_ns - base_ns : 0;
    if (after_ns  && rel < after_ns)  return false;
    if (before_ns && rel > before_ns) return false;
    return true;
}

// ---------------------------------------------------------------------------
// Causal ancestry / descendancy
// ---------------------------------------------------------------------------

static std::set<std::size_t>
causal_ancestors(const std::vector<TraceEvent>& events, std::size_t target_idx) {
    std::set<std::size_t> result;
    const VectorClock& target_vc = events[target_idx].vc;
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (i == target_idx) continue;
        if (happens_before(events[i].vc, target_vc)) result.insert(i);
    }
    return result;
}

static std::set<std::size_t>
causal_descendants(const std::vector<TraceEvent>& events, std::size_t origin_idx) {
    std::set<std::size_t> result;
    const VectorClock& origin_vc = events[origin_idx].vc;
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (i == origin_idx) continue;
        if (happens_before(origin_vc, events[i].vc)) result.insert(i);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Anomaly detection: flag causal edges with high latency.
// ---------------------------------------------------------------------------
struct Anomaly {
    std::size_t from_idx;
    std::size_t to_idx;
    double      latency_ms;
};

static std::vector<Anomaly>
find_anomalies(const std::vector<TraceEvent>& events, double threshold_ms) {
    std::vector<Anomaly> results;
    std::size_t n = events.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            if (!happens_before(events[i].vc, events[j].vc)) continue;
            // Check direct causal edge: no k such that i hb k hb j
            bool direct = true;
            for (std::size_t k = 0; k < n; ++k) {
                if (k == i || k == j) continue;
                if (happens_before(events[i].vc, events[k].vc) &&
                    happens_before(events[k].vc, events[j].vc)) {
                    direct = false; break;
                }
            }
            if (!direct) continue;
            if (events[j].wall_ns < events[i].wall_ns) continue;
            double lat_ms = static_cast<double>(events[j].wall_ns - events[i].wall_ns) / 1e6;
            if (lat_ms >= threshold_ms) {
                results.push_back({i, j, lat_ms});
            }
        }
    }
    // Sort by latency descending.
    std::sort(results.begin(), results.end(),
              [](const Anomaly& a, const Anomaly& b){ return a.latency_ms > b.latency_ms; });
    return results;
}

// ---------------------------------------------------------------------------
// Print helpers
// ---------------------------------------------------------------------------

static void print_event(std::size_t idx, const TraceEvent& e, uint64_t base_ns) {
    uint64_t rel_ns = (e.wall_ns >= base_ns) ? e.wall_ns - base_ns : 0;
    double   rel_ms = static_cast<double>(rel_ns) / 1e6;
    std::cout << std::setw(5) << (idx + 1) << "  "
              << std::left << std::setw(20) << e.service
              << std::setw(35) << e.event_name
              << std::right << std::setw(10) << std::fixed << std::setprecision(3) << rel_ms << "ms"
              << "  " << format_vc(e.vc);
    if (!e.metadata.empty()) std::cout << "  [" << e.metadata << "]";
    std::cout << '\n';
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: causedeb_analyze <trace_file> [options]\n"
                  << "Options:\n"
                  << "  --service <name>          Filter by service name\n"
                  << "  --event <substr>          Filter by event name (substring)\n"
                  << "  --after <ms>              Events at or after offset (ms)\n"
                  << "  --before <ms>             Events at or before offset (ms)\n"
                  << "  --ancestors <N>           Causal ancestors of event N (1-based)\n"
                  << "  --descendants <N>         Causal descendants of event N (1-based)\n"
                  << "  --latency <from> <to>     Latency between two event indices\n"
                  << "  --anomalies <threshold>   Flag high-latency causal edges (ms)\n";
        return 1;
    }

    TraceReader reader;
    if (!reader.load(argv[1])) {
        std::cerr << "Failed to load trace: " << argv[1] << '\n';
        return 1;
    }

    auto& events = reader.events;
    if (events.empty()) {
        std::cout << "No events in trace.\n";
        return 0;
    }

    uint64_t base_ns = events[0].wall_ns;
    for (const auto& e : events) if (e.wall_ns < base_ns) base_ns = e.wall_ns;

    // Parse options
    std::string svc_filter, evt_filter;
    uint64_t after_ns  = 0, before_ns = 0;
    int      ancestors_of = -1, descendants_of = -1;
    int      latency_from = -1, latency_to = -1;
    double   anomaly_threshold = -1.0;

    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--service") == 0 && i + 1 < argc) {
            svc_filter = argv[++i];
        } else if (std::strcmp(argv[i], "--event") == 0 && i + 1 < argc) {
            evt_filter = argv[++i];
        } else if (std::strcmp(argv[i], "--after") == 0 && i + 1 < argc) {
            double v = std::stod(argv[++i]);
            if (v < 0) { std::cerr << "--after value must be non-negative\n"; return 1; }
            after_ns = static_cast<uint64_t>(v * 1e6);
        } else if (std::strcmp(argv[i], "--before") == 0 && i + 1 < argc) {
            double v = std::stod(argv[++i]);
            if (v < 0) { std::cerr << "--before value must be non-negative\n"; return 1; }
            before_ns = static_cast<uint64_t>(v * 1e6);
        } else if (std::strcmp(argv[i], "--ancestors") == 0 && i + 1 < argc) {
            ancestors_of = std::stoi(argv[++i]) - 1;
        } else if (std::strcmp(argv[i], "--descendants") == 0 && i + 1 < argc) {
            descendants_of = std::stoi(argv[++i]) - 1;
        } else if (std::strcmp(argv[i], "--latency") == 0 && i + 2 < argc) {
            latency_from = std::stoi(argv[++i]) - 1;
            latency_to   = std::stoi(argv[++i]) - 1;
        } else if (std::strcmp(argv[i], "--anomalies") == 0 && i + 1 < argc) {
            anomaly_threshold = std::stod(argv[++i]);
        }
    }

    // -----------------------------------------------------------------------
    // Ancestor query
    // -----------------------------------------------------------------------
    if (ancestors_of >= 0) {
        std::size_t idx = static_cast<std::size_t>(ancestors_of);
        if (idx >= events.size()) {
            std::cerr << "Event index out of range.\n"; return 1;
        }
        std::cout << "\nCausal ancestors of event " << (idx + 1)
                  << " (" << events[idx].service << " / " << events[idx].event_name << "):\n";
        auto anc = causal_ancestors(events, idx);
        std::cout << "  " << anc.size() << " ancestor(s)\n\n";
        for (std::size_t a : anc) print_event(a, events[a], base_ns);
        return 0;
    }

    // -----------------------------------------------------------------------
    // Descendant query
    // -----------------------------------------------------------------------
    if (descendants_of >= 0) {
        std::size_t idx = static_cast<std::size_t>(descendants_of);
        if (idx >= events.size()) {
            std::cerr << "Event index out of range.\n"; return 1;
        }
        std::cout << "\nCausal descendants of event " << (idx + 1)
                  << " (" << events[idx].service << " / " << events[idx].event_name << "):\n";
        auto desc = causal_descendants(events, idx);
        std::cout << "  " << desc.size() << " descendant(s)\n\n";
        for (std::size_t d : desc) print_event(d, events[d], base_ns);
        return 0;
    }

    // -----------------------------------------------------------------------
    // Latency between two events
    // -----------------------------------------------------------------------
    if (latency_from >= 0 && latency_to >= 0) {
        auto fi = static_cast<std::size_t>(latency_from);
        auto ti = static_cast<std::size_t>(latency_to);
        if (fi >= events.size() || ti >= events.size()) {
            std::cerr << "Event index out of range.\n"; return 1;
        }
        const TraceEvent& ef = events[fi];
        const TraceEvent& et = events[ti];
        if (!happens_before(ef.vc, et.vc)) {
            std::cout << "Event " << (fi + 1) << " does not causally precede event "
                      << (ti + 1) << " (they may be concurrent).\n";
        } else {
            double lat = static_cast<double>(et.wall_ns - ef.wall_ns) / 1e6;
            std::cout << "\nLatency from event " << (fi + 1) << " to event " << (ti + 1)
                      << ": " << std::fixed << std::setprecision(3) << lat << "ms\n";
            std::cout << "  From: " << ef.service << " / " << ef.event_name
                      << "  " << format_vc(ef.vc) << '\n';
            std::cout << "  To:   " << et.service << " / " << et.event_name
                      << "  " << format_vc(et.vc) << '\n';
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // Anomaly detection
    // -----------------------------------------------------------------------
    if (anomaly_threshold >= 0.0) {
        std::cout << "\nAnomalous causal edges (latency >= " << anomaly_threshold << "ms):\n\n";
        auto anomalies = find_anomalies(events, anomaly_threshold);
        if (anomalies.empty()) {
            std::cout << "  No anomalies found.\n";
        } else {
            for (const auto& a : anomalies) {
                const auto& ef = events[a.from_idx];
                const auto& et = events[a.to_idx];
                std::cout << std::fixed << std::setprecision(3) << a.latency_ms << "ms  "
                          << ef.service << "/" << ef.event_name
                          << "  →  "
                          << et.service << "/" << et.event_name << '\n';
            }
        }
        return 0;
    }

    // -----------------------------------------------------------------------
    // Default: list events with optional filters
    // -----------------------------------------------------------------------
    std::cout << "\nCauseDeb Trace: " << argv[1] << '\n';
    std::cout << "Total events: " << events.size() << '\n';
    if (!svc_filter.empty()) std::cout << "Filter service: " << svc_filter << '\n';
    if (!evt_filter.empty()) std::cout << "Filter event: "   << evt_filter << '\n';
    std::cout << '\n';
    std::cout << std::left
              << std::setw(5)  << "Idx"   << "  "
              << std::setw(20) << "Service"
              << std::setw(35) << "Event"
              << std::right << std::setw(10) << "Wall" << "   VectorClock\n";
    std::cout << std::string(90, '-') << '\n';

    for (std::size_t i = 0; i < events.size(); ++i) {
        if (!matches_filters(events[i], svc_filter, evt_filter, after_ns, before_ns, base_ns))
            continue;
        print_event(i, events[i], base_ns);
    }
    return 0;
}
