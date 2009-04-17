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
require 'phusion_passenger/message_channel'
require 'phusion_passenger/utils'
module PhusionPassenger
module WSGI

# Class for spawning WSGI applications.
class ApplicationSpawner
	include Utils
	REQUEST_HANDLER = File.expand_path(File.dirname(__FILE__) + "/request_handler.py")
	
	def self.spawn_application(*args)
		@@instance ||= ApplicationSpawner.new
		@@instance.spawn_application(*args)
	end
	
	# Spawn an instance of the given WSGI application. When successful, an
	# Application object will be returned, which represents the spawned
	# application.
	#
	# Raises:
	# - AppInitError: The WSGI application raised an exception or called
	#   exit() during startup.
	# - SystemCallError, IOError, SocketError: Something went wrong.
	def spawn_application(app_root, lower_privilege = true, lowest_user = "nobody", environment = "production")
		a, b = UNIXSocket.pair
		pid = safe_fork(self.class.to_s, true) do
			a.close
			
			file_descriptors_to_leave_open = [0, 1, 2, b.fileno]
			NativeSupport.close_all_file_descriptors(file_descriptors_to_leave_open)
			close_all_io_objects_for_fds(file_descriptors_to_leave_open)
			
			run(MessageChannel.new(b), app_root, lower_privilege, lowest_user, environment)
		end
		b.close
		Process.waitpid(pid) rescue nil
		
		channel = MessageChannel.new(a)
		pid, socket_name, socket_type = channel.read
		if pid.nil?
			raise IOError, "Connection closed"
		end
		owner_pipe = channel.recv_io
		return Application.new(@app_root, pid, socket_name,
			socket_type, owner_pipe)
	end

private
	def run(channel, app_root, lower_privilege, lowest_user, environment)
		$0 = "WSGI: #{app_root}"
		ENV['WSGI_ENV'] = environment
		Dir.chdir(app_root)
		if lower_privilege
			lower_privilege('passenger_wsgi.py', lowest_user)
		end
		
		socket_file = "#{passenger_tmpdir}/backends/wsgi_backend.#{Process.pid}.#{rand 10000000}"
		server = UNIXServer.new(socket_file)
		begin
			reader, writer = IO.pipe
			channel.write(Process.pid, socket_file, "unix")
			channel.send_io(writer)
			writer.close
			channel.close
			
			NativeSupport.close_all_file_descriptors([0, 1, 2, server.fileno,
				reader.fileno])
			exec(REQUEST_HANDLER, socket_file, server.fileno.to_s,
				reader.fileno.to_s)
		rescue
			server.close
			File.unlink(socket_file)
			raise
		end
	end
end

end # module WSGI
end # module PhusionPassenger
