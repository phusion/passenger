# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2018 Phusion Holding B.V.
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

PhusionPassenger.require_passenger_lib 'utils/tmpio'

module PhusionPassenger

  # This module autodetects various platform-specific information, and
  # provides that information through constants.
  module PlatformInfo
  private
    @@cache_dir = nil
    @@verbose   = ['1', 'true', 'on', 'yes'].include?(ENV['VERBOSE'])
    @@log_implementation = lambda do |message|
      message = reindent(message, 3)
      message.sub!(/^   /, '')
      STDERR.puts " * #{message}"
    end

    def self.private_class_method(name)
      metaclass = class << self; self; end
      metaclass.send(:private, name)
    end
    private_class_method :private_class_method

    # Turn the specified class method into a memoized one. If the given
    # class method is called without arguments, then its result will be
    # memoized, frozen, and returned upon subsequent calls without arguments.
    # Calls with arguments are never memoized.
    #
    # If +cache_to_disk+ is true and a cache directory has been set with
    # <tt>PlatformInfo.cache_dir=</tt> then result is cached to a file on disk,
    # so that memoized results persist over multiple process runs. This
    # cache file expires in +cache_time+ seconds (1 hour by default) after
    # it has been written.
    #
    #   def self.foo(max = 10)
    #     rand(max)
    #   end
    #   memoize :foo
    #
    #   foo        # => 3
    #   foo        # => 3
    #   foo(100)   # => 49
    #   foo(100)   # => 26
    #   foo        # => 3
    def self.memoize(method, cache_to_disk = false, cache_time = 3600)
        # We use class_eval here because Ruby 1.8.5 doesn't support class_variable_get/set.
        metaclass = class << self; self; end
        metaclass.send(:alias_method, "_unmemoized_#{method}", method)
        variable_name = "@@memoized_#{method}".sub(/\?/, '')
        check_variable_name = "@@has_memoized_#{method}".sub(/\?/, '')
        eval(%Q{
          #{variable_name} = nil
          #{check_variable_name} = false
        })
        line = __LINE__ + 1
        source = %Q{
          def self.#{method}(*args)                                           # def self.httpd(*args)
            if args.empty?                                                    #   if args.empty?
              if !#{check_variable_name}                                      #     if !@@has_memoized_httpd
                if @@cache_dir                                                #       if @@cache_dir
                  cache_file = File.join(@@cache_dir, "#{method}")            #         cache_file = File.join(@@cache_dir, "httpd")
                end                                                           #       end
                read_from_cache_file = false                                  #       read_from_cache_file = false
                if #{cache_to_disk} && cache_file && File.exist?(cache_file)  #       if #{cache_to_disk} && File.exist?(cache_file)
                  cache_file_stat = File.stat(cache_file)                     #         cache_file_stat = File.stat(cache_file)
                  read_from_cache_file =                                      #         read_from_cache_file =
                    Time.now - cache_file_stat.mtime < #{cache_time}          #           Time.now - cache_file_stat.mtime < #{cache_time}
                end                                                           #       end
                if read_from_cache_file                                       #       if read_from_cache_file
                  data = File.read(cache_file)                                #         data = File.read(cache_file)
                  #{variable_name} = Marshal.load(data).freeze                #         @@memoized_httpd = Marshal.load(data).freeze
                  #{check_variable_name} = true                               #         @@has_memoized_httpd = true
                else                                                          #       else
                  #{variable_name} = _unmemoized_#{method}.freeze             #         @@memoized_httpd = _unmemoized_httpd.freeze
                  #{check_variable_name} = true                               #         @@has_memoized_httpd = true
                  if cache_file && #{cache_to_disk}                           #         if cache_file && #{cache_to_disk}
                    begin                                                     #           begin
                      if !File.directory?(@@cache_dir)                        #             if !File.directory?(@@cache_dir)
                        FileUtils.mkdir_p(@@cache_dir)                        #               FileUtils.mkdir_p(@@cache_dir)
                      end                                                     #             end
                      File.open(cache_file, "wb") do |f|                      #             File.open(cache_file, "wb") do |f|
                        f.write(Marshal.dump(#{variable_name}))               #               f.write(Marshal.dump(@@memoized_httpd))
                      end                                                     #             end
                    rescue Errno::EACCES                                      #           rescue Errno::EACCES
                      # Ignore permission error.                              #             # Ignore permission error.
                    end                                                       #           end
                  end                                                         #         end
                end                                                           #       end
              end                                                             #     end
              #{variable_name}                                                #     @@memoized_httpd
            else                                                              #   else
              _unmemoized_#{method}(*args)                                    #     _unmemoized_httpd(*args)
            end                                                               #   end
          end                                                                 # end
        }
        class_eval(source, __FILE__, line)
    end
    private_class_method :memoize

    # Look in the directory +dir+ and check whether there's an executable
    # whose base name is equal to one of the elements in +possible_names+.
    # If so, returns the full filename. If not, returns nil.
    def self.select_executable(dir, *possible_names)
      possible_names.each do |name|
        filename = "#{dir}/#{name}"
        if File.file?(filename) && File.executable?(filename)
          return filename
        end
      end
      return nil
    end
    private_class_method :select_executable

    def self.unindent(str)
      str = str.dup
      str.gsub!(/\A([\s\t]*\n)+/, '')
      str.gsub!(/[\s\t\n]+\Z/, '')
      indent = str.split("\n").select{ |line| !line.strip.empty? }.map{ |line| line.index(/[^\s]/) }.compact.min || 0
      str.gsub!(/^[[:blank:]]{#{indent}}/, '')
      return str
    end
    private_class_method :unindent

    def self.reindent(str, level)
      str = unindent(str)
      str.gsub!(/^/, ' ' * level)
      return str
    end
    private_class_method :reindent

    def self.create_temp_file(name, dir = tmpdir)
      # This function is mostly used for compiling C programs to autodetect
      # system properties. We create a secure temp subdirectory to prevent
      # TOCTU attacks, especially because we don't know how the compiler
      # handles this.
      PhusionPassenger::Utils.mktmpdir("passenger.", dir) do |subdir|
        filename = "#{subdir}/#{name}"
        f = File.open(filename, "w")
        begin
          yield(filename, f)
        ensure
          f.close if !f.closed?
        end
      end
    end
    private_class_method :create_temp_file

    def self.log(message)
      if verbose?
        @@log_implementation.call(message)
      end
    end
    private_class_method :log

  public
    class RuntimeError < ::RuntimeError
    end


    def self.cache_dir=(value)
      @@cache_dir = value
    end

    def self.cache_dir
      return @@cache_dir
    end

    def self.verbose=(val)
      @@verbose = val
    end

    def self.verbose?
      return @@verbose
    end

    def self.log_implementation=(impl)
      @@log_implementation = impl
    end

    def self.log_implementation
      return @@log_implementation
    end


    def self.env_defined?(name)
      return !ENV[name].nil? && !ENV[name].empty?
    end

    def self.string_env(name, default_value = nil)
      value = ENV[name]
      if value.nil? || value.empty?
        return default_value
      else
        return value
      end
    end

    def self.read_file(filename)
      return File.open(filename, "rb") do |f|
        f.read
      end
    rescue
      return ""
    end

    # Clears all memoized values. However, the disk cache (if any is configured)
    # is still used.
    def self.clear_memoizations
      class_variables.each do |name|
        if name.to_s =~ /^@@has_memoized_/
          class_variable_set(name, false)
        end
      end
    end

    def self.tmpdir
      result = ENV['TMPDIR']
      if result && !result.empty?
        return result.sub(/\/+\Z/, '')
      else
        return '/tmp'
      end
    end
    memoize :tmpdir

    # Returns the directory in which test executables should be placed. The
    # returned directory is guaranteed to be writable and guaranteed to
    # not be mounted with the 'noexec' option.
    # If no such directory can be found then it will raise a PlatformInfo::RuntimeError
    # with an appropriate error message.
    def self.tmpexedir
      basename = "test-exe.#{Process.pid}.#{Thread.current.object_id}"
      attempts = []

      dir = tmpdir
      filename = "#{dir}/#{basename}"
      begin
        File.open(filename, 'w') do |f|
          f.chmod(0700)
          f.puts("#!/bin/sh")
        end
        if system(filename)
          return dir
        else
          attempts << { :dir => dir,
            :error => "This directory's filesystem is mounted with the 'noexec' option." }
        end
      rescue Errno::ENOENT
        attempts << { :dir => dir, :error => "This directory doesn't exist." }
      rescue Errno::EACCES
        attempts << { :dir => dir, :error => "This program doesn't have permission to write to this directory." }
      rescue SystemCallError => e
        attempts << { :dir => dir, :error => e.message }
      ensure
        File.unlink(filename) rescue nil
      end

      dir = Dir.pwd
      filename = "#{dir}/#{basename}"
      begin
        File.open(filename, 'w') do |f|
          f.chmod(0700)
          f.puts("#!/bin/sh")
        end
        if system(filename)
          return dir
        else
          attempts << { :dir => dir,
            :error => "This directory's filesystem is mounted with the 'noexec' option." }
        end
      rescue Errno::ENOENT
        attempts << { :dir => dir, :error => "This directory doesn't exist." }
      rescue Errno::EACCES
        attempts << { :dir => dir, :error => "This program doesn't have permission to write to this directory." }
      rescue SystemCallError => e
        attempts << { :dir => dir, :error => e.message }
      ensure
        File.unlink(filename) rescue nil
      end

      message = "ERROR: Cannot find suitable temporary directory\n" +
        "In order to run certain tests, this program " +
        "must be able to write temporary\n" +
        "executable files to some directory. However no such " +
        "directory can be found. \n" +
        "The following directories have been tried:\n\n"
      attempts.each do |attempt|
        message << " * #{attempt[:dir]}\n"
        message << "   #{attempt[:error]}\n"
      end
      message << "\nYou can solve this problem by telling this program what directory to write\n" <<
        "temporary executable files to, as follows:\n" <<
        "\n" <<
        "  Set the $TMPDIR environment variable to the desired directory's filename and\n" <<
        "  re-run this program.\n" <<
        "\n" <<
        "Notes:\n" <<
        "\n" <<
        " * If you're using 'sudo'/'rvmsudo', remember that 'sudo'/'rvmsudo' unsets all\n" <<
        "   environment variables, so you must set the environment variable *after*\n" <<
        "   having gained root privileges.\n" <<
        " * The directory you choose must writeable and must not be mounted with the\n" <<
        "   'noexec' option."
      raise RuntimeError, message
    end
    memoize :tmpexedir

    def self.rb_config
      if defined?(::RbConfig)
        return ::RbConfig::CONFIG
      else
        return ::Config::CONFIG
      end
    end

    # Check whether the specified command is in $PATH, and return its
    # absolute filename. Returns nil if the command is not found.
    #
    # This function exists because system('which') doesn't always behave
    # correctly, for some weird reason.
    #
    # When `is_executable` is true, this function checks whether
    # there is an executable named `name` in $PATH. When false, it
    # assumes that `name` is not an executable name but a command string
    # (e.g. "ccache gcc"). It then infers the executable name ("ccache")
    # from the command string, and checks for that instead.
    def self.find_command(name, is_executable = true)
      name = name.to_s
      if !is_executable && name =~ / /
        name = name.sub(/ .*/, '')
      end
      if name =~ /^\//
        if File.executable?(name)
          return name
        else
          return nil
        end
      else
        ENV['PATH'].to_s.split(File::PATH_SEPARATOR).each do |directory|
          next if directory.empty?
          path = File.join(directory, name)
          if File.file?(path) && File.executable?(path)
            return path
          end
        end
        return nil
      end
    end

    def self.find_all_commands(name)
      search_dirs = ENV['PATH'].to_s.split(File::PATH_SEPARATOR)
      search_dirs.concat(%w(/bin /sbin /usr/bin /usr/sbin /usr/local/bin /usr/local/sbin))
      ["/opt/*/bin", "/opt/*/sbin", "/usr/local/*/bin", "/usr/local/*/sbin"].each do |glob|
        search_dirs.concat(Dir[glob])
      end

      # Solaris systems may have Apache installations in
      # /usr/apache2/2.2/bin/sparcv9/
      Dir["/usr/apache2/*/bin"].each do |bindir|
        search_dirs << bindir
        Dir["#{bindir}/*"].each do |binsubdir|
          if File.directory?(binsubdir)
            search_dirs << binsubdir
          end
        end
      end

      search_dirs.delete("")
      search_dirs.uniq!

      result = []
      search_dirs.each do |directory|
        path = File.join(directory, name)
        if !File.exist?(path)
          log "Looking for #{path}: not found"
        elsif !File.file?(path)
          log "Looking for #{path}: found, but is not a file"
        elsif !File.executable?(path)
          log "Looking for #{path}: found, but is not executable"
        else
          log "Looking for #{path}: found"
          result << path
        end
      end
      return result
    end

    # Check whether the specified command is in $PATH or in
    # /sbin:/usr/sbin:/usr/local/sbin (in case these aren't already in $PATH),
    # and return its absolute filename. Returns nil if the command is not
    # found.
    def self.find_system_command(name)
      result = find_command(name)
      if result.nil?
        ['/sbin', '/usr/sbin', '/usr/local/sbin'].each do |dir|
          path = File.join(dir, name)
          if File.file?(path) && File.executable?(path)
            return path
          end
        end
      end
      result
    end
  end

end # module PhusionPassenger
