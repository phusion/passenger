require 'passenger/abstract_server'
require 'passenger/framework_spawner'
require 'passenger/application'
require 'passenger/message_channel'
require 'passenger/html_template'
require 'passenger/utils'
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
	SPAWNER_CLEAN_INTERVAL = 30 * 60
	SPAWNER_MAX_IDLE_TIME = 29 * 60
	
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
	# See ApplicationSpawner.new for an explanation of the +lower_privilege+
	# and +lowest_user+ parameters.
	#
	# SpawnManager will internally cache the code of applications, in order to
	# speed up future spawning attempts. This implies that, if you've
	# changed the application's code, you must do one of these things:
	# - Restart this SpawnManager by calling AbstractServer#stop, then AbstractServer#start.
	# - Reload the application by calling reload with the correct app_root argument.
	#
	# Raises:
	# - ArgumentError: +app_root+ doesn't appear to be a valid Ruby on Rails application root.
	# - VersionNotFound: The Ruby on Rails framework version that the given application requires
	#   is not installed.
	# - AbstractServer::ServerError: One of the server processes exited unexpectedly.
	# - FrameworkInitError: The Ruby on Rails framework that the application requires could not be loaded.
	# - AppInitError: The application raised an exception or called exit() during startup.
	def spawn_application(app_root, lower_privilege = true, lowest_user = "nobody")
		options = {}
		framework_version = Application.detect_framework_version(app_root)
		if framework_version.nil?
			options[:vendor] = normalize_path("#{app_root}/vendor/rails")
			key = "vendor:#{options[:vendor]}"
		else
			options[:version] = framework_version
			key = "version:#{options[:version]}"
		end
		spawner = nil
		@lock.synchronize do
			spawner = @spawners[key]
			if !spawner
				spawner = FrameworkSpawner.new(options)
				spawner.start
				@spawners[key] = spawner
			end
		end
		spawner.time = Time.now
		return spawner.spawn_application(app_root, lower_privilege, lowest_user)
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
		@lock.synchronize do
			@spawners.each_value do |spawner|
				spawner.reload(app_root)
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
	def handle_spawn_application(app_root, lower_privilege, lowest_user)
		lower_privilege = lower_privilege == "true"
		app = nil
		if @refresh_gems_cache
			Gem.refresh_all_caches!
			@refresh_gems_cache = false
		end
		begin
			app = spawn_application(app_root, lower_privilege, lowest_user)
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
				# will install probably the gem. So next time an app
				# is launched, we'll refresh the RubyGems cache so
				# that we can detect new gems.
				@refresh_gems_cache = true
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
						if current_time - spawner.time > SPAWNER_MAX_IDLE_TIME
							spawner.stop
							@spawners.delete(key)
						end
					end
				end
			end
		end
	end
	
	def send_error_page(channel, template_name, options = {})
		data = HTMLTemplate.new(template_name, options).result
		channel.write('error_page')
		channel.write_scalar(data)
	end
end

end # module Passenger
