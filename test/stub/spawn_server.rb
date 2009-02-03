#!/usr/bin/env ruby
$LOAD_PATH << "#{File.dirname(__FILE__)}/../../lib"
$LOAD_PATH << "#{File.dirname(__FILE__)}/../../ext"
require 'phusion_passenger/spawn_manager'

include PhusionPassenger
class SpawnManager
	def handle_spawn_application(*options)
		client.write('ok')
		client.write(1234, "/tmp/nonexistant.socket", false)
		client.send_io(STDERR)
	end
end

DEFAULT_INPUT_FD = 3

manager = SpawnManager.new
input = IO.new(DEFAULT_INPUT_FD)
manager.start_synchronously(input)
manager.cleanup
