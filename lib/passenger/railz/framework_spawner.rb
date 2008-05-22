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
require 'passenger/abstract_server'
require 'passenger/railz/application_spawner'
require 'passenger/exceptions'
require 'passenger/constants'
require 'passenger/utils'
module Passenger
module Railz

# This class is capable of spawning Ruby on Rails application instances
# quickly. This is done by preloading the Ruby on Rails framework into memory,
# before spawning the application instances.
#
# A single FrameworkSpawner instance can only hold a single Ruby on Rails
# framework version. So be careful when using FrameworkSpawner: the applications
# that you spawn through it must require the same RoR version. To handle multiple
# RoR versions, use multiple FrameworkSpawner instances.
#
# FrameworkSpawner uses ApplicationSpawner internally.
#
# *Note*: FrameworkSpawner may only be started asynchronously with AbstractServer#start.
# Starting it synchronously with AbstractServer#start_synchronously has not been tested.
class FrameworkSpawner < AbstractServer
	include Utils
	
	# This exception means that the FrameworkSpawner server process exited unexpectedly.
	class Error < AbstractServer::ServerError
	end
	
	# An attribute, used internally. This should not be used outside Passenger.
	attr_accessor :time

	# Creates a new instance of FrameworkSpawner.
	#
	# Valid options:
	# - <tt>:version</tt>: The Ruby on Rails version to use. It is not checked whether
	#   this version is actually installed.
	# - <tt>:vendor</tt>: The directory to the vendor Rails framework to use. This is
	#   usually something like "/webapps/foo/vendor/rails".
	#
	# It is not allowed to specify both +version+ and +vendor+.
	#
	# Note that the specified Rails framework will be loaded during the entire life time
	# of the FrameworkSpawner server. If you wish to reload the Rails framework's code,
	# then restart the server by calling AbstractServer#stop and AbstractServer#start.
	def initialize(options = {})
		if !options.respond_to?(:'[]')
			raise ArgumentError, "The 'options' argument not seem to be an options hash"
		end
		@version = options[:version]
		@vendor = options[:vendor]
		if !@version && !@vendor
			raise ArgumentError, "Either the 'version' or the 'vendor' option must specified"
		elsif @version && @vendor
			raise ArgumentError, "It is not allowed to specify both the 'version' and the 'vendor' options"
		end
		
		super()
		define_message_handler(:spawn_application, :handle_spawn_application)
		define_message_handler(:reload, :handle_reload)
	end
	
	# Overrided from AbstractServer#start.
	#
	# May raise these additional exceptions:
	# - FrameworkInitError: The specified Ruby on Rails framework could not be loaded.
	# - FrameworkSpawner::Error: The FrameworkSpawner server exited unexpectedly.
	def start
		super
		begin
			status = server.read[0]
			if status == 'exception'
				child_exception = unmarshal_exception(server.read_scalar)
				stop
				if @version
					message = "Could not load Ruby on Rails framework version #{@version}: " <<
						"#{child_exception.class} (#{child_exception.message})"
				else
					message = "Could not load Ruby on Rails framework at '#{@vendor}': " <<
						"#{child_exception.class} (#{child_exception.message})"
				end
				options = { :vendor => @vendor, :version => @version }
				raise FrameworkInitError.new(message, child_exception, options)
			end
		rescue IOError, SystemCallError, SocketError
			stop
			raise Error, "The framework spawner server exited unexpectedly"
		end
	end
	
	# Spawn a RoR application using the Ruby on Rails framework
	# version associated with this FrameworkSpawner.
	# When successful, an Application object will be returned, which represents
	# the spawned RoR application.
	#
	# See ApplicationSpawner.new for an explanation of the +lower_privilege+,
	# +lowest_user+ and +environment+ parameters.
	#
	# FrameworkSpawner will internally cache the code of applications, in order to
	# speed up future spawning attempts. This implies that, if you've changed
	# the application's code, you must do one of these things:
	# - Restart this FrameworkSpawner by calling AbstractServer#stop, then AbstractServer#start.
	# - Reload the application by calling reload with the correct app_root argument.
	#
	# Raises:
	# - AbstractServer::ServerNotStarted: The FrameworkSpawner server hasn't already been started.
	# - ArgumentError: +app_root+ doesn't appear to be a valid Ruby on Rails application root.
	# - AppInitError: The application raised an exception or called exit() during startup.
	# - ApplicationSpawner::Error: The ApplicationSpawner server exited unexpectedly.
	# - FrameworkSpawner::Error: The FrameworkSpawner server exited unexpectedly.
	def spawn_application(app_root, lower_privilege = true, lowest_user = "nobody", environment = "production")
		app_root = normalize_path(app_root)
		assert_valid_app_root(app_root)
		exception_to_propagate = nil
		begin
			server.write("spawn_application", app_root, lower_privilege, lowest_user, environment)
			result = server.read
			if result.nil?
				raise IOError, "Connection closed"
			end
			if result[0] == 'exception'
				raise unmarshal_exception(server.read_scalar)
			else
				pid, listen_socket_name, using_abstract_namespace = server.read
				if pid.nil?
					raise IOError, "Connection closed"
				end
				owner_pipe = server.recv_io
				return Application.new(app_root, pid, listen_socket_name,
					using_abstract_namespace == "true", owner_pipe)
			end
		rescue SystemCallError, IOError, SocketError => e
			raise Error, "The framework spawner server exited unexpectedly"
		end
	end
	
	# Remove the cached application instances at the given application root.
	# If nil is specified as application root, then all cached application
	# instances will be removed, no matter the application root.
	#
	# <b>Long description:</b>
	# Application code might be cached in memory by a FrameworkSpawner. But
	# once it a while, it will be necessary to reload the code for an
	# application, such as after deploying a new version of the application.
	# This method makes sure that any cached application code is removed, so
	# that the next time an application instance is spawned, the application
	# code will be freshly loaded into memory.
	#
	# Raises:
	# - ArgumentError: +app_root+ doesn't appear to be a valid Ruby on Rails
	#   application root.
	# - FrameworkSpawner::Error: The FrameworkSpawner server exited unexpectedly.
	def reload(app_root = nil)
		if app_root.nil?
			server.write("reload")
		else
			server.write("reload", normalize_path(app_root))
		end
	rescue SystemCallError, IOError, SocketError
		raise Error, "The framework spawner server exited unexpectedly"
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
		$0 = "Passenger FrameworkSpawner: #{@version || @vendor}"
		@spawners = {}
		@spawners_lock = Mutex.new
		@spawners_cond = ConditionVariable.new
		@spawners_cleaner = Thread.new do
			begin
				spawners_cleaner_main_loop
			rescue Exception => e
				print_exception(self.class.to_s, e)
			end
		end
		begin
			preload_rails
		rescue StandardError, ScriptError, NoMemoryError => e
			client.write('exception')
			client.write_scalar(marshal_exception(e))
			return
		end
		client.write('success')
	end
	
	# Overrided method.
	def finalize_server # :nodoc:
		@spawners_lock.synchronize do
			@spawners_cond.signal
		end
		@spawners_cleaner.join
		@spawners.each_value do |spawner|
			spawner.stop
		end
	end

