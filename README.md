# Telemetrix Engine (TransitMind)

# Telemetrix Engine (TransitMind)

A high-performance telemetry ingestion engine for the Hamilton Street Railway (HSR), written in C.

## Overview

Telemetrix Engine is the backbone of the TransitMind project. It is a systems-level tool designed to ingest, decode, and persist real-time GTFS-Realtime vehicle positions. By utilizing C for the ingestion layer, the system achieves minimal latency and low memory overhead compared to traditional high-level implementations.

## Technical Architecture

- **Networking:** `libcurl` handles asynchronous binary data retrieval from Hamilton Open Data endpoints.
- **Decoding:** Google Protocol Buffers (`protobuf-c`) are used to deserialize binary streams into structured C objects.
- **Memory Management:** Manual heap management and pointer navigation provide direct control over data buffers.
- **Persistence:** Moving toward an embedded **SQLite** implementation to facilitate rapid data analysis and historical logging.

## Why C?

This project demonstrates a "performance-first" approach to urban geography. Using C allows for:

1. **Binary Precision:** Direct handling of the GTFS-RT protocol without the overhead of JSON translation.
2. **Efficiency:** Capable of running on low-power edge devices or scaling to handle massive high-frequency data streams.
3. **Control:** Deep understanding of the data lifecycle, from the raw network socket to the database row.

## Current Project Status

- [x] **Phase 1: Ingestion** - Live connectivity to HSR binary endpoints.
- [x] **Phase 2: Decoding** - Full extraction of Fleet IDs, Route IDs, and GPS coordinates.
- [ ] **Phase 3: Persistence** - Implementation of SQLite for historical telemetry storage.
- [ ] **Phase 4: Spatial Analysis** - Correlating bus speeds with urban infrastructure conflicts.

## Build Instructions

Ensure you have `libcurl` and `protobuf-c` installed on your system.

```bash
make
./bin/telemetrix
```

## Data Insights

The engine currently extracts:

- **Fleet Number:** The physical vehicle identifier.
- **Trip/Route ID:** The active service assignment.
- **Geospatial Data:** High-precision Latitude and Longitude coordinates.
