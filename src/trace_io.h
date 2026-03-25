#pragma once
// trace_io.h — Shared utilities for reading and writing CauseDeb trace files.
// Used by collector_server, replay_engine, and causedeb_analyze to guarantee
// format consistency.

#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace causedeb {

// ---------------------------------------------------------------------------
// Trace file format
// ---------------------------------------------------------------------------
// Line 1:  CAUSEDEB_TRACE v1
// Line 2:  SERVICES <n> <svc0> <svc1> ... <svcN-1>
// Subsequent lines (one per event):
//   <service>\t<event_name>\t<wall_ns>\t<mono_ns>\t<pid>\t<seq>\t<vc0>,<vc1>,...\t<metadata>
// Fields are TAB-separated. Metadata may be empty.
// ---------------------------------------------------------------------------

static constexpr const char* TRACE_MAGIC   = "CAUSEDEB_TRACE";
static constexpr const char* TRACE_VERSION = "v1";

// A vector clock is an array of uint64_t, one slot per participating service.
using VectorClock = std::vector<uint64_t>;

// One recorded event.
struct TraceEvent {
    std::string  service;      // originating service name
    std::string  event_name;   // descriptive event identifier
    uint64_t     wall_ns{0};   // wall-clock time (nanoseconds since epoch)
    uint64_t     mono_ns{0};   // monotonic time  (nanoseconds since boot)
    pid_t        pid{0};       // originating process ID
    uint64_t     seq{0};       // per-service sequence number
    VectorClock  vc;           // vector clock at event time
    std::string  metadata;     // optional key=value pairs, space-separated
};

// Serialise a single event to a tab-separated string (no trailing newline).
inline std::string serialize_event(const TraceEvent& e) {
    std::ostringstream oss;
    oss << e.service     << '\t'
        << e.event_name  << '\t'
        << e.wall_ns     << '\t'
        << e.mono_ns     << '\t'
        << e.pid         << '\t'
        << e.seq         << '\t';
    for (std::size_t i = 0; i < e.vc.size(); ++i) {
        if (i) oss << ',';
        oss << e.vc[i];
    }
    oss << '\t' << e.metadata;
    return oss.str();
}

// Parse a single line produced by serialize_event back into a TraceEvent.
// Returns false if the line is malformed.
inline bool deserialize_event(const std::string& line, TraceEvent& out) {
    std::istringstream iss(line);
    std::string field;
    std::vector<std::string> fields;
    while (std::getline(iss, field, '\t')) {
        fields.push_back(std::move(field));
    }
    if (fields.size() < 8) return false;

    out.service    = fields[0];
    out.event_name = fields[1];
    try {
        out.wall_ns = std::stoull(fields[2]);
        out.mono_ns = std::stoull(fields[3]);
        out.pid     = static_cast<pid_t>(std::stoul(fields[4]));
        out.seq     = std::stoull(fields[5]);
    } catch (...) {
        return false;
    }

    // Parse comma-separated vector clock
    out.vc.clear();
    std::istringstream vc_stream(fields[6]);
    std::string component;
    while (std::getline(vc_stream, component, ',')) {
        try {
            out.vc.push_back(std::stoull(component));
        } catch (...) {
            return false;
        }
    }
    out.metadata = fields[7];
    return true;
}

// ---------------------------------------------------------------------------
// TraceWriter — append events to a trace file.
// Not thread-safe on its own; the collector serialises access externally.
// ---------------------------------------------------------------------------
class TraceWriter {
public:
    explicit TraceWriter(const std::string& path)
        : path_(path) {}

    // Write the file header (call once, before any events).
    // service_names must list every participating service in index order.
    bool write_header(const std::vector<std::string>& service_names) {
        file_.open(path_, std::ios::out | std::ios::trunc);
        if (!file_.is_open()) return false;
        file_ << TRACE_MAGIC << ' ' << TRACE_VERSION << '\n';
        file_ << "SERVICES " << service_names.size();
        for (const auto& s : service_names) file_ << ' ' << s;
        file_ << '\n';
        file_.flush();
        return true;
    }

    // Append a single event line.
    bool append(const TraceEvent& e) {
        if (!file_.is_open()) {
            // Open in append mode if header was written externally
            file_.open(path_, std::ios::out | std::ios::app);
            if (!file_.is_open()) return false;
        }
        file_ << serialize_event(e) << '\n';
        file_.flush();
        return true;
    }

    void close() { file_.close(); }

private:
    std::string  path_;
    std::ofstream file_;
};

// ---------------------------------------------------------------------------
// TraceReader — read a complete trace file into memory.
// ---------------------------------------------------------------------------
class TraceReader {
public:
    // Maps service name -> index in the vector clock array.
    std::map<std::string, std::size_t> service_index;
    std::vector<std::string>           service_names;
    std::vector<TraceEvent>            events;

    bool load(const std::string& path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            std::cerr << "TraceReader: cannot open " << path << '\n';
            return false;
        }

        std::string line;

        // First line: magic
        if (!std::getline(file, line)) return false;
        {
            std::istringstream iss(line);
            std::string magic, version;
            iss >> magic >> version;
            if (magic != TRACE_MAGIC || version != TRACE_VERSION) {
                std::cerr << "TraceReader: unrecognised file header\n";
                return false;
            }
        }

        // Second line: SERVICES <n> <names...>
        if (!std::getline(file, line)) return false;
        {
            std::istringstream iss(line);
            std::string tag;
            std::size_t n;
            iss >> tag >> n;
            if (tag != "SERVICES") return false;
            service_names.resize(n);
            for (std::size_t i = 0; i < n; ++i) {
                iss >> service_names[i];
                service_index[service_names[i]] = i;
            }
        }

        // Remaining lines: events
        while (std::getline(file, line)) {
            if (line.empty() || line[0] == '#') continue;
            TraceEvent e;
            if (deserialize_event(line, e)) {
                events.push_back(std::move(e));
            } else {
                std::cerr << "TraceReader: skipping malformed line: " << line << '\n';
            }
        }
        return true;
    }
};

// ---------------------------------------------------------------------------
// Vector clock helpers
// ---------------------------------------------------------------------------

// Returns true if a happens-before b (a < b in the happens-before order).
// a must not equal b.
inline bool happens_before(const VectorClock& a, const VectorClock& b) {
    if (a.size() != b.size()) return false;
    bool strictly_less = false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i] > b[i]) return false;
        if (a[i] < b[i]) strictly_less = true;
    }
    return strictly_less;
}

// Returns true if the two clocks are concurrent (neither happens before the other).
inline bool concurrent(const VectorClock& a, const VectorClock& b) {
    return !happens_before(a, b) && !happens_before(b, a) && a != b;
}

// Component-wise maximum (merge) — mutates dst.
inline void merge_clocks(VectorClock& dst, const VectorClock& src) {
    if (dst.size() < src.size()) dst.resize(src.size(), 0);
    for (std::size_t i = 0; i < src.size(); ++i) {
        if (src[i] > dst[i]) dst[i] = src[i];
    }
}

// Format a vector clock for display, e.g. "[1,2,0]"
inline std::string format_vc(const VectorClock& vc) {
    std::string s = "[";
    for (std::size_t i = 0; i < vc.size(); ++i) {
        if (i) s += ',';
        s += std::to_string(vc[i]);
    }
    s += ']';
    return s;
}

} // namespace causedeb
