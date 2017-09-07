#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2014-2017 Phusion Holding B.V.
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
require 'net/http'
require 'socket'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'admin_tools/instance_registry'
PhusionPassenger.require_passenger_lib 'config/command'
PhusionPassenger.require_passenger_lib 'config/utils'
PhusionPassenger.require_passenger_lib 'utils/json'

module PhusionPassenger
  module Config

    class ReopenLogsCommand < Command
      include PhusionPassenger::Config::Utils

      def run
        parse_options
        select_passenger_instance
        perform_reopen_logs
      end

    private
      def self.create_option_parser(options)
        OptionParser.new do |opts|
          nl = "\n" + ' ' * 37
          opts.banner = "Usage: passenger-config reopen-logs [OPTIONS]\n"
          opts.separator ""
          opts.separator "  Instruct #{PROGRAM_NAME} agent processes to reopen their log files. This"
          opts.separator "  should be invoked after you've rotated log files. This command returns after"
          opts.separator "  the log files have been reopened."
          opts.separator ""

          opts.separator "Options:"
          opts.on("--ignore-logs-not-available", "Exit successfully if #{PROGRAM_NAME}#{nl}" +
            "was not configured with a log file") do
            options[:ignore_logs_not_available] = true
          end
          opts.on("--instance NAME", String, "The #{PROGRAM_NAME} instance to select") do |value|
            options[:instance] = value
          end
          opts.on("-h", "--help", "Show this help") do
            options[:help] = true
          end
        end
      end

      def perform_reopen_logs
        perform_reopen_logs_on("watchdog", "watchdog_api")
        perform_reinherit_logs_on("core", "core_api")
        if using_standalone_nginx_engine?
          perform_reopen_logs_on_nginx
        end
        puts "All done"
      end

      def perform_reopen_logs_on(name, socket_name)
        puts "Reopening logs for #{PROGRAM_NAME} #{name}"
        request = Net::HTTP::Post.new("/reopen_logs.json")
        try_performing_full_admin_basic_auth(request, @instance)
        request.content_type = "application/json"
        response = @instance.http_request("agents.s/#{socket_name}", request)
        if response.code.to_i == 401
          print_full_admin_command_permission_error
          abort
        elsif response["content-type"] == "application/json"
          if response.code.to_i / 100 != 2
            handle_error(name, response)
          end
        else
          STDERR.puts "*** An error occured while communicating with the #{PROGRAM_NAME} #{name} (code #{response.code}):"
          STDERR.puts response.body
          abort
        end
      end

      def perform_reinherit_logs_on(name, socket_name)
        puts "Reopening logs for #{PROGRAM_NAME} #{name} (through reinheritance)"
        request = Net::HTTP::Post.new("/reinherit_logs.json")
        try_performing_full_admin_basic_auth(request, @instance)
        request.content_type = "application/json"
        response = @instance.http_request("agents.s/#{socket_name}", request)
        if response.code.to_i == 401
          print_full_admin_command_permission_error
          abort
        elsif response["content-type"] == "application/json"
          if response.code.to_i / 100 != 2
            handle_error(name, response)
          end
        else
          STDERR.puts "*** An error occured while communicating with the #{PROGRAM_NAME} #{name} (code #{response.code}):"
          STDERR.puts response.body
          abort
        end
      end

      def using_standalone_nginx_engine?
        @instance.properties["integration_mode"] == "standalone" &&
          @instance.properties["standalone_engine"] == "nginx"
      end

      def perform_reopen_logs_on_nginx
        puts "Reopening logs for Nginx engine"
        Process.kill('USR1', @instance.web_server_control_process_pid)
      end

      def handle_error(name, response)
        json = PhusionPassenger::Utils::JSON.parse(response.body)
        if !should_ignore_error?(json)
          STDERR.puts "*** An error occured while communicating with the #{PROGRAM_NAME} #{name} (code #{response.code}):"
          STDERR.puts json['message']
          abort
        end
      end

      def should_ignore_error?(json)
        return @options[:ignore_logs_not_available] && json["code"] == "NO_LOG_FILE"
      end
    end

  end # module Config
end # module PhusionPassenger
