#pragma once
// causedeb.h — CauseDeb instrumentation library (single-header).
//
// Include this header in any service that should participate in CauseDeb
// distributed tracing.  Call CauseDeb::init() once at startup, then call
// CauseDeb::record_event() wherever something noteworthy happens.
//
// Events are queued internally and forwarded to the Collector on a dedicated
// background thread — the calling service is never blocked.
//
// Example:
//   causedeb::Tracer tracer("PaymentService", 0, "127.0.0.1", 9000);
//   tracer.record("order_received");
//   tracer.record("payment_requested", "order_id=ORD-942");
//   tracer.send_message("FraudService");   // piggybacks current VC on message
//   tracer.receive_message(peer_vc);       // merge incoming VC

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <netdb.h>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "trace_io.h"

namespace causedeb {

// ---------------------------------------------------------------------------
// Tracer — one instance per service process.
// ---------------------------------------------------------------------------
class Tracer {
public:
    // service_name  — unique name for this service (e.g. "PaymentService")
    // service_index — position of this service in the shared vector clock
    // collector_host/port — where the Collector is running
    Tracer(std::string service_name,
           std::size_t service_index,
           std::string collector_host = "127.0.0.1",
           uint16_t    collector_port = 9000)
        : service_name_(std::move(service_name))
        , service_index_(service_index)
        , collector_host_(std::move(collector_host))
        , collector_port_(collector_port)
    {
        connect_to_collector();
        sender_thread_ = std::thread(&Tracer::sender_loop, this);
    }

    ~Tracer() {
        running_.store(false);
        queue_cv_.notify_all();
        if (sender_thread_.joinable()) sender_thread_.join();
        if (sock_fd_ >= 0) ::close(sock_fd_);
    }

    // Disallow copy / move.
    Tracer(const Tracer&)            = delete;
    Tracer& operator=(const Tracer&) = delete;

    // -----------------------------------------------------------------------
    // Record a local event.  This increments the local vector clock entry,
    // snapshots the full vector clock, and enqueues the event for sending.
    // -----------------------------------------------------------------------
    void record(const std::string& event_name,
                const std::string& metadata = "") {
        TraceEvent e;
        {
            std::lock_guard<std::mutex> lk(vc_mutex_);
            vc_[service_index_]++;
            e.vc       = vc_;
            e.seq      = vc_[service_index_];
        }
        e.service    = service_name_;
        e.event_name = event_name;
        e.wall_ns    = wall_ns_now();
        e.mono_ns    = mono_ns_now();
        e.pid        = ::getpid();
        e.metadata   = metadata;
        enqueue(std::move(e));
    }

    // -----------------------------------------------------------------------
    // Call before sending a message to another service.
    // Returns the current vector clock that should be piggybacked on the
    // outgoing message.  Also records a "message_sent" event.
    // -----------------------------------------------------------------------
    VectorClock send_message(const std::string& dest_service,
                             const std::string& extra_metadata = "") {
        std::string meta = "dest=" + dest_service;
        if (!extra_metadata.empty()) meta += ' ' + extra_metadata;
        record("message_sent", meta);
        std::lock_guard<std::mutex> lk(vc_mutex_);
        return vc_;
    }

    // -----------------------------------------------------------------------
    // Call after receiving a message from another service.
    // Merges the piggybacked vector clock, increments the local entry, and
    // records a "message_received" event.
    // -----------------------------------------------------------------------
    void receive_message(const VectorClock& sender_vc,
                         const std::string& src_service = "",
                         const std::string& extra_metadata = "") {
        {
            // Merge the sender's clock into ours (component-wise max).
            // Do NOT increment here — record() will perform the required
            // vc_[self]++ as part of logging the message_received event.
            std::lock_guard<std::mutex> lk(vc_mutex_);
            if (vc_.size() < sender_vc.size()) vc_.resize(sender_vc.size(), 0);
            merge_clocks(vc_, sender_vc);
        }
        std::string meta;
        if (!src_service.empty()) meta = "src=" + src_service;
        if (!extra_metadata.empty()) {
            if (!meta.empty()) meta += ' ';
            meta += extra_metadata;
        }
        record("message_received", meta);
    }

