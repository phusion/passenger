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

require 'passenger/abstract_server'
require 'passenger/framework_spawner'
require 'passenger/application'
require 'passenger/message_channel'
require 'passenger/html_template'
require 'passenger/utils'
require 'passenger/platform_info'
module Passenger

# This class is capable of spawning Ruby on Rails application instances.
# Spawning a Ruby on Rails application is usually slow. But SpawnManager
# will preload and cache Ruby on Rails frameworks, as well as application
# code, so subsequent spawns will be very fast.
#
# Internally, SpawnManager uses FrameworkSpawner to preload and cache
# Ruby on Rails frameworks. FrameworkSpawner, in turn, uses
# ApplicationSpawner to preload and cache application code.
#
# *Note*: SpawnManager may only be started synchronously with
# AbstractServer#start_synchronously. Starting asynchronously has not been
# tested. Don't forget to call cleanup after the server's main loop has
# finished.
class SpawnManager < AbstractServer
	DEFAULT_INPUT_FD = 3
	FRAMEWORK_SPAWNER_MAX_IDLE_TIME = 30 * 60
	APP_SPAWNER_MAX_IDLE_TIME = FrameworkSpawner::APP_SPAWNER_MAX_IDLE_TIME
	SPAWNER_CLEAN_INTERVAL = [FRAMEWORK_SPAWNER_MAX_IDLE_TIME,
		APP_SPAWNER_MAX_IDLE_TIME].min + 5
	
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
	end

	# Spawn a RoR application When successful, an Application object will be
	# returned, which represents the spawned RoR application.
	#
	# See ApplicationSpawner.new for an explanation of the +lower_privilege+,
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
	                      environment = "production", spawn_method = "smart")
		if spawn_method == "smart"
			framework_version = Application.detect_framework_version(app_root)
			if framework_version == :vendor
				vendor_path = normalize_path("#{app_root}/vendor/rails")
				key = "vendor:#{vendor_path}"
				create_spawner = proc do
					FrameworkSpawner.new(:vendor => vendor_path)
				end
			elsif framework_version.nil?
				app_root = normalize_path(app_root)
				key = "app:#{app_root}"
				create_spawner = proc do
					ApplicationSpawner.new(app_root, lower_privilege, lowest_user, environment)
				end
			else
				key = "version:#{framework_version}"
				create_spawner = proc do
					FrameworkSpawner.new(:version => framework_version)
				end
			end
		else
			app_root = normalize_path(app_root)
			key = "app:#{app_root}"
			create_spawner = proc do
				ApplicationSpawner.new(app_root, lower_privilege, lowest_user, environment)
			end
		end
		
		spawner = nil
		@lock.synchronize do
			spawner = @spawners[key]
			if !spawner
				spawner = create_spawner.call
				spawner.start
				@spawners[key] = spawner
			end
			spawner.time = Time.now
			begin
				if spawner.is_a?(FrameworkSpawner)
					return spawner.spawn_application(app_root, lower_privilege,
						lowest_user, environment)
				elsif spawn_method == "smart"
					return spawner.spawn_application
				else
					return spawner.spawn_application!
				end
			rescue AbstractServer::ServerError
				spawner.stop
				@spawners.delete(key)
				raise
			end
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
					spawner.stop
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
				spawner.stop
			end
			@spawners.clear
		end
	end

private
	def handle_spawn_application(app_root, lower_privilege, lowest_user, environment, spawn_method)
		lower_privilege = lower_privilege == "true"
		app = nil
		begin
			app = spawn_application(app_root, lower_privilege, lowest_user,
				environment, spawn_method)
		rescue ArgumentError => e
			send_error_page(client, 'invalid_app_root', :error => e, :app_root => app_root)
		rescue AbstractServer::ServerError => e
			send_error_page(client, 'general_error', :error => e)
		rescue VersionNotFound => e
			send_error_page(client, 'version_not_found', :error => e, :app_root => app_root)
		rescue AppInitError => e
			if (defined?(Mysql::Error) && e.child_exception.is_a?(Mysql::Error)) ||
			   (e.child_exception.is_a?(UnknownError) && e.child_exception.real_class_name =~ /^ActiveRecord/)
				send_error_page(client, 'database_error', :error => e,
					:app_root => app_root)
			elsif e.child_exception.is_a?(LoadError) ||
			   (e.child_exception.is_a?(UnknownError) && e.child_exception.real_class_name == "MissingSourceFile")
				# A source file failed to load, maybe because of a
				# missing gem. If that's the case then the sysadmin
				# will install probably the gem. So we clear RubyGems's
				# cache so that it can detect new gems.
				Gem.clear_paths
				send_error_page(client, 'load_error', :error => e, :app_root => app_root)
			elsif e.child_exception.nil?
				send_error_page(client, 'app_exited_during_initialization', :error => e,
					:app_root => app_root)
			else
				send_error_page(client, 'app_init_error', :error => e,
					:app_root => app_root)
			end
		rescue FrameworkInitError => e
			send_error_page(client, 'framework_init_error', :error => e)
		end
		if app
			client.write('ok')
			client.write(app.pid, app.listen_socket_name, app.using_abstract_namespace?)
			client.send_io(app.owner_pipe)
			app.close
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
						if spawner.is_a?(FrameworkSpawner)
							max_idle_time = FRAMEWORK_SPAWNER_MAX_IDLE_TIME
						else
							max_idle_time = APP_SPAWNER_MAX_IDLE_TIME
						end
						if current_time - spawner.time > max_idle_time
							spawner.stop
							@spawners.delete(key)
						end
					end
				end
			end
		end
	end
	
	def send_error_page(channel, template_name, options = {})
		options["enterprisey"] = File.exist?("#{File.dirname(__FILE__)}/../../enterprisey.txt")
		data = HTMLTemplate.new(template_name, options).result
		channel.write('error_page')
		channel.write_scalar(data)
	end
end

end # module Passenger
