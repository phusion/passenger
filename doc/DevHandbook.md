# Passenger development handbook

## Fundamentals

- [Basic architecture overview](BasicArchitectureOverview.md)
- [Development environment setup](DevEnvironmentSetup.md)
- [Build system](BuildSystem.md) — How it works, how to compile various parts of Passenger (Rake targets)
- [Running tests](RunningTests.md)
- [Writing C++ tests](WritingCxxTests.md) — How C++ tests are structured, how our testing framework (forked tut) works
  - [C++ mocking strategy](CxxMockingStrategy.md)

## Best practices

- [Coding tips and common pitfalls](CodingTipsAndPitfalls.md)
- [Secure handling of temp files](SecureTempFileHandling.md)

## Design decisions & aspects

- [Limited gem dependencies](DesignAspects/LimitedGemDependencies.md)
- [No gem activation during Ruby loader initialization](DesignAspects/NoGemActivationDuringRubyLoaderInitialization.md)

## Subsystems in C++

- [ConfigKit](../src/cxx_supportlib/ConfigKit/README.md): JSON-based configuration framework for Passenger’s composable C++ components: runtime-introspectable schemas/stores, validation, schema composition/translation, and live reconfiguration.
    - [ConfigKit practical usage & design patterns](../src/cxx_supportlib/ConfigKit/IN_PRACTICE.md): component configuration patterns: parent/child composition, translators, and atomic prepare/commit configuration changes.
- [SpawningKit](../src/agent/Core/SpawningKit/README.md): reliably starting web application worker/preloader processes: spawn methods (direct/smart), wrappers/start commands, process environment and privilege setup, startup handshakes/readiness checks, timeouts, and detailed spawn-failure diagnostics.
- [Wrapper registry](../src/cxx_supportlib/WrapperRegistry/README.md): registry of supported application types and language wrappers: maps app type names/aliases to wrapper loader, interpreter, process title, and startup-file signatures used by application autodetection and spawning.
