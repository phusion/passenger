#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010 Phusion
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
PhusionPassenger.require_passenger_lib 'standalone/command'

module PhusionPassenger
module Standalone

class StatusCommand < Command
	def self.description
		return "Show the status of a running Passenger Standalone instance."
	end
	
	def run
		parse_options!("status") do |opts|
			opts.separator "Options:"
			opts.on("-p", "--port NUMBER", Integer,
				wrap_desc("The port number of a Phusion Passenger Standalone instance (default: #{@options[:port]})")) do |value|
				@options[:port] = value
			end
			opts.on("--pid-file FILE", String,
				wrap_desc("PID file of a running Phusion Passenger Standalone instance.")) do |value|
				@options[:pid_file] = value
			end
		end
		
		determine_various_resource_locations(false)
		create_nginx_controller
		begin
			running = @nginx.running?
			pid = @nginx.pid
		rescue SystemCallError, IOError
			running = false
		end
		if running
			puts "Phusion Passenger Standalone is running on PID #{pid}, according to PID file #{@options[:pid_file]}"
		else
			puts "Phusion Passenger Standalone is not running, according to PID file #{@options[:pid_file]}"
		end
	end
end

end
end
