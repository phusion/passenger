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

require 'rubygems'
require 'socket'
require 'etc'
require 'fcntl'
require 'phusion_passenger/application'
require 'phusion_passenger/abstract_server'
require 'phusion_passenger/application'
require 'phusion_passenger/constants'
require 'phusion_passenger/events'
require 'phusion_passenger/railz/request_handler'
require 'phusion_passenger/rack/request_handler'
require 'phusion_passenger/exceptions'
require 'phusion_passenger/utils'

module PhusionPassenger
module Railz

# This class is capable of spawning instances of a single Ruby on Rails application.
# It does so by preloading as much of the application's code as possible, then creating
# instances of the application using what is already preloaded. This makes it spawning
# application instances very fast, except for the first spawn.
#
# Use multiple instances of ApplicationSpawner if you need to spawn multiple different
# Ruby on Rails applications.
#
# *Note*: ApplicationSpawner may only be started asynchronously with AbstractServer#start.
# Starting it synchronously with AbstractServer#start_synchronously has not been tested.
class ApplicationSpawner < AbstractServer
	include Utils
	
	# This exception means that the ApplicationSpawner server process exited unexpectedly.
	class Error < AbstractServer::ServerError
	end
	
	# The user ID of the root user.
	ROOT_UID = 0
	# The group ID of the root user.
	ROOT_GID = 0
	
	# The application root of this spawner.
	attr_reader :app_root

	# +app_root+ is the root directory of this application, i.e. the directory
	# that contains 'app/', 'public/', etc. If given an invalid directory,
	# or a directory that doesn't appear to be a Rails application root directory,
	# then an InvalidPath will be raised.
	#
	# Additional options are:
	# - +lower_privilege+ and +lowest_user+:
	#   If +lower_privilege+ is true, then ApplicationSpawner will attempt to
	#   switch to the user who owns the application's <tt>config/environment.rb</tt>,
	#   and to the default group of that user.
	#
	#   If that user doesn't exist on the system, or if that user is root,
	#   then ApplicationSpawner will attempt to switch to the username given by
	#   +lowest_user+ (and to the default group of that user).
	#   If +lowest_user+ doesn't exist either, or if switching user failed
	#   (because the current process does not have the privilege to do so),
	#   then ApplicationSpawner will continue without reporting an error.
	#
	# - +environment+:
	#   Allows one to specify the RAILS_ENV environment to use.
	#
	# - +environment_variables+:
	#   Environment variables which should be passed to the spawned application.
	#   This is NULL-seperated string of key-value pairs, encoded in base64.
	#   The last byte in the unencoded data must be a NULL.
	#
	# - +base_uri+:
	#   The base URI on which this application is deployed. It equals "/"
	#   string if the application is deployed on the root URI. It must not
	#   equal the empty string.
	#
	# - +print_exceptions+:
	#   Whether exceptions that have occurred during application initialization
	#   should be printed to STDERR. The default is true.
	#
	# All other options will be passed on to RequestHandler.
	def initialize(app_root, options = {})
		super()
		@app_root = app_root
		@canonicalized_app_root = canonicalize_path(app_root)
		@options = sanitize_spawn_options(options)
		@lower_privilege = @options["lower_privilege"]
		@lowest_user     = @options["lowest_user"]
		@environment     = @options["environment"]
		@encoded_environment_variables = @options["environment_variables"]
		@base_uri = @options["base_uri"] if @options["base_uri"] && @options["base_uri"] != "/"
		@print_exceptions = @options["print_exceptions"]
		self.max_idle_time = DEFAULT_APP_SPAWNER_MAX_IDLE_TIME
		assert_valid_app_root(@app_root)
		define_message_handler(:spawn_application, :handle_spawn_application)
	end
	
	# Spawn an instance of the RoR application. When successful, an Application object
	# will be returned, which represents the spawned RoR application.
	#
	# Raises:
	# - AbstractServer::ServerNotStarted: The ApplicationSpawner server hasn't already been started.
	# - ApplicationSpawner::Error: The ApplicationSpawner server exited unexpectedly.
	def spawn_application
		server.write("spawn_application")
		pid, socket_name, socket_type = server.read
		if pid.nil?
			raise IOError, "Connection closed"
		end
		owner_pipe = server.recv_io
		return Application.new(@app_root, pid, socket_name,
			socket_type, owner_pipe)
	rescue SystemCallError, IOError, SocketError => e
		raise Error, "The application spawner server exited unexpectedly: #{e}"
	end
	
	# Spawn an instance of the RoR application. When successful, an Application object
	# will be returned, which represents the spawned RoR application.
	#
	# Unlike spawn_application, this method may be called even when the ApplicationSpawner
	# server isn't started. This allows one to spawn a RoR application without preloading
	# any source files.
	#
	# This method may only be called if no Rails framework has been loaded in the current
	# Ruby VM.
	#
	# Raises:
	# - AppInitError: The Ruby on Rails application raised an exception
	#   or called exit() during startup.
	# - SystemCallError, IOError, SocketError: Something went wrong.
	def spawn_application!
		a, b = UNIXSocket.pair
		pid = safe_fork('application', true) do
			begin
				a.close
				
				file_descriptors_to_leave_open = [0, 1, 2, b.fileno]
				NativeSupport.close_all_file_descriptors(file_descriptors_to_leave_open)
				close_all_io_objects_for_fds(file_descriptors_to_leave_open)
				
				channel = MessageChannel.new(b)
				success = report_app_init_status(channel) do
					ENV['RAILS_ENV'] = @environment
					ENV['RACK_ENV'] = @environment
					ENV['RAILS_RELATIVE_URL_ROOT'] = @base_uri
					Dir.chdir(@app_root)
					if @encoded_environment_variables
						set_passed_environment_variables
					end
					if @lower_privilege
						lower_privilege('config/environment.rb', @lowest_user)
					end
					# Make sure RubyGems uses any new environment variable values
					# that have been set now (e.g. $HOME, $GEM_HOME, etc) and that
					# it is able to detect newly installed gems.
					Gem.clear_paths
					setup_bundler_support
					
					require File.expand_path('config/environment')
					require 'dispatcher'
				end
				if success
					start_request_handler(channel, false)
				end
			rescue SignalException => e
				if e.message != AbstractRequestHandler::HARD_TERMINATION_SIGNAL &&
				   e.message != AbstractRequestHandler::SOFT_TERMINATION_SIGNAL
					raise
				end
			end
		end
		b.close
		Process.waitpid(pid) rescue nil
		
		channel = MessageChannel.new(a)
		unmarshal_and_raise_errors(channel, @print_exceptions)
		
		# No exception was raised, so spawning succeeded.
		pid, socket_name, socket_type = channel.read
		if pid.nil?
			raise IOError, "Connection closed"
		end
		owner_pipe = channel.recv_io
		return Application.new(@app_root, pid, socket_name,
			socket_type, owner_pipe)
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
			unmarshal_and_raise_errors(server, @print_exceptions)
		rescue IOError, SystemCallError, SocketError => e
			stop
			raise Error, "The application spawner server exited unexpectedly: #{e}"
		rescue
			stop
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
		report_app_init_status(client) do
			$0 = "Passenger ApplicationSpawner: #{@app_root}"
			ENV['RAILS_ENV'] = @environment
			ENV['RACK_ENV'] = @environment
			ENV['RAILS_RELATIVE_URL_ROOT'] = @base_uri
			if defined?(RAILS_ENV)
				Object.send(:remove_const, :RAILS_ENV)
				Object.const_set(:RAILS_ENV, ENV['RAILS_ENV'])
			end
			Dir.chdir(@app_root)
			if @encoded_environment_variables
				set_passed_environment_variables
			end
			if @lower_privilege
				lower_privilege('config/environment.rb', @lowest_user)
			end
			# Make sure RubyGems uses any new environment variable values
			# that have been set now (e.g. $HOME, $GEM_HOME, etc) and that
			# it is able to detect newly installed gems.
			Gem.clear_paths
			setup_bundler_support
			preload_application
		end
	end
	
