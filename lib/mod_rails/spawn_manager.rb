require 'mod_rails/abstract_server'
require 'mod_rails/framework_spawner'
require 'mod_rails/application'
require 'mod_rails/message_channel'
require 'mod_rails/utils'
module ModRails # :nodoc:

class SpawnManager < AbstractServer
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

	def spawn_application(app_root, lower_privilege = true, lowest_user = "nobody")
		options = {}
		framework_version = Application.detect_framework_version(app_root)
		if framework_version.nil?
			options[:vendor] = normalize_path("#{app_root}/vendor/rails")
		else
			options[:version] = framework_version
		end
		spawner = nil
		@lock.synchronize do
			spawner = @spawners[options]
			if !spawner
				spawner = FrameworkSpawner.new(options)
				spawner.file_descriptors_to_close = []
				@spawners.each_value do |s|
					spawner.file_descriptors_to_close << s.server.fileno
				end
				@spawners[options] = spawner
				spawner.start
			end
		end
		spawner.time = Time.now
		return spawner.spawn_application(app_root, lower_privilege, lowest_user)
	end
	
	# Remove the cached application instances at the given application root.
	# If nil is specified as application root, then all cached application
	# instances will be removed, no matter the application root.
	#
	# _Long description:_
	# Application code might be cached in memory. But once it a while, it will
	# be necessary to reload the code for an application, such as after
	# deploying a new version of the application. This method makes sure that
	# any cached application code is removed, so that the next time an
	# application instance is spawned, the application code will be freshly
	# loaded into memory.
	#
	# Raises IOError if something went wrong.
	def reload(app_root = nil)
		@lock.synchronize do
			@spawners.each_value do |spawner|
				spawner.reload(app_root)
			end
		end
	end
	
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
		app = spawn_application(app_root, lower_privilege, lowest_user)
		client.write(app.pid, app.listen_socket_name, app.using_abstract_namespace?)
		client.send_io(app.owner_pipe)
		app.close
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
end

end # module ModRails
