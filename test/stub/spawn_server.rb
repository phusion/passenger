#!/usr/bin/env ruby
$LOAD_PATH << "#{File.dirname(__FILE__)}/../../lib"
require 'passenger/spawn_manager'

include Passenger
class SpawnManager
	def handle_spawn_application(app_root, lower_privilege, lowest_user, environment)
		client.write('ok')
		client.write(1234, "/tmp/nonexistant.socket", false)
		client.send_io(STDERR)
	end
end

manager = SpawnManager.new
input = IO.new(Passenger::SpawnManager::DEFAULT_INPUT_FD)
manager.start_synchronously(input)
manager.cleanup
