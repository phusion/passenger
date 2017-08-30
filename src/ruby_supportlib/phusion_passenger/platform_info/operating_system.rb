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

module PhusionPassenger

  module PlatformInfo
    # Returns the operating system's name in as simple a form as possible. For example,
    # Linux is always identified as "linux". OS X is always identified as "macosx" (despite
    # the actual os name being something like "darwin"). This is useful as a stable indicator
    # of the os without having to worry about version numbers, etc.
    # N.B. unrecognized os names will just be returned as-is.
    def self.os_name_simple
      if rb_config['target_os'] =~ /darwin/ && (sw_vers = find_command('sw_vers'))
        'macosx'
      elsif rb_config['target_os'] =~ /^linux-/
        'linux'
      elsif rb_config['target_os'] =~ /solaris/
        'solaris'
      elsif rb_config['target_os'] =~ /freebsd/
        'freebsd'
      elsif rb_config['target_os'] =~ /aix/
        'aix'
      else
        rb_config['target_os']
      end
    end
    memoize :os_name_simple

    # Returns the operating system's name exactly as advertised by the system. While it is
    # in lowercase and contains no spaces, it can contain things like version number or
    # may be less intuitive (e.g. "darwin" for OS X).
    def self.os_name_full
      rb_config['target_os']
    end
    memoize :os_name_full

    # Returns the operating system's version number, or nil if unknown.
    # This includes the patch version, so for example on macOS Sierra
    # it could return "10.12.5".
    #
    # On Debian/Ubuntu, this returns the version number (e.g. "16.04")
    # as opposed to the codename ("Trusty").
    def self.os_version
      case os_name_simple
      when 'macosx'
        `/usr/bin/sw_vers -productVersion`.strip.split.last

      when 'linux'
        # Parse LSB (applicable to e.g. Ubuntu)
        if read_file('/etc/lsb-release') =~ /DISTRIB_RELEASE=(.+)/
          version = $1.gsub(/["']/, '')
          return version if !version.empty?
        end

        # Parse CentOS/RedHat
        data = read_file('/etc/centos-release')
        data = read_file('/etc/redhat-release') if data.empty?
        if !data.empty?
          data =~ /^(.+?) (Linux )?(release |version )?(.+?)( |$)/i
          return $4 if $4
        end

        if File.exist?('/etc/debian_version')
          return read_file('/etc/debian_version').strip
        end

        nil

      else
        nil
      end
    end

    # The current platform's shared library extension ('so' on most Unices).
    def self.library_extension
      if os_name_simple == "macosx"
        return "bundle"
      else
        return "so"
      end
    end

    # Returns the `uname` command, or nil if `uname` cannot be found.
    # In addition to looking for `uname` in `PATH`, this method also looks
    # for `uname` in /bin and /usr/bin, just in case the user didn't
    # configure its PATH properly.
    def self.uname_command
      if result = find_command("uname")
        result
      elsif File.exist?("/bin/uname")
        return "/bin/uname"
      elsif File.exist?("/usr/bin/uname")
        return "/usr/bin/uname"
      else
        return nil
      end
    end

    # Returns a list of all CPU architecture names that the current machine CPU
    # supports. If there are multiple such architectures then the first item in
    # the result denotes that OS runtime's main/preferred architecture.
    #
    # This function normalizes some names. For example x86 is always reported
    # as "x86" regardless of whether the OS reports it as "i386" or "i686".
    # x86_64 is always reported as "x86_64" even if the OS reports it as "amd64".
    #
    # Please note that even if the CPU supports multiple architectures, the
    # operating system might not. For example most x86 CPUs nowadays also
    # support x86_64, but x86_64 Linux systems require various x86 compatibility
    # libraries to be installed before x86 executables can be run. This function
    # does not detect whether these compatibility libraries are installed.
    # The only guarantee that you have is that the OS can run executables in
    # the architecture denoted by the first item in the result.
    #
    # For example, on x86_64 Linux this function can return ["x86_64", "x86"].
    # This indicates that the CPU supports both of these architectures, and that
    # the OS's main/preferred architecture is x86_64. Most executables on the
    # system are thus be x86_64. It is guaranteed that the OS can run x86_64
    # executables, but not x86 executables per se.
    #
    # Another example: on MacOS X this function can return either
    # ["x86_64", "x86"] or ["x86", "x86_64"]. The former result indicates
    # OS X 10.6 (Snow Leopard) and beyond because starting from that version
    # everything is 64-bit by default. The latter result indicates an OS X
    # version older than 10.6.
    def self.cpu_architectures
      uname = uname_command
      raise "The 'uname' command cannot be found" if !uname
      if os_name_simple == "macosx"
        arch = `#{uname} -p`.strip
        if arch == "i386"
          # Macs have been x86 since around 2007. I think all of them come with
          # a recent enough Intel CPU that supports both x86 and x86_64, and I
          # think every OS X version has both the x86 and x86_64 runtime installed.
          major, minor, *rest = `sw_vers -productVersion`.strip.split(".")
          major = major.to_i
          minor = minor.to_i
          if major >= 10 || (major == 10 && minor >= 6)
            # Since Snow Leopard x86_64 is the default.
            ["x86_64", "x86"]
          else
            # Before Snow Leopard x86 was the default.
            ["x86", "x86_64"]
          end
        else
          arch
        end
      else
        arch = `#{uname} -p`.strip
        # On some systems 'uname -p' returns something like
        # 'Intel(R) Pentium(R) M processor 1400MHz' or
        # 'Intel(R)_Xeon(R)_CPU___________X7460__@_2.66GHz'.
        if arch == "unknown" || arch =~ / / || arch =~ /Hz$/
          arch = `#{uname} -m`.strip
        end
        if arch =~ /^i.86$/
          arch = "x86"
        elsif arch == "amd64"
          arch = "x86_64"
        end

        if arch == "x86"
          # Most x86 operating systems nowadays are probably running on
          # a CPU that supports both x86 and x86_64, but we're not gonna
          # go through the trouble of checking that. The main architecture
          # is what we usually care about.
          ["x86"]
        elsif arch == "x86_64"
          # I don't think there's a single x86_64 CPU out there
          # that doesn't support x86 as well.
          ["x86_64", "x86"]
        else
          [arch]
        end
      end
    end
    memoize :cpu_architectures, true

    # Returns whether the flock() function is supported on this OS.
    def self.supports_flock?
      defined?(File::LOCK_EX) && os_name_simple != 'solaris'
    end

    # Returns whether the OS's main CPU architecture supports the
    # x86/x86_64 sfence instruction.
    def self.supports_sfence_instruction?
      arch = cpu_architectures[0]
      return arch == "x86_64" || (arch == "x86" &&
        try_compile_and_run("Checking for sfence instruction support", :c, %Q{
          int
          main() {
            __asm__ __volatile__ ("sfence" ::: "memory");
            return 0;
          }
        }))
    end
    memoize :supports_sfence_instruction?, true

    # Returns whether the OS's main CPU architecture supports the
    # x86/x86_64 lfence instruction.
    def self.supports_lfence_instruction?
      arch = cpu_architectures[0]
      return arch == "x86_64" || (arch == "x86" &&
        try_compile_and_run("Checking for lfence instruction support", :c, %Q{
          int
          main() {
            __asm__ __volatile__ ("lfence" ::: "memory");
            return 0;
          }
        }))
    end
    memoize :supports_lfence_instruction?, true

    def self.requires_no_tls_direct_seg_refs?
      return File.exists?("/proc/xen/capabilities") && cpu_architectures[0] == "x86"
    end
    memoize :requires_no_tls_direct_seg_refs?, true
  end

end # module PhusionPassenger
