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

require 'rubygems'
require 'socket'
require 'etc'
require 'fcntl'
require 'phusion_passenger/abstract_server'
require 'phusion_passenger/app_process'
require 'phusion_passenger/constants'
require 'phusion_passenger/debug_logging'
require 'phusion_passenger/classic_rails/request_handler'
require 'phusion_passenger/rack/request_handler'
require 'phusion_passenger/public_api'
require 'phusion_passenger/exceptions'
require 'phusion_passenger/utils'

module PhusionPassenger
module ClassicRails

# Spawning of Rails 1 and Rails 2 applications.
#
# ClassicRails::ApplicationSpawner can operate in two modes:
# - Smart mode. In this mode, the Rails application's code is first preloaded into
#   a temporary process, which can then further fork off application processes.
#   Once the code has been preloaded, forking off application processes is very fast,
#   and all the forked off application processes can share code memory with each other.
#   To use this mode, create an ApplicationSpawner object, start it, and call
#   #spawn_application on it.
#   A single ApplicationSpawner object can only handle a single Rails application.
# - Conservative mode. In this mode, a Rails app process is directly spawned
#   without any preloading. This increases compatibility with applications. To use this
#   mode, call ApplicationSpawner.spawn_application.
class ApplicationSpawner < AbstractServer
	include Utils
	extend Utils
	include DebugLogging
	
	# This exception means that the ApplicationSpawner server process exited unexpectedly.
	class Error < AbstractServer::ServerError
	end
	
	# The application root of this spawner.
	attr_reader :app_root
	
	# Spawns an instance of a Rails application. When successful, an AppProcess object
	# will be returned, which represents the spawned Rails application.
	#
	# This method spawns the application directly, without preloading its code.
	# This method may only be called if no Rails framework has been loaded in the current
	# Ruby VM.
	#
	# The "app_root" option must be given. All other options are passed to the request
	# handler's constructor.
	#
	# Raises:
	# - AppInitError: The Ruby on Rails application raised an exception
	#   or called exit() during startup.
	# - SystemCallError, IOError, SocketError: Something went wrong.
	def self.spawn_application(options)
		options = sanitize_spawn_options(options)
		
		a, b = UNIXSocket.pair
		pid = safe_fork('application', true) do
			a.close
			
			file_descriptors_to_leave_open = [0, 1, 2, b.fileno]
			NativeSupport.close_all_file_descriptors(file_descriptors_to_leave_open)
			close_all_io_objects_for_fds(file_descriptors_to_leave_open)
			
			channel = MessageChannel.new(b)
			success = report_app_init_status(channel) do
				prepare_app_process('config/environment.rb', options)
				require File.expand_path('config/environment')
				require 'dispatcher'
				after_loading_app_code(options)
			end
			if success
				start_request_handler(channel, false, options)
			end
		end
		b.close
		Process.waitpid(pid) rescue nil
		
		channel = MessageChannel.new(a)
		unmarshal_and_raise_errors(channel, options["print_exceptions"])
		
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
	
	# Spawns an instance of the Rails application. When successful, an AppProcess object
	# will be returned, which represents the spawned Rails application.
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
	# - AppInitError: The Ruby on Rails application raised an exception
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
			prepare_app_process('config/environment.rb', @options)
			if defined?(RAILS_ENV)
				Object.send(:remove_const, :RAILS_ENV)
				Object.const_set(:RAILS_ENV, ENV['RAILS_ENV'])
			end
			preload_application
			after_loading_app_code(@options)
		end
	end
	
private
	def preload_application
		Object.const_set(:RAILS_ROOT, @canonicalized_app_root)
		if defined?(::Rails::Initializer)
			::Rails::Initializer.run(:set_load_path)
			
			# The Rails framework is loaded at the moment.
			# environment.rb may set ENV['RAILS_ENV']. So we re-initialize
			# RAILS_ENV in Rails::Initializer.load_environment.
			::Rails::Initializer.class_eval do
				def load_environment_with_passenger
					using_default_log_path =
						configuration.log_path ==
						configuration.send(:default_log_path)
					
					if defined?(::RAILS_ENV)
						Object.send(:remove_const, :RAILS_ENV)
					end
					Object.const_set(:RAILS_ENV, (ENV['RAILS_ENV'] || 'development').dup)
					
					if using_default_log_path
						# We've changed the environment, so open the
						# correct log file.
						configuration.log_path = configuration.send(:default_log_path)
					end
					
					load_environment_without_passenger
				end
				
				alias_method :load_environment_without_passenger, :load_environment
				alias_method :load_environment, :load_environment_with_passenger
			end
		end
		if File.exist?('config/preinitializer.rb')
			require File.expand_path('config/preinitializer')
		end
		require File.expand_path('config/environment')
		if ActionController::Base.page_cache_directory.blank?
			ActionController::Base.page_cache_directory = "#{RAILS_ROOT}/public"
		end
		if defined?(ActionController::Dispatcher) \
		   && ActionController::Dispatcher.respond_to?(:error_file_path)
			ActionController::Dispatcher.error_file_path = "#{RAILS_ROOT}/public"
		end
		
		require 'rails/version' if !defined?(::Rails::VERSION)
		if !defined?(Dispatcher)
			begin
				require 'dispatcher'
			rescue LoadError
				# Early versions of Rails 3 still had the dispatcher, but
				# later versions disposed of it, in which case we'll need
				# to use the application object.
				raise if Rails::VERSION::MAJOR < 3
			end
		end
		
		# - No point in preloading the application sources if the garbage collector
		#   isn't copy-on-write friendly.
		# - Rails >= 2.2 already preloads application sources by default, so no need
		#   to do that again.
		if GC.copy_on_write_friendly? && !rails_will_preload_app_code?
			# Rails 2.2+ uses application_controller.rb while olde
			# versions use application.rb.
			require_dependency 'application'
			['models','controllers','helpers'].each do |section|
				Dir.glob("app/#{section}}/*.rb").each do |file|
					require_dependency canonicalize_path(file)
				end
			end
		end
	end
	
	def rails_will_preload_app_code?
		if defined?(Rails::Initializer)
			return ::Rails::Initializer.method_defined?(:load_application_classes)
		else
			return Rails::VERSION::MAJOR >= 3
		end
	end

	def handle_spawn_application(client, *options)
		options = sanitize_spawn_options(Hash[*options])
		a, b = UNIXSocket.pair
		safe_fork('application', true) do
			begin
				a.close
				client.close
				options = @options.merge(options)
				self.class.send(:start_request_handler, MessageChannel.new(b),
					true, options)
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
	
	# Initialize the request handler and enter its main loop.
	# Spawn information will be sent back via +channel+.
	# The +forked+ argument indicates whether a new process was forked off
	# after loading environment.rb (i.e. whether smart spawning is being
	# used).
	def self.start_request_handler(channel, forked, options)
		app_root = options["app_root"]
		$0 = "Rails: #{app_root}"
		reader, writer = IO.pipe
		begin
			reader.close_on_exec!
			
			if Rails::VERSION::STRING >= '2.3.0'
				rack_app = find_rack_app
				handler = Rack::RequestHandler.new(reader, rack_app, options)
			else
				handler = RequestHandler.new(reader, options)
			end
			
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
	
	def self.find_rack_app
		if Rails::VERSION::MAJOR >= 3
			File.read("config/application.rb") =~ /^module (.+)$/
			app_module = Object.const_get($1)
			return app_module::Application
		else
			return ActionController::Dispatcher.new
		end
	end
	private_class_method :find_rack_app
end

end # module ClassicRails
end # module PhusionPassenger
