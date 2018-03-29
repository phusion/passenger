# Introduction

This document describes how packagers can package Phusion Passenger binaries
for their operating system.

Phusion Passenger can be configured in 2 ways, the "originally packaged"
configuration where everything is in the same directory, and the
"natively packaged" configuration where files are scattered across the
filesystem, e.g. in a FHS-compliant configuration. This document describes
how you can configure Phusion Passenger to locate its own files when they're
scattered across the filesystem.

Phusion Passenger files are also called _assets_ in this document.

## Originally packaged

This is the configuration you get when you checkout Phusion Passenger from git,
when you install Phusion Passenger from a gem or when you extract it from a
tarball. All the original files are stored in a single directory tree, which we
call the _source root_.

The git repository, gems and tarballs do not come with any binaries; they have
to be compiled by the user. Phusion Passenger looks for binaries in, and (if
the user initiates the compilation process) stores binaries in, the following
directories:

 * Normally, binaries are to be located in the `buildout` subdirectory under
   the source root.
 * Phusion Passenger Standalone does things a little differently. Binaries are
   to be located in one of the following directories, whichever it finds first:

    - `~/.passenger/standalone/<VERSION>/<TYPE-AND-ARCH>` (a)
    - `/var/lib/passenger-standalone/<VERSION-AND-ARCH>` (b)

   If neither directories exist, then Passenger Standalone compiles the
   binaries and stores them in (b) (when running as root) or in (a). It still
   looks for everything else (like the .rb files) in the source root.

## Natively packaged

Phusion Passenger is packaged, usually (but not necessarily) through a DEB or RPM
package. This configuration comes not only with all necessary binaries, but also
with some (but not all) source files. This is because when you run Phusion Passenger
with a different Ruby interpreter than the packager intended, Phusion Passenger
must be able to compile a new Ruby extension for that Ruby interpreter. This
configuration does not however allow compiling against a different Apache version
than the packager intended (but does allow compiling against a different Nginx
version).

In this configuration, files can be scattered anywhere throughout the filesystem. This
way Phusion Passenger can be packaged in an FHS-compliant way. The exact locations
of the different types of files can be specified through a
_location configuration file_. The existance and usage of a location configuration
file does not automatically imply that Phusion Passenger is natively packaged.

If Phusion Passenger needs to have a new Ruby extension compiled, then it will
store that in `~/.passenger/native_support/<VERSION>/<ARCH>`.


# The location configuration file

The Phusion Passenger administration tools, such as `passenger-status`, look for a
location configuration file in the following places, in the given order:

 * The environment variable `$PASSENGER_LOCATION_CONFIGURATION_FILE`.
 * `<RUBYLIBDIR>/phusion_passenger/locations.ini`, where <LIBDIR> is the Ruby library
   directory that contains phusion_passenger.rb. For example,
   `/usr/lib/ruby/1.9.0/phusion_passenger/locations.ini`.
 * `~/.passenger/locations.ini`
 * `/etc/phusion-passenger/locations.ini`

If it cannot find a location configuration file, then it assumes that Phusion
Passenger is originally packaged. If a location configuration file is found then
the configuration is determined by the `natively_packaged` option in the
location configuration file, which can be either "true" or "false".

The Apache module and the Nginx module expect `PassengerRoot`/`passenger_root` to
refer to either a directory or a file. If the value refers to a directory, then it
assumes that Phusion Passenger is originally packaged, where the source root is the
specified directory. If the value refers to a file, then it will use it as the
location configuration file, and the configuration depends on the
`natively_packaged` setting.

