# Module 05 — TCP Networking and Socket Programming

## Learning Objectives

- Understand the TCP/IP socket lifecycle
- Write a TCP server that accepts multiple concurrent connections
- Write a TCP client that connects and sends framed messages
- Understand length-prefix framing and why it matters
- See how CauseDeb's Collector and Tracer communicate

---

## 1. The TCP Socket Lifecycle

```
Server                              Client
------                              ------
socket()                            socket()
bind()
listen()
accept() ←─── SYN ─────────────── connect()
             SYN-ACK ──────────►
             ACK ◄───────────────
send()/recv() ←──── data ──────── send()/recv()
close()                             close()
```

---

## 2. A Minimal TCP Server

```cpp
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int listen_fd = socket(AF_INET, SOCK_STREAM, 0);

int opt = 1;
setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

struct sockaddr_in addr{};
addr.sin_family      = AF_INET;
addr.sin_addr.s_addr = INADDR_ANY;
addr.sin_port        = htons(9000);

bind(listen_fd, (struct sockaddr*)&addr, sizeof(addr));
listen(listen_fd, 64);

while (true) {
    int client_fd = accept(listen_fd, nullptr, nullptr);
    // Spawn a thread to handle client_fd
    std::thread(handle_client, client_fd).detach();
}
```

---

## 3. A Minimal TCP Client

```cpp
#include <arpa/inet.h>

int sock = socket(AF_INET, SOCK_STREAM, 0);

struct sockaddr_in server{};
server.sin_family      = AF_INET;
server.sin_port        = htons(9000);
inet_pton(AF_INET, "127.0.0.1", &server.sin_addr);

connect(sock, (struct sockaddr*)&server, sizeof(server));
send(sock, "hello", 5, 0);
close(sock);
```

---

## 4. Length-Prefix Framing

TCP is a **stream** protocol — `send("hello world")` may arrive as `"hello"`
followed by `" world"`. You must add your own message boundaries.

CauseDeb uses a **4-byte big-endian length prefix**:

```
┌───────────────────┬────────────────────────────────────────────┐
│  Length (4 bytes) │  Payload (Length bytes)                    │
│  big-endian uint32│  serialised TraceEvent string              │
└───────────────────┴────────────────────────────────────────────┘
```

Sending:
```cpp
std::string payload = serialize_event(e) + "\n";
uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
send(sock_fd, &len, 4, MSG_NOSIGNAL);
send(sock_fd, payload.data(), payload.size(), MSG_NOSIGNAL);
```

`MSG_NOSIGNAL` suppresses `SIGPIPE` — without it, writing to a closed
connection would kill your process.

Receiving (Collector):
```cpp
uint32_t net_len;
recv(client_fd, &net_len, 4, MSG_WAITALL);
uint32_t len = ntohl(net_len);

std::string buf(len, '\0');
recv(client_fd, buf.data(), len, MSG_WAITALL);
```

`MSG_WAITALL` blocks until all `len` bytes have arrived, even if they come
in multiple TCP segments.

---

## 5. `htonl` / `ntohl` — Network Byte Order

Different CPU architectures store multi-byte integers differently (big-endian
vs little-endian). TCP conventions use **big-endian** (network byte order).

- `htonl(x)` — host-to-network-long (converts uint32 to big-endian)
- `ntohl(x)` — network-to-host-long (converts big-endian to host byte order)

Always convert when writing integers to the wire.

---

## 6. Handling Partial Sends and Receives

Even after `connect()`, `send()` and `recv()` may return fewer bytes than
requested. Production code must loop:

```cpp
ssize_t send_all(int fd, const void* buf, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, (const char*)buf + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return n;   // error or connection closed
        sent += n;
    }
    return (ssize_t)sent;
}
```

---

## Mini Project

Build a simple "echo server":
1. Server listens on port 9999, accepts connections, and echoes back any
   message it receives using length-prefix framing.
2. Client connects, sends three events in series, receives them back, and
   prints them.

## Exercises

1. What happens if you call `send()` on a socket whose remote peer has
   closed the connection?
2. Why does the Collector use `MSG_WAITALL`?
3. What is the maximum payload length a 4-byte length prefix can represent?
