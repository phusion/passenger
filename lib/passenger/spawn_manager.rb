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

require 'passenger/abstract_server'
require 'passenger/constants'
require 'passenger/utils'
module Passenger

# The spawn manager is capable of spawning Ruby on Rails or Rack application
# instances. It acts like a simple fascade for the rest of the spawn manager
# system.
#
# *Note*: SpawnManager may only be started synchronously with
# AbstractServer#start_synchronously. Starting asynchronously has not been
# tested. Don't forget to call cleanup after the server's main loop has
# finished.
#
# == Ruby on Rails optimizations ===
#
# Spawning a Ruby on Rails application is usually slow. But SpawnManager
# will preload and cache Ruby on Rails frameworks, as well as application
# code, so subsequent spawns will be very fast.
#
# Internally, SpawnManager uses Railz::FrameworkSpawner to preload and cache
# Ruby on Rails frameworks. Railz::FrameworkSpawner, in turn, uses
# Railz::ApplicationSpawner to preload and cache application code.
#
# In case you're wondering why the namespace is "Railz" and not "Rails":
# it's to work around an obscure bug in ActiveSupport's Dispatcher.
class SpawnManager < AbstractServer
	include Utils
	
	def initialize
		super()
		@spawners = {}
		@lock = Mutex.new
		@cond = ConditionVariable.new
		@cleaner_thread = Thread.new do
			cleaner_thread_main
		end
		define_message_handler(:spawn_application, :handle_spawn_application)
		define_message_handler(:reload, :handle_reload)
		define_signal_handler('SIGHUP', :reload)
		
		GC.start
		if GC.copy_on_write_friendly?
			# Preload libraries for copy-on-write semantics.
			require 'base64'
			require 'passenger/application'
			require 'passenger/railz/framework_spawner'
			require 'passenger/railz/application_spawner'
			require 'passenger/rack/application_spawner'
			require 'passenger/html_template'
			require 'passenger/platform_info'
			require 'passenger/exceptions'
			
			# Commonly used libraries.
			['mysql', 'sqlite3'].each do |lib|
				require lib
			end
		end
	end
	
	# Spawn a RoR application When successful, an Application object will be
	# returned, which represents the spawned RoR application.
	#
	# See Railz::ApplicationSpawner.new for an explanation of the +lower_privilege+,
	# +lowest_user+ and +environment+ parameters.
	#
	# The +spawn_method+ argument may be one of "smart" or "conservative".
	# When "smart" is specified (the default), SpawnManager will internally cache the
	# code of applications, in order to speed up future spawning attempts. This implies
	# that, if you've changed the application's code, you must do one of these things:
	# - Restart this SpawnManager by calling AbstractServer#stop, then AbstractServer#start.
	# - Reload the application by calling reload with the correct app_root argument.
	# Caching however can be incompatible with some applications.
	#
	# The "conservative" spawning method does not involve any caching at all.
	# Spawning will be slower, but is guaranteed to be compatible with all applications.
	#
	# Raises:
	# - ArgumentError: +app_root+ doesn't appear to be a valid Ruby on Rails application root.
	# - VersionNotFound: The Ruby on Rails framework version that the given application requires
	#   is not installed.
	# - AbstractServer::ServerError: One of the server processes exited unexpectedly.
	# - FrameworkInitError: The Ruby on Rails framework that the application requires could not be loaded.
	# - AppInitError: The application raised an exception or called exit() during startup.
	def spawn_application(app_root, lower_privilege = true, lowest_user = "nobody",
	                      environment = "production", spawn_method = "smart",
	                      app_type = "rails")
		if app_type == "rack"
			if !defined?(Rack::ApplicationSpawner)
				require 'passenger/rack/application_spawner'
			end
			return Rack::ApplicationSpawner.spawn_application(app_root,
				lower_privilege, lowest_user, environment)
		elsif app_type == "wsgi"
			require 'passenger/wsgi/application_spawner'
			return WSGI::ApplicationSpawner.spawn_application(app_root,
				lower_privilege, lowest_user, environment)
		else
			if !defined?(Railz::FrameworkSpawner)
				require 'passenger/application'
				require 'passenger/railz/framework_spawner'
				require 'passenger/railz/application_spawner'
			end
			return spawn_rails_application(app_root, lower_privilege, lowest_user,
				environment, spawn_method)
		end
	end
	
	# Remove the cached application instances at the given application root.
	# If nil is specified as application root, then all cached application
	# instances will be removed, no matter the application root.
	#
	# <b>Long description:</b>
	# Application code might be cached in memory. But once it a while, it will
	# be necessary to reload the code for an application, such as after
	# deploying a new version of the application. This method makes sure that
	# any cached application code is removed, so that the next time an
	# application instance is spawned, the application code will be freshly
	# loaded into memory.
	#
	# Raises AbstractServer::SpawnError if something went wrong.
	def reload(app_root = nil)
		if app_root
			begin
				app_root = normalize_path(app_root)
			rescue ArgumentError
			end
		end
		@lock.synchronize do
			if app_root
				# Delete associated ApplicationSpawner.
				key = "app:#{app_root}"
				spawner = @spawners[key]
				if spawner
					if spawner.started?
						spawner.stop
					end
					@spawners.delete(key)
				end
			end
			@spawners.each_value do |spawner|
				# Reload FrameworkSpawners.
				if spawner.respond_to?(:reload)
					spawner.reload(app_root)
				end
			end
		end
	end
	
	# Cleanup resources. Should be called when this SpawnManager is no longer needed.
	def cleanup
		@lock.synchronize do
			@cond.signal
		end
		@cleaner_thread.join
		@lock.synchronize do
			@spawners.each_value do |spawner|
				if spawner.started?
					spawner.stop
				end
			end
			@spawners.clear
		end
	end

