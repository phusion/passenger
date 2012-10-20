# Debugging Phusion Passenger

This guide tells you how to debug Phusion Passenger in case of any serious problems, e.g. crashes and mysterious connection problems.

## Process output

All Phusion Passenger agent processes (PassengerWatchdog, PassengerHelperAgent, PassengerLoggingAgent) as well as all spawned application processes have their stdout and stderr redirected to the global web server error log. This is usually '/var/log/apache2/error.log' or '/var/log/nginx/error.log'.

## Crash behavior

Whenever a Phusion Passenger agent process crashes because of a signal (SIGABRT, SIGBUS, SIGSEGV and similar signals), its default behavior is to attempt to write a crash report to its stderr. This crash report contains:

 * A simple libc-level backtrace of the current thread. This backtrace may or may not correspond to the thread that caused the crash.
 * A detailed backtrace report, covering all threads. This report even contains the values of variables on the stack. The report is obtained through the [crash-watch](https://github.com/FooBarWidget/crash-watch) tool so you must have it installed. Crash-watch in turn requires gdb, which must also be installed.
 * Agent-specific diagnostics information. For example the HelperAgent will report the status of its process pool and its connected clients.

You can change the crash behavior with the following environment variables:

 * `PASSENGER_ABORT_HANDLER` (default: true) - Whether agent processes should install their crash handlers. When disabled, crashes will be handled by the default signal handler, meaning that they will likely just crash without dumping any crash report.
 * `PASSENGER_DUMP_WITH_CRASH_WATCH` (default: true) - Whether [crash-watch](https://github.com/FooBarWidget/crash-watch) should be used to obtain detailed backtraces.
 * `PASSENGER_BEEP_ON_ABORT` (default: false) - Whether agent processes should beep when they crash. This is useful during development, e.g. when you're stress testing the system and want to be notified when a crash occurs. On OS X, it will execute `osascript -e "beep 2"` to trigger the beep. On Linux it will execute the `beep` command.
 * `PASSENGER_STOP_ON_ABORT` (default: false) - When enabled, causes agent processes to stop themselves on crash, by raising SIGSTOP. This gives you the opportunity to attach gdb on them.

## Behavior logging

## Debugging with AddressSanitizer
