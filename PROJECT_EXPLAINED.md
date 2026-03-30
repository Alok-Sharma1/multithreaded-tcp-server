# Multi-Threaded TCP/IP Server in C++ — Complete Deep-Dive Guide

> **"Built a high-concurrency network server utilizing POSIX threads and mutexes
> to manage real-time client connections. Implemented robust IPC and Socket
> communication layers, optimizing socket buffer management for non-blocking
> I/O operations."**

This document explains **everything** — from what a socket is, to why condition
variables exist, to every function call in every source file.  Read it top to
bottom once and you will be able to explain this project confidently in any
technical interview.

---

## Table of Contents

1. [What Is This Project?](#1-what-is-this-project)
2. [Pre-Requisite Concepts](#2-pre-requisite-concepts)
   - 2.1 [What Is a Process?](#21-what-is-a-process)
   - 2.2 [What Is a Thread?](#22-what-is-a-thread)
   - 2.3 [Why Do We Need Threads for a Server?](#23-why-do-we-need-threads-for-a-server)
3. [Computer Networking From Zero](#3-computer-networking-from-zero)
   - 3.1 [The OSI Model](#31-the-osi-model)
   - 3.2 [IP — The Network Layer](#32-ip--the-network-layer)
   - 3.3 [TCP — The Transport Layer](#33-tcp--the-transport-layer)
   - 3.4 [Ports](#34-ports)
   - 3.5 [The TCP Three-Way Handshake](#35-the-tcp-three-way-handshake)
   - 3.6 [TCP Four-Way Teardown](#36-tcp-four-way-teardown)
4. [Berkeley Sockets API](#4-berkeley-sockets-api)
   - 4.1 [What Is a Socket?](#41-what-is-a-socket)
   - 4.2 [Server-Side Socket Lifecycle](#42-server-side-socket-lifecycle)
   - 4.3 [Client-Side Socket Lifecycle](#43-client-side-socket-lifecycle)
   - 4.4 [Important Socket System Calls](#44-important-socket-system-calls)
5. [POSIX Threads (pthreads)](#5-posix-threads-pthreads)
   - 5.1 [Creating a Thread](#51-creating-a-thread)
   - 5.2 [Joining a Thread](#52-joining-a-thread)
   - 5.3 [The Thread Stack & Memory Model](#53-the-thread-stack--memory-model)
   - 5.4 [Data Races — The Core Problem](#54-data-races--the-core-problem)
6. [Synchronisation Primitives](#6-synchronisation-primitives)
   - 6.1 [Mutex (pthread_mutex_t)](#61-mutex-pthread_mutex_t)
   - 6.2 [Condition Variables (pthread_cond_t)](#62-condition-variables-pthread_cond_t)
   - 6.3 [Atomic Variables (std::atomic)](#63-atomic-variables-stdatomic)
   - 6.4 [Comparing the Three](#64-comparing-the-three)
7. [Inter-Process Communication (IPC)](#7-inter-process-communication-ipc)
   - 7.1 [What Is IPC?](#71-what-is-ipc)
   - 7.2 [IPC #1 — Self-Pipe Trick](#72-ipc-1--self-pipe-trick)
   - 7.3 [IPC #2 — Task Queue via Mutex + Condvar](#73-ipc-2--task-queue-via-mutex--condvar)
   - 7.4 [IPC #3 — Atomic Shared Counters](#74-ipc-3--atomic-shared-counters)
   - 7.5 [IPC #4 — Mutex-Protected Shared Map](#75-ipc-4--mutex-protected-shared-map)
8. [Non-Blocking I/O with select()](#8-non-blocking-io-with-select)
   - 8.1 [The Problem with Blocking recv()](#81-the-problem-with-blocking-recv)
   - 8.2 [How select() Works](#82-how-select-works)
   - 8.3 [select() vs poll() vs epoll()](#83-select-vs-poll-vs-epoll)
9. [Socket Buffer Optimisation](#9-socket-buffer-optimisation)
   - 9.1 [Kernel Ring Buffers](#91-kernel-ring-buffers)
   - 9.2 [Nagle's Algorithm & TCP_NODELAY](#92-nagles-algorithm--tcp_nodelay)
   - 9.3 [Full Table of Socket Options Used](#93-full-table-of-socket-options-used)
10. [Thread Pool Pattern](#10-thread-pool-pattern)
    - 10.1 [Why Not One Thread Per Connection?](#101-why-not-one-thread-per-connection)
    - 10.2 [Producer-Consumer Model](#102-producer-consumer-model)
    - 10.3 [Bounded Queue & Back-Pressure](#103-bounded-queue--back-pressure)
    - 10.4 [Thundering Herd Problem](#104-thundering-herd-problem)
    - 10.5 [Graceful Shutdown Sequence](#105-graceful-shutdown-sequence)
11. [Project Architecture](#11-project-architecture)
    - 11.1 [Directory Structure](#111-directory-structure)
    - 11.2 [Full Architecture Diagram](#112-full-architecture-diagram)
    - 11.3 [Data Flow Walkthrough](#113-data-flow-walkthrough)
12. [File-by-File Code Walkthrough](#12-file-by-file-code-walkthrough)
    - 12.1 [logger.hpp / logger.cpp](#121-loggerhpp--loggercpp)
    - 12.2 [stats.hpp / stats.cpp](#122-statshpp--statscpp)
    - 12.3 [client_registry.hpp / client_registry.cpp](#123-client_registryhpp--client_registrycpp)
    - 12.4 [thread_pool.hpp / thread_pool.cpp](#124-thread_poolhpp--thread_poolcpp)
    - 12.5 [connection_handler.hpp / connection_handler.cpp](#125-connection_handlerhpp--connection_handlercpp)
    - 12.6 [server.cpp — Main Entry Point](#126-servercpp--main-entry-point)
    - 12.7 [client.cpp — Test Client](#127-clientcpp--test-client)
13. [How to Build](#13-how-to-build)
    - 13.1 [Prerequisites](#131-prerequisites)
    - 13.2 [Build Commands](#132-build-commands)
    - 13.3 [Understanding the Makefile](#133-understanding-the-makefile)
14. [How to Run](#14-how-to-run)
    - 14.1 [Starting the Server](#141-starting-the-server)
    - 14.2 [Connecting Interactively](#142-connecting-interactively)
    - 14.3 [Running the Benchmark](#143-running-the-benchmark)
    - 14.4 [Server Command-Line Options](#144-server-command-line-options)
    - 14.5 [Protocol Commands Reference](#145-protocol-commands-reference)
15. [How to Push to GitHub](#15-how-to-push-to-github)
16. [Common Errors & Fixes](#16-common-errors--fixes)
17. [Interview Q&A — Every Question You Will Be Asked](#17-interview-qa--every-question-you-will-be-asked)
18. [Glossary](#18-glossary)

---

## 1. What Is This Project?

This project is a **production-style TCP/IP server** written in C++ from scratch,
using only the Linux system-call layer (no third-party libraries like Boost, Asio,
or libuv).

### What it does at runtime

1. Opens a TCP socket and listens on port 8080 (configurable).
2. For every incoming client connection, hands it to one of N pre-created
   **worker threads** (thread pool).
3. Each worker runs a simple **text command protocol** with the client:
   PING, ECHO, TIME, STATS, INFO, WHO, BROADCAST, HELP, QUIT.
4. All threads update shared statistics safely using **atomic** variables.
5. When any client sends `BROADCAST hello`, the server forwards that message
   to **every other** connected client.
6. Press Ctrl+C → the server shuts down gracefully (drains all in-flight
   connections, prints final stats, then exits).

### What concepts it demonstrates

| Concept | Where |
|---------|-------|
| TCP socket programming | `server.cpp`, `connection_handler.cpp` |
| POSIX thread creation | `thread_pool.cpp` |
| Mutex (mutual exclusion) | Logger, ClientRegistry, ThreadPool |
| Condition variables | ThreadPool (cond_not_empty, cond_not_full) |
| Lock-free atomics | `stats.hpp` |
| IPC — Self-pipe trick | `server.cpp` (signal handling) |
| Non-blocking I/O (select) | `connection_handler.cpp` (readLine) |
| Socket buffer tuning | `connection_handler.cpp` (configureSocket) |
| Graceful shutdown | `thread_pool.cpp` (shutdown) |
| Singleton pattern | Logger, Stats, ClientRegistry |
| Producer-Consumer pattern | ThreadPool task queue |

---

## 2. Pre-Requisite Concepts

### 2.1 What Is a Process?

A **process** is a running program. The OS gives it:
- Its own **virtual address space** (private memory — other processes cannot
  read it without permission)
- One or more **threads** of execution
- A **file descriptor table** (open files, sockets, pipes)
- A **PID** (Process ID)

```
Process Memory Layout (simplified)
┌───────────────────────┐  High address
│        Stack          │  ← grows downward; local variables
├───────────────────────┤
│          ↓            │
│     (free space)      │
│          ↑            │
├───────────────────────┤
│         Heap          │  ← grows upward; malloc/new
├───────────────────────┤
│    BSS (zero-init)    │  ← global/static uninitialized vars
├───────────────────────┤
│   Data (initialized)  │  ← global/static initialized vars
├───────────────────────┤
│    Text (code)        │  ← compiled machine instructions
└───────────────────────┘  Low address
```

### 2.2 What Is a Thread?

A **thread** is an independent execution path **inside** a process. All threads
in a process **share** the heap and globals, but each has its own:
- **Stack** (~2–8 MB by default) — for local variables and function call frames
- **Program counter** (next instruction to execute)
- **Registers** (CPU state)

Think of a process as a factory, and threads as workers inside it.
They all use the same tools (shared memory), but each worker is doing
something different simultaneously.

```
One Process, Multiple Threads
┌────────────────────────────────────────┐
│              PROCESS                   │
│  Shared: heap, globals, fds, code      │
│                                        │
│  Thread 0 (main)   Thread 1   Thread 2 │
│  [own stack]       [own stack][own stack]│
│  PC=0x40120        PC=0x40380 PC=0x40500│
└────────────────────────────────────────┘
```

### 2.3 Why Do We Need Threads for a Server?

Imagine a server that handles clients one at a time:

```
Server (single-threaded):
  Client A connects → server handles A → A disconnects
                                         ↑
                           Client B waits here (could be seconds!)
```

This is terrible. A slow client (or an idle one) blocks everyone else.

**Solution 1 — One thread per connection:**
```
Client A connects → spawn thread → thread handles A
Client B connects → spawn thread → thread handles B
```
Works, but `pthread_create()` costs ~5–20 microseconds and ~2–8 MB of stack
each time. At 1000 simultaneous connections = 8 GB of stack memory just for
stacks. The OS scheduler also struggles with thousands of threads.

**Solution 2 (used here) — Thread pool:**
```
Server starts → pre-create 4 threads → they wait for work
Client A → enqueue fd → Thread 0 wakes → handles A
Client B → enqueue fd → Thread 1 wakes → handles B
Client C → enqueue fd → Thread 2 wakes → handles C
Client D → enqueue fd → Thread 3 wakes → handles D
Client E → enqueue fd → waits in queue (bounded to 256)
```

Pre-created threads — no startup cost per connection. Bounded memory.

---

## 3. Computer Networking From Zero

### 3.1 The OSI Model

The **Open Systems Interconnection** model splits networking into 7 layers.
Each layer handles a specific job and communicates only with layers directly
above and below it.

```
Layer | Name         | What It Does                    | Examples
──────┼──────────────┼─────────────────────────────────┼──────────────────
  7   │ Application  │ Actual data for the application │ HTTP, SSH, our protocol
  6   │ Presentation │ Encoding/encryption/compression │ TLS/SSL, JPEG
  5   │ Session      │ Connection management           │ NetBIOS, RPC
  4   │ Transport    │ End-to-end data delivery        │ TCP, UDP
  3   │ Network      │ Routing across networks         │ IP, ICMP
  2   │ Data Link    │ Node-to-node delivery           │ Ethernet, Wi-Fi frames
  1   │ Physical     │ Raw bits over a medium          │ Cables, radio waves
```

This project operates at **Layer 4 (TCP)** and implements its own
**Layer 7 (Application)** protocol (the PING/ECHO/STATS commands).

### 3.2 IP — The Network Layer

**IP (Internet Protocol)** is responsible for **routing** packets from a
source machine to a destination machine anywhere in the world.

- Every machine on a network has an **IP address** (e.g., `192.168.1.10`)
- `INADDR_ANY` (= `0.0.0.0`) means "listen on ALL network interfaces of
  this machine" — that's what the server binds to
- `127.0.0.1` is the **loopback** address — packets sent here never leave
  the machine (used for testing client and server on the same host)
- IP is **unreliable** — packets can be lost, duplicated, or arrive out of order
- That's why we use TCP on top of it

### 3.3 TCP — The Transport Layer

**TCP (Transmission Control Protocol)** adds reliability and ordering on top of IP:

| Property | How TCP Achieves It |
|----------|---------------------|
| **Reliable delivery** | Every segment is ACK'd; lost segments are retransmitted |
| **Ordered delivery** | Sequence numbers; receiver reorders out-of-order segments |
| **Flow control** | Receiver advertises its window size; sender respects it |
| **Congestion control** | Slow start + AIMD; backs off when the network is congested |
| **Full-duplex** | Data flows in both directions simultaneously on one connection |
| **Connection-oriented** | Explicit setup (handshake) and teardown |

`SOCK_STREAM` in the `socket()` call means "give me a TCP socket."

### 3.4 Ports

A **port** is a 16-bit number (0–65535) that identifies a specific process on
a machine. It allows multiple services to run on the same IP address.

```
IP address = the apartment building address
Port       = the apartment number inside the building
```

| Port Range | Usage |
|-----------|-------|
| 0–1023 | Well-known / reserved (HTTP=80, HTTPS=443, SSH=22) |
| 1024–49151 | Registered ports (registered with IANA) |
| 49152–65535 | Ephemeral (temporary; assigned by OS to clients) |

Our server uses port **8080** (above 1023, so no root privileges needed).

When `nc localhost 8080` connects:
- The client gets an ephemeral port like `54321` assigned by the OS
- The server endpoint: `0.0.0.0:8080`
- The client endpoint: `127.0.0.1:54321`
- A TCP connection is uniquely identified by the 4-tuple:
  `(src_ip, src_port, dst_ip, dst_port)`

### 3.5 The TCP Three-Way Handshake

Before data can flow, TCP performs a handshake to establish the connection:

```
Client                          Server
  │                                │
  │──── SYN (seq=x) ──────────────►│  "I want to connect; my initial seq is x"
  │                                │
  │◄─── SYN-ACK (seq=y, ack=x+1) ─│  "OK, my seq is y; I received your seq x"
  │                                │
  │──── ACK (ack=y+1) ────────────►│  "I received your seq y"
  │                                │
  │         [CONNECTED]            │
  │    data can now flow both ways │
```

This happens **inside the kernel** when a client calls `connect()`.
By the time the server's `accept()` returns, the handshake is complete.

### 3.6 TCP Four-Way Teardown

Closing a TCP connection requires 4 messages (because each direction is
independent):

```
Client                          Server
  │──── FIN ──────────────────────►│  "I'm done sending"
  │◄─── ACK ───────────────────────│  "Got it"
  │◄─── FIN ───────────────────────│  "I'm also done sending"
  │──── ACK ──────────────────────►│
  │
  │ [TIME_WAIT: ~60s before port can be reused]
```

`SO_REUSEADDR` on the server socket lets us skip this wait on restart.

---

## 4. Berkeley Sockets API

### 4.1 What Is a Socket?

A **socket** is a file descriptor — just like a file — but for network I/O.
Linux's "everything is a file" philosophy means you can use `read()`/`write()`
on sockets (though `send()`/`recv()` give more control).

The socket API was designed at Berkeley (UC Berkeley) in the early 1980s and
is now standardised as part of POSIX.

### 4.2 Server-Side Socket Lifecycle

```c
// 1. Create the socket
int server_fd = socket(AF_INET,      // IPv4
                       SOCK_STREAM,  // TCP
                       0);           // auto-select protocol

// 2. Set socket options (before bind)
setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

// 3. Bind to an address (IP + port)
struct sockaddr_in addr;
addr.sin_family      = AF_INET;
addr.sin_addr.s_addr = INADDR_ANY;  // 0.0.0.0
addr.sin_port        = htons(8080); // host-to-network byte order
bind(server_fd, (struct sockaddr*)&addr, sizeof(addr));

// 4. Start listening; 128 = max pending connections in kernel queue
listen(server_fd, 128);

// 5. Accept loop
while (true) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int client_fd = accept(server_fd,
                           (struct sockaddr*)&client_addr,
                           &client_len);
    // client_fd is now a fully-connected socket to ONE specific client
    // server_fd is still listening for MORE clients
    handle(client_fd);  // or pass to a thread
}
```

### 4.3 Client-Side Socket Lifecycle

```c
// 1. Create socket
int fd = socket(AF_INET, SOCK_STREAM, 0);

// 2. Fill in server address
struct sockaddr_in addr;
addr.sin_family = AF_INET;
addr.sin_port   = htons(8080);
inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

// 3. Connect (triggers the TCP 3-way handshake)
connect(fd, (struct sockaddr*)&addr, sizeof(addr));

// 4. Send and receive
send(fd, "PING\r\n", 6, 0);
char buf[256];
recv(fd, buf, sizeof(buf), 0);

// 5. Close
close(fd);
```

### 4.4 Important Socket System Calls

| Call | Signature (simplified) | What It Does |
|------|----------------------|--------------|
| `socket()` | `socket(domain, type, protocol)` | Allocate a new socket fd |
| `bind()` | `bind(fd, addr, addrlen)` | Assign local IP:port |
| `listen()` | `listen(fd, backlog)` | Mark fd as passive; set pending queue depth |
| `accept()` | `accept(fd, &caddr, &clen)` | Dequeue one connection; return new fd |
| `connect()` | `connect(fd, addr, addrlen)` | Initiate TCP handshake to server |
| `send()` | `send(fd, buf, len, flags)` | Copy data into kernel send buffer |
| `recv()` | `recv(fd, buf, len, flags)` | Copy data from kernel receive buffer |
| `setsockopt()` | `setsockopt(fd, level, optname, val, len)` | Configure socket behaviour |
| `getpeername()` | `getpeername(fd, addr, len)` | Get remote IP:port of connected client |
| `inet_ntop()` | `inet_ntop(AF_INET, &in_addr, buf, len)` | Binary IP → human-readable string |
| `htons()` | `htons(port)` | Host-to-network byte order (16-bit) |
| `close()` | `close(fd)` | Initiate TCP teardown; free fd |

**Byte Order Note:** CPUs may be little-endian (x86) or big-endian.
The network protocol uses **big-endian** ("network byte order").
`htons()` = "Host TO Network Short" — converts a 16-bit port number
to network byte order.  `ntohs()` does the reverse.

---

## 5. POSIX Threads (pthreads)

### 5.1 Creating a Thread

```c
// The function a thread will execute must have this signature:
void* my_function(void* arg) {
    int* n = (int*)arg;
    printf("Thread got: %d\n", *n);
    return nullptr;
}

pthread_t tid;
int value = 42;
pthread_create(&tid,       // output: the thread ID
               nullptr,    // thread attributes (stack size, etc.) — null = defaults
               my_function,// the function to run
               &value);    // argument passed to the function
```

In our project, `ThreadPool::ThreadPool()` calls `pthread_create` N times:

```cpp
for (size_t i = 0; i < num_threads; ++i) {
    auto* arg = new WorkerArg{this, i};    // heap-allocated (avoids dangling pointer)
    pthread_t tid;
    pthread_create(&tid, nullptr, workerEntry, arg);
    threads_.push_back(tid);
}
```

`workerEntry` is a static method (required — member functions can't be passed
directly to `pthread_create` because they have a hidden `this` pointer).

### 5.2 Joining a Thread

```c
pthread_join(tid, nullptr);  // blocks until the thread exits
```

This is how `shutdown()` waits for all workers to finish their current
client before the server exits. Without `join()`, the process could exit
while worker threads still have active client connections.

### 5.3 The Thread Stack & Memory Model

```
Process Memory After Creating 4 Worker Threads:
┌─────────────────────────────────────────┐
│               SHARED                    │
│  Heap: queue_, clients_, stats, logger  │
│  Globals: g_session_counter, g_pipe     │
│  Text: all function code                │
├─────────────────────────────────────────┤
│  Thread 0 stack  │  Thread 1 stack      │
│  (local vars in  │  (local vars in      │
│   workerLoop)    │   workerLoop)        │
├──────────────────┼──────────────────────┤
│  Thread 2 stack  │  Thread 3 stack      │
└──────────────────┴──────────────────────┘
```

Because threads share the heap, they can read and write each other's data —
which is both the power of threads AND their greatest danger.

### 5.4 Data Races — The Core Problem

A **data race** occurs when two threads access the same memory location
concurrently and at least one is writing, with no synchronisation.

```cpp
// UNSAFE — data race
int counter = 0;

void* thread_fn(void*) {
    for (int i = 0; i < 1000000; ++i)
        counter++;       // NOT atomic — this is 3 machine instructions:
                         // LOAD counter into register
                         // ADD 1
                         // STORE back to counter
    return nullptr;
}
```

If two threads run this simultaneously:
```
Thread A:  LOAD  counter=5
Thread B:  LOAD  counter=5    ← reads BEFORE A writes
Thread A:  ADD   → 6
Thread B:  ADD   → 6          ← both compute 6 instead of 7!
Thread A:  STORE counter=6
Thread B:  STORE counter=6    ← lost update!
```

Expected result: 2,000,000. Actual result: varies, always less.
This is **undefined behaviour** in C++.

Solutions: Mutex, Atomic, or Condition Variable.

---

## 6. Synchronisation Primitives

### 6.1 Mutex (pthread_mutex_t)

A **mutex** (mutual exclusion lock) ensures that only **one thread at a time**
executes a critical section.

```c
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

// Thread A and Thread B both try to do this:
pthread_mutex_lock(&mutex);   // blocks if another thread holds it
// ─── CRITICAL SECTION ─────
shared_queue.push(value);
// ─────────────────────────
pthread_mutex_unlock(&mutex); // releases; one waiting thread wakes up
```

**Under the hood:** The OS marks the thread as "waiting" and removes it from
the run queue. When the lock is released, one waiting thread is put back on
the run queue. This is much better than busy-waiting (spinning in a loop).

**Initialization in our project:**
```cpp
pthread_mutex_init(&mutex_, nullptr);   // in constructors
pthread_mutex_destroy(&mutex_);         // in destructors
```

**Deadlock** — the deadly scenario:
```
Thread A holds Lock 1, waits for Lock 2
Thread B holds Lock 2, waits for Lock 1
→ Both wait forever
```

Our project avoids deadlocks by:
1. Each class has exactly **one** mutex.
2. We never hold a mutex while calling any function that acquires another mutex.
3. We **release the mutex before doing I/O** (send(), recv()) in ClientRegistry.

### 6.2 Condition Variables (pthread_cond_t)

A condition variable lets a thread **sleep** until some condition becomes true.
Without it, you'd have to busy-wait:

```cpp
// BAD (busy-wait) — wastes 100% CPU
while (queue.empty()) { /* spin */ }
fd = queue.front();
```

```cpp
// GOOD (condition variable) — thread sleeps with no CPU usage
pthread_mutex_lock(&mutex);
while (queue.empty())
    pthread_cond_wait(&cond_not_empty, &mutex);
// ↑ This call atomically:
//   1. Releases the mutex (so the producer can add to the queue)
//   2. Puts this thread to sleep
//   3. When signalled, re-acquires the mutex before returning
fd = queue.front(); queue.pop();
pthread_mutex_unlock(&mutex);
```

**Why `while` instead of `if`?**
Spurious wakeups — the OS may wake a thread even without a signal (allowed by
POSIX). Re-checking the condition guards against this.

**Signalling in our project:**

```cpp
// Producer (submit() in server.cpp → ThreadPool::submit()):
pthread_mutex_lock(&mutex_);
queue_.push(client_fd);
pthread_cond_signal(&cond_not_empty_);  // wake ONE sleeping worker
pthread_mutex_unlock(&mutex_);

// Shutdown (ThreadPool::shutdown()):
stop_.store(true);
pthread_cond_broadcast(&cond_not_empty_); // wake ALL workers so they exit
```

`signal` vs `broadcast`:
- `pthread_cond_signal` — wakes exactly **one** sleeping thread (used when adding
  one task, because only one worker is needed)
- `pthread_cond_broadcast` — wakes **all** sleeping threads (used at shutdown
  so every worker sees `stop_=true` and exits)

### 6.3 Atomic Variables (std::atomic)

`std::atomic<T>` makes any read-modify-write operation **indivisible** at the
hardware level, with no OS involvement (no mutex, no context switch).

```cpp
std::atomic<uint64_t> counter{0};

// These are safe from any thread, simultaneously:
counter++;              // fetch_add(1)
counter += 100;         // fetch_add(100)
uint64_t n = counter;   // load()
```

On x86-64, `counter++` compiles to a single `LOCK XADD` instruction.
The `LOCK` prefix prevents any other CPU core from accessing that memory
location until the operation completes.

**Memory order:**
```cpp
counter.load(std::memory_order_relaxed)
```
`memory_order_relaxed` means: only atomicity is guaranteed, not ordering
relative to other memory accesses. Fine for independent counters like Stats,
but wrong if you're using the atomic to guard other shared data.

### 6.4 Comparing the Three

| Primitive | Use When | Overhead | Blocks Thread? |
|-----------|----------|----------|----------------|
| `pthread_mutex_t` | Protecting arbitrary shared data | Medium | Yes (OS suspends thread) |
| `pthread_cond_t` | Waiting for a condition to change | Medium | Yes (OS suspends thread) |
| `std::atomic` | Simple counters or flags | Very low | No (1–2 CPU instructions) |

---

## 7. Inter-Process Communication (IPC)

### 7.1 What Is IPC?

**IPC** is any mechanism that lets two execution units (threads or processes)
exchange information or coordinate. In this project, all IPC is between
threads (intra-process), but the same primitives apply across processes.

This project uses **four** distinct IPC mechanisms:

### 7.2 IPC #1 — Self-Pipe Trick

**Problem:** When the user presses Ctrl+C, the OS sends `SIGINT` to the
process. We want the main `select()` loop to notice this and shut down
gracefully. But `select()` watches file descriptors, not signals or flags.

**Naive broken attempt:**
```cpp
volatile bool shutdown = false;

void signalHandler(int) { shutdown = true; }

// In main loop:
while (!shutdown) {
    select(server_fd + 1, &rdset, NULL, NULL, &timeout);
    // ↑ If select() is blocking and signal arrives, select()
    //   returns with EINTR. But we check shutdown AFTER select() returns.
    //   What if select() blocks for 5 minutes? The server hangs.
}
```

**The Self-Pipe solution (used in this project):**

```c
// At startup: create a pipe (two fds: read end and write end)
pipe(g_wakeup_pipe);  // g_wakeup_pipe[0]=read, g_wakeup_pipe[1]=write

// Signal handler writes 1 byte to the write end:
void signalHandler(int sig) {
    write(g_wakeup_pipe[1], &sig, 1);  // async-signal-safe
}

// Main select() loop watches BOTH the server socket AND the pipe read end:
FD_SET(server_fd,        &rdset);
FD_SET(g_wakeup_pipe[0], &rdset);
select(max_fd + 1, &rdset, NULL, NULL, &timeout);

if (FD_ISSET(g_wakeup_pipe[0], &rdset)) {
    // Signal arrived — read the byte and shut down
    read(g_wakeup_pipe[0], &sig_byte, 1);
    running = false;
}
```

**Why is this correct?**
- `write()` is one of the few async-signal-safe functions — it won't deadlock
  inside a signal handler
- As soon as the signal arrives, the write end gets data → select() returns
  immediately → the main loop notices within microseconds

### 7.3 IPC #2 — Task Queue via Mutex + Condvar

The bounded `std::queue<int>` inside `ThreadPool` is shared between:
- **Producer:** `server.cpp` main thread (calls `pool.submit(client_fd)`)
- **Consumers:** All worker threads (call `workerLoop()`)

```
┌──────────────────────────────────────────────────────────┐
│                   ThreadPool                             │
│                                                          │
│  ┌─────────────────────────────────────────────────┐    │
│  │  queue_: [ fd=7 | fd=9 | fd=12 ]  (size=3/256) │    │
│  └──────────────────────────────────────────────────┘   │
│        ↑ submit()                    ↓ workerLoop()      │
│  pthread_mutex_t  mutex_             (pop from front)    │
│  pthread_cond_t   cond_not_empty_  ← workers wait here  │
│  pthread_cond_t   cond_not_full_   ← producer waits here│
└──────────────────────────────────────────────────────────┘
```

Complete producer flow:
```cpp
bool ThreadPool::submit(int client_fd, bool block) {
    pthread_mutex_lock(&mutex_);

    // If queue is full and block=false, reject immediately (no blocking):
    if (!block && queue_.size() >= max_queue_) {
        pthread_mutex_unlock(&mutex_);
        return false;
    }

    // If queue is full and block=true, sleep until there's room:
    while (queue_.size() >= max_queue_ && !stop_.load())
        pthread_cond_wait(&cond_not_full_, &mutex_);

    queue_.push(client_fd);
    pthread_cond_signal(&cond_not_empty_);  // wake one worker
    pthread_mutex_unlock(&mutex_);
    return true;
}
```

Complete consumer flow:
```cpp
void ThreadPool::workerLoop(size_t id) {
    while (true) {
        pthread_mutex_lock(&mutex_);
        while (queue_.empty() && !stop_.load())
            pthread_cond_wait(&cond_not_empty_, &mutex_); // sleep until work arrives

        if (stop_.load() && queue_.empty()) {  // shutdown condition
            pthread_mutex_unlock(&mutex_);
            break;
        }

        int fd = queue_.front(); queue_.pop();
        pthread_cond_signal(&cond_not_full_); // tell producer there's room
        pthread_mutex_unlock(&mutex_);

        ConnectionHandler::handle(fd);  // handle client WITHOUT holding lock
    }
}
```

### 7.4 IPC #3 — Atomic Shared Counters

`Stats` is updated by **every thread** (main + all workers) simultaneously.

```cpp
// Any thread calls this:
Stats::instance().onConnect();   // calls ++total_connections_; ++active_connections_;
Stats::instance().onMessage();   // calls ++total_messages_;
```

Because `std::atomic<uint64_t>` increments are indivisible, no mutex is needed.
This is the fastest form of shared-state IPC — a single CPU instruction.

### 7.5 IPC #4 — Mutex-Protected Shared Map

`ClientRegistry` holds a `std::unordered_map<uint64_t, ClientInfo>` that every
worker thread reads and writes when clients connect, disconnect, or broadcast.

**Critical design — never hold the lock during I/O:**

```cpp
int ClientRegistry::broadcast(const std::string& msg, uint64_t sender_id) {
    // Step 1: collect target fds while holding the lock
    std::vector<int> fds;
    pthread_mutex_lock(&mutex_);
    for (auto& [id, info] : clients_)
        if (id != sender_id) fds.push_back(info.fd);
    pthread_mutex_unlock(&mutex_);      // ← RELEASE before send()

    // Step 2: send WITHOUT the lock (send() can block)
    for (int fd : fds)
        send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
}
```

If we held the mutex during `send()`, and `send()` blocked (e.g., slow client's
receive buffer is full), then NO other thread could add/remove clients for the
entire duration. This would starve the entire server.

---

## 8. Non-Blocking I/O with select()

### 8.1 The Problem with Blocking recv()

```cpp
// NAIVE — worker thread calls recv() directly:
char buf[4096];
recv(client_fd, buf, sizeof(buf), 0);  // BLOCKS HERE
```

If the client connects but never sends data (idle connection, or a slow
network), this `recv()` blocks forever. The worker thread is permanently
stuck on that one client — even if the server wants to shut down.

### 8.2 How select() Works

`select()` monitors multiple file descriptors and returns when ANY of them
become "ready" (readable, writable, or error), or when a timeout expires.

```c
int select(int nfds,            // highest fd + 1
           fd_set *readfds,     // fds to watch for readability
           fd_set *writefds,    // fds to watch for writability (NULL = don't watch)
           fd_set *exceptfds,   // fds to watch for exceptions  (NULL = don't watch)
           struct timeval *timeout); // max time to wait (NULL = wait forever)

// Return values:
//  > 0  → number of fds that became ready
//  = 0  → timeout expired; no fd was ready
//  < 0  → error (errno set); EINTR = interrupted by signal (retry)
```

**fd_set manipulation macros:**
```c
fd_set rdset;
FD_ZERO(&rdset);         // clear the set
FD_SET(fd, &rdset);      // add fd to the set
FD_CLR(fd, &rdset);      // remove fd from the set
FD_ISSET(fd, &rdset);    // test if fd is set (after select() returns)
```

**Our readLine() implementation:**
```cpp
int ConnectionHandler::readLine(int fd, char* buf, size_t max, int timeout_secs) {
    size_t total = 0;
    while (total < max - 1) {
        fd_set rdset; FD_ZERO(&rdset); FD_SET(fd, &rdset);
        struct timeval tv { timeout_secs, 0 };

        int rc = select(fd + 1, &rdset, nullptr, nullptr, &tv);

        if (rc == 0) return 0;              // ← TIMEOUT
        if (rc < 0) {
            if (errno == EINTR) continue;   // ← signal interrupted; retry
            return -1;                      // ← real error
        }
        // rc > 0 → data is available; recv() will NOT block
        char c;
        if (recv(fd, &c, 1, 0) <= 0) return -1; // EOF or error

        if (c == '\r') continue;            // skip carriage return (telnet compat)
        buf[total++] = c;
        if (c == '\n') break;               // end of line
    }
    buf[total] = '\0';
    if (total > 0 && buf[total-1] == '\n') buf[--total] = '\0'; // strip newline
    return (int)total;
}
```

**Our main accept loop also uses select() to watch two fds simultaneously:**
```cpp
fd_set rdset;
FD_SET(server_fd,        &rdset);  // new client connections
FD_SET(g_wakeup_pipe[0], &rdset);  // shutdown signal via self-pipe

struct timeval tv { 5, 0 };  // 5-second heartbeat interval
int rc = select(max_fd + 1, &rdset, nullptr, nullptr, &tv);
```

This single `select()` call handles both events at once, with no busy-waiting.

### 8.3 select() vs poll() vs epoll()

| Feature | select() | poll() | epoll() |
|---------|----------|--------|---------|
| Max fds | FD_SETSIZE (1024) | Unlimited | Unlimited |
| Complexity | Simple | Moderate | Complex |
| Scalability | O(n) scan | O(n) scan | O(1) per event |
| Use case | < few hundred fds | Hundreds of fds | Tens of thousands |
| Portability | POSIX, Windows | POSIX | Linux only |

We use `select()` because it is simple, well-understood, and sufficient for
hundreds of concurrent connections. For a production server handling 10,000+
clients, `epoll()` would be the right choice.

---

## 9. Socket Buffer Optimisation

### 9.1 Kernel Ring Buffers

Every TCP socket has two kernel-managed circular buffers:

```
Application                        Network
    │                                  │
    │ recv()                  send() │
    ▼                                  ▼
┌──────────────────┐      ┌──────────────────┐
│  SO_RCVBUF       │ ◄─── │  SO_SNDBUF       │
│  Receive buffer  │  TCP │  Send buffer     │
│  (kernel-managed)│      │  (kernel-managed)│
└──────────────────┘      └──────────────────┘
```

- **SO_RCVBUF:** When data arrives from the network, the kernel puts it here.
  `recv()` reads from here. If this buffer fills up, TCP tells the sender to
  slow down (flow control). Default: ~4–87 KB depending on Linux version.

- **SO_SNDBUF:** When `send()` is called, data goes here. The kernel drains
  it to the network. If full, `send()` blocks (or returns EAGAIN for
  non-blocking sockets). Default: ~4–87 KB.

**We set both to 64 KB:**
```cpp
int rbuf = 65536;
setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rbuf, sizeof(rbuf));
int sbuf = 65536;
setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sbuf, sizeof(sbuf));
```

Larger buffers allow more data to be "in-flight" without the application
needing to call `recv()`/`send()` constantly.

### 9.2 Nagle's Algorithm & TCP_NODELAY

**Nagle's algorithm** (enabled by default) batches small writes together:
```
Without Nagle:              With Nagle:
send("PI")  → 1 packet     send("PI")  → wait...
send("NG")  → 1 packet     send("NG")  → wait...
send("\r\n")→ 1 packet     send("\r\n")→ 1 packet (all 3 combined)
= 3 packets, less latency  = 1 packet, more latency
```

For our **interactive protocol** (client sends a command, server responds
immediately), latency matters more than packet count. So we disable Nagle:

```cpp
int flag = 1;
setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
```

Now every `send()` call results in an immediate packet — no batching delay.

### 9.3 Full Table of Socket Options Used

| Option | Level | Effect in This Project |
|--------|-------|----------------------|
| `SO_REUSEADDR` | `SOL_SOCKET` | Server can restart immediately without waiting 60s TIME_WAIT |
| `SO_REUSEPORT` | `SOL_SOCKET` | Multiple listener threads can bind the same port (kernel load-balances) |
| `TCP_NODELAY` | `IPPROTO_TCP` | Disable Nagle → low latency for interactive commands |
| `SO_RCVBUF` | `SOL_SOCKET` | Set kernel receive buffer to 64 KB |
| `SO_SNDBUF` | `SOL_SOCKET` | Set kernel send buffer to 64 KB |
| `SO_KEEPALIVE` | `SOL_SOCKET` | Kernel sends probes after idle time; detects dead peers |
| `SO_LINGER` | `SOL_SOCKET` | On `close()`: flush data, then RST after 2s; avoids port in TIME_WAIT |

---

## 10. Thread Pool Pattern

### 10.1 Why Not One Thread Per Connection?

| Metric | Per-Connection Thread | Thread Pool (N=4) |
|--------|----------------------|-------------------|
| 1000 concurrent clients → threads | 1000 | 4 |
| Stack memory (8 MB each) | 8 GB | 32 MB |
| `pthread_create` time per connection | ~10 µs | 0 (reused) |
| Scheduling overhead | Very high (1000 threads in run queue) | Low |
| Max connections | ~4000 (Linux default stack ulimit) | Bounded by queue (256) |

### 10.2 Producer-Consumer Model

This is a classic Computer Science pattern:

```
PRODUCER            SHARED BUFFER          CONSUMER
(generates items)   (bounded queue)        (processes items)
     │                   │                      │
     ▼                   ▼                      ▼
  submit(fd)        [ fd4 | fd7 | fd9 ]     workerLoop()
                    (max 256 items)
```

The buffer decouples the producer from the consumer:
- Producer doesn't need to wait for a consumer to be free
- Consumer doesn't need to poll; it sleeps until there's work
- The bound prevents unbounded memory growth

### 10.3 Bounded Queue & Back-Pressure

When the queue is full (256 items), the server applies **back-pressure**:

```cpp
// In server.cpp — non-blocking submit:
if (!pool.submit(cfd, false)) {
    // Queue is full → reject this connection immediately
    const char* msg = "ERR: Server busy. Try again later.\r\n";
    send(cfd, msg, strlen(msg), MSG_NOSIGNAL);
    close(cfd);
    LOG_WARN("Rejected: pool queue full");
}
```

This is the correct response to an overloaded server. The alternative
(silently dropping the connection) would leave the client hanging.

### 10.4 Thundering Herd Problem

If we use `pthread_cond_broadcast()` every time a new task arrives:

```
1 new task added → ALL 4 workers wake up
→ Only 1 can pop the task
→ 3 go back to sleep
→ 3 unnecessary context switches
```

We use `pthread_cond_signal()` (wakes exactly ONE) when submitting tasks,
and `pthread_cond_broadcast()` ONLY during shutdown (when we want all threads
to exit).

### 10.5 Graceful Shutdown Sequence

```
User presses Ctrl+C
       │
       ▼
signalHandler() writes 1 byte to wakeup_pipe[1]
       │
       ▼
main select() sees wakeup_pipe[0] is readable
       │
       ▼
main loop sets running=false, breaks
       │
       ▼
pool.shutdown() is called:
  1. pthread_mutex_lock(&mutex_)
  2. stop_ = true
  3. pthread_cond_broadcast(&cond_not_empty_)  ← wakes ALL workers
  4. pthread_mutex_unlock(&mutex_)
  5. for each thread: pthread_join(t)  ← waits for it to finish
       │
       ▼  (inside each worker)
workerLoop() wakes up:
  checks: stop_==true && queue_.empty() → break → thread exits
       │
       ▼
pthread_join() returns for all threads
       │
       ▼
close(server_fd), close(pipe fds)
Print final statistics
Process exits cleanly
```

---

## 11. Project Architecture

### 11.1 Directory Structure

```
multithreaded-tcp-server/
│
├── include/                    ← Header files (interface declarations)
│   ├── logger.hpp              Thread-safe singleton logger
│   ├── stats.hpp               Lock-free atomic server statistics
│   ├── thread_pool.hpp         Fixed-size thread pool
│   ├── connection_handler.hpp  Per-client session handler
│   └── client_registry.hpp    Shared map of active clients
│
├── src/                        ← Source files (implementations)
│   ├── server.cpp              main() — socket setup, accept loop, signals
│   ├── thread_pool.cpp         pthread pool with mutex + condvar
│   ├── connection_handler.cpp  select() I/O, socket opts, protocol dispatch
│   ├── client_registry.cpp    Mutex-protected add/remove/broadcast
│   ├── logger.cpp              Timestamped, thread-safe logging
│   ├── stats.cpp               Stats::report() formatting
│   └── client.cpp              Multi-threaded test client + interactive shell
│
├── build/                      ← Compiled binaries and object files (git-ignored)
│   ├── server                  ← The server executable
│   └── client                  ← The test client executable
│
├── Makefile                    ← Build system
├── .gitignore                  ← Excludes build/ from git
└── README.md                   ← Quick-start documentation
```

### 11.2 Full Architecture Diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Server Process                                │
│                                                                      │
│  ┌─────────────────────────────────────────────────────────────┐   │
│  │  Main Thread (server.cpp)                                   │   │
│  │                                                              │   │
│  │  socket() → bind() → listen()                               │   │
│  │                                                              │   │
│  │  select() loop:                                              │   │
│  │    watches:  server_fd ──────────── new connections          │   │
│  │              wakeup_pipe[0] ──────── SIGINT/SIGTERM          │   │
│  │    on new connection:                                        │   │
│  │      accept() → get client_fd                               │   │
│  │      pool.submit(client_fd)  ──────────────────────────┐   │   │
│  └──────────────────────────────────────────────────────  │  ─┘   │
│                                                           │         │
│  ┌────────────────────────────────────────────────────────▼──────┐ │
│  │  ThreadPool (thread_pool.cpp)                                  │ │
│  │                                                                 │ │
│  │  queue_: [ fd5 | fd8 ]     mutex_ + cond_not_empty_           │ │
│  │                                                                 │ │
│  │  Worker-0    Worker-1    Worker-2    Worker-3                  │ │
│  │  (pthread)   (pthread)   (pthread)   (pthread)                 │ │
│  │     │            │                                              │ │
│  │     ▼            ▼                                              │ │
│  │  ConnectionHandler::handle(fd)  (for each client)              │ │
│  │    configureSocket()  ← SO_RCVBUF, TCP_NODELAY, etc.          │ │
│  │    buildSession()     ← getpeername(), assign session ID       │ │
│  │    welcome banner     ← writeAll()                             │ │
│  │    loop:                                                        │ │
│  │      readLine()       ← select(timeout=60s) + recv()          │ │
│  │      processCommand() ← PING/ECHO/TIME/STATS/INFO/WHO/...     │ │
│  │      writeAll()       ← send() loop                           │ │
│  │    close(fd)                                                    │ │
│  └────────────────────────────────────────────────────────────────┘ │
│                                                                      │
│  ┌──────────────────────┐  ┌───────────────────────────────────┐   │
│  │  Stats (atomic)      │  │  ClientRegistry (mutex map)        │   │
│  │  All threads update  │  │  All threads read/write            │   │
│  │  lock-free           │  │  Used for BROADCAST & WHO          │   │
│  └──────────────────────┘  └───────────────────────────────────┘   │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │  Logger (mutex-protected singleton)                           │   │
│  │  All threads write through this to stdout / server.log        │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
```

### 11.3 Data Flow Walkthrough

Here's exactly what happens when Client A connects and types `PING`:

```
1. Client A: connect(server_ip, 8080)
   → TCP 3-way handshake completes in the kernel
   → server's listen() backlog queue now has 1 pending connection

2. Main thread: select() returns (server_fd is readable)
   → accept() → returns client_fd = 7
   → inet_ntop() → "127.0.0.1"
   → pool.submit(7)

3. ThreadPool::submit(7):
   → pthread_mutex_lock(&mutex_)
   → queue_.push(7)            queue: [7]
   → pthread_cond_signal(&cond_not_empty_)
   → pthread_mutex_unlock(&mutex_)

4. Worker-0 wakes from pthread_cond_wait():
   → fd = queue_.front() = 7; queue_.pop()   queue: []
   → pthread_cond_signal(&cond_not_full_)
   → pthread_mutex_unlock(&mutex_)
   → active_++

5. Worker-0: ConnectionHandler::handle(7)
   → configureSocket(7):
       setsockopt(7, IPPROTO_TCP, TCP_NODELAY, ...)
       setsockopt(7, SOL_SOCKET, SO_RCVBUF, 65536, ...)
       setsockopt(7, SOL_SOCKET, SO_SNDBUF, 65536, ...)
   → buildSession(7): sess.id=1, sess.remote_ip="127.0.0.1"
   → ClientRegistry::add(1, 7, "127.0.0.1:54321")
   → Stats::onConnect()
   → writeAll(7, banner, ...)   ← send welcome message

6. Worker-0 (protocol loop):
   → writeAll(7, "> ", 2)       ← send prompt
   → readLine(7, buf, 4096, 60):
       select(8, {fd=7}, NULL, NULL, timeout=60s)
       → client sends "PING\r\n"
       → select() returns 1 (fd 7 is readable)
       → recv(7, &c, 1, 0) × 6 chars  ("P","I","N","G","\r","\n")
       → strips \r, stops at \n
       → returns 4 ("PING")

7. Worker-0: processCommand("PING", sess)
   → returns "PONG\r\n"
   → writeAll(7, "PONG\r\n", 6)
       send(7, "PONG\r\n", 6, MSG_NOSIGNAL)
       → kernel puts "PONG\r\n" in send buffer
       → TCP delivers to client

8. Client A receives "PONG\r\n"
```

---

## 12. File-by-File Code Walkthrough

### 12.1 logger.hpp / logger.cpp

**Purpose:** A thread-safe, timestamped logger that writes to stdout and
optionally to a file.

**Design:** Singleton pattern — one global instance, accessed via
`Logger::instance()`. C++11 guarantees that local static initialisation
is thread-safe ("magic statics"):

```cpp
static Logger& instance() {
    static Logger inst;  // created exactly once, thread-safely
    return inst;
}
```

**Thread safety:** A single `pthread_mutex_t mutex_` protects all writes.
The log line is formatted OUTSIDE the lock (in a local `std::ostringstream`)
to minimise lock-hold time:

```cpp
void Logger::log(LogLevel level, const std::string& msg, ...) {
    // Format OUTSIDE the lock — no contention here
    std::ostringstream os;
    os << "[" << timestamp() << "] [" << levelStr(level) << "] " << msg << "\n";
    const std::string out = os.str();

    // Lock ONLY for the actual write
    pthread_mutex_lock(&mutex_);
    std::cout << out;
    if (log_to_file_) file_ << out;
    pthread_mutex_unlock(&mutex_);
}
```

**Convenience macros** capture `__FILE__` and `__LINE__` automatically:
```cpp
#define LOG_INFO(msg) do { \
    std::ostringstream _os; _os << msg; \
    Logger::instance().log(LogLevel::INFO, _os.str(), __FILE__, __LINE__); \
} while(0)
```

The `do { } while(0)` trick makes the macro safe inside `if` statements
without braces.

**Thread ID in log output:**
Each log line includes `[tid:XXXXXXX]` — this is `pthread_self()`, the
unique ID of the thread that made the log call. This is invaluable for
tracing which worker handled which client.

### 12.2 stats.hpp / stats.cpp

**Purpose:** Server-wide statistics updated by every thread, with zero lock
contention.

All fields are `std::atomic<uint64_t>`:
```cpp
std::atomic<uint64_t> total_connections_{0};
std::atomic<uint64_t> active_connections_{0};
std::atomic<uint64_t> total_bytes_sent_{0};
std::atomic<uint64_t> total_bytes_recv_{0};
std::atomic<uint64_t> total_messages_{0};
std::atomic<uint64_t> total_errors_{0};
```

The update API is intentionally simple:
```cpp
void onConnect()   { ++total_connections_; ++active_connections_; }
void onDisconnect(){ --active_connections_; }
void onMessage()   { ++total_messages_; }
```

`report()` formats a human-readable summary including uptime (computed from
`start_time_` captured at construction), bytes in human units (B/KiB/MiB/GiB).

### 12.3 client_registry.hpp / client_registry.cpp

**Purpose:** A global map of all active client sessions, enabling the WHO
and BROADCAST commands to work across threads.

**Internal structure:**
```cpp
struct ClientInfo { int fd; std::string remote_addr; };
std::unordered_map<uint64_t, ClientInfo> clients_;  // session_id → ClientInfo
mutable pthread_mutex_t mutex_;
```

**Key design — copy then release:**
```cpp
int ClientRegistry::broadcast(const std::string& msg, uint64_t sender_id) {
    // Phase 1: collect all target fds (brief lock)
    std::vector<int> fds;
    pthread_mutex_lock(&mutex_);
    for (auto& [id, info] : clients_)
        if (id != sender_id) fds.push_back(info.fd);
    pthread_mutex_unlock(&mutex_);  // ← released BEFORE send()

    // Phase 2: send without the lock (can block)
    for (int fd : fds)
        send(fd, msg.c_str(), msg.size(), MSG_NOSIGNAL);
}
```

`MSG_NOSIGNAL`: Without this flag, if the remote side has closed the connection,
`send()` would raise `SIGPIPE`, which by default kills the process. `MSG_NOSIGNAL`
suppresses it — we handle errors via the return value instead.

### 12.4 thread_pool.hpp / thread_pool.cpp

**Purpose:** Pre-create N threads that process incoming connections from a
bounded queue.

**Constructor** initialises all POSIX objects then spawns threads:
```cpp
ThreadPool::ThreadPool(size_t num_threads, size_t max_queue) {
    pthread_mutex_init(&mutex_,          nullptr);
    pthread_cond_init (&cond_not_empty_, nullptr);
    pthread_cond_init (&cond_not_full_,  nullptr);

    for (size_t i = 0; i < num_threads; ++i) {
        auto* arg = new WorkerArg{this, i};  // heap-allocated to avoid
                                              // dangling ptr after this scope
        pthread_t tid;
        pthread_create(&tid, nullptr, workerEntry, arg);
        threads_.push_back(tid);
    }
}
```

**Static `workerEntry`** bridges the C API to the C++ member function:
```cpp
static void* workerEntry(void* arg) {
    auto* wa = static_cast<WorkerArg*>(arg);
    wa->pool->workerLoop(wa->id);  // calls the actual C++ method
    delete wa;                      // clean up heap allocation
    return nullptr;
}
```

**`submit()` in non-blocking mode (used in server.cpp):**
```cpp
if (!pool.submit(cfd, false)) {
    // Queue full → reject immediately (don't block the accept loop)
    send(cfd, "ERR: Server busy\r\n", ..., MSG_NOSIGNAL);
    close(cfd);
}
```

This is crucial: the main accept loop must never block — it needs to keep
calling `accept()` for new connections. Using `block=false` ensures `submit()`
returns instantly even when the pool is saturated.

### 12.5 connection_handler.hpp / connection_handler.cpp

**Purpose:** Everything that happens during a single client's session — from
socket configuration to command processing to cleanup.

**`configureSocket()`** applies 5 socket options:
```cpp
setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)); // no Nagle
setsockopt(fd, SOL_SOCKET,  SO_RCVBUF,  &rbuf, sizeof(rbuf)); // 64KB rcv buf
setsockopt(fd, SOL_SOCKET,  SO_SNDBUF,  &sbuf, sizeof(sbuf)); // 64KB snd buf
setsockopt(fd, SOL_SOCKET,  SO_KEEPALIVE, &ka, sizeof(ka));   // TCP keepalive
setsockopt(fd, SOL_SOCKET,  SO_LINGER,   &lg, sizeof(lg));    // clean close
```

**`buildSession()`** captures the client's IP and port using `getpeername()`:
```cpp
struct sockaddr_in addr{};
socklen_t addrlen = sizeof(addr);
getpeername(fd, (struct sockaddr*)&addr, &addrlen);
// addr.sin_addr → binary IP → inet_ntop() → "192.168.1.5"
// addr.sin_port → network byte order → ntohs() → 54321
```

**`writeAll()`** — why we loop:
```cpp
bool ConnectionHandler::writeAll(int fd, const char* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, MSG_NOSIGNAL);
        if (n <= 0) return false;  // error or closed
        sent += n;
    }
    return true;
}
```

`send()` is allowed to do a "short write" — it may copy fewer bytes than
requested into the kernel buffer. This loop retries from where the last
`send()` left off until all bytes have been sent.

**Session ID generation** uses a process-wide atomic counter:
```cpp
static std::atomic<uint64_t> g_session_counter{1};

uint64_t ConnectionHandler::nextId() {
    return g_session_counter.fetch_add(1, std::memory_order_relaxed);
}
```

`fetch_add(1)` atomically adds 1 and returns the OLD value — so session IDs
are 1, 2, 3, ... even when multiple threads call `nextId()` simultaneously.

### 12.6 server.cpp — Main Entry Point

**`createServerSocket()`:**
```cpp
int fd = socket(AF_INET, SOCK_STREAM, 0);

int opt = 1;
setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)); // fast restart
setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt)); // load balancing

struct sockaddr_in addr{};
addr.sin_family      = AF_INET;
addr.sin_addr.s_addr = INADDR_ANY;     // listen on all interfaces
addr.sin_port        = htons(cfg.port);

bind(fd, (struct sockaddr*)&addr, sizeof(addr));
listen(fd, cfg.backlog);               // start queuing connections
```

**Signal setup:**
```cpp
struct sigaction sa{};
sa.sa_handler = signalHandler;  // our handler
sigemptyset(&sa.sa_mask);       // don't block other signals during handler
sa.sa_flags   = SA_RESTART;     // auto-restart syscalls interrupted by signal
sigaction(SIGINT,  &sa, nullptr);  // Ctrl+C
sigaction(SIGTERM, &sa, nullptr);  // kill command
signal(SIGPIPE, SIG_IGN);          // ignore SIGPIPE (broken pipes handled via send() rc)
```

`SA_RESTART` means that if a signal interrupts a system call (like `accept()`),
the kernel restarts the call automatically rather than returning `EINTR`.

**The accept loop:**
```cpp
while (running) {
    fd_set rdset; FD_ZERO(&rdset);
    FD_SET(server_fd,        &rdset);
    FD_SET(g_wakeup_pipe[0], &rdset);
    int max_fd = std::max(server_fd, g_wakeup_pipe[0]);

    struct timeval tv { 5, 0 };  // 5-second heartbeat
    int rc = select(max_fd + 1, &rdset, nullptr, nullptr, &tv);

    if (rc == 0) { /* heartbeat log */ continue; }
    if (FD_ISSET(g_wakeup_pipe[0], &rdset)) { /* shutdown */ break; }
    if (FD_ISSET(server_fd, &rdset)) {
        int cfd = accept(server_fd, ...);
        pool.submit(cfd, false);  // non-blocking submit
    }
}
```

### 12.7 client.cpp — Test Client

Two modes:

**Benchmark mode** (default):
- Spawns N `pthread`s, each connecting and sending M commands
- Measures wall-clock time from first connect to last disconnect
- Reports throughput in messages/second
- Used to verify the server's concurrency correctness

```cpp
for (int i = 0; i < cfg.num_threads; ++i) {
    pthread_create(&threads[i], nullptr, benchmarkWorker, &results[i]);
}
for (int i = 0; i < cfg.num_threads; ++i)
    pthread_join(threads[i], nullptr);  // wait for all to finish
```

**Interactive mode** (`-i` flag):
- Reads commands from stdin, sends to server, prints responses
- Equivalent to telnet/nc but built into the project
- Useful for manual testing

**`recvUntilPrompt()`** reads char-by-char until it sees `"> "` (the server's
prompt), which signals the server is ready for the next command. This is a
simple state machine for the application protocol.

---

## 13. How to Build

### 13.1 Prerequisites

```bash
# Check if you have g++ with C++17 support
g++ --version      # Need: g++ >= 7

# Install if missing (Ubuntu/Debian)
sudo apt-get install g++ make

# pthreads is part of glibc — already installed on any Linux system
```

### 13.2 Build Commands

```bash
cd ~/multithreaded-tcp-server

# Build both server and client executables
make

# Build with extra debug symbols (add to Makefile CXXFLAGS if needed)
make CXXFLAGS="-std=c++17 -Wall -Wextra -O0 -g -I./include"

# Clean all build artefacts
make clean

# Rebuild from scratch
make clean && make
```

**Expected output:**
```
g++ -std=c++17 -Wall -Wextra -O2 -I./include -c -o build/srv_server.o src/server.cpp
g++ -std=c++17 -Wall -Wextra -O2 -I./include -c -o build/srv_thread_pool.o src/thread_pool.cpp
...
Linked: build/server
Linked: build/client

  Build complete!
  Start server : ./build/server -p 8080 -t 4
  Benchmark    : ./build/client -t 10 -n 20
  Interactive  : ./build/client -i
```

### 13.3 Understanding the Makefile

```makefile
CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -I./include
LDFLAGS  := -lpthread
```

- `-std=c++17` — use C++17 (needed for structured bindings `auto& [id, info]`)
- `-Wall -Wextra` — enable all warnings (good practice)
- `-O2` — optimisation level 2 (fast, still debuggable)
- `-I./include` — add `include/` to the header search path
- `-lpthread` — link against the POSIX threads library

**Separate object file prefixes for server vs client:**
```makefile
SERVER_OBJS := $(patsubst src/%.cpp, $(BUILD)/srv_%.o, $(SERVER_SRCS))
CLIENT_OBJS := $(patsubst src/%.cpp, $(BUILD)/cli_%.o, $(CLIENT_SRCS))
```

Both the server and the client include `logger.cpp`. Without separate prefixes,
they would produce the same `logger.o` file and one would overwrite the other.
By using `srv_logger.o` vs `cli_logger.o`, both can be compiled independently.

---

## 14. How to Run

### 14.1 Starting the Server

```bash
# Terminal 1 — start the server
cd ~/multithreaded-tcp-server

# Basic start (port 8080, 4 threads, INFO logging)
./build/server

# Custom port and thread count
./build/server -p 9090 -t 8

# Verbose mode (shows DEBUG log lines including per-message details)
./build/server -p 8080 -t 4 -v

# Write logs to server.log file as well as stdout
./build/server -p 8080 -t 4 -l

# Combined: verbose + log file
./build/server -p 8080 -t 4 -v -l
```

You should see:
```
  ████████╗ ██████╗██████╗ ...
  Listening on   : 0.0.0.0:8080
  Worker threads : 4
  Listen backlog : 128
  Connect via    : telnet localhost 8080  or  nc localhost 8080
  Ctrl+C to stop.

[2026-03-30 19:00:00] [INFO ] [tid:...] Worker #0 ready
[2026-03-30 19:00:00] [INFO ] [tid:...] Worker #1 ready
[2026-03-30 19:00:00] [INFO ] [tid:...] Worker #2 ready
[2026-03-30 19:00:00] [INFO ] [tid:...] Worker #3 ready
[2026-03-30 19:00:00] [INFO ] [tid:...] Server started — port=8080 threads=4
```

### 14.2 Connecting Interactively

```bash
# Terminal 2 — connect using the built-in client
./build/client -i

# Or use system tools
telnet localhost 8080
nc localhost 8080          # netcat
nc -C localhost 8080       # netcat with CRLF line endings (more compatible)
```

You will see the welcome banner:
```
╔══════════════════════════════════════════════╗
║    Multi-Threaded TCP/IP Server  v1.0        ║
╚══════════════════════════════════════════════╝
Type HELP for available commands.
>
```

Try commands:
```
> PING
PONG
> ECHO Hello World
Hello World
> TIME
Mon Mar 30 19:00:00 2026
> STATS
=== Server Statistics ===
  Uptime         : 42 sec
  Total connects : 3
  Active connects: 2
  ...
> INFO
+-----------------------------------------+
| Your Session Info                       |
  Session ID   : 1
  Remote addr  : 127.0.0.1:54321
  ...
> WHO
Connected clients (2):
  [1] 127.0.0.1:54321
  [2] 127.0.0.1:54322
> BROADCAST Hello everyone!
OK: delivered to 1 client(s)
> QUIT
Goodbye!
```

### 14.3 Running the Benchmark

```bash
# In a second terminal (server must be running in the first):

# 10 concurrent clients, 20 messages each
./build/client -t 10 -n 20

# 50 concurrent clients, 50 messages each (stress test)
./build/client -t 50 -n 50

# Single client, 100 messages, verbose (shows each command/response)
./build/client -t 1 -n 100 -v

# Connect to a remote server
./build/client -h 192.168.1.10 -p 8080 -t 5 -n 10
```

**Sample benchmark output:**
```
Benchmark: 8 thread(s) x 15 msgs -> 127.0.0.1:8080
-------------------------------------------------------
[Thread   0]   15 msgs  5.2 ms
[Thread   6]   15 msgs  7.9 ms
[Thread   1]   15 msgs  8.7 ms
[Thread   4]   15 msgs  9.6 ms
[Thread   2]   15 msgs  10.1 ms
[Thread   3]   15 msgs  12.6 ms
[Thread   7]   15 msgs  14.9 ms
[Thread   5]   15 msgs  17.3 ms
-------------------------------------------------------
  Total threads  : 8
  Total messages : 120
  Failed threads : 0
  Wall time      : 17.6 ms
  Throughput     : 6827 msgs/sec
-------------------------------------------------------
```

### 14.4 Server Command-Line Options

```
./build/server [OPTIONS]

  -p, --port    <port>   Listening port (default: 8080)
  -t, --threads <n>      Worker thread count (default: 4)
  -b, --backlog <n>      TCP listen() backlog (default: 128)
  -v, --verbose          Enable DEBUG-level logging
  -l, --logfile          Also write logs to server.log
  -h, --help             Show help and exit
```

**Client options:**
```
./build/client [OPTIONS]

  -h, --host     <host>  Server hostname or IP (default: 127.0.0.1)
  -p, --port     <port>  Server port (default: 8080)
  -t, --threads  <n>     Number of concurrent client threads (default: 1)
  -n, --msgs     <n>     Messages per client thread (default: 10)
  -i, --interactive      Interactive shell mode (single connection)
  -v, --verbose          Print each command and response
  --help                 Show help
```

### 14.5 Protocol Commands Reference

| Command | Syntax | Description | Response |
|---------|--------|-------------|----------|
| PING | `PING` | Connectivity check | `PONG` |
| ECHO | `ECHO <message>` | Echoes the message back | `<message>` |
| TIME | `TIME` | Current server time | Human-readable timestamp |
| STATS | `STATS` | Server-wide statistics | Uptime, connections, bytes |
| INFO | `INFO` | Your session details | Session ID, IP, message count |
| WHO | `WHO` | List all connected clients | Session IDs and addresses |
| BROADCAST | `BROADCAST <message>` | Send message to all other clients | `OK: delivered to N client(s)` |
| HELP | `HELP` | Command reference | Full list |
| QUIT | `QUIT` or `BYE` or `EXIT` | Close connection | `Goodbye!` |

---

## 15. How to Push to GitHub

```bash
cd ~/multithreaded-tcp-server

# 1. The repo is already initialised and committed (done during setup)
git log --oneline
# ca4c941 (HEAD -> master) Initial commit: Multi-Threaded TCP/IP Server in C++

# 2. Create a new repository on github.com (do NOT initialise with README)
#    Name suggestion: multithreaded-tcp-server

# 3. Add the remote
git remote add origin https://github.com/YOUR_USERNAME/multithreaded-tcp-server.git

# 4. Push
git branch -M main
git push -u origin main

# 5. Future changes
git add -A
git commit -m "feat: describe your change"
git push
```

**Recommended GitHub repository settings:**
- Description: "High-concurrency TCP/IP server in C++ using POSIX threads, mutexes, condition variables, and select()-based non-blocking I/O"
- Topics: `cpp`, `tcp-ip`, `pthreads`, `networking`, `systems-programming`, `linux`, `socket-programming`
- Add a language badge: the project will auto-detect as C++

---

## 16. Common Errors & Fixes

| Error | Cause | Fix |
|-------|-------|-----|
| `bind(): Address already in use` | Port 8080 in use by another process | `lsof -i :8080` to find it; or use `-p 9090` |
| `connect failed: Connection refused` | Server not running | Start server first: `./build/server` |
| `pthread_create failed: Resource temporarily unavailable` | Too many threads | Reduce `-t` value |
| `select(): Bad file descriptor` | fd closed before select() | Check for logic errors in fd management |
| `Makefile:16: *** missing separator` | Spaces instead of tabs in Makefile | Replace leading spaces with tabs in recipe lines |
| `error: structured bindings only available with -std=c++17` | Using C++14 or older | Ensure `-std=c++17` in CXXFLAGS |
| Garbled output in terminal | telnet sending 2-byte CRLF but server echoing \r | Use `nc` or the built-in client instead of telnet |

---

## 17. Interview Q&A — Every Question You Will Be Asked

**Q: Walk me through the architecture of your server.**

A: The server has three layers. The main thread runs a `select()` loop that
watches the server socket for new connections and a self-pipe for shutdown
signals. When a new connection arrives, it calls `accept()` and submits the
resulting file descriptor to a fixed-size thread pool using a mutex-protected
bounded queue. Worker threads pop file descriptors from the queue and run
`ConnectionHandler::handle()`, which configures the socket, then loops reading
commands with a 60-second `select()` timeout and dispatching responses. Shared
state (stats, client registry, logger) uses mutexes or atomics for
thread safety.

---

**Q: What is a mutex and why do you need one in a server?**

A: A mutex is a synchronisation primitive that ensures mutual exclusion —
only one thread can hold it at a time. Without one, two threads writing to
the same `std::cout` simultaneously produce interleaved, garbled output,
and two threads modifying a `std::map` concurrently cause memory corruption.
I use mutexes in the logger (one mutex protecting stdout), in `ClientRegistry`
(one mutex protecting the client map), and in `ThreadPool` (one mutex
protecting the task queue).

---

**Q: Explain condition variables. Why not just use a mutex?**

A: A mutex protects shared data, but it doesn't let a thread *wait* for a
condition to become true. Without condition variables, a worker thread would
have to busy-wait: `while (queue.empty()) { unlock(); sleep(1ms); lock(); }`.
This wastes CPU. `pthread_cond_wait()` atomically releases the mutex and
suspends the thread until `pthread_cond_signal()` is called — zero CPU usage
while idle. The "atomically releases" part is critical: it prevents the race
where the producer adds a task and signals *before* the consumer has started
waiting (the signal would be lost).

---

**Q: What is the self-pipe trick?**

A: Signal handlers run asynchronously and can interrupt any instruction in the
program. The only async-signal-safe operations are a small set of system calls
like `write()`. We can't call `pthread_mutex_lock()` in a signal handler
because the handler might interrupt a thread that is already holding the mutex,
causing a deadlock. The problem is that `select()` needs a file descriptor to
watch — it can't watch a variable. The solution: create a `pipe()` at startup.
The signal handler calls `write(pipe[1], sig, 1)` — safe and non-blocking.
The main `select()` loop watches `pipe[0]`. When the signal arrives, `select()`
returns immediately, the main loop reads the byte, and performs the shutdown.

---

**Q: What is a data race? Did you have any in this project?**

A: A data race is when two threads access the same memory location concurrently,
at least one is writing, and there is no synchronisation between them. The result
is undefined behaviour — the value read may be any combination of old and new
bytes. I prevented data races by: (1) protecting `std::cout` and `std::map` with
mutexes, (2) using `std::atomic` for all counters in `Stats`, (3) using a mutex
for the `ThreadPool` task queue. I never access shared state without appropriate
synchronisation.

---

**Q: Why use a thread pool instead of creating a new thread per connection?**

A: Three reasons. First, `pthread_create()` has overhead of ~5–20 microseconds
and allocates ~2–8 MB of stack per thread. Under high load, this adds up. Second,
the OS scheduler degrades with thousands of threads — they all want CPU time but
most are just waiting for I/O. Third, unbounded thread creation can exhaust
system resources (Linux default stack limit leads to ~4,000 threads max). A
thread pool limits both memory usage and scheduler overhead. The fixed N threads
are created once at startup, and connection-handling work is distributed to them
via a lock-free (well, mutex-protected) queue.

---

**Q: What is non-blocking I/O? How did you implement it?**

A: Non-blocking I/O means that I/O operations return immediately even if no data
is available, rather than blocking the thread indefinitely. I implemented it using
`select()` with a timeout. Before each `recv()` call, I call `select()` with a
60-second timeout. If `select()` returns 0, the client has been idle for 60
seconds and I warn them. If it returns a positive number, data is available and
`recv()` is guaranteed not to block. This is different from setting `O_NONBLOCK`
on the socket, which makes `recv()` return `EAGAIN` immediately if no data is
ready — requiring an explicit retry loop.

---

**Q: Explain the socket options you tuned and why.**

A: `TCP_NODELAY` disables Nagle's algorithm, which would batch small writes to
reduce packet count. For a command-response protocol, batching adds latency — I
want each response sent immediately. `SO_RCVBUF` and `SO_SNDBUF` increase the
kernel ring buffers from the default ~8KB to 64KB — larger buffers mean
`send()`/`recv()` block less frequently under load. `SO_REUSEADDR` allows the
server to restart immediately after stopping without waiting 60 seconds for
the OS to release the port (TIME_WAIT state). `SO_KEEPALIVE` makes the kernel
send probe packets after an idle period to detect dead connections — clients
that crash without sending a FIN.

---

**Q: How does your server handle Ctrl+C gracefully?**

A: I register a `SIGINT` handler using `sigaction()`. The handler writes 1 byte
to the write end of a self-pipe. The main `select()` loop watches both the server
socket and the read end of the pipe. When the pipe becomes readable, the main
loop reads the byte, sets `running=false`, and falls through to the shutdown
code. The shutdown calls `ThreadPool::shutdown()`, which sets `stop_=true`,
broadcasts the condition variable (waking all sleeping workers), then calls
`pthread_join()` on each worker thread to wait for any in-flight connections
to finish cleanly. Only then does the process exit.

---

**Q: What would you improve if you were building a production server?**

A: Several things. Replace `select()` with `epoll()` for scalability to 10,000+
concurrent connections — `select()` is O(n) while `epoll()` is O(1) per event.
Add TLS using OpenSSL or wolfSSL. Use `SO_REUSEPORT` with multiple accept threads
for better multi-core utilisation. Implement connection rate limiting to prevent
DDoS. Add a configuration file (JSON/YAML) instead of just command-line flags.
Use `SIGALRM` or a timer thread for more precise idle-connection timeouts.
Consider `io_uring` (Linux 5.1+) for truly asynchronous I/O with kernel support.

---

## 18. Glossary

| Term | Definition |
|------|-----------|
| **fd (file descriptor)** | An integer handle for an open resource (file, socket, pipe). 0=stdin, 1=stdout, 2=stderr, 3+ = program-defined. |
| **socket** | An fd that represents a network endpoint. Created with `socket()`. |
| **TCP** | Transmission Control Protocol — reliable, ordered, connection-oriented transport |
| **IP** | Internet Protocol — routes packets across networks |
| **port** | 16-bit number that identifies a service on a machine |
| **bind** | Associate a socket with a local IP:port |
| **listen** | Start accepting incoming connections; set the pending-connection queue depth |
| **accept** | Dequeue one pending connection; return a new fd for that client |
| **thread** | Independent execution path within a process; shares heap/globals |
| **mutex** | Binary lock: only one thread can hold it at a time |
| **condition variable** | Mechanism for a thread to sleep until a condition changes |
| **atomic** | Hardware-level indivisible operation; no mutex needed |
| **IPC** | Inter-Process (or Inter-Thread) Communication |
| **self-pipe** | A pipe used to convert a signal into a readable fd event |
| **data race** | Two threads accessing shared memory concurrently with no sync; undefined behavior |
| **deadlock** | Two threads each hold a lock the other needs; both wait forever |
| **thread pool** | Pre-created N threads that reuse their lifetime handling many tasks |
| **producer-consumer** | Pattern: one entity generates work items, another processes them, via a shared queue |
| **back-pressure** | Slowing down the producer when the consumer can't keep up |
| **Nagle's algorithm** | TCP optimisation that batches small writes; causes latency for interactive protocols |
| **select()** | Syscall that waits for any of a set of fds to become ready |
| **SO_REUSEADDR** | Allows re-binding a port in TIME_WAIT state |
| **SO_RCVBUF** | Kernel receive ring buffer size |
| **TCP_NODELAY** | Disable Nagle; send data immediately |
| **MSG_NOSIGNAL** | Flag for `send()` to suppress SIGPIPE on broken connections |
| **TIME_WAIT** | TCP state after close(); lasts ~60s; prevents port re-use without SO_REUSEADDR |
| **htons()** | host-to-network short; convert 16-bit integer to big-endian |
| **POSIX** | Portable Operating System Interface; the Unix API standard |
| **singleton** | Design pattern: class that has exactly one instance |
| **graceful shutdown** | Finishing in-flight work before exiting; opposite of SIGKILL |
| **throughput** | Amount of work done per unit time (here: messages/second) |
| **latency** | Time from request to response |
