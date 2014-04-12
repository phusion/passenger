#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2014 Phusion
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

class HelpCommand < Command
	def self.show_in_command_list
		return false
	end
	
	def run
		puts "Phusion Passenger Standalone, the easiest way to deploy web apps."
		puts
		puts "Available commands:"
		puts
		Main.each_command do |command_name, command_class|
			if command_class.show_in_command_list
				printf "  passenger %-15s  %s\n",
					command_name,
					wrap_desc(command_class.description, 51, 29)
			end
		end
		puts
		puts "Special options:"
		puts
		puts "  passenger --help      Display this help message."
		puts "  passenger --version   Display version number."
		puts
		puts "For more information about a specific command, please type"
		puts "'passenger <COMMAND> --help', e.g. 'passenger start --help'."
	end
end

end # module Standalone
end # module PhusionPassenger
