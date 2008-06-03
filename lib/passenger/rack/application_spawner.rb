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

require 'rack'
require 'socket'
require 'passenger/application'
require 'passenger/message_channel'
require 'passenger/abstract_request_handler'
require 'passenger/utils'
require 'passenger/rack/request_handler'
module Passenger
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
	# Raises:
	# - AppInitError: The Rack application raised an exception or called
	#   exit() during startup.
	# - SystemCallError, IOError, SocketError: Something went wrong.
	def spawn_application(app_root, lower_privilege = true, lowest_user = "nobody", environment = "production")
		a, b = UNIXSocket.pair
		# Double fork in order to prevent zombie processes.
		pid = safe_fork(self.class.to_s) do
			safe_fork(self.class.to_s) do
				a.close
				run(MessageChannel.new(b), app_root, lower_privilege, lowest_user, environment)
			end
		end
		b.close
		Process.waitpid(pid) rescue nil
		
		channel = MessageChannel.new(a)
		unmarshal_and_raise_errors(channel, "rack")
		
		# No exception was raised, so spawning succeeded.
		pid, socket_name, using_abstract_namespace = channel.read
		if pid.nil?
			raise IOError, "Connection closed"
		end
		owner_pipe = channel.recv_io
		return Application.new(@app_root, pid, socket_name,
			using_abstract_namespace == "true", owner_pipe)
	end

private
	
	def run(channel, app_root, lower_privilege, lowest_user, environment)
		$0 = "Rack: #{app_root}"
		app = nil
		success = report_app_init_status(channel) do
			ENV['RACK_ENV'] = environment
			Dir.chdir(app_root)
			if lower_privilege
				lower_privilege('config.ru', lowest_user)
			end
			app = load_rack_app
		end
		
		if success
			reader, writer = IO.pipe
			begin
				handler = RequestHandler.new(reader, app)
				channel.write(Process.pid, handler.socket_name,
					handler.using_abstract_namespace?)
				channel.send_io(writer)
				writer.close
				channel.close
				handler.main_loop
			ensure
				channel.close rescue nil
				writer.close rescue nil
				handler.cleanup rescue nil
			end
		end
	end

	def load_rack_app
		rackup_code = File.read("config.ru")
		eval("Rack::Builder.new {( #{rackup_code}\n )}.to_app", TOPLEVEL_BINDING, "config.ru")
	end
end

end # module Rack
end # module Passenger
