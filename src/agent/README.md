# Passenger Agent

This directory contains the source code of the Passenger agent. The Passenger agent is a single executable that consists of multiple parts. Within each subdirectory, you will find the source code of that particular part.

## Important parts

The most important parts are:

 * The Watchdog is the main Passenger process. It starts the Passenger Core and the UstRouter, and restarts them when they crash. It also cleans everything up upon shut down.
 * The Core performs most of the heavy lifting. It parses requests, spawns application processes, forwards requests to the correct process and forwards application responses back to the web server.
 * The UstRouter processes Union Station data and sends them to the Union Station server.

## Minor parts

 * SpawnPreparer is a tool used internally by the Core to spawn application processes.
 * SystemMetrics is a tool that shows system metrics such as CPU and memory usage. The main functionality is implemented in src/cxxUtils/SystemMetricsCollector.h. This tool is mainly useful for developing and debugging SystemMetricsCollector.h.
 * TempDirToucher is a tool used internally by Passenger Standalone to keep a temporary directory's timestamp up-to-date so that it doesn't get removed by /tmp cleaner daemons.

## Shared code

There is also code that is shared between the different agent parts. That code is located in `Shared`. Unlike `src/cxx_supportlib`, which is meant to be usable outside Passenger, the code in `Shared` is Passenger-specific.
