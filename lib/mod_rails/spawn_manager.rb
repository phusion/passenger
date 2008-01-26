if __FILE__ == $0
	$LOAD_PATH << "#{File.dirname(__FILE__)}/.."
end
require 'mod_rails/framework_spawner'
require 'mod_rails/application'
require 'mod_rails/message_channel'
require 'mod_rails/core_extensions'
module ModRails # :nodoc:

class SpawnManager
	SPAWNER_CLEAN_INTERVAL = 125
	SPAWNER_MAX_IDLE_TIME = 120

	def initialize
		@previous_signal_handlers = {}
		@spawners = {}
		@lock = Mutex.new
		@cond = ConditionVariable.new
		@cleaner_thread = Thread.new do
			cleaner_thread_main
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

	def spawn_application(app_root, username = nil)
		framework_version = Application.get_framework_version(app_root)
		@lock.synchronize do
			spawner = @spawners[framework_version]
			if !spawner
				spawner = FrameworkSpawner.new(framework_version)
				@spawners[framework_version] = spawner
				spawner.start
			end
		end
		spawner.time = Time.now
		return spawner.spawn_application(app_root, username)
	end
	
	def server_main(unix_socket)
		@done = false
		@channel = MessageChannel.new(unix_socket)
		install_signal_handlers
		begin
			while !@done
				begin
					name, *args = @channel.read
					if name.nil?
						@done = true
					elsif !MESSAGE_HANDLERS.has_key?(name)
						raise StandardError, "Unknown message '#{name}' received."
					else
						name, *args = message
						__send__(MESSAGE_HANDLERS[name], *args)
					end
				rescue ExitNow
					@done = true
				end
			end
		ensure
			revert_signal_handlers
		end
	end

private
	class ExitNow < RuntimeError
	end

	SIGNAL_HANDLERS = {
		'TERM' => :exit_now!,
		'INT'  => :exit_now!,
		'USR1' => :exit_asap,
		'HUP'  => :reload
	}
	MESSAGE_HANDLERS = {
		'spawn_application' => :handle_spawn_application
	}
	
	def install_signal_handlers
		Signal.list.each_key do |signal|
			begin
				prev_handler = trap(signal, 'DEFAULT')
				if prev_handler != 'DEFAULT'
					@previous_signal_handlers[signal] = prev_handler
				end
			rescue ArgumentError
				# Signal cannot be trapped; ignore it.
			end
		end
		SIGNAL_HANDLERS.each_pair do |signal, handler|
			if handler == :ignore
				trap(signal, 'IGNORE')
			else
				trap(signal) do
					__send__(handler)
				end
			end
		end
	end
	
	def revert_signal_handlers
		@previous_signal_handlers.each_pair do |signal, handler|
			trap(signal, handler)
		end
		@previous_signal_handlers = {}
	end

	def handle_spawn_application(app_root, username = nil)
		username = nil if username && username.empty?
		app = spawn_application(app_root, username)
		@channel.write(app.pid)
		@channel.send_io(app.reader)
		@channel.send_io(app.writer)
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
	
	def exit_now!
		raise ExitNow, "Exit requested"
	end
	
	def exit_asap
		@done = true
	end
	
	def reload
		@lock.synchronize do
			@spawners.each_each do |spawner|
				spawner.reload
			end
		end
	end
end

if __FILE__ == $0
	unix_socket = IO.new(ARGV[0].to_i, "a+")
	spawn_manager = SpawnManager.new
	spawn_manager.server_main(unix_socket)
	spawn_manager.cleanup
end

end # module ModRails
