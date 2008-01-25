require 'mod_rails/framework_spawner'
require 'mod_rails/application'
require 'mod_rails/message_channel'
module ModRails # :nodoc:

class SpawnManager
	def initialize
		@spawners = {}
	end

	def spawn_application(app_root, username = nil)
		framework_version = Application.get_framework_version(app_root)
		spawner = @spawners[framework_version]
		if !spawner
			spawner = FrameworkSpawner.new(framework_version)
			@spawners[framework_version] = spawner
			spawner.start
		end
		return spawner.spawn_application(app_root, username)
	end
	
	def server_main(unix_socket)
		@channel = MessageChannel.new(unix_socket)
		install_signal_handlers
		@done = false
		while !@done
			begin
				name, *args = channel.read
				if name.nil?
					@done = true
				elsif name.???
				else
					name, *args = message
					__send__(MESSAGE_HANDLERS[name], *args)
				end
			end
		end
		@spawners.each_value do |spawner|
			spawner.stop
		end
		@spawners.clear
	end

private
	MESSAGE_HANDLERS = {
		'spawn_application' => :handle_spawn_application
	}

	def handle_spawn_application(app_root, username)
		app = spawn_application(app_root, username)
		@channel.write(app.pid)
		@channel.send_io(app.socket)
		app.socket.close
	end
end

if __FILE__ == $0
	exit(SpawnManager.new.server_main)
end

end # module ModRails