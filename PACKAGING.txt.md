# Introduction

The Phusion Passenger files (also called _assets_) can be installed in 2
configurations:

**1. ORIGINAL SOURCE TREE**

This is the source tree that you get when you checkout Phusion Passenger from git,
when you install Phusion Passenger from a gem or when you extract it from a tarball.
This configuration does not come with any binaries until you compile them.
Everything, including compiled binaries, are stored in a single directory tree.

Phusion Passenger Standalone does things a little differently. It looks for
its binaries in one of these places, whichever first exists:

 - (a) ~/.passenger/standalone/<VERSION>/<TYPE-AND-ARCH>
 - (b) /var/lib/passenger-standalone/<VERSION-AND-ARCH>

If neither directories exist, then Passenger Standalone compiles the binaries and
stores them in (b) (when running as root) or in (a). It still looks for everything
else (like the .rb files) in the original source tree.

**2. NATIVELY PACKAGED**

Phusion Passenger is packaged, usually (but not necessarily) through a DEB or RPM
package. This configuration comes not only with all necessary binaries, but also
with some (but not all) source files. This is because when you run Phusion Passenger
with a different Ruby interpreter than the packager intended, Phusion Passenger
must be able to compile a new Ruby extension for that Ruby interpreter. This
configuration does not however allow compiling against a different Apache or Nginx
version than the packager intended.

In this configuration, files are scattered throughout the filesystem for FHS
compliance. The exact locations of the different types of files can be specified
through a _location configuration file_.

This configuration also does not allow running Phusion Passenger Standalone against
a different Nginx version than the packager intended, but does allow running
against a different Ruby version. The Standalone binaries are stored in

    /var/lib/passenger-standalone/<VERSION-AND-ARCH>

Passenger Standalone will make no attempt to compile something to
`~/.passenger/standalone/<VERSION>/<TYPE-AND-ARCH>`.

If either the non-Standalone or the Standalone Passenger needs to have a new Ruby
extension compiled, then it will store that in `~/.passenger/native_support/<VERSION>/<ARCH>`.


## The Phusion Passenger Ruby libraries

### phusion_passenger.rb

The Phusion Passenger administration tools, such as `passenger-status`, are written
in Ruby. So the first thing they do is trying to load `phusion_passenger.rb`,
which is the source file responsible for figuring out where all the other Phusion
Passenger files are. It tries to look for phusion_passenger.rb in
`<OWN_DIRECTORY>/../lib` where `<OWN_DIRECTORY>` is the directory that the tool is
located in. If phusion_passenger.rb is not there, then it tries to load it from
the normal Ruby load path.

### Ruby extension

The Phusion Passenger loader scripts try to load the Phusion Passenger Ruby
extension (`passenger_native_support.so`) from the following places, in the given order:

 * If Phusion Passenger is in the "original source tree" configuration, it will
   look for the Ruby extension in `<SOURCE_ROOT>/ext/ruby/<ARCH>`. Otherwise,
   this step is skipped.
 * The Ruby library load path.
 * `~/.passenger/native_support/<VERSION>/<ARCH>`

If it cannot find the Ruby extension in any of the above places, then it will
attempt to compile the Ruby extension and store it in
`~/.passenger/native_support/<VERSION>/<ARCH>`.

### Conclusion for packagers

If you're packaging Phusion Passenger then you should put both phusion_passenger.rb
and `passenger_native_support.so` somewhere in the Ruby load path, or make sure that
that directory is included in the `$RUBYLIB` environment variable. You cannot specify
a custom directory though the location configuration file.


## The location configuration file

phusion_passenger.rb looks for a location configuration file in the following
places, in the given order:

 * The environment variable `$PASSENGER_LOCATION_CONFIGURATION_FILE`.
 * `<RUBYLIBDIR>/phusion_passenger/locations.ini`, where <LIBDIR> is the Ruby library
   directory that contains phusion_passenger.rb. For example,
   `/usr/lib/ruby/1.9.0/phusion_passenger/locations.ini`.
 * `~/.passenger/locations.ini`
 * `/etc/phusion-passenger/locations.ini`

If it cannot find a location configuration file, then it assumes that Phusion
Passenger is in the "original source tree" configuration. If a location configuration
file is found then it assumes that it is in the "natively packaged" configuration.