private
	def spawn_rails_application(app_root, lower_privilege, lowest_user,
	                            environment, spawn_method)
		if spawn_method == "smart"
			spawner_must_be_started = true
			framework_version = Application.detect_framework_version(app_root)
			if framework_version.nil? || framework_version == :vendor
				app_root = normalize_path(app_root)
				key = "app:#{app_root}"
				create_spawner = proc do
					Railz::ApplicationSpawner.new(app_root, lower_privilege,
						lowest_user, environment)
				end
			else
				key = "version:#{framework_version}"
				create_spawner = proc do
					Railz::FrameworkSpawner.new(:version => framework_version)
				end
			end
		else
			app_root = normalize_path(app_root)
			key = "app:#{app_root}"
			create_spawner = proc do
				Railz::ApplicationSpawner.new(app_root, lower_privilege, lowest_user, environment)
			end
			spawner_must_be_started = false
		end
		
		spawner = nil
		@lock.synchronize do
			spawner = @spawners[key]
			if !spawner
				spawner = create_spawner.call
				if spawner_must_be_started
					spawner.start
				end
				@spawners[key] = spawner
			end
			spawner.time = Time.now
			begin
				if spawner.is_a?(Railz::FrameworkSpawner)
					return spawner.spawn_application(app_root, lower_privilege,
						lowest_user, environment)
				elsif spawner.started?
					return spawner.spawn_application
				else
					return spawner.spawn_application!
				end
			rescue AbstractServer::ServerError
				if spawner.started?
					spawner.stop
				end
				@spawners.delete(key)
				raise
			end
		end
	end
	
	def handle_spawn_application(app_root, lower_privilege, lowest_user, environment,
	                             spawn_method, app_type)
		lower_privilege = lower_privilege == "true"
		app = nil
		begin
			app = spawn_application(app_root, lower_privilege, lowest_user,
				environment, spawn_method, app_type)
		rescue ArgumentError => e
			send_error_page(client, 'invalid_app_root', :error => e, :app_root => app_root)
		rescue AbstractServer::ServerError => e
			send_error_page(client, 'general_error', :error => e)
		rescue VersionNotFound => e
			send_error_page(client, 'version_not_found', :error => e, :app_root => app_root)
		rescue AppInitError => e
			if database_error?(e)
				send_error_page(client, 'database_error', :error => e,
					:app_root => app_root, :app_name => app_name(app_type),
					:app_type => app_type)
			elsif load_error?(e)
				# A source file failed to load, maybe because of a
				# missing gem. If that's the case then the sysadmin
				# will install probably the gem. So we clear RubyGems's
				# cache so that it can detect new gems.
				Gem.clear_paths
				send_error_page(client, 'load_error', :error => e, :app_root => app_root,
					:app_name => app_name(app_type))
			elsif e.child_exception.nil?
				send_error_page(client, 'app_exited_during_initialization', :error => e,
					:app_root => app_root, :app_name => app_name(app_type))
			else
				send_error_page(client, 'app_init_error', :error => e,
					:app_root => app_root, :app_name => app_name(app_type))
			end
		rescue FrameworkInitError => e
			send_error_page(client, 'framework_init_error', :error => e)
		end
		if app
			begin
				client.write('ok')
				client.write(app.pid, app.listen_socket_name, app.using_abstract_namespace?)
				client.send_io(app.owner_pipe)
			rescue Errno::EPIPE
				# The Apache module may be interrupted during a spawn command,
				# in which case it will close the connection. We ignore this error.
			ensure
				app.close
			end
		end
	end
	
	def handle_reload(app_root)
		reload(app_root)
	end
	
	def cleaner_thread_main
		@lock.synchronize do
			while true
				if @cond.timed_wait(@lock, SPAWNER_CLEAN_INTERVAL)
					break
				else
					current_time = Time.now
					@spawners.keys.each do |key|
						spawner = @spawners[key]
						if spawner.is_a?(Railz::FrameworkSpawner)
							max_idle_time = FRAMEWORK_SPAWNER_MAX_IDLE_TIME
						else
							max_idle_time = APP_SPAWNER_MAX_IDLE_TIME
						end
						if current_time - spawner.time > max_idle_time
							if spawner.started?
								spawner.stop
							end
							@spawners.delete(key)
						end
					end
				end
			end
		end
	end
	
	def send_error_page(channel, template_name, options = {})
		require 'passenger/html_template' unless defined?(HTMLTemplate)
		require 'passenger/platform_info' unless defined?(PlatformInfo)
		options["enterprisey"] = File.exist?("#{File.dirname(__FILE__)}/../../enterprisey.txt") ||
			File.exist?("/etc/passenger_enterprisey.txt")
		data = HTMLTemplate.new(template_name, options).result
		channel.write('error_page')
		channel.write_scalar(data)
	end
	
	def database_error?(e)
		return ( defined?(Mysql::Error) && e.child_exception.is_a?(Mysql::Error) ) ||
		       ( e.child_exception.is_a?(UnknownError) &&
		           (
		               e.child_exception.real_class_name =~ /^ActiveRecord/ ||
		               e.child_exception.real_class_name =~ /^Mysql::/
		           )
		       )
	end
	
	def load_error?(e)
		return e.child_exception.is_a?(LoadError) || (
		           e.child_exception.is_a?(UnknownError) &&
		           e.child_exception.real_class_name == "MissingSourceFile"
		)
	end
	
	def app_name(app_type)
		if app_type == "rails"
			return "Ruby on Rails"
		else
			return "Ruby (Rack)"
		end
	end
end

end # module Passenger
