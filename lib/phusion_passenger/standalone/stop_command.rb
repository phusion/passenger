#  Phusion Passenger - http://www.modrails.com/
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
require 'phusion_passenger/standalone/command'

module PhusionPassenger
module Standalone

class StopCommand < Command
	def self.description
		return "Stop a running Phusion Passenger Standalone instance."
	end
	
	def run
		parse_options!("stop") do |opts|
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
		rescue SystemCallError, IOError
			running = false
		end
		if running
			@nginx.stop
		else
			STDERR.puts "According to the PID file '#{@options[:pid_file]}', " <<
				"Phusion Passenger Standalone doesn't seem to be running."
			STDERR.puts
			STDERR.puts "If you know that Phusion Passenger Standalone *is* running then one of these"
			STDERR.puts "might be the cause of this error:"
			STDERR.puts
			STDERR.puts " * The Phusion Passenger Standalone instance that you want to stop isn't running"
			STDERR.puts "   on port #{@options[:port]}, but on another port. If this is the case then you"
			STDERR.puts "   should specify the right port with --port."
			STDERR.puts "   If the instance is listening on a Unix socket file instead of a TCP port,"
			STDERR.puts "   then please specify the PID file's filename with --pid-file."
			STDERR.puts " * The instance that you want to stop has stored its PID file in a non-standard"
			STDERR.puts "   location. In this case please specify the right PID file with --pid-file."
			exit 1
		end
	end
end

end
end