The Apache module and the Nginx module expect `PassengerRoot`/`passenger_root` to
refer to either a directory or a file. If the value refers to a directory, then it
assumes that Phusion Passenger is in the "original source tree" configuration,
where the source tree is the specified directory. If the value refers to a file,
then it assumes that Phusion Passenger is in the "natively packaged" configuration,
with the given filename as the location configuration file.

Thus, if you wish to package Phusion Passenger, then we recommend that you put a
locations.ini file in `<RUBYLIBDIR>/phusion_passenger/locations.ini` and set
`PassengerRoot`/`passenger_root` to that filename. We don't recommend using
`~/.passenger` or `/etc/phusion-passenger` because if the user wants to install
a different Phusion Passenger version alongside the one that you've packaged,
then that other version will incorrectly locate your packaged files instead of
its own files.

The location configuration file is an ini file that looks as follows:

    [locations]
    bin=/usr/bin
    agents=/usr/lib/phusion-passenger
    helper_scripts=/usr/share/phusion-passenger/helper-scripts
    resources=/usr/share/phusion-passenger
    doc=/usr/share/doc
    rubylibdir=/usr/lib/ruby/1.9.0
    apache2_module=/usr/lib/apache2/modules/mod_passenger.so
    ruby_extension_source=/usr/share/phusion_passenger/ruby_native_support_source

Each key specifies the location of an asset or an asset directory. The next section
gives a description of all asset types.

## Asset types

Throughout the Phusion Passenger codebase, we refer to all kinds of assets. Here's
a list of all possible assets and asset directories.

 * `source_root`

   When Phusion Passenger is in the "original source tree" configuration, this
   refers to the directory that contains the entire Phusion passenger source tree.
   Not available when natively packaged.

 * `bin`

   A directory containing administration binaries and scripts and like
   `passenger-status`; tools that the user may directly invoke on the command line.

   Location in original source tree: `<SOURCE_ROOT>/bin`.

 * `agents`

   A directory that contains (platform-dependent) binaries that Phusion Passenger
   uses, but that should not be directly invoked from the command line. Things like
   PassengerHelperAgent are located here.

   Location in original source tree: `<SOURCE_ROOT>/agents`.

 * `helper_scripts`

   A directory that contains non-binary scripts that Phusion Passenger uses, but
   that should not be directly invoked from the command line. Things like
   rack-loader.rb are located here.

   Location in original source tree: `<SOURCE_ROOT>/helper-scripts`.

 * `resources`
   A directory that contains non-executable, platform-independent resource files
   that the user should not directly access, like error page templates and
   configuration file templates.

   Location in original source tree: `<SOURCE_ROOT>/resources`.

 * `doc`

   A directory that contains documentation.

   Location in original source tree: `<SOURCE_ROOT>/doc`.

 * `rubylibdir`

   A directory that contains the Phusion Passenger Ruby library files. Note that
   the Phusion Passenger administration tools still locate phusion_passenger.rb
   as described in the section "The Phusion Passenger Ruby libraries",
   irregardless of the value of this key in the location configuration file.
   The value is only useful to non-Ruby Phusion Passenger code.

   Location in original source tree: `<SOURCE_ROOT>/lib`.

 * `precompiled_ruby_native_support`

   The filename of the Phusion Passenger Ruby extension (typically called
   `passenger_native_support.so`).

 * `apache2_module`

   The filename of the Apache 2 module, or the filename that the Apache 2 module
   will be stored after it's compiled. Used by `passenger-install-module` to
   print an example configuration snippet.

   Location in original source tree: `<SOURCE_ROOT>/ext/apache2/mod_passenger.so`.

 * `ruby_extension_source`

   The directory that contains the source code for the Phusion Passenger Ruby
   extension.

   Location in original source tree: `<SOURCE_ROOT>/ext/ruby`.


## Vendoring of libraries

Phusion Passenger vendors libev and libeio in order to make installation easier
for users on operating systems without proper package management, like OS X.
If you want Phusion Passenger to compile against the system-provided
libev and/or libeio instead, then set the following environment variables
before compiling:

 * `export USE_VENDORED_LIBEV=no`
 * `export USE_VENDORED_LIBEIO=no`

Note that we require at least libev 4.11 and libeio 1.0.


## Misc notes

You can generate a fakeroot with the command `rake fakeroot`. This will
generate an FHS-compliant directory tree in `pkg/fakeroot`, which you can
directly package or with minor modifications. The fakeroot even contains
a location configuration file.

If the default fakeroot structure is not sufficient, please consider
sending a patch.
