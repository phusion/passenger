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

PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'console_text_template'
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'
PhusionPassenger.require_passenger_lib 'utils/ansi_colors'
PhusionPassenger.require_passenger_lib 'utils/download'
require 'fileutils'
require 'logger'
require 'etc'

# IMPORTANT: do not directly or indirectly require native_support; we can't compile
# it yet until we have a compiler, and installers usually check whether a compiler
# is installed.

module PhusionPassenger

  # Abstract base class for text mode installers. Used by
  # passenger-install-apache2-module and passenger-install-nginx-module.
  #
  # Subclasses must at least implement the #run_steps method which handles
  # the installation itself.
  #
  # Usage:
  #
  #   installer = ConcereteInstallerClass.new(options...)
  #   installer.run
  class AbstractInstaller
    PASSENGER_WEBSITE = "https://www.phusionpassenger.com"
    PASSENGER_LIBRARY_URL = "https://www.phusionpassenger.com/library/"
    PHUSION_WEBSITE = "www.phusion.nl"

    # Create an AbstractInstaller. All options will be stored as instance
    # variables, for example:
    #
    #   installer = AbstractInstaller.new(:foo => "bar")
    #   installer.instance_variable_get(:"@foo")   # => "bar"
    def initialize(options = {})
      @stdout = STDOUT
      @stderr = STDERR
      @auto   = !STDIN.tty?
      @colors = Utils::AnsiColors.new(options[:colorize] || :auto)
      options.each_pair do |key, value|
        instance_variable_set(:"@#{key}", value)
      end
    end

    # Start the installation by calling the #install! method.
    def run
      before_install
      run_steps
      return true
    rescue Abort
      puts
      return false
    rescue SignalException, SystemExit
      raise
    rescue PlatformInfo::RuntimeError => e
      new_screen
      puts "<red>An error occurred</red>"
      puts
      puts e.message
      exit 1
    rescue Exception => e
      show_support_options_for_installer_bug(e)
      exit 2
    ensure
      after_install
    end

  protected
    class Abort < StandardError
    end

    class CommandError < Abort
    end


    def interactive?
      return !@auto
    end

    def non_interactive?
      return !interactive?
    end


    def before_install
      if STDOUT.respond_to?(:set_encoding)
        STDOUT.set_encoding("UTF-8")
      end
      STDOUT.write(@colors.default_terminal_color)
      STDOUT.flush
    end

    def after_install
      STDOUT.write(@colors.reset)
      STDOUT.flush
    end

    def install_doc_url
      "https://www.phusionpassenger.com/library/install/"
    end

    def troubleshooting_doc_url
      "https://www.phusionpassenger.com/library/admin/troubleshooting/"
    end

    def dependencies
      return [[], []]
    end

    def check_dependencies(show_new_screen = true)
      new_screen if show_new_screen
      puts "<banner>Checking for required software...</banner>"
      puts

      PhusionPassenger.require_passenger_lib 'platform_info/depcheck'
      specs, ids = dependencies
      runner = PlatformInfo::Depcheck::ConsoleRunner.new(@colors)

      specs.each do |spec|
        PlatformInfo::Depcheck.load(spec)
      end
      ids.each do |id|
        runner.add(id)
      end

      if runner.check_all
        return true
      else
        puts
        puts "<red>Some required software is not installed.</red>"
        puts "But don't worry, this installer will tell you how to install them.\n"
        puts "<b>Press Enter to continue, or Ctrl-C to abort.</b>"
        if PhusionPassenger.originally_packaged?
          wait
        else
          wait(10)
        end

        line
        puts
        puts "<banner>Installation instructions for required software</banner>"
        puts
        runner.missing_dependencies.each do |dep|
          puts " * To install <yellow>#{dep.name}</yellow>:"
          puts "   #{dep.install_instructions}"
          puts
        end
        puts "If the aforementioned instructions didn't solve your problem, then please take"
        puts "a look at our documentation for troubleshooting tips:"
        puts
        puts "  <yellow>#{install_doc_url}</yellow>"
        puts "  <yellow>#{troubleshooting_doc_url}</yellow>"
        return false
      end
    end

    def check_whether_os_is_broken
      # No known broken OSes at the moment.
    end

    def check_gem_install_permission_problems
      return true if PhusionPassenger.custom_packaged?
      begin
        require 'rubygems'
      rescue LoadError
        return true
      end

      if Process.uid != 0 &&
         PhusionPassenger.build_system_dir =~ /^#{Regexp.escape home_dir}\// &&
         PhusionPassenger.build_system_dir =~ /^#{Regexp.escape Gem.dir}\// &&
         File.stat(PhusionPassenger.build_system_dir).uid == 0
        new_screen
        render_template 'installer_common/gem_install_permission_problems'
        return false
      else
        return true
      end
    end

    def check_directory_accessible_by_web_server
      return true if PhusionPassenger.custom_packaged?
      inaccessible_directories = []
      list_parent_directories(PhusionPassenger.build_system_dir).each do |path|
        if !world_executable?(path)
          inaccessible_directories << path
        end
      end
      if !inaccessible_directories.empty?
        new_screen
        render_template 'installer_common/world_inaccessible_directories',
          :directories => inaccessible_directories
        wait
      end
    end

    def check_whether_system_has_enough_ram(required = 1024)
      begin
        meminfo = File.read("/proc/meminfo")
        if meminfo =~ /^MemTotal: *(\d+) kB$/
          ram_mb = $1.to_i / 1024
          if meminfo =~ /^SwapTotal: *(\d+) kB$/
            swap_mb = $1.to_i / 1024
          else
            swap_mb = 0
          end
        end
      rescue Errno::ENOENT, Errno::EACCES
        # Don't do anything on systems without memory information.
        ram_mb = nil
        swap_mb = nil
      end
      if ram_mb && swap_mb && ram_mb + swap_mb < required
        new_screen
        render_template 'installer_common/low_amount_of_memory_warning',
          :required => required,
          :current => ram_mb + swap_mb,
          :ram => ram_mb,
          :swap => swap_mb,
          :install_doc_url => install_doc_url
        wait
      end
    end

    def show_support_options_for_installer_bug(e)
      # We do not use template rendering here. Since we've determined that there's
      # a bug, *anything* may be broken, so we use the safest codepath to ensure that
      # the user sees the proper messages.
      begin
        line
        @stderr.puts "*** EXCEPTION: #{e} (#{e.class})\n    " +
          e.backtrace.join("\n    ")
        new_screen
        puts '<red>Oops, something went wrong :-(</red>'
        puts
        puts "We're sorry, but it looks like this installer ran into an unexpected problem.\n" +
          "Please visit the following website for support. We'll do our best to help you.\n\n" +
          "  <b>#{SUPPORT_URL}</b>\n\n" +
          "When submitting a support inquiry, please copy and paste the entire installer\n" +
          "output."
      rescue Exception => e2
        # Raise original exception so that it doesn't get lost.
        raise e
      end
    end


    def use_stderr
      old_stdout = @stdout
      begin
        @stdout = @stderr
        yield
      ensure
        @stdout = old_stdout
      end
    end

    def print(text)
      @stdout.write(@colors.ansi_colorize(text))
      @stdout.flush
    end

    def puts(text = nil)
      if text
        @stdout.puts(@colors.ansi_colorize(text.to_s))
      else
        @stdout.puts
      end
      @stdout.flush
    end

    def puts_error(text)
      @stderr.puts(@colors.ansi_colorize("<red>#{text}</red>"))
      @stderr.flush
    end

    def render_template(name, options = {})
      options.merge!(:colors => @colors)
      puts ConsoleTextTemplate.new({ :file => name }, options).result
    end

    def new_screen
      puts
      line
      puts
    end

    def line
      puts "--------------------------------------------"
    end

    def prompt(message, default_value = nil)
      done = false
      while !done
        print "#{message}: "

        if non_interactive? && default_value
          puts default_value
          return default_value
        end

        begin
          result = STDIN.readline
        rescue EOFError
          exit 2
        end
        result.strip!
        if result.empty?
          if default_value
            result = default_value
            done = true
          else
            done = !block_given? || yield(result)
          end
        else
          done = !block_given? || yield(result)
        end
      end
      return result
    rescue Interrupt
      raise Abort
    end

    def prompt_confirmation(message)
      result = prompt("#{message} [y/n]") do |value|
        if value.downcase == 'y' || value.downcase == 'n'
          true
        else
          puts_error "Invalid input '#{value}'; please enter either 'y' or 'n'."
          false
        end
      end
      return result.downcase == 'y'
    rescue Interrupt
      raise Abort
    end

    def prompt_confirmation_with_default(message, default)
      if default
        default_str = "[Y/n]"
      else
        default_str = "[y/N]"
      end
      result = prompt("#{message} #{default_str}") do |value|
        if value.downcase == 'y' || value.downcase == 'n'
          true
        elsif value.empty?
          true
        else
          puts_error "Invalid input '#{value}'; please enter either 'y' or 'n'."
          false
        end
      end
      if result.empty?
        return default
      else
        return result.downcase == 'y'
      end
    rescue Interrupt
      raise Abort
    end

    def wait(timeout = nil)
      if interactive?
        if timeout
          require 'timeout' unless defined?(Timeout)
          begin
            Timeout.timeout(timeout) do
              STDIN.readline
            end
          rescue Timeout::Error
            # Do nothing.
          end
        else
          STDIN.readline
        end
      end
    rescue Interrupt
      raise Abort
    end

    def home_dir
      return PhusionPassenger.home_dir
    end


    def sh(*args)
      puts "# #{args.join(' ')}"
      result = system(*args)
      if result
        return true
      elsif $?.signaled? && $?.termsig == Signal.list["INT"]
        raise Interrupt
      else
        return false
      end
    end

    def sh!(*args)
      if !sh(*args)
        puts_error "*** Command failed: #{args.join(' ')}"
        raise CommandError
      end
    end

    def rake(*args)
      PhusionPassenger.require_passenger_lib 'platform_info/ruby'
      if !PlatformInfo.rake_command
        puts_error 'Cannot find Rake.'
        raise Abort
      end
      sh("#{PlatformInfo.rake_command} #{args.join(' ')}")
    end

    def rake!(*args)
      PhusionPassenger.require_passenger_lib 'platform_info/ruby'
      if !PlatformInfo.rake_command
        puts_error 'Cannot find Rake.'
        raise Abort
      end
      sh!("#{PlatformInfo.rake_command} #{args.join(' ')}")
    end

    def download(url, output, options = {})
      options[:logger] ||= begin
        logger = Logger.new(STDOUT)
        logger.level = Logger::WARN
        logger.formatter = proc { |severity, datetime, progname, msg| "*** #{msg}\n" }
        logger
      end
      return PhusionPassenger::Utils::Download.download(url, output, options)
    end

    def list_parent_directories(dir)
      dirs = []
      components = File.expand_path(dir).split(File::SEPARATOR)
      components.shift # Remove leading /
      components.size.times do |i|
        dirs << File::SEPARATOR + components[0 .. i].join(File::SEPARATOR)
      end
      return dirs.reverse
    end

    def world_executable?(dir)
      begin
        stat = File.stat(dir)
      rescue Errno::EACCESS
        return false
      end
      return stat.mode & 0000001 != 0
    end
  end

end # module PhusionPassenger
