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
PhusionPassenger.require_passenger_lib 'debug_logging'
PhusionPassenger.require_passenger_lib 'message_channel'
PhusionPassenger.require_passenger_lib 'utils'
PhusionPassenger.require_passenger_lib 'utils/native_support_utils'
PhusionPassenger.require_passenger_lib 'utils/unseekable_socket'

module PhusionPassenger
  class RequestHandler

    # This class encapsulates the logic of a single RequestHandler thread.
    class ThreadHandler
      include DebugLogging
      include Utils

      class Interrupted < StandardError
      end

      REQUEST_METHOD = 'REQUEST_METHOD'.freeze
      GET            = 'GET'.freeze
      PING           = 'PING'.freeze
      OOBW           = 'OOBW'.freeze
      PASSENGER_CONNECT_PASSWORD  = 'PASSENGER_CONNECT_PASSWORD'.freeze
      CONTENT_LENGTH = 'CONTENT_LENGTH'.freeze
      HTTP_TRANSFER_ENCODING = 'HTTP_TRANSFER_ENCODING'.freeze

      MAX_HEADER_SIZE = 128 * 1024

      OBJECT_SPACE_SUPPORTS_LIVE_OBJECTS      = ObjectSpace.respond_to?(:live_objects)
      OBJECT_SPACE_SUPPORTS_ALLOCATED_OBJECTS = ObjectSpace.respond_to?(:allocated_objects)
      OBJECT_SPACE_SUPPORTS_COUNT_OBJECTS     = ObjectSpace.respond_to?(:count_objects)
      GC_SUPPORTS_TIME        = GC.respond_to?(:time)
      GC_SUPPORTS_CLEAR_STATS = GC.respond_to?(:clear_stats)

      attr_reader :thread
      attr_reader :stats_mutex
      attr_reader :interruptable
      attr_reader :iteration

      def initialize(request_handler, options = {})
        @request_handler   = request_handler
        @server_socket     = Utils.require_option(options, :server_socket)
        @socket_name       = Utils.require_option(options, :socket_name)
        @protocol          = Utils.require_option(options, :protocol)
        @app_group_name    = Utils.require_option(options, :app_group_name)
        Utils.install_options_as_ivars(self, options,
          :app,
          :connect_password,
          :keepalive_enabled
        )

        @stats_mutex   = Mutex.new
        @interruptable = false
        @iteration     = 0

        if @protocol == :session
          metaclass = class << self; self; end
          metaclass.class_eval do
            alias parse_request parse_session_request
          end
        elsif @protocol == :http
          metaclass = class << self; self; end
          metaclass.class_eval do
            alias parse_request parse_http_request
          end
        else
          raise ArgumentError, "Unknown protocol specified"
        end
      end

      def install
        @thread = Thread.current
        Thread.current[:passenger_thread_handler] = self
        PhusionPassenger.call_event(:starting_request_handler_thread)
      end

      def main_loop(finish_callback)
        socket_wrapper = Utils::UnseekableSocket.new
        channel        = MessageChannel.new
        buffer         = ''
        buffer.force_encoding('binary') if buffer.respond_to?(:force_encoding)

        begin
          finish_callback.call
          while true
            hijacked = accept_and_process_next_request(socket_wrapper, channel, buffer)
            socket_wrapper = Utils::UnseekableSocket.new if hijacked
          end
        rescue Interrupted
          # Do nothing.
        end
        debug("Thread handler main loop exited normally")
      ensure
        @stats_mutex.synchronize { @interruptable = true }
      end

    private
      # Returns true if the socket has been hijacked, false otherwise.
      def accept_and_process_next_request(socket_wrapper, channel, buffer)
        @stats_mutex.synchronize do
          @interruptable = true
        end
        if @last_connection
          connection = @last_connection
          channel.io = connection
          @last_connection = nil
          headers = parse_request(connection, channel, buffer)
        else
          connection = socket_wrapper.wrap(@server_socket.accept)
        end
        @stats_mutex.synchronize do
          @interruptable = false
          @iteration    += 1
        end
        trace(3, "Accepted new request on socket #{@socket_name}")
        if !headers
          # New socket accepted, instead of keeping-alive an old one
          channel.io = connection
          headers = parse_request(connection, channel, buffer)
        end
        if headers
          prepare_request(connection, headers)
          begin
            if headers[REQUEST_METHOD] == GET
              process_request(headers, connection, socket_wrapper, @protocol == :http)
            elsif headers[REQUEST_METHOD] == PING
              process_ping(headers, connection)
              false
            elsif headers[REQUEST_METHOD] == OOBW
              process_oobw(headers, connection)
              false
            else
              process_request(headers, connection, socket_wrapper, @protocol == :http)
            end
          rescue Exception
            has_error = true
            raise
          ensure
            if headers[RACK_HIJACK_IO]
              socket_wrapper = nil
              connection = nil
              channel = nil
            end
            finalize_request(connection, headers, has_error)
            trace(3, "Request done.")
          end
        else
          trace(2, "No headers parsed; disconnecting client.")
          false
        end
      rescue Interrupted
        raise
      rescue => e
        if socket_wrapper && socket_wrapper.source_of_exception?(e)
          # EPIPE is harmless, it just means that the client closed the connection.
          if !e.is_a?(Errno::EPIPE)
            print_exception("Passenger RequestHandler's client socket", e)
          end
        else
          # should_reraise_error? returns true except in unit tests,
          # so we normally stop the request handler upon encountering
          # non-EPIPE errors. We do this because process_request is already
          # supposed to catch application-level exceptions. If an
          # exception happened outside of process_request, then it's
          # probably serious enough to warrant stopping the request handler.
          raise e if should_reraise_error?(e)
        end
        # Here we do not know whether the connection was hijacked, but
        # to be on the safe side (and have a new socket_wrapper created)
        # let's say that it is.
        true
      ensure
        # Close connection if keep-alive not possible
        if connection && !connection.closed? && !@last_connection
          # The 'close_write' here prevents forked child
          # processes from unintentionally keeping the
          # connection open.
          begin
            connection.close_write
          rescue SystemCallError, IOError
          end
          begin
            connection.close
          rescue SystemCallError
          end
        end
      end

      def parse_session_request(connection, channel, buffer)
        headers_data = channel.read_scalar(buffer, MAX_HEADER_SIZE)
        if headers_data.nil?
          return
        end
        headers = Utils::NativeSupportUtils.split_by_null_into_hash(headers_data)
        if @connect_password && headers[PASSENGER_CONNECT_PASSWORD] != @connect_password
          warn "*** Passenger RequestHandler warning: " <<
            "someone tried to connect with an invalid connect password."
          return
        else
          return headers
        end
      rescue SecurityError => e
        warn("*** Passenger RequestHandler warning: " <<
          "HTTP header size exceeded maximum.")
        return
      end

      # Like parse_session_request, but parses an HTTP request. This is a very minimalistic
      # HTTP parser and is not intended to be complete, fast or secure, since the HTTP server
      # socket is intended to be used for debugging purposes only.
      def parse_http_request(connection, channel, buffer)
        headers = {}

        data = ""
        while data !~ /\r\n\r\n/ && data.size < MAX_HEADER_SIZE
          data << connection.readpartial(16 * 1024)
        end
        if data.size >= MAX_HEADER_SIZE
          warn("*** Passenger RequestHandler warning: " <<
            "HTTP header size exceeded maximum.")
          return
        end

        data.gsub!(/\r\n\r\n.*/, '')
        data.split("\r\n").each_with_index do |line, i|
          if i == 0
            # GET / HTTP/1.1
            line =~ /^([A-Za-z]+) (.+?) (HTTP\/\d\.\d)$/
            request_method = $1
            request_uri    = $2
            protocol       = $3
            if request_method.nil?
              warn("*** Passenger RequestHandler warning: " <<
                "Invalid HTTP request.")
              return
            end
            path_info, query_string    = request_uri.split("?", 2)
            headers[REQUEST_METHOD]    = request_method
            headers["REQUEST_URI"]     = request_uri
            headers["QUERY_STRING"]    = query_string || ""
            headers["SCRIPT_NAME"]     = ""
            headers["PATH_INFO"]       = path_info
            headers["SERVER_NAME"]     = "127.0.0.1"
            headers["SERVER_PORT"]     = connection.addr[1].to_s
            headers["SERVER_PROTOCOL"] = protocol
          else
            header, value = line.split(/\s*:\s*/, 2)
            header.upcase!            # "Foo-Bar" => "FOO-BAR"
            header.gsub!("-", "_")    #           => "FOO_BAR"
            if header == CONTENT_LENGTH || header == "CONTENT_TYPE"
              headers[header] = value
            else
              headers["HTTP_#{header}"] = value
            end
          end
        end

        if @connect_password && headers["HTTP_X_PASSENGER_CONNECT_PASSWORD"] != @connect_password
          warn "*** Passenger RequestHandler warning: " <<
            "someone tried to connect with an invalid connect password."
          return
        else
          return headers
        end
      rescue EOFError
        return
      end

      def process_ping(env, connection)
        connection.write("pong")
      end

      def process_oobw(env, connection)
        PhusionPassenger.call_event(:oob_work)
        connection.write("oobw done")
      end

    # def process_request(env, connection, socket_wrapper, full_http_response)
    #   raise NotImplementedError, "Override with your own implementation!"
    # end

      def prepare_request(connection, headers)
        transfer_encoding = headers[HTTP_TRANSFER_ENCODING]
        content_length = headers[CONTENT_LENGTH]
        @can_keepalive = @keepalive_enabled &&
          !transfer_encoding &&
          !content_length
        @keepalive_performed = false

        if !transfer_encoding && !content_length
          connection.simulate_eof!
        end

        #################
      end

      def finalize_request(connection, headers, has_error)
        if connection
          connection.stop_simulating_eof!
        end

        if !has_error && @keepalive_performed && connection
          trace(3, "Keep-aliving connection.")
          @last_connection = connection
        end

        #################
      end

      def should_reraise_error?(e)
        # Stubable by unit tests.
        return true
      end

      def should_swallow_app_error?(e, socket_wrapper)
        return socket_wrapper && socket_wrapper.source_of_exception?(e) && e.is_a?(Errno::EPIPE)
      end
    end

  end # class RequestHandler
end # module PhusionPassenger
