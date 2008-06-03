#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2008  Phusion
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; version 2 of the License.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

require 'passenger/abstract_request_handler'
module Passenger
module Rack

# A request handler for Rack applications.
class RequestHandler < AbstractRequestHandler
	# Constants which exist to relieve Ruby's garbage collector.
	RACK_VERSION       = "rack.version"        # :nodoc:
	RACK_VERSION_VALUE = [0, 1]                # :nodoc:
	RACK_INPUT         = "rack.input"          # :nodoc:
	RACK_ERRORS        = "rack.errors"         # :nodoc:
	RACK_MULTITHREAD   = "rack.multithread"    # :nodoc:
	RACK_MULTIPROCESS  = "rack.multiprocess"   # :nodoc:
	RACK_RUN_ONCE      = "rack.run_once"       # :nodoc:
	RACK_URL_SCHEME    = "rack.url_scheme"     # :nodoc:
	HTTPS          = "HTTPS"  # :nodoc:
	HTTPS_DOWNCASE = "https"  # :nodoc:
	HTTP           = "http"   # :nodoc:
	YES            = "yes"    # :nodoc:
	ON             = "on"     # :nodoc:
	ONE            = "one"    # :nodoc:
	CRLF           = "\r\n"   # :nodoc:

	# +app+ is the Rack application object.
	def initialize(owner_pipe, app)
		super(owner_pipe)
		@app = app
	end

protected
	# The real input stream is not seekable (calling _seek_ on it
	# will raise an exception). But Merb calls _seek_ if the object
	# responds to it. So we wrap the input stream in a proxy object
	# that doesn't respond to _seek_.
	class InputReader
		def initialize(io)
			@io = io
		end
		
		def gets
			@io.gets
		end
		
		def read(*args)
			@io.read(*args)
		end
		
		def each(&block)
			@io.each(&block)
		end
	end

	# Overrided method.
	def process_request(env, input, output)
		env[RACK_VERSION]      = RACK_VERSION_VALUE
		env[RACK_INPUT]        = InputReader.new(input)
		env[RACK_ERRORS]       = STDERR
		env[RACK_MULTITHREAD]  = false
		env[RACK_MULTIPROCESS] = true
		env[RACK_RUN_ONCE]     = false
		if env[HTTPS] == YES || env[HTTPS] == ON || env[HTTPS] == ONE
			env[RACK_URL_SCHEME] = HTTPS_DOWNCASE
		else
			env[RACK_URL_SCHEME] = HTTP
		end
    env["SCRIPT_NAME"]   ||= ''
		
		status, headers, body = @app.call(env)
		begin
			output.write("Status: #{status}\r\n")
			headers[X_POWERED_BY] = PASSENGER_HEADER
			headers.each do |k, vs|
				vs.each do |v|
					output.write("#{k}: #{v}\r\n")
				end
			end
			output.write(CRLF)
			body.each do |s|
				output.write(s)
			end
		ensure
			body.close if body.respond_to?(:close)
		end
	end
end

end # module Rack
end # module Passenger
