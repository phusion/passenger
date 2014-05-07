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

PhusionPassenger.require_passenger_lib 'constants'

module PhusionPassenger
module Config

module Utils
	extend self    # Make methods available as class methods.
	
	def self.included(klass)
		# When included into another class, make sure that Utils
		# methods are made private.
		public_instance_methods(false).each do |method_name|
			klass.send(:private, method_name)
		end
	end

	def select_passenger_instance
		if pid = @options[:instance]
			@server_instance = AdminTools::ServerInstance.for_pid(pid)
			if !@server_instance
				puts "*** ERROR: there doesn't seem to be a #{PROGRAM_NAME} instance running on PID #{pid}."
				list_all_passenger_instances(AdminTools::ServerInstance.list)
				puts
				puts "Please pass `--instance <#{PROGRAM_NAME}> PID>` to select a specific #{PROGRAM_NAME} instance."
				abort
			end
		else
			server_instances = AdminTools::ServerInstance.list
			if server_instances.empty?
				abort "*** ERROR: #{PROGRAM_NAME} doesn't seem to be running."
			elsif server_instances.size == 1
				@server_instance = server_instances.first
			else
				complain_that_multiple_passenger_instances_are_running(server_instances)
				abort
			end
		end
	end

	def complain_that_multiple_passenger_instances_are_running(server_instances)
		puts "It appears that multiple #{PROGRAM_NAME} instances are running. Please select"
		puts "a specific one by passing:"
		puts
		puts "  --instance <#{PROGRAM_NAME} PID>"
		puts
		list_all_passenger_instances(server_instances)
		abort
	end

	def list_all_passenger_instances(server_instances)
		puts "The following #{PROGRAM_NAME} instances are running:"
		server_instances.each do |instance|
			begin
				description = instance.web_server_description
			rescue Errno::EACCES, Errno::ENOENT
				description = nil
			end
			printf "  PID: %-8s   %s\n", instance.pid, description
		end
	end

	def connect_to_passenger_admin_socket(options)
		return @server_instance.connect(options)
	rescue AdminTools::ServerInstance::RoleDeniedError
		PhusionPassenger.require_passenger_lib 'platform_info/ruby'
		STDERR.puts "*** ERROR: You are not authorized to query the status for " +
			"this #{PROGRAM_NAME} instance. Please try again with '#{PlatformInfo.ruby_sudo_command}'."
		exit 2
	rescue AdminTools::ServerInstance::CorruptedDirectoryError
		STDERR.puts "*** ERROR: The server instance directory #{server_instance.path} is corrupted. " +
			"This could have two causes:\n" +
			"\n" +
			"  1. The #{PROGRAM_NAME} instance is no longer running, but it failed to cleanup the directory. " +
				"Please delete this directory and ignore the problem.\n" +
			"  2. An external program corrupted the directory. Please restart this #{PROGRAM_NAME} instance.\n"
		exit 2
	end

	def is_enterprise?
		return defined?(PhusionPassenger::PASSENGER_IS_ENTERPRISE) && PhusionPassenger::PASSENGER_IS_ENTERPRISE
	end
end

end # module Config
end # module PhusionPassenger
