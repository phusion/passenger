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

    class ApiCallCommand < Command
      include PhusionPassenger::Config::Utils

      def self.create_default_options
        { :agent_name => "core_api" }
      end

      def run
        parse_options
        initialize_objects
        infer_socket_path_and_credentials
        invoke
      end

    private
      def self.create_option_parser(options)
        OptionParser.new do |opts|
          nl = "\n" + ' ' * 37
          opts.banner = "Usage: passenger-config api-call <METHOD> <PATH> [OPTIONS]\n"
          opts.separator ""
          opts.separator "  Makes an API call to one of the #{PROGRAM_NAME} agents. #{PROGRAM_NAME}"
          opts.separator "  listens on a local HTTP server for admin commands. Many `passenger-config`"
          opts.separator "  commands are just shortcuts for sending HTTP API calls to the"
          opts.separator "  #{PROGRAM_NAME} admin HTTP server. `passenger-config api-call` allows"
          opts.separator "  you to send API calls directly."
          opts.separator ""
          opts.separator "  METHOD is an HTTP verb, like 'GET', 'POST', 'PUT' or 'DELETE'."
          opts.separator "  PATH is the admin URI. You can pass POST data with '-d'."
          opts.separator ""
          opts.separator "  Example 1: passenger-config api-call GET /server.json"
          opts.separator "  Sends the 'GET /server.json' command to the Passenger core process."
          opts.separator ""
          opts.separator "  Example 2: passenger-config api-call PUT /config.json \\"
          opts.separator "             -d '{\"log_level\", 7}'"
          opts.separator "  Sends the 'PUT /config.json' command to the Passenger core process, with"
          opts.separator "  the given PUT data."
          opts.separator ""
          opts.separator "  Example 3: passenger-config api-call POST /shutdown.json -a watchdog_api"
          opts.separator "  Sends the 'POST /shutdown.json' command to the watchdog, with no POST data."
          opts.separator ""
          opts.separator "  Example 4: passenger-config api-call POST /shutdown.json \\"
          opts.separator "             -S /tmp/watchdog.sock"
          opts.separator "  Sends the 'POST /shutdown.json' command to the watchdog listening at the"
          opts.separator "  specific socket file /tmp/watchdog.sock. No POST data."
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
            "is sent to. Choices: watchdog_api,#{nl}" +
            "core_api.#{nl}" +
            "Default: core_api") do |val|
            options[:agent_name] = val
          end
          opts.on("-S", "--socket PATH", String, "Instead of inferring the socket path from#{nl}" +
            "the #{PROGRAM_NAME} instance directory#{nl}" +
            "and agent name, send the command to a#{nl}" +
            "specific Unix domain socket directly") do |val|
            options[:socket_path] = val
          end
          opts.on("--show-headers", "Show HTTP response headers") do
            options[:show_headers] = true
          end
          opts.on("--ignore-response-code", "Exit successfully even if a non-2xx#{nl}" +
            "response was returned") do
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

      def infer_socket_path_and_credentials
        if @options[:socket_path]
          @socket_path = @options[:socket_path]
        else
          select_passenger_instance
          @socket_path = "#{@instance.path}/agents.s/#{@options[:agent_name]}"
          begin
            @password = @instance.full_admin_password
          rescue Errno::EACCES
          end
        end
      end

      def invoke
        if @password
          @request.basic_auth("admin", @password)
        end
        @request["connection"] = "close"
        if @options[:data]
          @request.content_type = "application/json"
          @request.body = @options[:data]
        end

        sock = Net::BufferedIO.new(UNIXSocket.new(@socket_path))
        begin
          @request.exec(sock, "1.1", @request.path)

          done = false
          while !done
            response = Net::HTTPResponse.read_new(sock)
            done = !response.kind_of?(Net::HTTPContinue)
          end

          response.reading_body(sock, @request.response_body_permitted?) do
            # Nothing
          end
        ensure
          sock.close
        end

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
