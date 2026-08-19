# Running tests

All commands also compile whatever is necessary.

Common test suites:

- `rake test:ruby` — Ruby support unit tests
- `rake test:node` — Node.js support unit tests
- `rake test:cxx` and `rake test:oxt` — C++ unit tests (requires `test/config.json`, see [Development environment setup](DevEnvironmentSetup.md)). See also [Advanced C++ test suite usage](#advanced-c-test-suite-usage).
- `rake test:integration:apache2` — Apache 2 integration tests
- `rake test:integration:nginx` — Nginx integration tests
- `rake test:integration:standalone` — Passenger Standalone integration tests

> [!NOTE]
> Some tests, such as the ones about privilege lowering, require root. Those will only be run if Rake is run as root. For running `test:cxx` in root specifically, use `SUDO=1` (see below).

Packaging-specific test suites:

- `rake test:integration:native_packaging` — run from Debian and RPM packaging automation.
- `rake test:source_packaging` — run from gem and source tarball packaging automation.

## Advanced C++ test suite usage

The `test:cxx` unit test suite contains many different test groups. To run a specific one, set the environment variable `GROUPS` to a comma-delimited list of group names:

```bash
rake test:cxx GROUPS='ApplicationPool2_PoolTest,UtilsTest'
```

To run specific tests within a suite., pass the relevant test number(s):

```bash
rake test:cxx GROUPS='ApplicationPool2_PoolTest:82,83'
```

To run them in GDB, LLDB or Valgrind:

```bash
rake test:cxx GDB=1
rake test:cxx LLDB=1
rake test:cxx VALGRIND=1
```

> [!TIP]
> See [Development environment setup](DevEnvironmentSetup.md) for setting up our recommended GDB config file.

Some tests, such as the ones about privilege lowering, require root. To run compilation commands as a normal user, and only the test suite as root:

```bash
rake test:cxx SUDO=1
```
