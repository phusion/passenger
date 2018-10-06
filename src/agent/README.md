# Passenger Agent

This directory contains the source code of the Passenger agent. The Passenger agent is a single executable that consists of multiple parts. Within each subdirectory, you will find the source code of that particular part.

## Important parts

The most important parts are:

 * The Watchdog is the main Passenger process. It starts the Passenger Core, and restarts them when it crashes. It also cleans everything up upon shut down.
 * The Core performs most of the heavy lifting. It parses requests, spawns application processes, forwards requests to the correct process and forwards application responses back to the web server.

## Minor parts

 * SpawnEnvSetupper is a tool used internally by `Core/SpawningKit/` to spawn application processes. See the README in that directory for more information.
 * SystemMetrics is a tool that shows system metrics such as CPU and memory usage. The main functionality is implemented in src/cxx/SystemTools/SystemMetricsCollector.h. This tool is mainly useful for developing and debugging SystemMetricsCollector.h.
 * TempDirToucher is a tool used internally by Passenger Standalone to keep a temporary directory's timestamp up-to-date so that it doesn't get removed by /tmp cleaner daemons.

## Shared code

There is also code that is shared between the different agent parts. That code is located in `Shared`. Unlike `src/cxx_supportlib`, which is meant to be usable outside Passenger, the code in `Shared` is Passenger-specific.
