#!/usr/bin/env ruby
$LOAD_PATH << "#{File.dirname(__FILE__)}/../../lib"
require 'mod_rails/spawn_manager'

include ModRails
class SpawnManager
	def handle_spawn_application(app_root, user, group)
		client.write(1234)
		client.send_io(STDOUT)
	end
end

manager = SpawnManager.new
manager.start_synchronously(IO.new(0))
manager.cleanup
