#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2017 Phusion Holding B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Phusion Holding B.V.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

require 'rbconfig'
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/ruby'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'

module PhusionPassenger

  module PlatformInfo
    # Returns a string that describes the current Ruby
    # interpreter's extension binary compatibility. A Ruby extension
    # compiled for a certain Ruby interpreter can also be loaded on
    # a different Ruby interpreter with the same binary compatibility
    # identifier.
    #
    # The result depends on the following factors:
    # - Ruby engine name.
    # - Ruby extension version.
    #   This is not the same as the Ruby language version, which
    #   identifies language-level compatibility. This is rather about
    #   binary compatibility of extensions.
    #   MRI seems to break source compatibility between tiny releases,
    #   though patchlevel releases tend to be source and binary
    #   compatible.
    # - Ruby extension architecture.
    #   This is not necessarily the same as the operating system
    #   runtime architecture or the CPU architecture.
    #   For example, in case of JRuby, the extension architecture is
    #   just "java" because all extensions target the Java platform;
    #   the architecture the JVM was compiled for has no effect on
    #   compatibility.
    #   On systems with universal binaries support there may be multiple
    #   architectures. In this case the architecture is "universal"
    #   because extensions must be able to support all of the Ruby
    #   executable's architectures.
    # - The operating system for which the Ruby interpreter was compiled.
    def self.ruby_extension_binary_compatibility_id
      ruby_engine = defined?(RUBY_ENGINE) ? RUBY_ENGINE : "ruby"
      ruby_ext_version = RUBY_VERSION
      if RUBY_PLATFORM =~ /darwin/
        if RUBY_PLATFORM =~ /universal/
          ruby_arch = "universal"
        else
          # OS X often prints something like:
          #   "/opt/ruby-enterprise/bin/ruby: Mach-O 64-bit executable x86_64"
          output = `file -L "#{ruby_executable}"`.lines.first.strip
          ruby_arch = output.split.last
          if ruby_arch == "executable"
            # some OS X print something like:
            #   "/opt/ruby-enterprise/bin/ruby: Mach-O 64-bit executable"
            if output =~ /arm64/
              ruby_arch = "arm"
            elsif output =~ /Mach-O 64-bit/
              ruby_arch = "x86_64"
            else
              raise "Cannot autodetect the Ruby interpreter's architecture"
            end
          end
        end
      elsif RUBY_PLATFORM == "java"
        ruby_arch = "java"
      else
        ruby_arch = cpu_architectures[0]
      end
      return "#{ruby_engine}-#{ruby_ext_version}-#{ruby_arch}-#{os_name_simple}"
    end
    memoize :ruby_extension_binary_compatibility_id

    # Returns an identifier string that describes the current
    # platform's binary compatibility with regard to C/C++
    # binaries. Two systems with the same binary compatibility
    # identifiers should be able to run the same C/C++ binaries.
    #
    # The the string depends on the following factors:
    # - The operating system name.
    # - Operating system runtime identifier.
    #   This may include the kernel version, libc version, C++ ABI version,
    #   etc. Everything that is of interest for binary compatibility with
    #   regard to C/C++ binaries.
    # - Operating system default runtime architecture.
    #   This is not the same as the CPU architecture; some CPUs support
    #   multiple architectures, e.g. Intel Core 2 Duo supports x86 and
    #   x86_64. Some operating systems actually support multiple runtime
    #   architectures: a lot of x86_64 Linux distributions also include
    #   32-bit runtimes, and OS X Snow Leopard is x86_64 by default but
    #   all system libraries also support x86.
    #   This component identifies the architecture that is used when
    #   compiling a binary with the system's C++ compiler with its default
    #   options.
    def self.cxx_binary_compatibility_id
      if os_name_simple == "macosx"
        # RUBY_PLATFORM gives us the kernel version, but we want
        # the OS X version.
        # sw_vers returns something like "10.6.2". We're only
        # interested in the first two digits (MAJOR.MINOR) since
        # tiny releases tend to be binary compatible with each
        # other. After macOS 10.15 Apple switched to bumping major
        # versions (like 11.0, 12.0) so we need to look at that.
        # However, if the env var SYSTEM_VERSION_COMPAT=1 then
        # macOS 11 reports as 10.16 (and so on), so we need to deal with that too.
        major, minor, *rest = os_version.split(".").map(&:to_i)
        os_version_string = if major >= 11
                       major
                     elsif minor >= 16
                       # 10.16 -> 11
                       # 10.17 -> 12
                       minor - 5
                     else
                       "#{major}.#{minor}"
                     end
        os_runtime = os_version_string.to_s
      else
        os_runtime = nil
      end
      os_arch = cpu_architectures[0]
      return [os_arch, os_name_simple, os_runtime].compact.join("-")
    end
    memoize :cxx_binary_compatibility_id
  end

end # module PhusionPassenger
