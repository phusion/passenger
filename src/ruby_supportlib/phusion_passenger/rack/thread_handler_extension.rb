# encoding: binary
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
      HTTP_VERSION       = "HTTP_VERSION"        # :nodoc:
      HTTP_1_1           = "HTTP/1.1"            # :nodoc:
      SCRIPT_NAME        = "SCRIPT_NAME"         # :nodoc:
      REQUEST_METHOD = "REQUEST_METHOD"          # :nodoc:
      TRANSFER_ENCODING_HEADER  = "Transfer-Encoding"   # :nodoc:
      TRANSFER_ENCODING_HEADERS = ["Transfer-Encoding", "Transfer-encoding", "transfer-encoding"] # :nodoc:
      CONTENT_LENGTH_HEADER     = "Content-Length"      # :nodoc:
      CONTENT_LENGTH_HEADERS    = ["Content-Length", "Content-length", "content-length"] # :nodoc:
      X_SENDFILE_HEADER         = "X-Sendfile"          # :nodoc:
      X_ACCEL_REDIRECT_HEADER   = "X-Accel-Redirect"    # :nodoc:
      CONTENT_LENGTH_HEADER_AND_SEPARATOR      = "Content-Length: " # :nodoc
      TRANSFER_ENCODING_HEADER_AND_VALUE_CRLF  = "Transfer-Encoding: chunked\r\n" # :nodoc:
      CONNECTION_CLOSE_CRLF2    = "Connection: close\r\n\r\n"     # :nodoc:
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
          env[HTTP_VERSION] = HTTP_1_1

          # Rails somehow modifies env['REQUEST_METHOD'], so we perform the comparison
          # before the Rack application object is called.
          is_head_request = env[REQUEST_METHOD] == HEAD

          begin
            status, headers, body = @app.call(env)
          rescue => e
            if !should_swallow_app_error?(e, socket_wrapper)
              # It's a good idea to catch application exceptions here because
              # otherwise maliciously crafted responses can crash the app,
              # forcing it to be respawned, and thereby effectively DoSing it.
              print_exception("Rack application object", e)
            end
            return false
          end

          # Application requested a full socket hijack.
          if env[RACK_HIJACK_IO]
            # Since the app hijacked the socket, we don't know what state we're
            # in and we don't know whether we can recover from it, so we don't
            # catch any exception here.
            #
            # The Rack specification doesn't specify whether the body should
            # be closed when the socket is hijacked. As of February 2 2015,
            # Puma and Thin close the body, while Unicorn does not.
            # However, Rack::Lock and possibly many other middlewares count
            # on the body being closed, as described here:
            # https://github.com/ngauthier/tubesock/issues/10#issuecomment-72539461
            # So we have chosen to close the body.
            body.close if body && body.respond_to?(:close)
            return true
          end

          # Application requested a partial socket hijack.
          if hijack_callback = headers[RACK_HIJACK]
            # We don't catch exceptions here. EPIPE is already handled
            # by ThreadHandler's #accept_and_process_next_request.
            # On any other exception, we don't know what state we're
            # in and we don't know whether we can recover from it.
            begin
              headers_output = generate_headers_array(status, headers)
              headers_output << CONNECTION_CLOSE_CRLF2
              connection.writev(headers_output)
              connection.flush
              hijacked_socket = env[RACK_HIJACK].call
              hijack_callback.call(hijacked_socket)
              return true
            ensure
              body.close if body && body.respond_to?(:close)
            end
          end

          begin
            process_body(env, connection, socket_wrapper, status.to_i, is_head_request,
              headers, body)
          rescue => e
            if !should_swallow_app_error?(e, socket_wrapper)
              print_exception("Rack response body object", e)
            end
          ensure
            close_body(body, env, socket_wrapper)
          end
          false
        ensure
          rewindable_input.close
        end
      end

    private
      def process_body(env, connection, socket_wrapper, status, is_head_request, headers, body)
        if @ush_reporter
          ush_log_id = @ush_reporter.log_writing_rack_body_begin
        end

        # Fix up incompliant body objects. Ensure that the body object
        # can respond to #each.
        output_body = should_output_body?(status, is_head_request)
        if body.is_a?(String)
          body = [body]
        elsif body.nil?
          body = []
        elsif output_body && body.is_a?(Array)
          # The body may be an ActionController::StringCoercion::UglyBody
          # object instead of a real Array, even when #is_a? claims so.
          # Call #to_a just to be sure that connection.writev() can
          # accept the body object.
          body = body.to_a
        end

        # Generate preliminary headers and determine whether we need to output a body.
        headers_output = generate_headers_array(status, headers)


        # Determine how big the body is, determine whether we should try to keep-alive
        # the connection, and fix up the headers according to the situation.
        #
        # It may not be possible to determine the body's size (e.g. because it's streamed
        # through #each). In that case we'll want to output the body with a chunked transfer
        # encoding. But it matters whether the app has already chunked the body or not.
        #
        # Note that if the Rack response header contains "Transfer-Encoding: chunked",
        # then we assume that the Rack body is already in chunked form. This is the way
        # Rails streaming and Rack::Chunked::Body behave.
        # The only gem that doesn't behave this way is JRuby-Rack (see
        # https://blog.engineyard.com/2011/taking-stock-jruby-web-servers), but I'm not
        # aware of anybody using JRuby-Rack these days.
        #
        # We only try to keep-alive the connection if we are able to determine ahead of
        # time that the body we write out is guaranteed to match what the headers say.
        # Otherwise we disable keep-alive to prevent the app from being able to mess
        # up the keep-alive connection.
        if header = lookup_header(headers, CONTENT_LENGTH_HEADERS)
          # Easiest case: app has a Content-Length header. The headers
          # need no fixing.
          message_length_type = :content_length
          content_length = header.to_i
          if lookup_header(headers, TRANSFER_ENCODING_HEADERS)
            # Disallowed by the HTTP spec
            raise "Response object may not contain both Content-Length and Transfer-Encoding"
          end
          if output_body
            if !body.is_a?(Array) || headers.has_key?(X_SENDFILE_HEADER) ||
               headers.has_key?(X_ACCEL_REDIRECT_HEADER)
              # If X-Sendfile or X-Accel-Redirect is set, don't check the
              # body size. Passenger's Core Controller will ignore the
              # body anyway. See
              # ServerKit::HttpHeaderParser::processParseResult(const HttpParseResponse &)
              @can_keepalive = false
            else
              body_size = 0
              body.each { |part| body_size += bytesize(part) }
              if body_size != content_length
                raise "Response body size doesn't match Content-Length header: #{body_size} vs #{content_length}"
              end
            end
          end
        elsif lookup_header(headers, TRANSFER_ENCODING_HEADERS)
          # App has a Transfer-Encoding header. We assume that the app
          # has already chunked the body. The headers need no fixing.
          message_length_type = :chunked_by_app
          if output_body
            # We have no way to determine whether the body was correct (save for
            # parsing the chunking headers), so we don't keep-alive the connection
            # just to be safe.
            @can_keepalive = false
          end
          if lookup_header(headers, CONTENT_LENGTH_HEADERS)
            # Disallowed by the HTTP spec
            raise "Response object may not contain both Content-Length and Transfer-Encoding"
          end
        elsif status_code_allows_body?(status)
          # This is a response for which a body is allowed, although the request
          # may be one which does not expect a body (HEAD requests).
          #
          # The app has set neither the Content-Length nor the Transfer-Encoding
          # header. This means we'll have to add one of those headers. We know exactly how
          # big our body will be, so we can keep-alive the connection.
          if body.is_a?(Array)
            message_length_type = :content_length
            content_length = 0
            body.each { |part| content_length += bytesize(part.to_s) }

            headers_output << CONTENT_LENGTH_HEADER_AND_SEPARATOR
            headers_output << content_length.to_s
            headers_output << CRLF
          else
            message_length_type = :needs_chunking
            headers_output << TRANSFER_ENCODING_HEADER_AND_VALUE_CRLF
          end
        end

        # Finalize headers data.
        if @can_keepalive
          headers_output << CRLF
        else
          headers_output << CONNECTION_CLOSE_CRLF2
        end


        # If this is a request without body, write out headers without body.
        if !output_body
          connection.writev(headers_output)
        else
          # Otherwise, write out headers and body.
          case message_length_type
          when :content_length
            if body.is_a?(Array)
              connection.writev2(headers_output, body)
            else
              connection.writev(headers_output)
              body.each do |part|
                connection.write(part.to_s)
              end
            end
          when :chunked_by_app
            connection.writev(headers_output)
            body.each do |part|
              connection.write(part.to_s)
            end
          when :needs_chunking
            connection.writev(headers_output)
            body.each do |part|
              size = bytesize(part.to_s)
              if size != 0
                connection.writev(chunk_data(part.to_s, size))
              end
            end
            connection.write(TERMINATION_CHUNK)
          end
        end

        signal_keep_alive_allowed!
      ensure
        if @ush_reporter && ush_log_id
          @ush_reporter.log_writing_rack_body_end(ush_log_id)
        end
      end

      def close_body(body, env, socket_wrapper)
        if @ush_reporter
          ush_log_id = @ush_reporter.log_closing_rack_body_begin
        end
        begin
          body.close if body && body.respond_to?(:close)
        rescue => e
          if !should_swallow_app_error?(e, socket_wrapper)
            print_exception("Rack response body object's #close method", e)
          end
        ensure
          if @ush_reporter && ush_log_id
            @ush_reporter.log_closing_rack_body_end(ush_log_id)
          end
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

      def lookup_header(haystack, needles)
        needles.each do |needle|
          if result = haystack[needle]
            return result
          end
        end
        nil
      end

      def status_code_allows_body?(status)
        status < 100 || (status >= 200 && status != 204 && status != 304)
      end

      def should_output_body?(status, is_head_request)
        status_code_allows_body?(status) && !is_head_request
      end

      def chunk_data(data, size)
        [size.to_s(16), CRLF, data, CRLF]
      end

      # Called when body is written out successfully. Indicates that we should
      # keep-alive the connection if we can.
      def signal_keep_alive_allowed!
        @keepalive_performed = @can_keepalive
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
