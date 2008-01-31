#!/usr/bin/env ruby
$LOAD_PATH << "#{File.dirname(__FILE__)}/../../lib"
require 'mod_rails/spawn_manager'

include ModRails
class SpawnManager
	def handle_spawn_application(app_root, username = nil)
		r, w = IO.pipe
		@channel.write(1234)
		@channel.send_io(r)
		@channel.send_io(w)
		r.close
		w.close
	end
end

manager = SpawnManager.new
manager.server_main(IO.new(ARGV[0].to_i, "a+"))
manager.cleanup