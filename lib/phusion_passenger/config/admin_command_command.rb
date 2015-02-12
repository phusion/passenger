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

    class AdminCommandCommand < Command
      include PhusionPassenger::Config::Utils

      def self.create_default_options
        return { :socket => "server_admin" }
      end

      def run
        parse_options
        initialize_objects
        select_passenger_instance
        invoke
      end

    private
      def self.create_option_parser(options)
        OptionParser.new do |opts|
          nl = "\n" + ' ' * 37
          opts.banner = "Usage: passenger-config invoke-command <METHOD> <PATH> [OPTIONS]\n"
          opts.separator ""
          opts.separator "  Invoke an internal #{PROGRAM_NAME} admin command. #{PROGRAM_NAME} listens"
          opts.separator "  on a local HTTP server for admin commands. Other `passenger-config` commands"
          opts.separator "  are just shortcuts for sending specific HTTP requests to the"
          opts.separator "  #{PROGRAM_NAME} admin HTTP server. `passenger-config invoke-command` allows"
          opts.separator "  you to send requests directly."
          opts.separator ""
          opts.separator "  METHOD is an HTTP verb, like 'GET', 'POST', 'PUT' or 'DELETE'."
          opts.separator "  PATH is the admin URI. You can pass POST data with '-d'."
          opts.separator ""
          opts.separator "  Example: passenger-config admin-command GET /server.json"
          opts.separator "  Sends the 'GET /server.json' command to the HTTP server agent."
          opts.separator ""
          opts.separator "  Example: passenger-config admin-command PUT /config.json \\"
          opts.separator "           -d '{\"log_level\", 7}'"
          opts.separator "  Sends the 'PUT /config.json' command to the HTTP server agent, with the"
          opts.separator "  given PUT data."
          opts.separator ""
          opts.separator "  Example: passenger-config admin-command POST /shutdown.json -a watchdog"
          opts.separator "  Sends the 'POST /shutdown.json' command to the watchdog, with no POST data."
          opts.separator ""

          opts.separator "Options:"
          opts.on("-d", "--data DATA", String, "Specify HTTP request body data") do |value|
            options[:data] = value
          end
          opts.on("-i", "--stdin", "Read HTTP request body data from stdin") do
            options[:data_source] = :stdin
          end
          opts.on("-f", "--data-file PATH", String, "Read HTTP request body data from the given#{nl}" +
            "file") do |value|
            options[:data_source] = value
          end
          opts.on("-a", "--agent NAME", String, "The name of the socket to send the command#{nl}" +
            "to. This specifies which agent the request#{nl}" +
            "is sent to. Choices: watchdog,#{nl}" +
            "server_admin, logging_admin.#{nl}" +
            "Default: server_admin") do |val|
            options[:socket] = val
          end
          opts.on("--show-headers", "Show HTTP response headers") do
            options[:show_headers] = true
          end
          opts.on("--ignore-response-code", "Exit successfully even if a non-2xx response was returned") do
            options[:ignore_response_code] = true
          end
          opts.on("--instance NAME", String, "The #{PROGRAM_NAME} instance to select") do |value|
            options[:instance] = value
          end
          opts.on("-h", "--help", "Show this help") do
            options[:help] = true
          end
        end
      end

      def initialize_objects
        if @argv.size != 2
          abort "You've passed to few arguments. See --help for more information."
        end

        @method = @argv[0]
        @path   = @argv[1]

        case @method.upcase
        when "GET"
          @request = Net::HTTP::Get.new(@path)
        when "POST"
          @request = Net::HTTP::Post.new(@path)
        when "PUT"
          @request = Net::HTTP::Put.new(@path)
        when "DELETE"
          @request = Net::HTTP::Delete.new(@path)
        else
          abort "Unknown method #{@method.inspect}. Please specify either GET, POST, PUT or DELETE."
        end
        if @path !~ /\A\//
          abort "The path must start with a slash (/). See --help for more information."
        end

        if @options[:data] && @options[:data_source]
          abort "You cannot specify both --data and --stdin/--data-file. Please choose either one."
        end
        if @options[:data_source] == :stdin
          STDIN.binmode
          @options[:data] = STDIN.read
        elsif @options[:data_source]
          File.open(@options[:data_source], "rb") do |f|
            @options[:data] = f.read
          end
        end
      end

      def invoke
        password = obtain_full_admin_password(@instance)
        @request.basic_auth("admin", password)
        @request["connection"] = "close"
        if @options[:data]
          @request.content_type = "application/json"
          @request.body = @options[:data]
        end
        response = @instance.http_request("agents.s/#{@options[:socket]}", @request)
        if @options[:show_headers]
          print_headers(response)
        end
        puts response.body
        if !@options[:ignore_response_code] && response.code.to_i / 100 != 2
          abort
        end
      end

      def print_headers(response)
        puts "HTTP/1.1 #{response.message} #{response.code}"
        response.each_header do |name, val|
          puts "#{name}: #{val}"
        end
        puts
      end
    end

  end # module Config
end # module PhusionPassenger