The location configuration file is an ini file that looks as follows:

    [locations]
    natively_packaged=true
    bin_dir=/usr/bin
    support_binaries_dir=/usr/lib/phusion-passenger/support-binaries
    lib_dir=/usr/lib/phusion-passenger
    helper_scripts_dir=/usr/share/phusion-passenger/helper-scripts
    resources_dir=/usr/share/phusion-passenger
    include_dir=/usr/share/phusion-passenger/include
    doc_dir=/usr/share/doc/phusion-passenger
    ruby_libdir=/usr/lib/ruby/vendor_ruby
    apache2_module_path=/usr/lib/apache2/modules/mod_passenger.so
    ruby_extension_source_dir=/usr/share/phusion-passenger/ruby_extension_source
    nginx_module_source_dir=/usr/share/phusion-passenger/ngx_http_passenger_module

All keys except fo `natively_packaged` specify the locations of assets and asset
directories. The "Asset types" section provides a description of all asset types.

Thus, if you're packaging Phusion Passenger, then we recommend the following:

 * Put a locations.ini file in `<RUBYLIBDIR>/phusion_passenger/locations.ini` and
   set `PassengerRoot`/`passenger_root` to that filename. We don't recommend using
   `~/.passenger` or `/etc/phusion-passenger` because if the user wants to install
   a different Phusion Passenger version alongside the one that you've packaged,
   then that other version will incorrectly locate your packaged files instead of
   its own files.
 * Always set `natively_packaged` to "true". The "false" value is used
   internally for implementing Phusion Passenger Standalone and should never be
   used by packagers.


# The Phusion Passenger Ruby libraries

## phusion_passenger.rb

The Phusion Passenger administration tools are written in Ruby. So the first thing
they do is trying to load `phusion_passenger.rb`, which is the source file
responsible for figuring out where all the other Phusion Passenger files are. It
tries to look for phusion_passenger.rb in `<OWN_DIRECTORY>/../src/ruby_supportlib` where
`<OWN_DIRECTORY>` is the directory that the tool is located in. If
phusion_passenger.rb is not there, then it tries to load it from the normal Ruby
load path.

## Ruby extension

The Phusion Passenger loader scripts try to load the Phusion Passenger Ruby
extension (`passenger_native_support.so`) from the following places, in the given order:

 * If Phusion Passenger is originally packaged, it will look for the Ruby
   extension in `<SOURCE_ROOT>/libout/ruby/<ARCH>`. Otherwise, this step is skipped.
 * The Ruby library load path.
 * `~/.passenger/native_support/<VERSION>/<ARCH>`

If it cannot find the Ruby extension in any of the above places, then it will
attempt to compile the Ruby extension and store it in
`~/.passenger/native_support/<VERSION>/<ARCH>`.

## Conclusion for packagers

If you're packaging Phusion Passenger then you should put both phusion_passenger.rb
and `passenger_native_support.so` somewhere in the Ruby load path, or make sure that
that directory is included in the `$RUBYLIB` environment variable. You cannot specify
a custom directory though the location configuration file.


# Asset types

