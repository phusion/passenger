# Limited gem dependencies

## Context

1. Passenger can be installed through various ways, including as a Ruby gem (via Gemfile) or via native packages (APT/YUM).
2. Passenger has a bunch of tools (e.g. `bin/passenger-status`, `helper-scripts/backtrace-sanitizer.rb`, `helper-scripts/prespawn`) that are written in Ruby.
   - Tools in `bin/` might be run with gem bundle activation (only if Passenger is installed via Gemfile).
   - Tools in `helper-scripts/` are never run with gem bundle activation.
   - All these tools are run in either the "system" Ruby (if Passenger is natively packaged), or the Ruby the app runs in (if Passenger is installed via Gemfile).
3. On Debian-based systems, the OS package repositories may supply multiple Ruby versions. So our native packaging can't assume a single Ruby version.

This context raises interesting challenges that other Ruby apps don't have to deal with:

 - (1) implies that we can't rely solely on Bundler to take care of our gem dependencies.
 - (2) and (3) imply that we can't rely on only a single Ruby version being used. Different components of Passenger are potentially run in different Rubies, sometimes with and sometimes without bundle activation.

What does this mean for the number of gems we can depend on? An analysis of possible strategies and their implications/tradeoffs:

1. If the OS package repositories supply dependent gems, then our native packages can depend on those directly.

   Drawbacks: limited selection of gems and versions.

2. We publish native packages for each gem dependency. In addition, any dependency with native extensions must be packaged separately, once for each Ruby version installable through the native OS package repositories.

   Drawbacks: lots of work for us.

3. We vendor the gem dependencies within the native packages. In addition, any dependency with native extensions must be vendored separately, once for each Ruby version installable through the native OS package repositories.

   Drawbacks: more work for us. Less than option 2, but still significant.

## Decision

We pick strategy 1.

## Consequences

- **Limited gem selection**: any gems that the written-in-Ruby tools depend on, must either be available through OS repositories, or must be a Ruby standard library gem.
  - For example "json" is a good gem we can depend on, but "pry" is not.
  - All such dependencies must be specified in the Passenger gemspec.
- **Code with broad version compatibility**: Since we can't rely on specific gem versions, we must use gems in a way that's compatible with a wide number of gem versions.
- **Development code can YOLO with gem dependencies**: Development-related Passenger Ruby code can depend on any gem, since development code is not included in native packages.
  - All such dependencies must be specified in the Passenger Gemfile.
  - Note: the build system (Rakefile) does not count as "development-related Ruby code" because it's used outside just development. For example `passenger-install-XXX-module` calls Rake to compile things.

## See also

- [No gem activation during Ruby loader initialization](NoGemActivationDuringRubyLoaderInitialization.md) — a special case where we can't load *any* gem, whether or not those gems are packaged by the OS.
- System Ruby runner (only used in Debian packages): `packaging/debian/debian_specs/passenger/passenger_system_ruby.c.erb` — how we enforce that in Debian packages, tools in `bin/` only run in the system Ruby.
