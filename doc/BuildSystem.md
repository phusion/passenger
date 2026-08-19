# Build system

Our build system is [Rake](https://github.com/ruby/rake). Its functionality is similar to Make, but its DSL is in Ruby. `passenger-install-apache2-module` and `passenger-install-nginx-module` call Rake to compile things.

## Key Rake targets

- `rake apache2` — compiles the Apache 2 module, and other parts needed by the Apache integration mode (e.g., the Passenger agent binary).
- `rake nginx` — compiles parts needed by the Nginx integration mode (e.g., agent binary). Does *not* compile the Nginx module.
- For test suite targets, see [Running tests](RunningTests.md).

See more with `rake -T`.

## Environment variables

You can pass environment variables via CLI arguments. These are equivalent:

```bash
rake something FOO=123

export FOO=123 && rake something
```

## Customization

Customize the build system with environment variables. Boolean options can be set to true, false, 0 or 1.

- `OUTPUT_DIR`: where build output is placed. Default: `buildout/`
- `EXTRA_PRE_CFLAGS` and `EXTRA_CFLAGS`: insert additional flags to C compiler invocations. One for before our own flags, one for after. Does not cover linking invocations.
- `EXTRA_CXXFLAGS` and `EXTRA_PRE_CXXFLAGS`: ditto, but for C++ compiler invocations.
- `EXTRA_PRE_LDFLAGS` and `EXTRA_LDFLAGS`: insert additional flags to C/C++ compiler linking invocations. One for before our own flags, one for after.
- `EXTRA_PRE_C_LDFLAGS` and `EXTRA_C_LDFLAGS`: ditto, but only applies to C compiler linking invocations.
- `EXTRA_PRE_CXX_LDFLAGS` and `EXTRA_CXX_LDFLAGS`: ditto, but only applies to C++ compiler linking invocations.
- `USE_ASAN=<bool>` (default: false): compile with AddressSanitizer.
- `USE_UBSAN=<bool>` (default: false): compile with UndefinedBehaviorSanitizer.
- `OPTIMIZE=<bool>` (default: false): compile with optimizations.
- `USE_VENDORED_LIBEV=<bool>` (default: true): set to false to compile with system-provided libev instead of our vendored one.
- `USE_VENDORED_LIBUV=<bool>` (default: true): set to false to compile with system-provided libuv instead of our vendored one.
