#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2008, 2009 Phusion
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

require 'socket'
require 'phusion_passenger/application'
require 'phusion_passenger/events'
require 'phusion_passenger/message_channel'
require 'phusion_passenger/abstract_request_handler'
require 'phusion_passenger/utils'
require 'phusion_passenger/rack/request_handler'

module PhusionPassenger
module Rack

# Class for spawning Rack applications.
class ApplicationSpawner
	include Utils
	
	def self.spawn_application(*args)
		@@instance ||= ApplicationSpawner.new
		@@instance.spawn_application(*args)
	end
	
	# Spawn an instance of the given Rack application. When successful, an
	# Application object will be returned, which represents the spawned
	# application.
	#
	# Accepts the same options as Railz::ApplicationSpawner#initialize.
	#
	# Raises:
	# - AppInitError: The Rack application raised an exception or called
	#   exit() during startup.
	# - SystemCallError, IOError, SocketError: Something went wrong.
	def spawn_application(app_root, options = {})
		options = sanitize_spawn_options(options)
		
		a, b = UNIXSocket.pair
		pid = safe_fork(self.class.to_s, true) do
			a.close
			
			file_descriptors_to_leave_open = [0, 1, 2, b.fileno]
			NativeSupport.close_all_file_descriptors(file_descriptors_to_leave_open)
			close_all_io_objects_for_fds(file_descriptors_to_leave_open)
			
			run(MessageChannel.new(b), app_root, options)
		end
		b.close
		Process.waitpid(pid) rescue nil
		
		channel = MessageChannel.new(a)
		unmarshal_and_raise_errors(channel, !!options["print_exceptions"], "rack")
		
		# No exception was raised, so spawning succeeded.
		pid, socket_name, socket_type = channel.read
		if pid.nil?
			raise IOError, "Connection closed"
		end
		owner_pipe = channel.recv_io
		return Application.new(@app_root, pid, socket_name,
			socket_type, owner_pipe)
	end

private
	def run(channel, app_root, options)
		$0 = "Rack: #{app_root}"
		app = nil
		success = report_app_init_status(channel) do
			ENV['RACK_ENV'] = options["environment"]
			ENV['RAILS_ENV'] = options["environment"]
			if options["base_uri"] && options["base_uri"] != "/"
				ENV['RACK_BASE_URI'] = options["base_uri"]
				ENV['RAILS_RELATIVE_URL_ROOT'] = options["base_uri"]
			end
			Dir.chdir(app_root)
			if options["environment_variables"]
				set_passed_environment_variables(options["environment_variables"])
			end
			if options["lower_privilege"]
				lower_privilege('config.ru', options["lowest_user"])
			end
			# Make sure RubyGems uses any new environment variable values
			# that have been set now (e.g. $HOME, $GEM_HOME, etc) and that
			# it is able to detect newly installed gems.
			Gem.clear_paths
			setup_bundler_support
			app = load_rack_app
		end
		
		if success
			reader, writer = IO.pipe
			begin
				handler = RequestHandler.new(reader, app, options)
				channel.write(Process.pid, handler.socket_name,
					handler.socket_type)
				channel.send_io(writer)
				writer.close
				channel.close
				
				PhusionPassenger.call_event(:starting_worker_process)
				handler.main_loop
			ensure
				channel.close rescue nil
				writer.close rescue nil
				handler.cleanup rescue nil
				PhusionPassenger.call_event(:stopping_worker_process)
			end
		end
	end
	
	def set_passed_environment_variables(encoded_environment_variables)
		env_vars_string = encoded_environment_variables.unpack("m").first
		# Prevent empty string as last item from b0rking the Hash[...] statement.
		# See comment in Hooks.cpp (sendHeaders) for details.
		env_vars_string << "_\0_"
		env_vars = Hash[*env_vars_string.split("\0")]
		env_vars.each_pair do |key, value|
			ENV[key] = value
		end
	end

	def load_rack_app
		# Load Rack inside the spawned child process so that the spawn manager
		# itself doesn't preload Rack. This is necessary because some broken
		# Rails apps explicitly specify a Rack version as dependency.
		require 'rack'
		rackup_code = ::File.read("config.ru")
		eval("Rack::Builder.new {( #{rackup_code}\n )}.to_app", TOPLEVEL_BINDING, "config.ru")
	end
end

end # module Rack
end # module PhusionPassenger
