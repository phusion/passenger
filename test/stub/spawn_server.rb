#!/usr/bin/env ruby
$LOAD_PATH << "#{File.dirname(__FILE__)}/../../lib"
$LOAD_PATH << "#{File.dirname(__FILE__)}/../../ext"
require 'phusion_passenger/spawn_manager'
require 'phusion_passenger/app_process'

include PhusionPassenger
class SpawnManager
	def handle_spawn_application(*options)
		client.write('ok')
		app_process = AppProcess.new('/somewhere', 1234, STDERR,
			:main => ['/tmp/nonexistant.socket', 'unix'])
		app_process.write_to_channel(client)
	end
end

DEFAULT_INPUT_FD = 3

manager = SpawnManager.new
input = IO.new(DEFAULT_INPUT_FD)
manager.start_synchronously(input)
manager.cleanup
