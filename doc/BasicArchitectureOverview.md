# Basic architecture overview

## Multi-process

- Web server (Apache or Nginx, both with the Passenger module loaded). Only simple I/O processing and lifecycle management of the Passenger Watchdog Agent.
- PassengerAgent: single binary w/ multiple personalities. Most important:
  - Watchdog: spawns and supervises Core Agent and a bunch of other periodic helper tasks.
  - Core: most logic happens here. Main request processing (Controller), spawns (SpawningKit) and supervises (ApplicationPool) application processes, keeps track of various statistics. Has an API server (separate socket) for management operations (used by e.g. `passenger-status`).

### HTTP request flow

1. Requests are commonly first received by the web server, which forwards request to Passenger Core (via its Unix domain socket in the instance directory). Core's Controller subsystem then processes it.
2. Requests may also be directly received by Passenger Core (Passenger Standalone + "builtin" engine).
3. Passenger Core's Controller subsystem uses ApplicationPool to determine whether to spawn an application process or which process to route request to. Connects to the app's Unix domain socket (also located in the instance directory) or reuses keepalive socket.
4. Response from app forwarded back to the connecting client (could be a real client or the web server).

### Applcation processes lifecycle management and request routing

- Main logic in ApplicationPool (src/agent/Core/ApplicationPool). Keeps track of which application Processes exist. Contains logic for routing a request to best Process.
- Pool has many Groups. Group has many Processes (for application processes). Process has many Sockets (of which one is the "main socket" for receiving requests).
- Spawning logic outsourced to SpawningKit.

## C++ tech stack

- Agent written in C++
- C++14
- Boost. But prefer C++14

### HTTP and non-blocking I/O handling

- ServerKit (src/cxx_supportlib/ServerKit)
- libev
- Some parts use libuv. Integrates with libev via SafeLibev.h

## Important directories

- src: most source code
- src/agent: various agents
- src/agent/Core
- src/agent/Watchdog
- src/nginx_module
- src/apache2_module
- src/cxx_supportlib: C++ support library
- src/ruby_supportlib: Ruby parts of passenger: Passenger Standalone, Passenger Config, admin tool helpers, shared Ruby app loading code, generic utilities
- src/helper-scripts: various helper scripts, including SpawningKit language-specific (pre)loaders.
- bin: user-invocable executables (CLI tools)
- build: build system
- test: unit and integration tests
- dev: scripts used during development and CI, not used during runtime
- resources: non-executable resources, used during runtime
- packaging: Debian/RPM/Homebrew packaging automation

## See also

An older [Design & Architecture](https://www.phusionpassenger.com/documentation/Design%20and%20Architecture.html) document.