    // Resize the vector clock to accommodate num_services services.
    void set_num_services(std::size_t num_services) {
        std::lock_guard<std::mutex> lk(vc_mutex_);
        if (vc_.size() < num_services) vc_.resize(num_services, 0);
    }

    // Return a snapshot of the current vector clock.
    VectorClock current_vc() const {
        std::lock_guard<std::mutex> lk(vc_mutex_);
        return vc_;
    }

private:
    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    static uint64_t wall_ns_now() {
        struct timespec ts{};
        ::clock_gettime(CLOCK_REALTIME, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
             + static_cast<uint64_t>(ts.tv_nsec);
    }

    static uint64_t mono_ns_now() {
        struct timespec ts{};
        ::clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1'000'000'000ULL
             + static_cast<uint64_t>(ts.tv_nsec);
    }

    void connect_to_collector() {
        struct addrinfo hints{}, *res = nullptr;
        hints.ai_family   = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        std::string port_str = std::to_string(collector_port_);
        if (::getaddrinfo(collector_host_.c_str(), port_str.c_str(), &hints, &res) != 0) {
            // Connection failure is non-fatal; events will be dropped silently.
            sock_fd_ = -1;
            return;
        }
        sock_fd_ = ::socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock_fd_ < 0) { ::freeaddrinfo(res); return; }
        if (::connect(sock_fd_, res->ai_addr, res->ai_addrlen) != 0) {
            ::close(sock_fd_);
            sock_fd_ = -1;
        }
        ::freeaddrinfo(res);
    }

    void enqueue(TraceEvent e) {
        {
            std::lock_guard<std::mutex> lk(queue_mutex_);
            queue_.push(std::move(e));
        }
        queue_cv_.notify_one();
    }

    // Background thread: drain the queue and send events to the Collector.
    void sender_loop() {
        while (running_.load() || !queue_empty()) {
            std::unique_lock<std::mutex> lk(queue_mutex_);
            queue_cv_.wait(lk, [this]{ return !queue_.empty() || !running_.load(); });
            while (!queue_.empty()) {
                TraceEvent e = std::move(queue_.front());
                queue_.pop();
                lk.unlock();
                send_event(e);
                lk.lock();
            }
        }
    }

    bool queue_empty() {
        std::lock_guard<std::mutex> lk(queue_mutex_);
        return queue_.empty();
    }

    // Send a length-prefixed message to the Collector.
    // Format: 4-byte big-endian length, then the serialised event string.
    void send_event(const TraceEvent& e) {
        if (sock_fd_ < 0) {
            // Attempt reconnect once.
            connect_to_collector();
            if (sock_fd_ < 0) return;
        }
        std::string payload = serialize_event(e) + "\n";
        uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
        if (::send(sock_fd_, &len, sizeof(len), MSG_NOSIGNAL) < 0 ||
            ::send(sock_fd_, payload.data(), payload.size(), MSG_NOSIGNAL) < 0) {
            ::close(sock_fd_);
            sock_fd_ = -1;
        }
    }

    // -----------------------------------------------------------------------
    // Member data
    // -----------------------------------------------------------------------
    std::string  service_name_;
    std::size_t  service_index_;
    std::string  collector_host_;
    uint16_t     collector_port_;
    int          sock_fd_{-1};

    mutable std::mutex vc_mutex_;
    VectorClock        vc_;   // grows on demand; initialised empty → all zeros

    std::mutex              queue_mutex_;
    std::condition_variable queue_cv_;
    std::queue<TraceEvent>  queue_;
    std::atomic<bool>       running_{true};
    std::thread             sender_thread_;
};

} // namespace causedeb
