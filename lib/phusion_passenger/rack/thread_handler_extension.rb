# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2013 Phusion
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
	RACK_URL_SCHEME	   = "rack.url_scheme"     # :nodoc:
	RACK_HIJACK_P      = "rack.hijack?"        # :nodoc:
	RACK_HIJACK        = "rack.hijack"         # :nodoc:
	SCRIPT_NAME        = "SCRIPT_NAME"         # :nodoc:
	HTTPS          = "HTTPS"  # :nodoc:
	HTTPS_DOWNCASE = "https"  # :nodoc:
	HTTP           = "http"   # :nodoc:
	YES            = "yes"    # :nodoc:
	ON             = "on"     # :nodoc:
	ONE            = "1"      # :nodoc:
	CRLF           = "\r\n"   # :nodoc:
	NEWLINE        = "\n"     # :nodoc:
	STATUS         = "Status: "       # :nodoc:
	NAME_VALUE_SEPARATOR = ": "       # :nodoc:

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
				env[RACK_HIJACK_IO] ||= connection
			end

			begin
				status, headers, body = @app.call(env)
			rescue => e
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
				if full_http_response
					connection.write("HTTP/1.1 #{status.to_i.to_s} Whatever#{CRLF}")
					connection.write("Connection: close#{CRLF}")
				end
				headers_output = [
					STATUS, status.to_i.to_s, CRLF
				]
				headers.each do |key, values|
					if values.is_a?(String)
						values = values.split(NEWLINE)
					elsif key == RACK_HIJACK
						# We do not check for this key name in every loop
						# iteration as an optimization.
						next
					end
					values.each do |value|
						headers_output << key
						headers_output << NAME_VALUE_SEPARATOR
						headers_output << value
						headers_output << CRLF
					end
				end
				headers_output << CRLF

				if hijack_callback = headers[RACK_HIJACK]
					# Application requested a partial socket hijack.
					body = nil
					connection.writev(headers_output)
					connection.flush
					hijacked_socket = env[RACK_HIJACK].call
					hijack_callback.call(hijacked_socket)
					return true
				elsif body.is_a?(Array)
					# The body may be an ActionController::StringCoercion::UglyBody
					# object instead of a real Array, even when #is_a? claims so.
					# Call #to_a just to be sure.
					connection.writev2(headers_output, body.to_a)
					return false
				elsif body.is_a?(String)
					headers_output << body
					connection.writev(headers_output)
					return false
				else
					connection.writev(headers_output)
					if body
						begin
							body.each do |s|
								connection.write(s)
							end
						rescue => e
							if should_reraise_app_error?(e, socket_wrapper)
								raise e
							elsif !should_swallow_app_error?(e, socket_wrapper)
								# Body objects can raise exceptions in #each.
								print_exception("Rack body object #each method", e)
							end
							return false
						end
					end
					return false
				end
			ensure
				body.close if body && body.respond_to?(:close)
			end
		ensure
			rewindable_input.close
		end
	end

private
end

end # module Rack
end # module PhusionPassenger