Throughout the Phusion Passenger codebase, we refer to all kinds of assets. Here's
a list of all possible assets and asset directories.

 * `source_root`

   When Phusion Passenger is originally packaged, this refers to the directory
   that contains the entire Phusion passenger source tree. Not available when
   natively packaged.

 * `bin_dir`

   A directory containing administration binaries and scripts and like
   `passenger-status`; tools that the user may directly invoke on the command line.

   Value when originally packaged: `<SOURCE_ROOT>/bin`

 * `support_binaries_dir`

   A directory that contains (platform-dependent) binaries that Phusion Passenger
   uses, but that should not be directly invoked from the command line. Things like
   PassengerAgent are located here.

   Value when originally packaged:
   - Normally: `<SOURCE_ROOT>/buildout/support-binaries`
   - Passenger Standalone: `~/.passenger/standalone/<VERSION>/support-<ARCH>`

 * `helper_scripts_dir`

   A directory that contains non-binary scripts that Phusion Passenger uses, but
   that should not be directly invoked from the command line. Things like
   rack-loader.rb are located here.

   Value when originally packaged: `<SOURCE_ROOT>/helper-scripts`

 * `resources_dir`

   A directory that contains non-executable, platform-independent resource files
   that the user should not directly access, like error page templates and
   configuration file templates.

   Value when originally packaged: `<SOURCE_ROOT>/resources`.

 * `doc_dir`

   A directory that contains documentation.

   Value when originally packaged: `<SOURCE_ROOT>/doc`.

 * `include_dir`

   A directory that contains the Phusion Passenger header files that are
   necessary for compiling Nginx.

   Value when originally packaged: `<SOURCE_ROOT>/src`

 * `lib_dir`

   A directory that contains the Phusion Passenger library files, e.g.
   libboost_oxt.a and various .o files.

   Value when originally packaged: `<SOURCE_ROOT>/buildout`

 * `ruby_libdir`

   A directory that contains the Phusion Passenger Ruby library files. Note that
   the Phusion Passenger administration tools still locate phusion_passenger.rb
   as described in the section "The Phusion Passenger Ruby libraries",
   irrespective of the value of this key in the location configuration file.
   The value is only useful to non-Ruby Phusion Passenger code.

   Value when originally packaged: `<SOURCE_ROOT>/src/ruby_supportlib`.

 * `apache2_module_path`

   The filename of the Apache 2 module, or the filename that the Apache 2 module
   will be stored after it's compiled. Used by `passenger-install-module` to
   print an example configuration snippet.

   Value when originally packaged: `<SOURCE_ROOT>/buildout/apache2/mod_passenger.so`.

 * `ruby_extension_source_dir`

   The directory that contains the source code for the Phusion Passenger Ruby
   extension. Phusion Passenger uses these sources to build a Ruby extension,
   when it detects that the user is using a new Ruby interpreter for which
   no Ruby extension has been compiled.

   Value when originally packaged: `<SOURCE_ROOT>/src/ruby_native_extension`.

 * `nginx_module_source_dir`

   The directory that contains the source code for the Phusion Passenger Nginx
   module. passenger-install-nginx-module uses these sources to build Nginx
   with Phusion Passenger support.

   Value when originally packaged: `<SOURCE_ROOT>/src/nginx_module`.

Optional fields:

 * `build_system_dir`

   The directory that contains the Phusion Passenger main Rakefile, used for
   compiling the Apache module and the agent executable. This field is only
   present if Phusion Passenger is compilable. Native packages usually do not
   have this field because they ship a precompiled Apache module and a
   precompiled agent executable.

   Value when originally packaged: `<SOURCE_ROOT>`

 * `download_cache_dir`

   The directory that contains cached downloaded agent executables. Its main
   use case is to speed up agent executable downloading when Phusion Passenger
   is installed from a Ruby gem. When the user installs the gem, a script is
   invoked which downloads agent executables from the Phusion Passenger
   websites. The downloaded files are stored in this directory. Then later,
   when the user runs `passenger start` or any other command which requires
   the agent executable, the executable will be copied from this cache
   directory instead of downloaded.

   Native packages ship precompiled executables, so they can omit this field.

   Value when originally packaged: `<SOURCE_ROOT>/download_cache`




# Vendoring of libraries

Phusion Passenger vendors libev and libuv in order to make installation easier
for users on operating systems without proper package management, like OS X.
If you want Phusion Passenger to compile against the system-provided
libev and/or libuv instead, then set the following environment variables
before compiling:

 * `export USE_VENDORED_LIBEV=no`
 * `export USE_VENDORED_LIBUV=no`

Note that we require at least libev 4.11 and libuv 1.4.2.


# Generating gem and tarball

Use the following commands to generate a gem and tarball, in which Phusion
Passenger is originally packaged and without any binaries:

    rake package:gem
    rake package:tarball

The files will be stored in `pkg/`.


# Fakeroot

You can generate a fakeroot with the command `rake fakeroot`. This will
generate an FHS-compliant directory tree in `pkg/fakeroot`, which you can
directly package or with minor modifications. The fakeroot even contains
a location configuration file.
