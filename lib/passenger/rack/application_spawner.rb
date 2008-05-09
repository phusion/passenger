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

require 'passenger/passenger'
module Passenger
module Rack

class ApplicationSpawner
	include Utils
	
	def self.spawn_application(*args)
		@@instance ||= ApplicationSpawner.new
		@@instance.spawn_application(*args)
	end
	
	def spawn_application(app_root, lower_privilege = true, lowest_user = "nobody", environment = "production")
		a, b = UNIXSocket.pair
		pid = safe_fork(self.class.to_s) do
			safe_fork(self.class.to_s) do
				$0 = "Rack: #{app_root}"
				a.close
				channel = MessageChannel.new(b)
				ENV['RACK_ENV'] = environment
				Dir.chdir(app_root)
				app = load_rack_app(app_root)
				
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
		b.close
		Process.waitpid(pid) rescue nil
		
		channel = MessageChannel.new(a)
		pid, socket_name, using_abstract_namespace = channel.read
		if pid.nil?
			raise IOError, "Connection closed"
		end
		owner_pipe = channel.recv_io
		return Application.new(@app_root, pid, socket_name,
			using_abstract_namespace == "true", owner_pipe)
	end

private
	class RunnerContext
		attr_reader :result
		
		def run(app)
			@result = app
			throw :done
		end
		
		def get_binding
			return binding
		end
	end
	
	def load_rack_app(app_root)
		config_file = "#{app_root}/config.ru"
		context = RunnerContext.new
		catch(:done) do
			eval(File.read(config_file), context.get_binding, config_file)
		end
		return context.result
	end
end

end # module Rack
end # module Passenger
