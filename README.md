# devlinux_gateway_sensor

Mid-term project for the Devlinux course.

This project implements a multi-protocol sensor gateway running on Linux. It listens for sensor traffic over TCP/IP, validates incoming data, stores the results in an SQLite database, and logs important events. The gateway is designed to handle multiple sensor connections concurrently.

## Overview

The gateway accepts sensor packets from different device types and converts them into a unified internal data format before storing them.

Supported sensor protocols:
- TBP (Temperature Binary Protocol) on base port
- HLP (Humidity Line Protocol) on base port + 1
- EVP (Event TLV Protocol) on base port + 2
- SGP (Smoke-Gas Protocol) on base port + 3

Example:
- Base port = 1234
- HLP = 1235
- EVP = 1236
- SGP = 1237

Each incoming connection is processed by a dedicated thread. For SGP, the gateway must reply with ACK (0x06) or NAK (0x15). If the gateway rejects a packet, the client retries the message up to 3 times.

After successful parsing, the data is pushed into a shared buffer for validation and storage.

Current status:
- Implemented: TCP socket listener, protocol parsing, validation, SQLite storage, logging
- Not implemented: web/HTTP monitoring process and shared status feature

## Main components

### 1. Connection Manager (connmgr)
- Opens listening ports for all sensor protocols
- Accepts multiple TCP connections
- Parses incoming packets
- Handles protocol-specific ACK/NAK behavior for SGP
- Sends valid parsed data to the shared buffer

### 2. Data Manager (datamgr)
- Reads `room_sensor.txt`
- Authenticates sensor IDs
- Checks sensor type and valid value ranges
- Rejects invalid or malformed sensor readings before storing them

### 3. Storage Manager (stormgr)
- Connects to the SQLite database (`gateway.db`)
- Inserts validated records into the database
- Retries connection attempts up to 3 times when the database is unavailable

### 4. Logger
- Records key events such as gateway startup/shutdown
- Captures invalid sensor frames, alarm conditions, and database failures
- Helps monitor system behavior during testing and debugging

## Project structure

```text
.
├── Makefile
├── README.md
├── LICENSE
├── assets/
│   └── architecture_design.jpg
├── include/
│   ├── common.h
│   ├── connection_manager.h
│   ├── data_manager.h
│   ├── endecode_helper.h
│   ├── gateway.h
│   ├── logger.h
│   ├── protocol_adapter.h
│   ├── sensor_config.h
│   ├── sensor_types.h
│   ├── share_buffer.h
│   └── storage_manager.h
├── src/
│   ├── client.c
│   ├── connection_manager.c
│   ├── data_manager.c
│   ├── endecode_helper.c
│   ├── gateway.c
│   ├── logger.c
│   ├── share_buffer.c
│   └── storage_manager.c
├── test/
│   └── test_options.mk
├── room_sensor.txt
├── gateway.db
├── log_option.mk
├── compile_commands.json
└── lib/
    └── built libraries
```

## Prerequisites

- Linux OS
- GNU Make
- SQLite3 development library
- Valgrind (optional, for debugging and memory checking)

Install dependencies on Ubuntu/Debian:

```bash
sudo apt update
sudo apt install -y build-essential sqlite3 libsqlite3-dev valgrind
```

## Build the project

Build the default version:

```bash
make
```

Build with debug logging enabled:

```bash
make clean
make EXTRA_CFLAGS="-DDEBUG -DLOG_MAIN -DLOG_CONNMGR -DLOG_DATAMGR -DLOG_STORMGR -DLOG_SBUF"
```

The build output will generate binaries under `bin/` and libraries under `lib/`.

## Run the gateway

```bash
./bin/gateway <base-port> <http-port>
```

Example:

```bash
./bin/gateway 1234 8080
```

The second argument is reserved for the HTTP/web process, but the web monitoring feature is not implemented in the current project version. The gateway still runs correctly using the base port and the derived protocol ports.

## Run a sensor client

```bash
./bin/client <sensor-type> <sensor-id> <ip-address> <port> <interval-ms> [options]
```

Sensor types:
- temperature
- humidity
- motion
- door
- smoke_gas

Example:

```bash
./bin/client temperature 10 127.0.0.1 1234 1000
./bin/client humidity 21 127.0.0.1 1235 1000
./bin/client motion 30 127.0.0.1 1236 1000
./bin/client door 40 127.0.0.1 1236 1000
./bin/client smoke_gas 50 127.0.0.1 1237 1000
```

Optional client behaviors:
- `fragment`: split a frame into multiple packets
- `glue`: merge multiple frames before sending
- `corrupt`: inject malformed data for fault testing

## Example workflow

1. Build the project:
   ```bash
   make
   ```
2. Start the gateway:
   ```bash
   ./bin/gateway 1234 8080
   ```
3. Start several client sensors:
   ```bash
   ./bin/client temperature 10 127.0.0.1 1234 1000
   ./bin/client humidity 21 127.0.0.1 1235 1000
   ./bin/client smoke_gas 50 127.0.0.1 1237 1000
   ```
4. Observe the logs and database entries.

## Testing

Common test targets in the Makefile:

```bash
make gw
make gw_fail
make tbp
make hlp
make door
make motion
make sgp
make sgp_nak
make corrupt
make sql_fail
make sql_resume
make id_fail
```

These targets help validate the gateway under normal operating conditions, invalid inputs, corrupted packets, sensor ID failures, and database failure/retry cases.

## Debugging and validation

Memory leak check:

```bash
valgrind --leak-check=full --track-origins=yes ./bin/gateway 1234 8080
```

Thread/race condition check:

```bash
valgrind --tool=helgrind ./bin/gateway 1234 8080
```

## Notes

- This project is intended as a learning project and a gateway prototype.
- It focuses on networking, protocol parsing, validation, and SQLite persistence.
- The HTTP monitoring/dashboard is planned but not implemented yet.
- `room_sensor.txt` is used to define valid sensor IDs and ranges.

