require 'mod_rails/abstract_server'
require 'mod_rails/framework_spawner'
require 'mod_rails/application'
require 'mod_rails/message_channel'
require 'mod_rails/utils'
module ModRails # :nodoc:

class SpawnManager < AbstractServer
	SPAWNER_CLEAN_INTERVAL = 30 * 60
	SPAWNER_MAX_IDLE_TIME = 29 * 60
	
	def initialize
		super()
		@spawners = {}
		@lock = Mutex.new
		@cond = ConditionVariable.new
		@cleaner_thread = Thread.new do
			cleaner_thread_main
		end
		define_message_handler(:spawn_application, :handle_spawn_application)
		define_signal_handler('SIGHUP', :reload)
	end

	def spawn_application(app_root, lower_privilege = true, lowest_user = "nobody")
		framework_version = Application.get_framework_version(app_root)
		spawner = nil
		@lock.synchronize do
			spawner = @spawners[framework_version]
			if !spawner
				spawner = FrameworkSpawner.new(framework_version)
				@spawners[framework_version] = spawner
				spawner.start
			end
		end
		spawner.time = Time.now
		return spawner.spawn_application(app_root, lower_privilege, lowest_user)
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
		send_to_client(app.pid)
		send_io_to_client(app.listen_socket)
		app.close
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
	
	def reload
		@lock.synchronize do
			@spawners.each_each do |spawner|
				spawner.reload
			end
		end
	end
end

end # module ModRails
