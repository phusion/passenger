#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2008  Phusion
#
#  Phusion Passenger is a trademark of Hongli Lai & Ninh Bui.
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

require 'rubygems'
require 'socket'
require 'etc'
require 'passenger/application'
require 'passenger/abstract_server'
require 'passenger/application'
require 'passenger/railz/request_handler'
require 'passenger/exceptions'
require 'passenger/utils'

module Passenger
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
	
	# An attribute, used internally. This should not be used outside Passenger.
	attr_accessor :time
	# The application root of this spawner.
	attr_reader :app_root

	# +app_root+ is the root directory of this application, i.e. the directory
	# that contains 'app/', 'public/', etc. If given an invalid directory,
	# or a directory that doesn't appear to be a Rails application root directory,
	# then an ArgumentError will be raised.
	#
	# If +lower_privilege+ is true, then ApplicationSpawner will attempt to
	# switch to the user who owns the application's <tt>config/environment.rb</tt>,
	# and to the default group of that user.
	#
	# If that user doesn't exist on the system, or if that user is root,
	# then ApplicationSpawner will attempt to switch to the username given by
	# +lowest_user+ (and to the default group of that user).
	# If +lowest_user+ doesn't exist either, or if switching user failed
	# (because the current process does not have the privilege to do so),
	# then ApplicationSpawner will continue without reporting an error.
	#
	# The +environment+ argument allows one to specify the RAILS_ENV environment to use.
	def initialize(app_root, lower_privilege = true, lowest_user = "nobody", environment = "production")
		super()
		begin
			@app_root = normalize_path(app_root)
		rescue SystemCallError => e
			raise ArgumentError, e.message
		rescue ArgumentError
			raise
		end
		@lower_privilege = lower_privilege
		@lowest_user = lowest_user
		@environment = environment
		self.time = Time.now
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
		pid, socket_name, using_abstract_namespace = server.read
		if pid.nil?
			raise IOError, "Connection closed"
		end
		owner_pipe = server.recv_io
		return Application.new(@app_root, pid, socket_name,
			using_abstract_namespace == "true", owner_pipe)
	rescue SystemCallError, IOError, SocketError => e
		raise Error, "The application spawner server exited unexpectedly"
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
		# Double fork to prevent zombie processes.
		a, b = UNIXSocket.pair
		pid = safe_fork(self.class.to_s) do
			safe_fork('application') do
				begin
					a.close
					channel = MessageChannel.new(b)
					success = report_app_init_status(channel) do
						ENV['RAILS_ENV'] = @environment
						Dir.chdir(@app_root)
						if @lower_privilege
							lower_privilege('config/environment.rb', @lowest_user)
						end
						require 'config/environment'
						require 'dispatcher'
					end
					if success
						start_request_handler(channel)
					end
				rescue SignalException => e
					if e.message != AbstractRequestHandler::HARD_TERMINATION_SIGNAL &&
					   e.message != AbstractRequestHandler::SOFT_TERMINATION_SIGNAL
						raise
					end
				end
			end
		end
		b.close
		Process.waitpid(pid) rescue nil
		
		channel = MessageChannel.new(a)
		unmarshal_and_raise_errors(channel)
		
		# No exception was raised, so spawning succeeded.
		pid, socket_name, using_abstract_namespace = channel.read
		if pid.nil?
			raise IOError, "Connection closed"
		end
		owner_pipe = channel.recv_io
		return Application.new(@app_root, pid, socket_name,
			using_abstract_namespace == "true", owner_pipe)
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
			unmarshal_and_raise_errors(server)
		rescue IOError, SystemCallError, SocketError
			stop
			raise Error, "The application spawner server exited unexpectedly"
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
			if defined?(RAILS_ENV)
				Object.send(:remove_const, :RAILS_ENV)
				Object.const_set(:RAILS_ENV, ENV['RAILS_ENV'])
			end
			Dir.chdir(@app_root)
			if @lower_privilege
				lower_privilege('config/environment.rb', @lowest_user)
			end
			preload_application
		end
	end
	
private
	def preload_application
		Object.const_set(:RAILS_ROOT, @app_root)
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
			require 'config/preinitializer'
		end
		require 'config/environment'
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
		require_dependency 'application'
		if GC.copy_on_write_friendly?
			Dir.glob('app/{models,controllers,helpers}/*.rb').each do |file|
				require_dependency normalize_path(file)
			end
		end
	end

	def handle_spawn_application
		# Double fork to prevent zombie processes.
		pid = safe_fork(self.class.to_s) do
			safe_fork('application') do
				begin
					start_request_handler(client)
				rescue SignalException => e
					if e.message != AbstractRequestHandler::HARD_TERMINATION_SIGNAL &&
					   e.message != AbstractRequestHandler::SOFT_TERMINATION_SIGNAL
						raise
					end
				end
			end
		end
		Process.waitpid(pid)
	end
	
	# Initialize the request handler and enter its main loop.
	# Spawn information will be sent back via _channel_.
	def start_request_handler(channel)
		$0 = "Rails: #{@app_root}"
		reader, writer = IO.pipe
		begin
			# Re-establish connection if a connection was established
			# in environment.rb. This prevents us from concurrently
			# accessing the same MySQL connection handle.
			if defined?(::ActiveRecord::Base) && ::ActiveRecord::Base.connected?
				::ActiveRecord::Base.establish_connection
			end
			
			handler = RequestHandler.new(reader)
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

end # module Railz
end # module Passenger
