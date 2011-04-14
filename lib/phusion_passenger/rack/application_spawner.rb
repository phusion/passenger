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
require 'phusion_passenger/constants'
require 'phusion_passenger/message_channel'
require 'phusion_passenger/abstract_server'
require 'phusion_passenger/abstract_request_handler'
require 'phusion_passenger/debug_logging'
require 'phusion_passenger/public_api'
require 'phusion_passenger/utils'
require 'phusion_passenger/native_support'
require 'phusion_passenger/rack/request_handler'

module PhusionPassenger
module Rack

# Spawning of Rack applications.
class ApplicationSpawner < AbstractServer
	include Utils
	extend Utils
	include DebugLogging
	
	# This exception means that the ApplicationSpawner server process exited unexpectedly.
	class Error < AbstractServer::ServerError
	end
	
	# Spawn an instance of the given Rack application. When successful, an
	# AppProcess object will be returned, which represents the spawned
	# application.
	#
	# Accepts the same options as SpawnManager#spawn_application.
	#
	# Raises:
	# - AppInitError: The Rack application raised an exception or called
	#   exit() during startup.
	# - SystemCallError, IOError, SocketError: Something went wrong.
	def self.spawn_application(options = {})
		options = sanitize_spawn_options(options)
		
		a, b = UNIXSocket.pair
		pid = safe_fork(self.class.to_s, true) do
			a.close
			
			file_descriptors_to_leave_open = [0, 1, 2, b.fileno]
			NativeSupport.close_all_file_descriptors(file_descriptors_to_leave_open)
			close_all_io_objects_for_fds(file_descriptors_to_leave_open)
			
			channel = MessageChannel.new(b)
			app = nil
			success = report_app_init_status(channel) do
				prepare_app_process('config.ru', options)
				app = load_rack_app
				after_loading_app_code(options)
			end
			if success
				start_request_handler(channel, app, false, options)
			end
		end
		b.close
		Process.waitpid(pid) rescue nil
		
		channel = MessageChannel.new(a)
		unmarshal_and_raise_errors(channel, options["print_exceptions"], "rack")
		
		# No exception was raised, so spawning succeeded.
		return AppProcess.read_from_channel(channel)
	end
	
	# The following options are accepted:
	# - 'app_root'
	#
	# See SpawnManager#spawn_application for information about the options.
	def initialize(options)
		super()
		@options          = sanitize_spawn_options(options)
		@app_root         = @options["app_root"]
		@canonicalized_app_root = canonicalize_path(@app_root)
		self.max_idle_time = DEFAULT_APP_SPAWNER_MAX_IDLE_TIME
		define_message_handler(:spawn_application, :handle_spawn_application)
	end
	
	# Spawns an instance of the Rack application. When successful, an AppProcess object
	# will be returned, which represents the spawned Rack application.
	#
	# +options+ will be passed to the request handler's constructor.
	#
	# Raises:
	# - AbstractServer::ServerNotStarted: The ApplicationSpawner server hasn't already been started.
	# - ApplicationSpawner::Error: The ApplicationSpawner server exited unexpectedly.
	def spawn_application(options = {})
		connect do |channel|
			channel.write("spawn_application", *options.to_a.flatten)
			return AppProcess.read_from_channel(channel)
		end
	rescue SystemCallError, IOError, SocketError => e
		raise Error, "The application spawner server exited unexpectedly: #{e}"
	end
	
	# Overrided from AbstractServer#start.
	#
	# May raise these additional exceptions:
	# - AppInitError: The Rack application raised an exception
	#   or called exit() during startup.
	# - ApplicationSpawner::Error: The ApplicationSpawner server exited unexpectedly.
	def start
		super
		begin
			channel = MessageChannel.new(@owner_socket)
			unmarshal_and_raise_errors(channel, @options["print_exceptions"])
		rescue IOError, SystemCallError, SocketError => e
			stop if started?
			raise Error, "The application spawner server exited unexpectedly: #{e}"
		rescue
			stop if started?
			raise
		end
	end

protected
	# Overrided method.
	def before_fork # :nodoc:
		if GC.copy_on_write_friendly?
			# Garbage collect now so that the child process doesn't have to
			# do that (to prevent making pages dirty).
			GC.start
		end
	end

	# Overrided method.
	def initialize_server # :nodoc:
		report_app_init_status(MessageChannel.new(@owner_socket)) do
			$0 = "Passenger ApplicationSpawner: #{@app_root}"
			prepare_app_process('config.ru', @options)
			@app = self.class.send(:load_rack_app)
			after_loading_app_code(@options)
		end
	end

private
	def handle_spawn_application(client, *options)
		options = sanitize_spawn_options(Hash[*options])
		a, b = UNIXSocket.pair
		safe_fork('application', true) do
			begin
				a.close
				client.close
				options = @options.merge(options)
				self.class.send(:start_request_handler, MessageChannel.new(b),
					@app, true, options)
			rescue SignalException => e
				if e.message != AbstractRequestHandler::HARD_TERMINATION_SIGNAL &&
				   e.message != AbstractRequestHandler::SOFT_TERMINATION_SIGNAL
					raise
				end
			end
		end
		
		b.close
		worker_channel = MessageChannel.new(a)
		app_process = AppProcess.read_from_channel(worker_channel)
		app_process.write_to_channel(client)
	ensure
		a.close if a
		b.close if b && !b.closed?
		app_process.close if app_process
	end

	def self.start_request_handler(channel, app, forked, options)
		app_root = options["app_root"]
		$0 = "Rack: #{app_root}"
		reader, writer = IO.pipe
		begin
			reader.close_on_exec!
			
			handler = RequestHandler.new(reader, app, options)
			app_process = AppProcess.new(app_root, Process.pid, writer,
				handler.server_sockets)
			app_process.write_to_channel(channel)
			writer.close
			channel.close
			
			before_handling_requests(forked, options)
			handler.main_loop
		ensure
			channel.close rescue nil
			writer.close rescue nil
			handler.cleanup rescue nil
			after_handling_requests
		end
	end
	private_class_method :start_request_handler
	
	def self.load_rack_app
		# Load Rack inside the spawned child process so that the spawn manager
		# itself doesn't preload Rack. This is necessary because some broken
		# Rails apps explicitly specify a Rack version as dependency.
		require 'rack'
		rackup_file = ENV["RACKUP_FILE"] || "config.ru"
		rackup_code = ::File.read(rackup_file)
		eval("Rack::Builder.new {( #{rackup_code}\n )}.to_app", TOPLEVEL_BINDING, rackup_file)
	end
	private_class_method :load_rack_app
end

end # module Rack
end # module PhusionPassenger
