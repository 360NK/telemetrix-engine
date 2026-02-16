# TELEMETRIX: Distributed High-Frequency Ingestion Engine

**Formerly:** _Ghost Bus_ | _TransitMind (Legacy)_ **Status:** Phase 1 (Ingestion)  
**Target Platform:** macOS (Intel) / Linux  
**Language:** C (C11 Standard)

---

## 1. The Mission

**Telemetrix** is a high-performance, embedded-grade telemetry logger designed to capture high-frequency transit data with zero packet loss.

Unlike standard "transit apps" that poll APIs using high-level languages (Python/JS), Telemetrix is a **Systems Engineering** project designed to demonstrate mastery of:

- **Manual Memory Management** (No Garbage Collection).
- **Concurrency** (Pthreads, Mutexes, Ring Buffers).
- **Low-Level Networking** (Raw Sockets/Libcurl).
- **Real-Time Physics** (Calculating vectors/jerk on the fly).

**The "Why":** This project is built specifically to bridge the gap between "Data Science" (Python) and "Infrastructure/Firmware Engineering" (C/C++), targeting roles at companies like **Comma.ai**, **Google**, and **Tesla**.

---

## 2. The Architecture (The "Pipeline")

The system is designed as a **Producer-Consumer Daemon**.

### A. The Source

- **Data:** GTFS-Realtime (Protocol Buffers).
- **Endpoint:** `https://opendata.hamilton.ca/GTFS-RT/GTFS_VehiclePositions.pb`
- **Frequency:** Polling every 1-3 seconds (High Frequency).

### B. The Components

#### 1. The Fetcher (Producer Thread)

- **Role:** The "Firehose."
- **Job:** Wakes up, connects to the HSR server via `libcurl`, and downloads the binary blob into Heap Memory.
- **Key Tech:** `curl_easy_perform`, `malloc`, `realloc`.
- **Constraint:** Must assume the network is unstable. Must handle timeouts.

#### 2. The Decoder (The Parser)

- **Role:** The "Translator."
- **Job:** Decodes the raw Protobuf bytes into a C `struct` using `libprotobuf-c`.
- **Physics Upgrade:** Calculates immediate dynamics:
  - $\Delta t$ (Time since last ping).
  - $\vec{v}$ (Velocity Vector).
  - $J$ (Jerk / Harsh Braking Events).

#### 3. The Ring Buffer (The Pipe)

- **Role:** The "Shock Absorber."
- **Job:** A fixed-size Circular Buffer (e.g., 1024 slots) in shared memory.
- **Logic:** The Producer writes to `head`; The Consumer reads from `tail`.
- **Safety:** Protected by `pthread_mutex` (or Atomic operations in Phase 2).
- **Why:** Prevents memory fragmentation and ensures O(1) write times.

#### 4. The Logger (Consumer Thread)

- **Role:** The "Scribe."
- **Job:** Reads structs from the Ring Buffer and writes them to a **Binary Write-Ahead Log (WAL)** on disk (`telemetry.bin`).
- **Why:** Writing to disk is slow. We decouple it from the network thread to prevent blocking.

---

## 3. The Tech Stack & Tools

| Component         | Tool / Library       | Reason                                       |
| :---------------- | :------------------- | :------------------------------------------- |
| **Language**      | **C** (C11)          | Direct memory control, zero overhead.        |
| **Compiler**      | **GCC / Clang**      | Standard compilation on macOS/Linux.         |
| **Build System**  | **Make**             | Simple, reproducible build scripts.          |
| **Networking**    | **libcurl**          | Stable, high-performance HTTP client.        |
| **Serialization** | **protobuf-c**       | C implementation of Google Protocol Buffers. |
| **Concurrency**   | **pthreads**         | POSIX standard for threading.                |
| **Debugging**     | **Valgrind / Leaks** | Memory leak detection (Crucial).             |
| **Hardware**      | **MacBook Air 2017** | UNIX-based environment (no WSL needed).      |

---

## 4. The 12-Day Engineering Sprint

### Phase 1: The Metal (Days 1-4)

_Focus: Memory & Networking_

- [x] **Day 1:** Write `main.c` to download bytes via `libcurl`. (Completed).
- [ ] **Day 2:** Compile `.proto` files and decode binary data into structs.
- [ ] **Day 3:** Implement the Event Loop (Sleep/Wake) and Signal Handling (`SIGINT`).
- [ ] **Day 4:** **Valgrind Audit.** Ensure 0 bytes memory leak.

### Phase 2: The Architecture (Days 5-8)

_Focus: Concurrency & IPC_

- [ ] **Day 5:** Implement the **Ring Buffer** data structure.
- [ ] **Day 6:** Split code into **Producer** and **Consumer** threads.
- [ ] **Day 7:** Implement **Mutex Locks** to prevent race conditions.
- [ ] **Day 8:** Implement **Binary Logging** (writing raw structs to disk).

### Phase 3: The System (Days 9-12)

_Focus: Reliability & Physics_

- [ ] **Day 9:** Daemonize the process (Run in background).
- [ ] **Day 10:** Containerize with **Docker** (Alpine Linux).
- [ ] **Day 11:** Implement **Physics Engine** (Velocity/Jerk calculations).
- [ ] **Day 12:** Final Benchmark & Documentation.

---

## 5. Key Reference Implementation Details

### Legacy Context (TransitMind)

_Note to AI:_ I previously built a Python version (`TransitMind`) using `psycopg2` and `requests`. It was too slow (Latency > 50ms) and relied on Garbage Collection. **Telemetrix** is the rewrite of the _Ingestion Layer only_. We are keeping the PostgreSQL database schema for final storage, but the Ingestion Engine is now pure C.

### Directory Structure

```text
telemetrix/
├── src/
│   ├── main.c           # Entry point
│   ├── fetcher.c        # Libcurl wrapper
│   ├── parser.c         # Protobuf logic
│   ├── buffer.c         # Ring Buffer implementation
│   └── logger.c         # Disk I/O
├── include/
│   ├── telemetrix.h     # Shared structs & headers
│   └── gtfs-realtime.pb-c.h # Generated by Protobuf
├── proto/
│   └── gtfs-realtime.proto # The definition file from Google/HSR
├── Makefile             # Compilation script
└── telemetry.bin        # The output log (binary format)
```
