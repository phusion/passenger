#!/usr/bin/env ruby
$LOAD_PATH << "#{File.dirname(__FILE__)}/../../lib"
require 'mod_rails/spawn_manager'

include ModRails
class SpawnManager
	def handle_spawn_application(app_root, user, group)
		client.write(1234, "/tmp/nonexistant.socket", false)
		client.send_io(STDERR)
	end
end

manager = SpawnManager.new
manager.start_synchronously(IO.new(0))
manager.cleanup
