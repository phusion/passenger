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
	RACK_URL_SCHEME	   = "rack.url_scheme"     # :nodoc:
	SCRIPT_NAME        = "SCRIPT_NAME"         # :nodoc:
	PATH_INFO          = "PATH_INFO"           # :nodoc:
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
	# Overrided method.
	def process_request(env, input, output)
		env[RACK_VERSION]      = RACK_VERSION_VALUE
		env[RACK_INPUT]        = input
		env[RACK_ERRORS]       = STDERR
		env[RACK_MULTITHREAD]  = false
		env[RACK_MULTIPROCESS] = true
		env[RACK_RUN_ONCE]     = false
		env[SCRIPT_NAME]     ||= ''
		env[PATH_INFO].sub!(/^#{Regexp.escape(env[SCRIPT_NAME])}/, "")
		if env[HTTPS] == YES || env[HTTPS] == ON || env[HTTPS] == ONE
			env[RACK_URL_SCHEME] = HTTPS_DOWNCASE
		else
			env[RACK_URL_SCHEME] = HTTP
		end
		
		status, headers, body = @app.call(env)
		begin
			output.write("Status: #{status}#{CRLF}")
			headers[X_POWERED_BY] = PASSENGER_HEADER
			headers.each do |k, vs|
				vs.each do |v|
					output.write("#{k}: #{v}#{CRLF}")
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
