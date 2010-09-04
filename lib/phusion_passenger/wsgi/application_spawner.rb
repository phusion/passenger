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

require 'socket'
require 'phusion_passenger/app_process'
require 'phusion_passenger/message_channel'
require 'phusion_passenger/public_api'
require 'phusion_passenger/utils'
require 'phusion_passenger/utils/tmpdir'
require 'phusion_passenger/native_support'

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
	def spawn_application(options)
		a, b = UNIXSocket.pair
		pid = safe_fork(self.class.to_s, true) do
			a.close
			
			file_descriptors_to_leave_open = [0, 1, 2, b.fileno]
			NativeSupport.close_all_file_descriptors(file_descriptors_to_leave_open)
			close_all_io_objects_for_fds(file_descriptors_to_leave_open)
			
			run(MessageChannel.new(b), options)
		end
		b.close
		Process.waitpid(pid) rescue nil
		
		channel = MessageChannel.new(a)
		return AppProcess.read_from_channel(channel)
	end

private
	def run(channel, options)
		$0 = "WSGI: #{options['app_root']}"
		prepare_app_process("passenger_wsgi.py", options)
		ENV['WSGI_ENV'] = options['environment']
		
		if defined?(NativeSupport)
			unix_path_max = NativeSupport::UNIX_PATH_MAX
		else
			unix_path_max = 100
		end
		
		socket_file = "#{passenger_tmpdir}/backends/wsgi.#{Process.pid}.#{rand 10000000}"
		socket_file = socket_file.slice(0, unix_path_max - 1)
		server = UNIXServer.new(socket_file)
		begin
			File.chmod(0666, socket_file)
			reader, writer = IO.pipe
			app_process = AppProcess.new(options["app_root"], Process.pid, writer,
				:main => [socket_file, 'unix'])
			app_process.write_to_channel(channel)
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
