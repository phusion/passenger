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

require 'optparse'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'standalone/command'
PhusionPassenger.require_passenger_lib 'standalone/config_utils'
PhusionPassenger.require_passenger_lib 'standalone/control_utils'
PhusionPassenger.require_passenger_lib 'ruby_core_enhancements'

module PhusionPassenger
  module Standalone

    class StatusCommand < Command
      def run
        parse_options
        load_local_config_file
        load_env_config
        remerge_all_options

        find_pid_file
        create_controller
        begin
          running = @controller.running?
          pid = @controller.pid
        rescue SystemCallError, IOError
          running = false
        end
        if running
          puts "#{PROGRAM_NAME} Standalone is running on PID #{pid}, according to PID file #{@options[:pid_file]}"
        else
          puts "#{PROGRAM_NAME} Standalone is not running, according to PID file #{@options[:pid_file]}"
        end
      end

    private
      def self.create_option_parser(options)
        OptionParser.new do |opts|
          defaults = CONFIG_DEFAULTS
          nl = "\n" + ' ' * 37
          opts.banner = "Usage: passenger status [OPTIONS] [APP DIR]\n"
          opts.separator "Shows the status of a running #{PROGRAM_NAME} Standalone instance."
          opts.separator ""

          opts.separator "Options:"
          opts.on("-p", "--port NUMBER", Integer,
            "The port number of the #{PROGRAM_NAME}#{nl}" +
            "instance. Default: #{defaults[:port]}") do |value|
            options[:port] = value
          end
          opts.on("--pid-file FILE", String,
            "PID file of the running #{PROGRAM_NAME}#{nl}" +
            "Standalone instance") do |value|
            options[:pid_file] = value
          end
        end
      end

      def load_local_config_file
        @local_options = ConfigUtils.
          load_local_config_file_from_app_dir_param!(@argv)
      end

      def load_env_config
        @env_options = ConfigUtils.load_env_config!
      end

      def remerge_all_options
        @options = ConfigUtils.remerge_all_config(@global_options,
          @local_options, @env_options, @parsed_options)
      end

      def execution_root
        @argv[0] || Dir.logical_pwd
      end

      def find_pid_file
        ConfigUtils.find_pid_and_log_file(execution_root, @options)
        if !@options[:pid_file]
          if @options[:ignore_pid_not_found]
            exit
          else
            Standalone::ControlUtils.warn_pid_file_not_found(@options)
            exit 1
          end
        end
      end

      def create_controller
        Standalone::ControlUtils.require_daemon_controller
        @controller = DaemonController.new(
          :identifier    => "#{PROGRAM_NAME} Standalone engine",
          :start_command => "true", # Doesn't matter
          :ping_command  => "true", # Doesn't matter
          :pid_file      => @options[:pid_file],
          :log_file      => "/dev/null",
          :timeout       => 25
        )
      end
    end

  end
end
