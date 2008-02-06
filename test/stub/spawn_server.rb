#!/usr/bin/env ruby
$LOAD_PATH << "#{File.dirname(__FILE__)}/../../lib"
require 'mod_rails/spawn_manager'

include ModRails
class SpawnManager
	def handle_spawn_application(app_root, user, group)
		@channel.write(1234)
		@channel.send_io(STDOUT)
	end
end

manager = SpawnManager.new
manager.server_main(IO.new(0, "a+"))
manager.cleanup