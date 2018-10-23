# encoding: binary
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

PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/compiler'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'
PhusionPassenger.require_passenger_lib 'platform_info/linux'
PhusionPassenger.require_passenger_lib 'utils/shellwords'

module PhusionPassenger

  # Wow, I can't believe in how many ways one can build Apache in OS
  # X! We have to resort to all sorts of tricks to make Passenger build
  # out of the box on OS X. :-(
  #
  # In the name of usability and the "end user is the king" line of thought,
  # I shall suffer the horrible faith of writing tons of autodetection code!

  # Users can change the detection behavior by setting the environment variable
  # <tt>APXS2</tt> to the correct 'apxs' (or 'apxs2') binary, as provided by
  # Apache.

  module PlatformInfo
    ################ Programs ################

    # The absolute path to the 'apxs' or 'apxs2' executable, or nil if not found.
    def self.apxs2
      if env_defined?("APXS2")
        if ENV['APXS2'] == 'none'
          # This is a special value indicating that we should pretend as if
          # apxs2 is not found. It is applicable when the user is running
          # macOS >= 10.13 High Sierra, where apxs2 is not included in the OS.
          # When the user runs 'passenger-config about detect-apache2',
          # that command will tell the way to install against this specific
          # Apache is to run passenger-install-apache2-module --apxs2-path='none'
          return nil
        else
          return ENV["APXS2"]
        end
      end
      ['apxs2', 'apxs'].each do |name|
        command = find_command(name)
        if !command.nil?
          return command
        end
      end
      return nil
    end
    memoize :apxs2

    # The absolute path to the 'apachectl' or 'apache2ctl' binary, or nil if
    # not found.
    def self.apache2ctl(options = {})
      return find_apache2_executable('apache2ctl', 'apachectl2', 'apachectl', options)
    end
    memoize :apache2ctl

    # The absolute path to the Apache binary (that is, 'httpd', 'httpd2', 'apache'
    # or 'apache2'), or nil if not found.
    def self.httpd(options = {})
      apxs2 = options.fetch(:apxs2, self.apxs2)
      if env_defined?('HTTPD')
        return ENV['HTTPD']
      elsif apxs2.nil?
        ["apache2", "httpd2", "apache", "httpd"].each do |name|
          command = find_command(name)
          if !command.nil?
            return command
          end
        end
        return nil
      else
        return find_apache2_executable(`#{apxs2} -q TARGET`.strip, options)
      end
    end
    memoize :httpd

    # The Apache version, or nil if Apache is not found.
    def self.httpd_version(options = nil)
      if options
        httpd = options[:httpd] || self.httpd(options)
      else
        httpd = self.httpd
      end
      if httpd
        `#{httpd} -v` =~ %r{Apache/([\d\.]+)}
        return $1
      else
        return nil
      end
    end
    memoize :httpd_version

    # Run `apache2ctl -V` and return its output, or nil on failure.
    #
    # We used to run `httpd -V`, but on systems like Ubuntu it depends on various
    # environment variables or directories, wich apache2ctl loads or initializes.
    #
    # Because on some systems (mostly Ubuntu) `apache2ctl -V` attempts to parse
    # and load the config file, this command can fail because of configuration file
    # errors.
    def self.apache2ctl_V(options = nil)
      if options
        apache2ctl = options[:apache2ctl] || self.apache2ctl(options)
      else
        apache2ctl = self.apache2ctl
      end
      if os_name_simple == "linux" &&
         linux_distro_tags.include?(:gentoo) &&
         apache2ctl == "/usr/sbin/apache2ctl"
        # On Gentoo, `apache2ctl -V` doesn't forward the command to `apache2 -V`,
        # but instead prints the OpenRC init system's version.
        # https://github.com/phusion/passenger/issues/1510
        if options
          httpd = options[:httpd] || self.httpd(options)
        else
          httpd = self.httpd
        end
        version_command = httpd
      else
        version_command = apache2ctl
      end

      if version_command
        create_temp_file("apache2ctl_V") do |filename, f|
          e_filename = Shellwords.escape(filename)
          output = `#{version_command} -V 2>#{e_filename}`

          if $? && $?.exitstatus == 0
            stderr_text = File.open(filename, "rb") do |f2|
              f2.read
            end
            # This stderr message shows up on Ubuntu. We ignore it.
            stderr_text.sub!(/.*Could not reliably determine the server's fully qualified domain name.*\r?\n?/, "")
            # But we print the rest of stderr.
            STDERR.write(stderr_text)
            STDERR.flush

            output
          else
            nil
          end
        end
      else
        nil
      end
    end
    memoize :apache2ctl_V

    # The Apache executable's architectural bits. Returns 32 or 64,
    # or nil if unable to detect.
    def self.httpd_architecture_bits(options = nil)
      if options
        info = apache2ctl_V(options)
      else
        info = apache2ctl_V
      end
      if info
        info =~ %r{Architecture:(.*)}
        text = $1
        if text =~ /32/
          32
        elsif text =~ /64/
          64
        else
          nil
        end
      else
        nil
      end
    end
    memoize :httpd_architecture_bits

    # The default Apache root directory, as specified by its compilation parameters.
    # This may be different from the value of the ServerRoot directive.
    def self.httpd_default_root(options = nil)
      if options
        info = apache2ctl_V(options)
      else
        info = apache2ctl_V
      end
      if info
        info =~ / -D HTTPD_ROOT="(.+)"$/
        return $1
      else
        return nil
      end
    end
    memoize :httpd_default_root

    # The default Apache configuration file, or nil if Apache is not found.
    def self.httpd_default_config_file(options = nil)
      if options
        info = apache2ctl_V(options)
      else
        info = apache2ctl_V
      end
      if info
        info =~ /-D SERVER_CONFIG_FILE="(.+)"$/
        filename = $1
        if filename =~ /\A\//
          return filename
        else
          # Not an absolute path. Infer from default root.
          if root = httpd_default_root(options)
            return "#{root}/#{filename}"
          else
            return nil
          end
        end
      else
        return nil
      end
    end
    memoize :httpd_default_config_file

    # Given an Apache config file, returns the a hash with the following elements:
    #
    #  * `:files` - An array containing `config_file`, as well as all config files
    #               included from that config file, including recursively included
    #               ones. Only filenames that actually exist are put here.
    #  * `:unreadable_files` - All config files that this function was unable
    #                          to read.
    def self.httpd_included_config_files(config_file, options = nil)
      state = {
        :files => { config_file => true },
        :unreadable_files => [],
        :root => httpd_default_root(options)
      }
      scan_for_included_apache2_config_files(config_file, state, options)
      return {
        :files => state[:files].keys,
        :unreadable_files => state[:unreadable_files]
      }
    end

    # The default Apache error log's filename, as it is compiled into the Apache
    # main executable. This may not be the actual error log that is used. The actual
    # error log depends on the configuration file.
    #
    # Returns nil if Apache is not detected, or if the default error log filename
    # cannot be detected.
    def self.httpd_default_error_log(options = nil)
      if info = apache2ctl_V(options)
        info =~ /-D DEFAULT_ERRORLOG="(.+)"$/
        filename = $1
        if filename =~ /\A\//
          return filename
        else
          # Not an absolute path. Infer from default root.
          if root = httpd_default_root(options)
            return "#{root}/#{filename}"
          else
            return nil
          end
        end
      else
        return nil
      end
    end
    memoize :httpd_default_error_log

    def self.httpd_actual_error_log(options = nil)
      if config_file = httpd_default_config_file(options)
        begin
          contents = File.open(config_file, "rb") { |f| f.read }
        rescue Errno::ENOENT
          log "#{config_file} does not exist"
          return nil
        rescue Errno::EACCES
          log "Unable to open #{config_file} for reading"
          return nil
        end
        # We don't want to match comments
        contents.gsub!(/^[ \t]*#.*/, '')
        if contents =~ /^[ \t]*ErrorLog[ \t]+(.+)[ \t]*$/i
          filename = unescape_apache_config_value($1, options)
          if filename && filename !~ /\A\//
            # Not an absolute path. Infer from root.
            if root = httpd_default_root(options)
              return "#{root}/#{filename}"
            else
              return nil
            end
          else
            return filename
          end
        elsif contents =~ /ErrorLog/i
          # The user apparently has ErrorLog set somewhere but
          # we can't parse it. The default error log location,
          # as reported by `apache2ctl -V`, may be wrong (it is on OS X).
          # So to be safe, let's assume that we don't know.
          log "Unable to parse ErrorLog directive in Apache configuration file"
          return nil
        else
          log "No ErrorLog directive in Apache configuration file"
          return httpd_default_error_log(options)
        end
      else
        return nil
      end
    end
    memoize :httpd_actual_error_log

    # The location of the Apache envvars file, which exists on some systems such as Ubuntu.
    # Returns nil if Apache is not found or if the envvars file is not found.
    def self.httpd_envvars_file(options = nil)
      if options
        httpd = options[:httpd] || self.httpd(options)
      else
        httpd = self.httpd
      end

      httpd_dir = File.dirname(httpd)
      if httpd_dir == "/usr/bin" || httpd_dir == "/usr/sbin"
        if File.exist?("/etc/apache2/envvars")
          return "/etc/apache2/envvars"
        elsif File.exist?("/etc/httpd/envvars")
          return "/etc/httpd/envvars"
        end
      end

      conf_dir = File.expand_path(File.dirname(httpd) + "/../conf")
      if File.exist?("#{conf_dir}/envvars")
        return "#{conf_dir}/envvars"
      end

      return nil
    end

    def self.httpd_infer_envvar(varname, options = nil)
      if envfile = httpd_envvars_file(options)
        result = `. '#{envfile}' && echo $#{varname}`.strip
        if $? && $?.exitstatus == 0
          return result
        else
          return nil
        end
      else
        return nil
      end
    end

    # Returns the path to the Apache `mods-available` subdirectory,
    # or nil if it's not supported by this Apache.
    def self.httpd_mods_available_directory(options = nil)
      config_file = httpd_default_config_file(options)
      return nil if !config_file

      # mods-available is supposed to be a Debian extension that only works
      # on the APT-installed Apache, so only return non-nil if we're
      # working against the APT-installed Apache.
      config_dir = File.dirname(config_file)
      if config_dir == "/etc/httpd" || config_dir == "/etc/apache2"
        if File.exist?("#{config_dir}/mods-available") &&
           File.exist?("#{config_dir}/mods-enabled")
          return "#{config_dir}/mods-available"
        else
          return nil
        end
      else
        return nil
      end
    end
    memoize :httpd_mods_available_directory

    # Returns the path to the Apache `mods-enabled` subdirectory,
    # or nil if it's not supported by this Apache.
    def self.httpd_mods_enabled_directory(options = nil)
      config_file = httpd_default_config_file(options)
      return nil if !config_file

      # mods-enabled is supposed to be a Debian extension that only works
      # on the APT-installed Apache, so only return non-nil if we're
      # working against the APT-installed Apache.
      config_dir = File.dirname(config_file)
      if config_dir == "/etc/httpd" || config_dir == "/etc/apache2"
        if File.exist?("#{config_dir}/mods-available") &&
           File.exist?("#{config_dir}/mods-enabled")
          return "#{config_dir}/mods-enabled"
        else
          return nil
        end
      else
        return nil
      end
    end
    memoize :httpd_mods_enabled_directory

    # The absolute path to the 'a2enmod' executable.
    def self.a2enmod(options = {})
      apxs2 = options.fetch(:apxs2, self.apxs2)
      dir = File.dirname(apxs2) if apxs2
      # a2enmod is supposed to be a Debian extension that only works
      # on the APT-installed Apache, so only return non-nil if we're
      # working against the APT-installed Apache.
      if dir == "/usr/bin" || dir == "/usr/sbin"
        if env_defined?('A2ENMOD')
          log "Using $A2ENMOD (= #{ENV['A2ENMOD']})"
          return ENV['A2ENMOD']
        else
          return find_apache2_executable("a2enmod", options)
        end
      else
        log "Not applicable"
        nil
      end
    end
    memoize :a2enmod

    # The absolute path to the 'a2enmod' executable.
    def self.a2dismod(options = {})
      apxs2 = options.fetch(:apxs2, self.apxs2)
      dir = File.dirname(apxs2) if apxs2
      # a2dismod is supposed to be a Debian extension that only works
      # on the APT-installed Apache, so only return non-nil if we're
      # working against the APT-installed Apache.
      if dir == "/usr/bin" || dir == "/usr/sbin"
        if env_defined?('A2DISMOD')
          log "Using $A2DISMOD (= #{ENV['A2DISMOD']})"
          return ENV['A2DISMOD']
        else
          return find_apache2_executable("a2dismod", options)
        end
      else
        log "Not applicable"
        nil
      end
    end
    memoize :a2dismod

    # The absolute path to the 'apr-config' or 'apr-1-config' executable,
    # or nil if not found.
    def self.apr_config
      if env_defined?('APR_CONFIG')
        return ENV['APR_CONFIG']
      elsif apxs2.nil?
        return nil
      else
        filename = `#{apxs2} -q APR_CONFIG 2>/dev/null`.strip
        if filename.empty?
          apr_bindir = `#{apxs2} -q APR_BINDIR 2>/dev/null`.strip
          if apr_bindir.empty?
            return nil
          else
            return select_executable(apr_bindir,
              "apr-1-config", "apr-config")
          end
        elsif File.exist?(filename)
          return filename
        else
          return nil
        end
      end
    end
    memoize :apr_config

    # The absolute path to the 'apu-config' or 'apu-1-config' executable, or nil
    # if not found.
    def self.apu_config
      if env_defined?('APU_CONFIG')
        return ENV['APU_CONFIG']
      elsif apxs2.nil?
        return nil
      else
        filename = `#{apxs2} -q APU_CONFIG 2>/dev/null`.strip
        if filename.empty?
          apu_bindir = `#{apxs2} -q APU_BINDIR 2>/dev/null`.strip
          if apu_bindir.empty?
            return nil
          else
            return select_executable(apu_bindir,
              "apu-1-config", "apu-config")
          end
        elsif File.exist?(filename)
          return filename
        else
          return nil
        end
      end
    end
    memoize :apu_config

    # Find an executable in the Apache 'bin' and 'sbin' directories.
    # Returns nil if not found.
    def self.find_apache2_executable(*possible_names)
      if possible_names.last.is_a?(Hash)
        options = possible_names.pop
        options = nil if options.empty?
      end

      if options
        dirs = options[:dirs] || [apache2_bindir(options), apache2_sbindir(options)]
      else
        dirs = [apache2_bindir, apache2_sbindir]
      end

      dirs.each do |bindir|
        if bindir.nil?
          next
        end
        possible_names.each do |name|
          filename = "#{bindir}/#{name}"
          if !File.exist?(filename)
            log "Looking for #{filename}: not found"
          elsif !File.file?(filename)
            log "Looking for #{filename}: found, but is not a file"
          elsif !File.executable?(filename)
            log "Looking for #{filename}: found, but is not executable"
          else
            log "Looking for #{filename}: found"
            return filename
          end
        end
      end
      return nil
    end


    ################ Directories ################

    # The absolute path to the Apache 2 'bin' directory, or nil if unknown.
    def self.apache2_bindir(options = {})
      apxs2 = options.fetch(:apxs2, self.apxs2)
      if apxs2.nil?
        # macOS >= 10.13 High Sierra no longer ships apxs2, so we'll use
        # a hardcoded default.
        if os_name_simple == 'macosx' && os_version >= '10.13' \
          && httpd(:apxs2 => apxs2) == '/usr/sbin/httpd'
          '/usr/bin'
        else
          nil
        end
      else
        return `#{apxs2} -q BINDIR 2>/dev/null`.strip
      end
    end
    memoize :apache2_bindir

    # The absolute path to the Apache 2 'sbin' directory, or nil if unknown.
    def self.apache2_sbindir(options = {})
      apxs2 = options.fetch(:apxs2, self.apxs2)
      if apxs2.nil?
        # macOS >= 10.13 High Sierra no longer ships apxs2, so we'll use
        # a hardcoded default.
        if os_name_simple == 'macosx' && os_version >= '10.13' \
          && httpd(:apxs2 => apxs2) == '/usr/sbin/httpd'
          '/usr/sbin'
        else
          nil
        end
      else
        return `#{apxs2} -q SBINDIR`.strip
      end
    end
    memoize :apache2_sbindir

    def self.apache2_modulesdir(options = {})
      apxs2 = options.fetch(:apxs2, self.apxs2)
      if apxs2.nil?
        # macOS >= 10.13 High Sierra no longer ships apxs2, so we'll use
        # a hardcoded default.
        if os_name_simple == 'macosx' && os_version >= '10.13' \
          && httpd(:apxs2 => apxs2) == '/usr/sbin/httpd'
          '/usr/libexec/apache2'
        else
          nil
        end
      else
        `#{apxs2} -q LIBEXECDIR`.strip
      end
    end
    memoize :apache2_modulesdir


    ################ Compiler and linker flags ################

    def self.apache2_module_cflags(with_apr_flags = true)
      return apache2_module_c_or_cxxflags(:c, with_apr_flags)
    end
    memoize :apache2_module_cflags, true

    def self.apache2_module_cxxflags(with_apr_flags = true)
      return apache2_module_c_or_cxxflags(:cxx, with_apr_flags)
    end
    memoize :apache2_module_cxxflags, true

    # The C compiler flags that are necessary to compile an Apache module.
    # Also includes APR and APU compiler flags if with_apr_flags is true.
    def self.apache2_module_c_or_cxxflags(language, with_apr_flags = true)
      flags = [""]
      if (language == :c && cc_is_sun_studio?) || (language == :cxx && cxx_is_sun_studio?)
        flags << "-KPIC"
      else
        flags << "-fPIC"
      end
      if with_apr_flags
        if language == :c
          flags << apr_cflags
          flags << apu_cflags
        else
          flags << apr_cxxflags
          flags << apu_cxxflags
        end
      end
      if apxs2.nil?
        # macOS >= 10.13 High Sierra no longer includes apxs2
        # so we'll use hardcoded paths here.
        if os_name_simple == 'macosx' && os_version >= '10.13' && httpd == '/usr/sbin/httpd'
          # High Sierra doesn't have a consistent location for the Apache header
          # files. On Hongli's and Camden's systems it's inside the Xcode directory,
          # while on https://github.com/phusion/passenger/issues/1986 it's inside
          # /Library/Developer/CommandLineTools.
          xcode_prefix = `/usr/bin/xcode-select -p`.strip
          flags << "-I#{xcode_prefix}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/apache2"
          flags << "-I/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/apache2"
        end
      else
        apxs2_flags = `#{apxs2} -q CFLAGS`.strip << " -I" << `#{apxs2} -q INCLUDEDIR`.strip
        apxs2_flags.gsub!(/-O\d? /, '')

        if os_name_simple == "solaris"
          if (language == :c && !cc_is_sun_studio?) || (language == :cxx && !cxx_is_sun_studio?)
            # Remove flags not supported by GCC
            # The big problem is Coolstack apxs includes a bunch of solaris -x directives.
            options = apxs2_flags.split
            options.reject! { |f| f =~ /^\-x/ }
            options.reject! { |f| f =~ /^\-Xa/ }
            options.reject! { |f| f =~ /^\-fast/ }
            options.reject! { |f| f =~ /^\-mt/ }
            options.reject! { |f| f =~ /^\-W2/ }
            apxs2_flags = options.join(' ')
            apxs2_flags.gsub!(/ ?-Qoption cg ?/, " ")
          end
        end

        if os_name_simple == "linux" &&
           linux_distro_tags.include?(:redhat) &&
           apxs2 == "/usr/sbin/apxs" &&
           httpd_architecture_bits == 64
          # The Apache package in CentOS 5 x86_64 is broken.
          # 'apxs -q CFLAGS' contains directives for compiling
          # the module as 32-bit, even though httpd itself
          # is 64-bit. Fix this.
          apxs2_flags.gsub!('-m32 -march=i386 -mtune=generic', '')
        end

        # Some Apache installations include '-pie' in CFLAGS, which
        # won't work on shared libraries.
        # https://github.com/phusion/passenger/issues/1756
        apxs2_flags.gsub!('-pie', '')

        # macOS >= 10.12 Sierra no longer includes apr-config
        # so we'll use a hardcoded path here.
        if os_name_simple == 'macosx' && os_version >= '10.12' && httpd == '/usr/sbin/httpd'
          apxs2_flags << ' -I/usr/include/apr-1'
        end

        apxs2_flags.strip!
        flags << apxs2_flags
      end
      if !httpd.nil? && os_name_simple == "macosx"
        # The default Apache install on OS X is a universal binary.
        # Figure out which architectures it's compiled for and do the same
        # thing for mod_passenger. We use the 'file' utility to do this.
        #
        # Running 'file' on the Apache executable usually outputs something
        # like this:
        #
        #   /usr/sbin/httpd: Mach-O universal binary with 4 architectures
        #   /usr/sbin/httpd (for architecture ppc7400):     Mach-O executable ppc
        #   /usr/sbin/httpd (for architecture ppc64):       Mach-O 64-bit executable ppc64
        #   /usr/sbin/httpd (for architecture i386):        Mach-O executable i386
        #   /usr/sbin/httpd (for architecture x86_64):      Mach-O 64-bit executable x86_64
        #
        # But on some machines, it may output just:
        #
        #   /usr/sbin/httpd: Mach-O fat file with 4 architectures
        #
        # (http://code.google.com/p/phusion-passenger/issues/detail?id=236)
        output = `file "#{httpd}"`.strip
        if output =~ /Mach-O fat file/ && output !~ /for architecture/
          architectures = ["i386", "ppc", "x86_64", "ppc64"]
        else
          architectures = []
          output.split("\n").grep(/for architecture/).each do |line|
            line =~ /for architecture (.*?)\)/
            architectures << $1
          end
        end
        # The compiler may not support all architectures in the binary.
        # XCode 4 seems to have removed support for the PPC architecture
        # even though there are still plenty of Apache binaries around
        # containing PPC components.
        architectures.reject! do |arch|
          !compiler_supports_architecture?(arch)
        end
        architectures.map! do |arch|
          "-arch #{arch}"
        end
        flags << architectures.compact.join(' ')
      end
      return flags.compact.join(' ').strip
    end

    # Linker flags that are necessary for linking an Apache module.
    # Already includes APR and APU linker flags.
    def self.apache2_module_cxx_ldflags
      flags = ""
      if !apxs2.nil?
        flags << `#{apxs2} -q LDFLAGS`.strip
      end

      # We must include the cxxflags in the linker flags. On some multilib
      # Solaris systems, `apxs -q CFLAGS` outputs a flag that tells the compiler
      # which architecture to compile against, while `apxs -q LDFLAGS` doesn't.
      flags << " #{apache2_module_cxxflags} #{apr_cxx_ldflags} #{apu_cxx_ldflags}"

      if cxx_is_sun_studio?
        flags.gsub!("-fPIC", "-KPIC")
        flags << " -KPIC" if !flags.include?("-KPIC")
      else
        flags << " -fPIC" if !flags.include?("-fPIC")
      end

      flags.strip!
      flags
    end
    memoize :apache2_module_cxx_ldflags

    # The C compiler flags that are necessary for programs that use APR.
    def self.apr_cflags
      determine_apr_c_info[0]
    end

    # The C++ compiler flags that are necessary for programs that use APR.
    def self.apr_cxxflags
      determine_apr_c_info[0]
    end

    # The linker flags that are necessary for linking programs that use APR.
    def self.apr_c_ldflags
      determine_apr_c_info[1]
    end

    # The linker flags that are necessary for linking C++ programs that use APR.
    def self.apr_cxx_ldflags
      determine_apr_cxx_info[1]
    end

    # The C compiler flags that are necessary for programs that use APR-Util.
    def self.apu_cflags
      determine_apu_c_info[0]
    end

    # The C++ compiler flags that are necessary for programs that use APR-Util.
    def self.apu_cxxflags
      determine_apu_cxx_info[0]
    end

    # The linker flags that are necessary for linking C programs that use APR-Util.
    def self.apu_c_ldflags
      determine_apu_c_info[1]
    end

    # The linker flags that are necessary for linking C++ programs that use APR-Util.
    def self.apu_cxx_ldflags
      determine_apu_cxx_info[1]
    end


    ################ Miscellaneous information ################

    # Returns whether it is necessary to use information outputted by 'apxs2'
    # in order to compile an Apache module.
    #
    # Since macOS 10.13 High Sierra apxs2 is no longer included in the
    # operating system, so we return false in that case. We'll fallback
    # to hardcoded paths.
    def self.apxs2_needed_for_building_apache_modules?
      !(os_name_simple == 'macosx' &&
        os_version >= '10.13' &&
        httpd == '/usr/sbin/httpd')
    end

    # Returns whether it is necessary to use information outputted by
    # 'apr-config' and 'apu-config' in order to compile an Apache module.
    #
    # When Apache is installed with --with-included-apr, the APR/APU
    # headers are placed into the same directory as the Apache headers,
    # and so 'apr-config' and 'apu-config' won't be necessary in that case.
    #
    # Also, since macOS 10.12 Sierra apr-config is no longer included
    # in the operating system, so we also return false in that case.
    # We'll fallback to hardcoded paths.
    def self.apr_config_needed_for_building_apache_modules?
      !(
        os_name_simple == 'macosx' &&
        os_version >= '10.13' &&
        httpd == '/usr/sbin/httpd'
      ) &&
        !try_compile("whether APR is needed for building Apache modules",
          :c, "#include <apr.h>\n", apache2_module_cflags(false))
    end
    memoize :apr_config_needed_for_building_apache_modules?, true

  private
    def self.determine_apr_info(language)
      if apr_config.nil?
        if os_name_simple == 'macosx' && httpd == '/usr/sbin/httpd'
          # macOS >= 10.12 Sierra does not supply apr-config, so
          # we use hardcoded defaults.
          if os_version >= '10.13'
            # On macOS >= 10.13 High Sierra /usr/include no longer
            # exists.
            # High Sierra doesn't have a consistent location for the Apache header
            # files. On Hongli's and Camden's systems it's inside the Xcode directory,
            # while on https://github.com/phusion/passenger/issues/1986 it's inside
            # /Library/Developer/CommandLineTools.
            xcode_prefix = `/usr/bin/xcode-select -p`.strip
            ["-I#{xcode_prefix}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/apr-1 " \
             "-I#{xcode_prefix}/SDKs/MacOSX.sdk/usr/include/apr-1",
             '-lapr-1']
          else
            ['-I/usr/include/apr-1', '-lapr-1']
          end
        else
          [nil, nil]
        end
      else
        flags = `#{apr_config} --cppflags --includes`.strip
        libs = `#{apr_config} --link-ld`.strip
        flags.gsub!(/-O\d? /, '')
        if os_name_simple == "solaris"
          if (language == :c && !cc_is_sun_studio?) || (language == :cxx && !cxx_is_sun_studio?)
            # Remove flags not supported by non-Sun Studio compilers
            flags = flags.split(/ +/).reject do |f|
              f =~ /^\-mt/
            end
            flags = flags.join(' ')
          end
        elsif os_name_simple == "aix"
          libs << " -Wl,-G -Wl,-brtl"
        end
        [flags, libs]
      end
    end
    private_class_method :determine_apr_info

    def self.determine_apr_c_info
      determine_apr_info(:c)
    end
    private_class_method :determine_apr_c_info
    memoize :determine_apr_c_info, true

    def self.determine_apr_cxx_info
      determine_apr_info(:cxx)
    end
    private_class_method :determine_apr_cxx_info
    memoize :determine_apr_cxx_info, true

    def self.determine_apu_info(language)
      if apu_config.nil?
        if os_name_simple == 'macosx' && httpd == '/usr/sbin/httpd'
          # macOS >= 10.12 Sierra does not supply apu-config, so
          # we use hardcoded defaults.
          if os_version >= '10.13'
            # On macOS >= 10.13 High Sierra /usr/include no longer
            # exists.
            xcode_prefix = `/usr/bin/xcode-select -p`.strip
            ["-I#{xcode_prefix}/Platforms/MacOSX.platform/Developer/SDKs/MacOSX.sdk/usr/include/apr-1" \
             "-I#{xcode_prefix}/SDKs/MacOSX.sdk/usr/include/apr-1",
             '-laprutil-1']
          else
            ['-I/usr/include/apr-1', '-laprutil-1']
          end
        else
          [nil, nil]
        end
      else
        flags = `#{apu_config} --includes`.strip
        libs = `#{apu_config} --link-ld`.strip
        flags.gsub!(/-O\d? /, '')
        if os_name_simple == "solaris"
          if (language == :c && !cc_is_sun_studio?) || (language == :cxx && !cxx_is_sun_studio?)
            # Remove flags not supported by non-Sun Studio compilers
            flags = flags.split(/ +/).reject do |f|
              f =~ /^\-mt/
            end
            flags = flags.join(' ')
          end
        end
        [flags, libs]
      end
    end
    private_class_method :determine_apu_info

    def self.determine_apu_c_info
      determine_apu_info(:c)
    end
    private_class_method :determine_apu_c_info
    memoize :determine_apu_c_info, true

    def self.determine_apu_cxx_info
      determine_apu_info(:cxx)
    end
    private_class_method :determine_apu_cxx_info
    memoize :determine_apu_cxx_info, true

    def self.scan_for_included_apache2_config_files(config_file, state, options = nil)
      begin
        config = File.open(config_file, "rb") do |f|
          f.read
        end
      rescue Errno::EACCES
        state[:unreadable_files] << config_file
        return
      end

      found_filenames = []

      config.scan(/^[ \t]*(Include(Optional)?|ServerRoot)[ \t]+(.+?)[ \t]*$/i) do |match|
        if match[0].downcase == "serverroot"
          new_root = unescape_apache_config_value(match[2], options)
          state[:root] = new_root if new_root
        else
          filename = unescape_apache_config_value(match[2], options)
          next if filename.nil? || filename.empty?
          if filename !~ /\A\//
            # Not an absolute path. Infer from root.
            filename = "#{state[:root]}/#{filename}"
          end
          expand_apache2_glob(filename).each do |filename2|
            if !state[:files].has_key?(filename2)
              state[:files][filename2] = true
              scan_for_included_apache2_config_files(filename2, state, options)
            end
          end
        end
      end
    end
    private_class_method :scan_for_included_apache2_config_files

    def self.expand_apache2_glob(glob)
      if File.directory?(glob)
        glob = glob.sub(/\/*$/, '')
        result = Dir["#{glob}/**/*"]
      else
        result = []
        Dir[glob].each do |filename|
          if File.directory?(filename)
            result.concat(Dir["#{filename}/**/*"])
          else
            result << filename
          end
        end
      end
      result.reject! do |filename|
        File.directory?(filename)
      end
      return result
    end
    private_class_method :expand_apache2_glob

    def self.unescape_apache_config_value(value, options = nil)
      if value =~ /^"(.*)"$/
        value = unescape_c_string($1)
      end
      if value.include?("${")
        log "Attempting to substitute environment variables in Apache config value #{value.inspect}..."
      end
      # The Apache config file supports environment variable
      # substitution. Ubuntu uses this extensively.
      value.gsub!(/\$\{(.+?)\}/) do |varname|
        if substitution = httpd_infer_envvar($1, options)
          log "Substituted \"#{varname}\" -> \"#{substitution}\""
          substitution
        else
          log "Cannot substitute \"#{varname}\""
          varname
        end
      end
      if value.include?("${")
        # We couldn't substitute everything.
        return nil
      else
        return value
      end
    end
    private_class_method :unescape_apache_config_value

    def self.unescape_c_string(s)
      state = 0
      res = ''
      backslash = "\\"
      s.each_char do |c|
        case state
        when 0
          case c
          when backslash then state = 1
          else res << c
          end
        when 1
          case c
          when 'n' then res << "\n"; state = 0
          when 't' then res << "\t"; state = 0
          when backslash then res << backslash; state = 0
          else res << backslash; res << c; state = 0
          end
        end
      end
      return res
    end
    private_class_method :unescape_c_string
  end

end
