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

require 'rbconfig'
require 'etc'
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'

module PhusionPassenger

  module PlatformInfo
    # Store original $GEM_HOME value so that even if the app customizes
    # $GEM_HOME we can still work with the original value.
    gem_home = ENV['GEM_HOME']
    if gem_home
      gem_home = gem_home.strip.freeze
      gem_home = nil if gem_home.empty?
    end
    GEM_HOME = gem_home

    # Ditto for $GEM_PATH
    gem_path = ENV['GEM_PATH']
    if gem_path
      gem_path = gem_path.strip.freeze
      gem_path = nil if gem_path.empty?
    end
    GEM_PATH = gem_path

    # 'bundle exec' modifies $GEM_HOME and $GEM_PATH so let's
    # store the values that we had before Bundler's modifications.
    gem_home = ENV['BUNDLER_ORIG_GEM_HOME']
    if gem_home
      gem_home = gem_home.strip.freeze
      gem_home = nil if gem_home.empty?
    end
    BUNDLER_ORIG_GEM_HOME = gem_home

    gem_path = ENV['BUNDLER_ORIG_GEM_PATH']
    if gem_path
      gem_path = gem_path.strip.freeze
      gem_path = nil if gem_path.empty?
    end
    BUNDLER_ORIG_GEM_PATH = gem_path

    if defined?(::RUBY_ENGINE)
      RUBY_ENGINE = ::RUBY_ENGINE
    else
      RUBY_ENGINE = "ruby"
    end

    # Returns correct command for invoking the current Ruby interpreter.
    # In case of RVM this function will return the path to the RVM wrapper script
    # that executes the current Ruby interpreter in the currently active gem set.
    def self.ruby_command
      # Detect usage of gem-wrappers: https://github.com/rvm/gem-wrappers
      # This is currently used by RVM >= 1.25, although it's not exclusive to RVM.
      if GEM_HOME && File.exist?("#{GEM_HOME}/wrappers/ruby")
        return "#{GEM_HOME}/wrappers/ruby"
      end

      if in_rvm?
        # Detect old-school RVM wrapper script location.
        name = rvm_ruby_string
        dirs = rvm_paths
        if name && dirs
          dirs.each do |dir|
            filename = "#{dir}/wrappers/#{name}/ruby"
            if File.exist?(filename)
              contents = File.open(filename, 'rb') do |f|
                f.read
              end
              # Old wrapper scripts reference $HOME which causes
              # things to blow up when run by a different user.
              if contents.include?("$HOME")
                filename = nil
              end
            else
              filename = nil
            end
            if filename
              return filename
            end
          end

          # Correctness of these commands are confirmed by mpapis.
          # If we ever encounter a case for which this logic is not sufficient,
          # try mpapis' pseudo code:
          #
          #   rvm_update_prefix  = write_to rvm_path ? "" : "rvmsudo"
          #   rvm_gemhome_prefix  = write_to GEM_HOME ? "" : "rvmsudo"
          #   repair_command  = "#{rvm_update_prefix} rvm get stable && rvm reload && #{rvm_gemhome_prefix} rvm repair all"
          #   wrapper_command = "#{rvm_gemhome_prefix} rvm wrapper #{rvm_ruby_string} --no-prefix --all"
          case rvm_installation_mode
          when :single
            repair_command  = "rvm get stable && rvm reload && rvm repair all"
            wrapper_command = "rvm wrapper #{rvm_ruby_string} --no-prefix --all"
          when :multi
            repair_command  = "rvmsudo rvm get stable && rvm reload && rvmsudo rvm repair all"
            wrapper_command = "rvmsudo rvm wrapper #{rvm_ruby_string} --no-prefix --all"
          when :mixed
            repair_command  = "rvmsudo rvm get stable && rvm reload && rvm repair all"
            wrapper_command = "rvm wrapper #{rvm_ruby_string} --no-prefix --all"
          end

          STDERR.puts "Your RVM wrapper scripts are too old, or some " +
            "wrapper scripts are missing. Please update/regenerate " +
            "them first by running:\n\n" +
            "  #{repair_command}\n\n" +
            "If that doesn't seem to work, please run:\n\n" +
            "  #{wrapper_command}"
          exit 1
        else
          # Something's wrong with the user's RVM installation.
          # Raise an error so that the user knows this instead of
          # having things fail randomly later on.
          # 'name' is guaranteed to be non-nil because rvm_ruby_string
          # already raises an exception on error.
          STDERR.puts "Your RVM installation appears to be broken: the RVM " +
            "path cannot be found. Please fix your RVM installation " +
            "or contact the RVM developers for support."
          exit 1
        end
      else
        return ruby_executable
      end
    end
    memoize :ruby_command

    # Returns the full path to the current Ruby interpreter's executable file.
    # This might not be the actual correct command to use for invoking the Ruby
    # interpreter; use ruby_command instead.
    def self.ruby_executable
      @@ruby_executable ||=
        rb_config['bindir'] + '/' + rb_config['RUBY_INSTALL_NAME'] + rb_config['EXEEXT']
    end

    # Returns whether the Ruby interpreter supports process forking.
    def self.ruby_supports_fork?
      # MRI >= 1.9.2's respond_to? returns false for methods
      # that are not implemented.
      return Process.respond_to?(:fork) &&
        RUBY_ENGINE != "jruby" &&
        RUBY_ENGINE != "macruby" &&
        rb_config['target_os'] !~ /mswin|windows|mingw/
    end

    # Returns whether Phusion Passenger needs Ruby development headers to
    # be available for the current Ruby implementation.
    def self.passenger_needs_ruby_dev_header?
      # Too much of a trouble for JRuby. We can do without it.
      return RUBY_ENGINE != "jruby"
    end

    # Returns the correct 'gem' command for this Ruby interpreter.
    # If `:sudo => true` is given, then the gem command is prefixed by a
    # sudo command if filesystem permissions require this.
    def self.gem_command(options = {})
      command = ruby_tool_command('gem')
      if options[:sudo] && gem_install_requires_sudo?
        command = "#{ruby_sudo_command} #{command}"
      end
      return command
    end
    memoize :gem_command

    # Returns whether running 'gem install' as the current user requires sudo.
    def self.gem_install_requires_sudo?
      `#{gem_command} env` =~ /INSTALLATION DIRECTORY: (.+)/
      if install_dir = $1
        return !File.writable?(install_dir)
      else
        return nil
      end
    end
    memoize :gem_install_requires_sudo?

    # Returns the absolute path to the Rake executable that
    # belongs to the current Ruby interpreter. Returns nil if it
    # doesn't exist.
    #
    # The return value may not be the actual correct invocation
    # for Rake. Use `rake_command` for that.
    def self.rake
      return locate_ruby_tool('rake')
    end
    memoize :rake

    # Returns the correct command string for invoking the Rake executable
    # that belongs to the current Ruby interpreter. Returns nil if Rake is
    # not found.
    def self.rake_command
      ruby_tool_command('rake')
    end
    memoize :rake_command

    # Returns the absolute path to the RSpec runner program that
    # belongs to the current Ruby interpreter. Returns nil if it
    # doesn't exist.
    def self.rspec
      return locate_ruby_tool('rspec')
    end
    memoize :rspec

    # Returns whether the current Ruby interpreter is managed by RVM.
    def self.in_rvm?
      bindir = rb_config['bindir']
      return bindir.include?('/.rvm/') || bindir.include?('/rvm/')
    end

    # If the current Ruby interpreter is managed by RVM, returns all
    # directories in which RVM places its working files. This is usually
    # ~/.rvm or /usr/local/rvm, but in mixed-mode installations there
    # can be multiple such paths.
    #
    # Otherwise returns nil.
    def self.rvm_paths
      if in_rvm?
        result = []
        [ENV['rvm_path'], "#{PhusionPassenger.home_dir}/.rvm", "/usr/local/rvm"].each do |path|
          next if path.nil?
          rubies_path = File.join(path, 'rubies')
          wrappers_path = File.join(path, 'wrappers')
          gems_path = File.join(path, 'gems')
          if File.directory?(path) && (File.directory?(rubies_path) ||
             File.directory?(wrappers_path) || File.directory?(gems_path))
            result << path
          end
        end
        if result.empty?
          # Failure to locate the RVM path is probably caused by the
          # user customizing $rvm_path. Older RVM versions don't
          # export $rvm_path, making us unable to detect its value.
          STDERR.puts "Unable to locate the RVM path. Your RVM installation " +
            "is probably too old. Please update it with " +
            "'rvm get head && rvm reload && rvm repair all'."
          exit 1
        else
          return result
        end
      else
        return nil
      end
    end
    memoize :rvm_paths

    # If the current Ruby interpreter is managed by RVM, returns the
    # RVM name which identifies the current Ruby interpreter plus the
    # currently active gemset, e.g. something like this:
    # "ruby-1.9.2-p0@mygemset"
    #
    # Returns nil otherwise.
    def self.rvm_ruby_string
      if in_rvm?
        # Getting the RVM name of the Ruby interpreter ("ruby-1.9.2")
        # isn't so hard, we can extract it from the #ruby_executable
        # string. Getting the gemset name is a bit harder, so let's
        # try various strategies...

        # $GEM_HOME usually contains the gem set name.
        # It may be something like:
        #   /Users/hongli/.rvm/gems/ruby-1.9.3-p392
        # But also:
        #   /home/bitnami/.rvm/gems/ruby-1.9.3-p385-perf@njist325/ruby/1.9.1
        #
        # Caveat when we're executed through 'bundle exec':
        # if `bundle install` was run with `--path=`, then `bundle exec`
        # will modify $GEM_HOME to the --path directory. That's
        # why we need to parse the version of $GEM_HOME *before*
        # `bundle exec` had modified it.
        [GEM_HOME, BUNDLER_ORIG_GEM_HOME].each do |gem_home|
          if gem_home && gem_home =~ %r{rvm/gems/(.+)}
            return $1.sub(/\/.*/, '')
          end
        end

        # User might have explicitly set GEM_HOME to a custom directory,
        # or might have nuked $GEM_HOME. Extract info from $GEM_PATH.
        [GEM_PATH, BUNDLER_ORIG_GEM_PATH].each do |gem_path|
          if gem_path
            gem_path.split(':').each do |gem_path_part|
              if gem_path_part =~ %r{rvm/gems/(.+)}
                return $1.sub(/\/.*/, '')
              end
            end
          end
        end

        # That failed too. Try extracting info from from $LOAD_PATH.
        matching_path = $LOAD_PATH.find_all do |item|
          item.include?("rvm/gems/")
        end
        if matching_path && !matching_path.empty?
          subpath = matching_path.to_s.gsub(/^.*rvm\/gems\//, '')
          result = subpath.split('/').first
          return result if result
        end

        # On Ruby 1.9, $LOAD_PATH does not contain any gem paths until
        # at least one gem has been required so the above can fail.
        # We're out of options now, we can't detect the gem set.
        # Raise an exception so that the user knows what's going on
        # instead of having things fail in obscure ways later.
        STDERR.puts "Unable to autodetect the currently active RVM gem " +
          "set name. This could happen if you ran this program using 'sudo' " +
          "instead of 'rvmsudo'. When using RVM, you're always supposed to " +
          "use 'rvmsudo' instead of 'sudo!'.\n\n" +
          "Please try rerunning this program using 'rvmsudo'. If that " +
          "doesn't help, please contact this program's author for support."
        exit 1
      end
      return nil
    end
    memoize :rvm_ruby_string

    # Returns the RVM installation mode:
    # :single - RVM is installed in single-user mode.
    # :multi  - RVM is installed in multi-user mode.
    # :mixed  - RVM is in a mixed-mode installation.
    # nil     - The current Ruby interpreter is not using RVM.
    def self.rvm_installation_mode
      if in_rvm?
        if ENV['rvm_path'] =~ /\.rvm/
          return :single
        else
          if GEM_HOME =~ /\.rvm/
            return :mixed
          else
            return :multi
          end
        end
      else
        return nil
      end
    end

    # Returns either 'sudo' or 'rvmsudo' depending on whether the current
    # Ruby interpreter is managed by RVM.
    def self.ruby_sudo_command
      if in_rvm?
        return "rvmsudo"
      else
        return "sudo"
      end
    end

    # Returns a `sudo` or `rvmsudo` command that spawns a shell, depending
    # on whether the current Ruby interpreter is managed by RVM.
    def self.ruby_sudo_shell_command(args = nil)
      if in_rvm?
        shell = ENV['SHELL'].to_s
        if shell.empty?
          begin
            user = Etc.getpwuid(0)
          rescue ArgumentError
            user = nil
          end
          shell = user.shell if user
          shell = "bash" if !shell || shell.empty?
        end
        result = "rvmsudo "
        result << "#{args} " if args
        result << shell
        return result
      else
        return "sudo -s #{args}".strip
      end
    end

    # Locates a Ruby tool, e.g. 'gem', 'rake', 'bundle', etc. Instead of
    # naively looking in $PATH, this function uses a variety of search heuristics
    # to find the tool that's really associated with the current Ruby interpreter.
    # It should never locate a tool that's actually associated with a different
    # Ruby interpreter.
    # Returns nil when nothing's found.
    #
    # This method only returns the path to the tool script. Running this script
    # directly does not necessarily mean that it's run under the correct Ruby
    # interpreter. You may have to prepend it with the Ruby command. Use
    # `ruby_tool_command` if you want to get a command that you can execute.
    def self.locate_ruby_tool(name)
      result = locate_ruby_tool_by_basename(name)
      if !result
        exeext = rb_config['EXEEXT']
        exeext = nil if exeext.empty?
        if exeext
          result = locate_ruby_tool_by_basename("#{name}#{exeext}")
        end
        if !result
          result = locate_ruby_tool_by_basename(transform_according_to_ruby_exec_format(name))
        end
        if !result && exeext
          result = locate_ruby_tool_by_basename(transform_according_to_ruby_exec_format(name) + exeext)
        end
      end
      return result
    end

    # Locates a Ruby tool command, e.g. 'gem', 'rake', 'bundle', etc. Instead of
    # naively looking in $PATH, this function uses a variety of search heuristics
    # to find the command that's really associated with the current Ruby interpreter.
    # It should never locate a command that's actually associated with a different
    # Ruby interpreter.
    # Returns nil when nothing's found.
    #
    # Unlike `locate_ruby_tool`, which only returns the path to the tool script,
    # this method returns a full command that you can execute. The returned command
    # guarantees that the tool is run under the correct Ruby interpreter.
    def self.ruby_tool_command(name)
      path = locate_ruby_tool(name)
      if path
        if is_ruby_program?(path)
          "#{ruby_command} #{path}"
        else
          # The found tool is a wrapper script, e.g. in RVM's ~/.rvm/wrappers.
          # In this case, don't include the Ruby command in the result.
          path
        end
      else
        nil
      end
    end

  private
    def self.locate_ruby_tool_by_basename(name)
      if os_name_simple == "macosx" &&
         ruby_command =~ %r(\A/System/Library/Frameworks/Ruby.framework/Versions/.*?/usr/bin/ruby\Z)
        # On OS X we must look for Ruby binaries in /usr/bin.
        # RubyGems puts executables (e.g. 'rake') in there, not in
        # /System/Libraries/(...)/bin.
        filename = "/usr/bin/#{name}"
      else
        filename = File.dirname(ruby_command) + "/#{name}"
      end

      if !File.file?(filename) || !File.executable?(filename)
        # RubyGems might put binaries in a directory other
        # than Ruby's bindir. Debian packaged RubyGems and
        # DebGem packaged RubyGems are the prime examples.
        begin
          require 'rubygems' unless defined?(Gem)
          filename = Gem.bindir + "/#{name}"
        rescue LoadError
          filename = nil
        end
      end

      if !filename || !File.file?(filename) || !File.executable?(filename)
        # Looks like it's not in the RubyGems bindir. Search in $PATH, but
        # be very careful about this because whatever we find might belong
        # to a different Ruby interpreter than the current one.
        ENV['PATH'].split(':').each do |dir|
          filename = "#{dir}/#{name}"
          if File.file?(filename) && File.executable?(filename)
            shebang = File.open(filename, 'rb') do |f|
              f.readline.strip
            end
            if shebang == "#!#{ruby_command}"
              # Looks good.
              break
            end
          end

          # Not found. Try next path.
          filename = nil
        end
      end

      filename
    end
    private_class_method :locate_ruby_tool_by_basename

    def self.is_ruby_program?(filename)
      File.open(filename, 'rb') do |f|
        return f.readline =~ /ruby/
      end
    rescue EOFError
      return false
    end
    private_class_method :is_ruby_program?

    # Deduce Ruby's --program-prefix and --program-suffix from its install name
    # and transforms the given input name accordingly.
    #
    #   transform_according_to_ruby_exec_format("rake")    => "jrake", "rake1.8", etc
    def self.transform_according_to_ruby_exec_format(name)
      install_name = rb_config['RUBY_INSTALL_NAME']
      if install_name.include?('ruby')
        format = install_name.sub('ruby', '%s')
        return sprintf(format, name)
      else
        return name
      end
    end
    private_class_method :transform_according_to_ruby_exec_format
  end

end # module PhusionPassenger
