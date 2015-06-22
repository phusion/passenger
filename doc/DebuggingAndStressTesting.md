# Debugging and Stress Testing Phusion Passenger

This guide tells you:

 * How to debug Phusion Passenger in case of any serious problems, e.g. crashes and mysterious connection problems.
 * How to stress test Phusion Passenger.

## Process output

All PassengerAgent processes as well as all spawned application processes have their stdout and stderr redirected to the _global web server error log_ (that is, _not_ the per-virtual host error log). This is usually '/var/log/apache2/error.log' or '/var/log/nginx/error.log'.

Note that in case of Nginx, Phusion Passenger prints to the error log specified in the server context, not the "http" context. If the server context does not contain an `error_log` directive then the default error log location will be used. The default location depends on how Nginx is configured during compilation, but it is usually either '$PREFIX/logs/error.log' or '/var/log/nginx/error.log'. For example, if your Nginx configuration looks like this:

    worker_processes 2;

    http {
        error_log /home/nginx/error.log;
        ...
    }

then Phusion Passenger will print to the default error log location, *not* '/home/nginx/error.log'!

## Crash behavior

Whenever a Phusion Passenger agent process crashes because of a signal (SIGABRT, SIGBUS, SIGSEGV and similar signals), its default behavior is to attempt to write a crash report to its stderr. This crash report contains:

 * A simple libc-level backtrace of the current thread. This backtrace may or may not correspond to the thread that caused the crash.
 * A detailed backtrace report, covering all threads. This report even contains the values of variables on the stack. The report is obtained through the [crash-watch](https://github.com/FooBarWidget/crash-watch) tool so you must have it installed. Crash-watch in turn requires gdb, which must also be installed.
 * Agent-specific diagnostics information. For example the Passenger core will report the status of its process pool and its connected clients.

You can change the crash behavior with the following environment variables:

 * `PASSENGER_ABORT_HANDLER` (default: true) - Whether agent processes should install their crash handlers. When disabled, crashes will be handled by the default signal handler, meaning that they will likely just crash without dumping any crash report.
 * `PASSENGER_DUMP_WITH_CRASH_WATCH` (default: true) - Whether [crash-watch](https://github.com/FooBarWidget/crash-watch) should be used to obtain detailed backtraces.
 * `PASSENGER_BEEP_ON_ABORT` (default: false) - Whether agent processes should beep when they crash. This is useful during development, e.g. when you're stress testing the system and want to be notified when a crash occurs. On OS X, it will execute `osascript -e "beep 2"` to trigger the beep. On Linux it will execute the `beep` command.
 * `PASSENGER_STOP_ON_ABORT` (default: false) - When enabled, causes agent processes to stop themselves on crash, by raising SIGSTOP. This gives you the opportunity to attach gdb on them.

## Behavior logging

Increase PassengerLogLevel to print more debugging messages.

## Debugging with AddressSanitizer

[AddressSanitizer](http://code.google.com/p/address-sanitizer/) is an excellent tool created by Google to detect memory problems in C and C++ programs. It is for example used for detecting memory errors in Google Chrome. Unlike [Valgrind](http://www.valgrind.org/), which is an x86 emulator and makes everything 100 times slower, AddressSanitizer's performance penalty is only about 10%.

Recompile Phusion Passenger with the environment variable `USE_ASAN=1` to enable support for AddressSanitizer.

## Simulating system call failures

Error conditions are sometimes hard to test. Things like network errors are usually hard to simulate using real equipment. In order to facilitate with error testing, we've developed a system call failure simulation framework, inspired by sqlite's failure test suite. You specify which system call errors should be simulated, and with what probability they should occur. By running normal tests multiple times you can see how Phusion Passenger behaves under these simulated error conditions.

To enable, set the environment variable `PASSENGER_SIMULATE_SYSCALL_FAILURES`. The format is:

    program_name1=error1:probability1,error2:probability2,...;program_name2=...

`program_nameN` specifies the name of the Phusion Passenger process for which system call failure simulation should be enabled. This is followed by a list of system call `errno` names and the respective probabilities (between 0 and 1). For example:

    export PASSENGER_SIMULATE_SYSCALL_FAILURES='PassengerAgent watchdog=ENOSPC:0.01;PassengerAgent core=EMFILE:0.001,ECONNREFUSED:0.02'

This will enable system call failure simulation only for the watchdog and the core, but not for the UstRouter. All system calls in the watchdog will have a 1% probability of throwing ENOSPC. All system calls in the HTTP server will have a 0.1% probability of throwing EMFILE, and a 2% probability of throwing ECONNREFUSED.