private
	def set_passed_environment_variables
		env_vars_string = @encoded_environment_variables.unpack("m").first
		# Prevent empty string as last item from b0rking the Hash[...] statement.
		# See comment in Hooks.cpp (sendHeaders) for details.
		env_vars_string << "_\0_"
		env_vars = Hash[*env_vars_string.split("\0")]
		env_vars.each_pair do |key, value|
			ENV[key] = value
		end
	end
	
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
		if !defined?(Dispatcher)
			require 'dispatcher'
		end
		# Rails 2.2+ uses application_controller.rb while older versions use application.rb.
		begin
			require_dependency 'application_controller'
		rescue LoadError => e
			begin
				require_dependency 'application'
			rescue LoadError
				# Considering that most apps these das are written in Rails
				# 2.2+, if application.rb cannot be loaded either then it
				# probably just means that application_controller.rb threw
				# a LoadError. So we raise the original error here; if the
				# app is based on Rails < 2.2 then the error will make less
				# sense but we can only choose one or the other.
				raise e
			end
		end
		
		# - No point in preloading the application sources if the garbage collector
		#   isn't copy-on-write friendly.
		# - Rails >= 2.2 already preloads application sources by default, so no need
		#   to do that again.
		if GC.copy_on_write_friendly? && !rails_will_preload_app_code?
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
			return defined?(::Rails3)
		end
	end

	def handle_spawn_application
		a, b = UNIXSocket.pair
		safe_fork('application', true) do
			begin
				a.close
				client.close
				start_request_handler(MessageChannel.new(b), true)
			rescue SignalException => e
				if e.message != AbstractRequestHandler::HARD_TERMINATION_SIGNAL &&
				   e.message != AbstractRequestHandler::SOFT_TERMINATION_SIGNAL
					raise
				end
			end
		end
		
		b.close
		worker_channel = MessageChannel.new(a)
		info = worker_channel.read
		owner_pipe = worker_channel.recv_io
		client.write(*info)
		client.send_io(owner_pipe)
	ensure
		a.close if a
		b.close if b && !b.closed?
		owner_pipe.close if owner_pipe
	end
	
	# Initialize the request handler and enter its main loop.
	# Spawn information will be sent back via _channel_.
	# The _forked_ argument indicates whether a new process was forked off
	# after loading environment.rb (i.e. whether smart spawning is being
	# used).
	def start_request_handler(channel, forked)
		$0 = "Rails: #{@app_root}"
		reader, writer = IO.pipe
		begin
			# Clear or re-establish connection if a connection was established
			# in environment.rb. This prevents us from concurrently
			# accessing the same MySQL connection handle.
			if defined?(::ActiveRecord::Base)
				if ::ActiveRecord::Base.respond_to?(:clear_all_connections!)
					::ActiveRecord::Base.clear_all_connections!
				elsif ::ActiveRecord::Base.respond_to?(:clear_active_connections!)
					::ActiveRecord::Base.clear_active_connections!
				elsif ::ActiveRecord::Base.respond_to?(:connected?) &&
				      ::ActiveRecord::Base.connected?
					::ActiveRecord::Base.establish_connection
				end
			end
			
			reader.fcntl(Fcntl::F_SETFD, Fcntl::FD_CLOEXEC)
			
			if Rails::VERSION::STRING >= '2.3.0'
				rack_app = ::ActionController::Dispatcher.new
				handler = Rack::RequestHandler.new(reader, rack_app, @options)
			else
				handler = RequestHandler.new(reader, @options)
			end
			
			channel.write(Process.pid, handler.socket_name,
				handler.socket_type)
			channel.send_io(writer)
			writer.close
			channel.close
			
			PhusionPassenger.call_event(:starting_worker_process, forked)
			handler.main_loop
		ensure
			channel.close rescue nil
			writer.close rescue nil
			handler.cleanup rescue nil
			PhusionPassenger.call_event(:stopping_worker_process)
		end
	end
end

end # module Railz
end # module PhusionPassenger

