// collector_server.cpp — CauseDeb Collector
//
// A multi-threaded TCP server that accepts connections from instrumented
// services, reads length-prefixed event messages, and appends them to a
// trace file on disk.
//
// Usage:
//   causedeb_collector [--port <port>] [--out <trace_file>] <service0> <service1> ...
//
// The service names listed on the command line define the vector clock layout.
// At least one service name must be provided.

#include <arpa/inet.h>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "trace_io.h"

// ---------------------------------------------------------------------------
// Global state
// ---------------------------------------------------------------------------
static std::mutex         g_write_mutex;
static causedeb::TraceWriter* g_writer = nullptr;
static std::atomic<bool>  g_running{true};

// ---------------------------------------------------------------------------
// Handle one client connection (runs in its own thread).
// ---------------------------------------------------------------------------
static void handle_client(int client_fd) {
    while (g_running.load()) {
        // Read 4-byte big-endian length prefix.
        uint32_t net_len = 0;
        ssize_t r = ::recv(client_fd, &net_len, sizeof(net_len), MSG_WAITALL);
        if (r <= 0) break;  // client disconnected
        uint32_t len = ntohl(net_len);
        if (len == 0 || len > 1024 * 1024) {
            std::cerr << "Collector: invalid message length " << len << ", dropping connection\n";
            break;
        }

        // Read the event payload.
        std::string payload(len, '\0');
        ssize_t r2 = ::recv(client_fd, payload.data(), static_cast<std::size_t>(len), MSG_WAITALL);
        if (r2 <= 0 || static_cast<uint32_t>(r2) < len) goto done;

        {
            // Strip trailing newline if present.
            if (!payload.empty() && payload.back() == '\n')
                payload.pop_back();

            causedeb::TraceEvent e;
            if (!causedeb::deserialize_event(payload, e)) {
                std::cerr << "Collector: malformed event, skipping\n";
                continue;
            }

            std::lock_guard<std::mutex> lk(g_write_mutex);
            if (g_writer) g_writer->append(e);
        }
    }
done:
    ::close(client_fd);
}

// ---------------------------------------------------------------------------
// Signal handler for graceful shutdown.
// ---------------------------------------------------------------------------
static void on_signal(int) {
    g_running.store(false);
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    uint16_t    port       = 9000;
    std::string trace_path = "trace.log";
    std::vector<std::string> service_names;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if (std::strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            trace_path = argv[++i];
        } else {
            service_names.emplace_back(argv[i]);
        }
    }

    if (service_names.empty()) {
        std::cerr << "Usage: causedeb_collector [--port PORT] [--out FILE] SVC1 SVC2 ...\n";
        return 1;
    }

    // Write trace header.
    causedeb::TraceWriter writer(trace_path);
    if (!writer.write_header(service_names)) {
        std::cerr << "Collector: failed to create trace file: " << trace_path << '\n';
        return 1;
    }
    g_writer = &writer;

    // Install signal handlers.
    ::signal(SIGINT,  on_signal);
    ::signal(SIGTERM, on_signal);

    // Create listening socket.
    int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(port);

    if (::bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    if (::listen(listen_fd, 64) < 0) { perror("listen"); return 1; }

    std::cout << "CauseDeb Collector listening on port " << port
              << ", writing to " << trace_path << '\n';
    std::cout << "Services: ";
    for (std::size_t i = 0; i < service_names.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << service_names[i] << "[" << i << "]";
    }
    std::cout << '\n';

    // Accept loop.
    std::vector<std::thread> workers;
    while (g_running.load()) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(listen_fd,
                                 reinterpret_cast<struct sockaddr*>(&client_addr),
                                 &client_len);
        if (client_fd < 0) {
            if (g_running.load()) perror("accept");
            break;
        }
        char ip_buf[INET_ADDRSTRLEN];
        ::inet_ntop(AF_INET, &client_addr.sin_addr, ip_buf, sizeof(ip_buf));
        std::cout << "Collector: new connection from " << ip_buf << '\n';

        workers.emplace_back(handle_client, client_fd);
        workers.back().detach();
    }

    ::close(listen_fd);
    writer.close();
    std::cout << "Collector: shut down cleanly.\n";
    return 0;
}
