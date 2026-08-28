# Passenger development handbook

## Fundamentals

- [Basic architecture overview](BasicArchitectureOverview.md)
- [Development environment setup](DevEnvironmentSetup.md) — Prerequisites for C++, Ruby, Node.js, Apache, and Nginx work; test setup; clangd, ccache, and debugger configuration.
- [Build system](BuildSystem.md) — How it works, how to compile various parts of Passenger. Rake targets for Apache and Nginx integration, output and compiler/linker settings, sanitizers, optimization, and vendored libev/libuv controls.
- [Running tests](RunningTests.md) — Ruby, Node.js, C++/oxt, integration, and packaging test suites; selecting C++ groups or test numbers; debugger and Valgrind support; and root-only tests.
- [Writing C++ tests](WritingCxxTests.md) — How C++ tests are structured, how our testing framework (forked tut) works
  - [C++ mocking strategy](CxxMockingStrategy.md)

## Best practices

- [Coding tips and common pitfalls](CodingTipsAndPitfalls.md) — C++ object-lifetime and event-loop hazards involving `shared_ptr`, callbacks, exceptions, and thread-interruption-safe RAII destructors.
- [Secure handling of temp files](SecureTempFileHandling.md) — Preventing symlink, file-squatting, and TOCTOU attacks through unpredictable names, atomic reservation, restrictive permissions, `mkstemp()`, and `mkdtemp()`.

## Design decisions & aspects

- [Limited gem dependencies](DesignAspects/LimitedGemDependencies.md) — Runtime Ruby dependency policy for gem and native-package installs, multiple Ruby versions, execution with or without Bundler, OS repository availability, and gemspec versus Gemfile placement.
- [No gem activation during Ruby loader initialization](DesignAspects/NoGemActivationDuringRubyLoaderInitialization.md) — No-activation rule for code called before Bundler by the Ruby loaders, including gemified standard-library code and Ruby/C++ reimplementation strategies.

## Subsystems in C++

- [ConfigKit](../src/cxx_supportlib/ConfigKit/README.md) — Core JSON configuration API covering runtime-introspectable schemas and stores, defaults, nested schemas, validation, normalization, updates, inspection, and secret filtering.
    - [ConfigKit practical usage & design patterns](../src/cxx_supportlib/ConfigKit/IN_PRACTICE.md) — Configurable-component patterns covering config realizations, subclassing and composition, translators, atomic prepare/commit with infallible commit, asynchronous event-loop wrappers, and concurrency serialization.
- [SpawningKit](../src/agent/Core/SpawningKit/README.md) — Reliably starting web application worker/preloader processes: spawn methods (direct/smart), wrappers/start commands, process environment and privilege setup, startup handshakes/readiness checks, timeouts, and detailed spawn-failure diagnostics.
- [Wrapper registry](../src/cxx_supportlib/WrapperRegistry/README.md) — Registry of supported application types and language wrappers: maps app type names/aliases to wrapper loader, interpreter, process title, and startup-file signatures used by application autodetection and spawning.
