# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2015 Phusion
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

PhusionPassenger.require_passenger_lib 'utils/tee_input'

module PhusionPassenger
  module Rack

    module ThreadHandlerExtension
      # Constants which exist to relieve Ruby's garbage collector.
      RACK_VERSION       = "rack.version"        # :nodoc:
      RACK_VERSION_VALUE = [1, 2]                # :nodoc:
      RACK_INPUT         = "rack.input"          # :nodoc:
      RACK_ERRORS        = "rack.errors"         # :nodoc:
      RACK_MULTITHREAD   = "rack.multithread"    # :nodoc:
      RACK_MULTIPROCESS  = "rack.multiprocess"   # :nodoc:
      RACK_RUN_ONCE      = "rack.run_once"       # :nodoc:
      RACK_URL_SCHEME    = "rack.url_scheme"     # :nodoc:
      RACK_HIJACK_P      = "rack.hijack?"        # :nodoc:
      RACK_HIJACK        = "rack.hijack"         # :nodoc:
      SCRIPT_NAME        = "SCRIPT_NAME"         # :nodoc:
      REQUEST_METHOD = "REQUEST_METHOD"          # :nodoc:
      TRANSFER_ENCODING_HEADER  = "Transfer-Encoding"   # :nodoc:
      CONTENT_LENGTH_HEADER     = "Content-Length"      # :nodoc:
      CONTENT_LENGTH_HEADER_AND_SEPARATOR      = "Content-Length: " # :nodoc
      TRANSFER_ENCODING_HEADER_AND_VALUE_CRLF2 = "Transfer-Encoding: chunked\r\n\r\n" # :nodoc:
      CONNECTION_CLOSE_CRLF     = "Connection: close\r\n"     # :nodoc:
      HEAD           = "HEAD"   # :nodoc:
      HTTPS          = "HTTPS"  # :nodoc:
      HTTPS_DOWNCASE = "https"  # :nodoc:
      HTTP           = "http"   # :nodoc:
      YES            = "yes"    # :nodoc:
      ON             = "on"     # :nodoc:
      ONE            = "1"      # :nodoc:
      CRLF           = "\r\n"   # :nodoc:
      NEWLINE        = "\n"     # :nodoc:
      STATUS         = "Status: "         # :nodoc:
      NAME_VALUE_SEPARATOR = ": "         # :nodoc:
      TERMINATION_CHUNK    = "0\r\n\r\n"  # :nodoc:

      def process_request(env, connection, socket_wrapper, full_http_response)
        rewindable_input = PhusionPassenger::Utils::TeeInput.new(connection, env)
        begin
          env[RACK_VERSION]      = RACK_VERSION_VALUE
          env[RACK_INPUT]        = rewindable_input
          env[RACK_ERRORS]       = STDERR
          env[RACK_MULTITHREAD]  = @request_handler.concurrency > 1
          env[RACK_MULTIPROCESS] = true
          env[RACK_RUN_ONCE]     = false
          if env[HTTPS] == YES || env[HTTPS] == ON || env[HTTPS] == ONE
            env[RACK_URL_SCHEME] = HTTPS_DOWNCASE
          else
            env[RACK_URL_SCHEME] = HTTP
          end
          env[RACK_HIJACK_P] = true
          env[RACK_HIJACK] = lambda do
            env[RACK_HIJACK_IO] ||= begin
              connection.stop_simulating_eof!
              connection
            end
          end

          begin
            status, headers, body = @app.call(env)
          rescue => e
            disable_keep_alive
            if should_reraise_app_error?(e, socket_wrapper)
              raise e
            elsif !should_swallow_app_error?(e, socket_wrapper)
              # It's a good idea to catch application exceptions here because
              # otherwise maliciously crafted responses can crash the app,
              # forcing it to be respawned, and thereby effectively DoSing it.
              print_exception("Rack application object", e)
              PhusionPassenger.log_request_exception(env, e)
            end
            return false
          end

          # Application requested a full socket hijack.
          return true if env[RACK_HIJACK_IO]

          begin
            process_body(env, connection, socket_wrapper, status.to_i, headers, body)
          rescue Exception => e
            disable_keep_alive
            raise
          ensure
            body.close if body && body.respond_to?(:close)
          end
        ensure
          rewindable_input.close
        end
      end

    private
      # The code here is ugly, but it's necessary for performance.
      def process_body(env, connection, socket_wrapper, status, headers, body)
        if hijack_callback = headers[RACK_HIJACK]
          # Application requested a partial socket hijack.
          body = nil
          headers_output = generate_headers_array(status, headers)
          headers_output << "Connection: close\r\n"
          headers_output << CRLF
          connection.writev(headers_output)
          connection.flush
          hijacked_socket = env[RACK_HIJACK].call
          hijack_callback.call(hijacked_socket)
          true
        elsif body.is_a?(Array)
          # The body may be an ActionController::StringCoercion::UglyBody
          # object instead of a real Array, even when #is_a? claims so.
          # Call #to_a just to be sure.
          body = body.to_a
          output_body = should_output_body?(status, env)
          headers_output = generate_headers_array(status, headers)
          perform_keep_alive(env, headers_output)
          if output_body && should_add_message_length_header?(status, headers)
            body_size = 0
            body.each { |part| body_size += bytesize(part.to_s) }
            headers_output << CONTENT_LENGTH_HEADER_AND_SEPARATOR
            headers_output << body_size.to_s
            headers_output << CRLF
          end
          headers_output << CRLF
          if output_body
            connection.writev2(headers_output, body)
          else
            connection.writev(headers_output)
          end
          false
        elsif body.is_a?(String)
          output_body = should_output_body?(status, env)
          headers_output = generate_headers_array(status, headers)
          perform_keep_alive(env, headers_output)
          if output_body && should_add_message_length_header?(status, headers)
            headers_output << CONTENT_LENGTH_HEADER_AND_SEPARATOR
            headers_output << bytesize(body).to_s
            headers_output << CRLF
          end
          headers_output << CRLF
          if output_body
            headers_output << body
          end
          connection.writev(headers_output)
          false
        else
          output_body = should_output_body?(status, env)
          headers_output = generate_headers_array(status, headers)
          perform_keep_alive(env, headers_output)
          chunk = output_body && should_add_message_length_header?(status, headers)
          if chunk
            headers_output << TRANSFER_ENCODING_HEADER_AND_VALUE_CRLF2
          else
            headers_output << CRLF
          end
          connection.writev(headers_output)
          if output_body && body
            begin
              if chunk
                body.each do |part|
                  size = bytesize(part)
                  if size != 0
                    connection.writev(chunk_data(part, size))
                  end
                end
                connection.write(TERMINATION_CHUNK)
              else
                body.each do |s|
                  connection.write(s)
                end
              end
            rescue => e
              disable_keep_alive
              if should_reraise_app_error?(e, socket_wrapper)
                raise e
              elsif !should_swallow_app_error?(e, socket_wrapper)
                # Body objects can raise exceptions in #each.
                print_exception("Rack body object #each method", e)
              end
              false
            end
          end
          false
        end
      end

      def generate_headers_array(status, headers)
        status_str = status.to_s
        result = ["HTTP/1.1 #{status_str} Whatever\r\n"]
        headers.each do |key, values|
          if values.is_a?(String)
            values = values.split(NEWLINE)
          elsif key == RACK_HIJACK
            # We do not check for this key name in every loop
            # iteration as an optimization.
            next
          else
            values = values.to_s.split(NEWLINE)
          end
          values.each do |value|
            result << key
            result << NAME_VALUE_SEPARATOR
            result << value
            result << CRLF
          end
        end
        return result
      end

      def perform_keep_alive(env, headers)
        if @can_keepalive
          @keepalive_performed = true
        else
          headers << CONNECTION_CLOSE_CRLF
        end
      end

      def disable_keep_alive
        @keepalive_performed = false
      end

      def should_output_body?(status, env)
        return (status < 100 ||
          (status >= 200 && status != 204 && status != 205 && status != 304)) &&
          env[REQUEST_METHOD] != HEAD
      end

      def should_add_message_length_header?(status, headers)
        return !headers.has_key?(TRANSFER_ENCODING_HEADER) &&
          !headers.has_key?(CONTENT_LENGTH_HEADER)
      end

      def chunk_data(data, size)
        [size.to_s(16), CRLF, data, CRLF]
      end

      if "".respond_to?(:bytesize)
        def bytesize(str)
          str.bytesize
        end
      else
        def bytesize(str)
          str.size
        end
      end
    end

  end # module Rack
end # module PhusionPassenger
