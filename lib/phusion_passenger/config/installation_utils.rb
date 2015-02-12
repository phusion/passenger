# encoding: utf-8
#
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2014 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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

require 'fileutils'
require 'pathname'
require 'etc'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'ruby_core_enhancements'
PhusionPassenger.require_passenger_lib 'console_text_template'
PhusionPassenger.require_passenger_lib 'platform_info/ruby'
PhusionPassenger.require_passenger_lib 'platform_info/depcheck'
PhusionPassenger.require_passenger_lib 'utils/ansi_colors'

module PhusionPassenger
  module Config

    module InstallationUtils
      extend self    # Make methods available as class methods.

      def self.included(klass)
        # When included into another class, make sure that Utils
        # methods are made private.
        public_instance_methods(false).each do |method_name|
          klass.send(:private, method_name)
        end
      end

      def find_or_create_writable_support_binaries_dir!
        if File.exist?(PhusionPassenger.support_binaries_dir)
          result = directory_writable?(PhusionPassenger.support_binaries_dir)
          if result == true  # return value can be a SystemCallError
            return PhusionPassenger.support_binaries_dir
          end

          if Process.euid == 0
            if result == false
              print_installation_error_header
              render_template 'installation_utils/support_binaries_dir_not_writable_despite_running_as_root',
                :dir => PhusionPassenger.support_binaries_dir,
                :myself => myself
            else
              render_template 'installation_utils/unexpected_filesystem_problem',
                :dir => PhusionPassenger.support_binaries_dir,
                :exception => result
            end
            abort
          else
            return find_or_create_writable_user_support_binaries_dir!
          end
        else
          if Process.euid == 0
            mkdir_p_preserve_parent_owner(PhusionPassenger.support_binaries_dir)
            return PhusionPassenger.support_binaries_dir
          else
            return find_or_create_writable_user_support_binaries_dir!
          end
        end
      end

      def check_for_download_tool!
        PlatformInfo::Depcheck.load('depcheck_specs/utilities')
        result = PlatformInfo::Depcheck.find('download-tool').check
        # Don't output anything if there is a download tool.
        # We want to be as quiet as possible.
        return if result && result[:found]

        colors = @colors || Utils::AnsiColors.new
        puts colors.ansi_colorize("<banner>Checking for basic prerequities...</banner>")
        puts

        runner = PlatformInfo::Depcheck::ConsoleRunner.new
        runner.add('download-tool')

        result = runner.check_all
        puts
        if !result
          puts "---------------------------------------"
          puts
          render_template 'installation_utils/download_tool_missing',
            :runner => runner
          abort
        end
      end

      # Override this method to print a different header
      def print_installation_error_header
        if @colors
          red = @colors.red
          reset = @colors.reset
        else
          red = nil
          reset = nil
        end
        @logger.warn "------------------------------------------" if @logger
        puts "#{red}Cannot proceed with installation#{reset}"
        puts
      end

      def rake
        return "env NOEXEC_DISABLE=1 #{PlatformInfo.rake_command}"
      end

      def run_rake_task!(target)
        total_lines = `#{rake} #{target} --dry-run STDERR_TO_STDOUT=1`.split("\n").size - 1
        backlog = ""

        command = "#{rake} #{target} --trace STDERR_TO_STDOUT=1"
        IO.popen(command, "rb") do |io|
          progress = 1
          while !io.eof?
            line = io.readline
            yield(progress, total_lines)
            if line =~ /^\*\* /
              backlog.replace("")
              progress += 1
            else
              backlog << line
            end
          end
        end
        if $?.exitstatus != 0
          stderr = @stderr || STDERR
          stderr.puts
          stderr.puts "*** ERROR: the following command failed:"
          stderr.puts(backlog)
          exit 1
        end
      end

    private
      # We can't use File.writable() and friends here because they
      # don't always work right with ACLs. Instead of we use 'real'
      # checks.
      def directory_writable?(path)
        filename = "#{path}/.__test_#{object_id}__.txt"
        @logger.debug "Checking whether we can write to #{path}..." if @logger
        begin
          File.new(filename, "w").close
          @logger.debug "Yes" if @logger
          return true
        rescue Errno::EACCES
          @logger.debug "No" if @logger
          return false
        rescue SystemCallError => e
          @logger.warn "Unable to check whether we can write to #{path}: #{e}" if @logger
          return e
        ensure
          File.unlink(filename) rescue nil
        end
      end

      def find_or_create_writable_user_support_binaries_dir!
        if !File.exist?(PhusionPassenger.user_support_binaries_dir)
          create_user_support_binaries_dir!
        end
        result = directory_writable?(PhusionPassenger.user_support_binaries_dir)
        case result
        when true
          return PhusionPassenger.user_support_binaries_dir
        when false
          print_installation_error_header
          render_template 'installation_utils/user_support_binaries_dir_not_writable'
          abort
        else
          print_installation_error_header
          render_template 'installation_utils/unexpected_filesystem_problem',
            :dir => PhusionPassenger.support_binaries_dir,
            :exception => result
          abort
        end
      end

      def create_user_support_binaries_dir!
        dir = PhusionPassenger.user_support_binaries_dir
        begin
          mkdir_p_preserve_parent_owner(dir)
        rescue Errno::EACCES
          print_installation_error_header
          render_template 'installation_utils/cannot_create_user_support_binaries_dir',
            :dir => dir,
            :myself => myself
          abort
        rescue SystemCallError
          print_installation_error_header
          render_template 'installation_utils/unexpected_filesystem_problem',
            :dir => dir,
            :exception => result
          abort
        end
      end

      # When creating PhusionPassenger.support_binaries_dir, preserve the
      # parent directory's UID and GID. This way, running `passenger-config compile-agent`
      # with sudo privileged, even though Phusion Passenger isn't installed as root,
      # won't mess up permissions.
      def mkdir_p_preserve_parent_owner(path)
        Pathname.new(path).descend do |subpath|
          if !subpath.exist?
            stat = subpath.parent.stat
            Dir.mkdir(subpath.to_s)
            if Process.euid == 0
              File.chown(stat.uid, stat.gid, subpath.to_s)
            end
          end
        end
      end

      def myself
        return `whoami`.strip
      end

      def render_template(name, options = {})
        options.merge!(:colors => @colors || PhusionPassenger::Utils::AnsiColors.new)
        puts ConsoleTextTemplate.new({ :file => "config/#{name}" }, options).result
      end
    end

  end # module Config
end # module PhusionPassenger
