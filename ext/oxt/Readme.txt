== Introduction

OXT, OS eXtensions for boosT, is a utility library that provides important
functionality that's necessary for writing robust server software. It
provides essential things that should be part of C++, but unfortunately isn't,
such as:
- System call interruption support. This is important for multithreaded
  software that can block on system calls.
- Support for backtraces.

== Compilation and usage

Compile all .cpp files and link them into your program. No special build tools
are required. OXT depends on a specially patched version of Boost.

