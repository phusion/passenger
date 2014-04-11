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
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'admin_tools/server_instance'
PhusionPassenger.require_passenger_lib 'config/command'
PhusionPassenger.require_passenger_lib 'config/utils'

module PhusionPassenger
module Config

class DetachProcessCommand < Command
	include PhusionPassenger::Config::Utils

	def run
		parse_options
		select_passenger_instance
		@admin_client = connect_to_passenger_admin_socket(:role => :passenger_status)
		perform_detach
	end

private
	def self.create_option_parser(options)
		OptionParser.new do |opts|
			nl = "\n" + ' ' * 37
			opts.banner = "Usage: passenger-config detach-process [OPTIONS] <PID>\n"
			opts.separator ""
			opts.separator "  Remove an application process from the #{PROGRAM_NAME} process pool."
			opts.separator "  Has a similar effect to killing the application process directly with"
			opts.separator "  `kill <PID>`, but killing directly may cause the HTTP client to see an"
			opts.separator "  error, while using this command guarantees that clients see no errors."
			opts.separator ""

			opts.separator "Options:"
			opts.on("--instance INSTANCE_PID", Integer, "The #{PROGRAM_NAME} instance to select") do |value|
				options[:instance] = value
			end
			opts.on("-h", "--help", "Show this help") do
				options[:help] = true
			end
		end
	end

	def help
		puts @parser
	end

	def parse_options
		super
		if @argv.empty?
			abort "Please pass a PID. " +
				"See --help for more information."
		elsif @argv.size == 1
			@pid = @argv[0].to_i
		elsif @argv.size > 1
			help
			abort
		end
	end

	def perform_detach
		if @admin_client.pool_detach_process(@pid)
			puts "Process #{@pid} detached."
		else
			abort "Could not detach process #{@pid}."
		end
	end
end

end # module Config
end # module PhusionPassenger
