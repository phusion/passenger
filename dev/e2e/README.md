# End-to-end environments

Self-contained environments that build Passenger and exercise it against a
real web server, each in its own subdirectory with its own README. They are
development tools, not part of any test suite that CI runs.

They exist for things the ordinary suites cannot do on a workstation —
usually because a dependency is awkward to install outside Linux — and each
one is driven by a `run` script in its directory that starts a container and
does everything inside it.

- [apache-websocket](apache-websocket/README.md) — builds the Apache module on
  Rocky Linux and runs the Apache integration suite plus a WebSocket demo
  against it.
