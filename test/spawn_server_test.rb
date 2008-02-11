$LOAD_PATH << "#{File.dirname(__FILE__)}/../lib"
require 'test/unit'
require 'mod_rails/spawn_manager'
require 'mod_rails/message_channel'

class RequestHandlerTest < Test::Unit::TestCase
	include ModRails
	
	def setup
		@manager = SpawnManager.new
	end
	
	def teardown
		@manager.cleanup
	end

	def test_spawn_doesnt_crash
		app = @manager.spawn_application('stub/railsapp')
		app.close
	end
	
	def test_spawn_through_start_synchronously_doesnt_crash
		a, b = UNIXSocket.pair
		pid = fork do
			a.close
			sleep(1) # Give @manager the chance to start.
			channel = MessageChannel.new(b)
			channel.write("spawn_application", "stub/railsapp", "", "")
			pid = channel.read[0]
			io = channel.recv_io
			io.close
			channel.close
		end
		b.close
		@manager.start_synchronously(a)
		a.close
		Process.waitpid(pid)
	end
end
