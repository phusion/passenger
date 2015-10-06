This directory contains the source code of the Passenger agent. The Passenger agent consists of multiple parts. Within each subdirectory, you will find the source code of that particular part.

The most important parts are:

 * The Watchdog is the main Passenger process. It starts the Passenger core and the UstRouter, and restarts them when they crash. It also cleans everything up upon shut down.
 * The Core performs most of the heavy lifting. It parses requests, spawns application processes, forwards requests to the correct process and forwards application responses back to the web server.
 * The UstRouter processes Union Station data and sends them to the Union Station server.

There is also code that is shared between the different agent parts. That code is located in `Shared`.