private
	def preload_rails
		Object.const_set(:RAILS_ROOT, ".")
		if @version
			gem 'rails', "=#{@version}"
			require 'initializer'
		else
			$LOAD_PATH.unshift("#{@vendor}/railties/builtin/rails_info")
			Dir["#{@vendor}/*"].each do |entry|
				next unless File.directory?(entry)
				$LOAD_PATH.unshift("#{entry}/lib")
			end
			require "#{@vendor}/railties/lib/initializer"
		end
		require 'active_support'
		require 'active_record'
		require 'action_controller'
		require 'action_view'
		require 'action_pack'
		require 'action_mailer'
		require 'dispatcher'
		begin
			if ::Rails::VERSION::MAJOR >= 2
				require 'active_resource'
			else
				require 'action_web_service'
			end
			require 'ruby_version_check'
			require 'active_support/whiny_nil'
		rescue NameError
			# Rails < 1.1
			require 'action_web_service'
		end
		Object.send(:remove_const, :RAILS_ROOT)
	end

	def handle_spawn_application(app_root, lower_privilege, lowest_user, environment)
		lower_privilege = lower_privilege == "true"
		@spawners_lock.synchronize do
			spawner = @spawners[app_root]
			if spawner.nil?
				begin
					spawner = ApplicationSpawner.new(app_root,
						lower_privilege, lowest_user,
						environment)
					spawner.start
				rescue ArgumentError, AppInitError, ApplicationSpawner::Error => e
					client.write('exception')
					client.write_scalar(marshal_exception(e))
					if e.child_exception.is_a?(LoadError)
						# A source file failed to load, maybe because of a
						# missing gem. If that's the case then the sysadmin
						# will install probably the gem. So we clear RubyGems's
						# cache so that it can detect new gems.
						Gem.clear_paths
					end
					return
				end
				@spawners[app_root] = spawner
			end
			spawner.time = Time.now
			begin
				app = spawner.spawn_application
			rescue ApplicationSpawner::Error => e
				spawner.stop
				@spawners.delete(app_root)
				client.write('exception')
				client.write_scalar(marshal_exception(e))
				return
			end
			client.write('success')
			client.write(app.pid, app.listen_socket_name, app.using_abstract_namespace?)
			client.send_io(app.owner_pipe)
			app.close
		end
	end
	
	def handle_reload(app_root = nil)
		@spawners_lock.synchronize do
			if app_root.nil?
				@spawners.each_value do |spawner|
					spawner.stop
				end
				@spawners.clear
			else
				spawner = @spawners[app_root]
				if spawner
					spawner.stop
					@spawners.delete(app_root)
				end
			end
		end
	end
	
	# The main loop for the spawners cleaner thread.
	# This thread checks the spawners list every APP_SPAWNER_CLEAN_INTERVAL seconds,
	# and stops application spawners that have been idle for more than
	# APP_SPAWNER_MAX_IDLE_TIME seconds.
	def spawners_cleaner_main_loop
		@spawners_lock.synchronize do
			while true
				if @spawners_cond.timed_wait(@spawners_lock, APP_SPAWNER_CLEAN_INTERVAL)
					break
				else
					current_time = Time.now
					@spawners.keys.each do |key|
						spawner = @spawners[key]
						if current_time - spawner.time > APP_SPAWNER_MAX_IDLE_TIME
							spawner.stop
							@spawners.delete(key)
						end
					end
				end
			end
		end
	end
end

end # module Railz
end # module Passenger
