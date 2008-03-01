require 'support/config'
require 'passenger/spawn_manager'
require 'passenger/message_channel'
require 'passenger/utils'
require 'abstract_server_spec'
require 'minimal_spawner_spec'
require 'spawner_privilege_lowering_spec'
include Passenger
include Passenger::Utils

describe SpawnManager do
	before :each do
		@manager = SpawnManager.new
	end
	
	after :each do
		@manager.cleanup
	end
	
	describe "AbstractServer-like behavior" do
		before :each do
			@server = @manager
			@server.start
		end
		
		it_should_behave_like "AbstractServer"
	end
	
	it_should_behave_like "a minimal spawner"
	
	it "should not crash on spawning when running asynchronously" do
		app = @manager.spawn_application('stub/railsapp')
		app.close
	end
	
	it "should not crash on spawning when running synchronously" do
		a, b = UNIXSocket.pair
		pid = fork do
			begin
				a.close
				sleep(1) # Give @manager the chance to start.
				channel = MessageChannel.new(b)
				channel.write("spawn_application", "stub/minimal-railsapp", "true", "nobody")
				pid, listen_socket = channel.read
				channel.recv_io.close
				channel.close
			rescue Exception => e
				print_exception("child", e)
			ensure
				exit!
			end
		end
		b.close
		@manager.start_synchronously(a)
		a.close
		Process.waitpid(pid) rescue nil
	end
	
	def spawn_application
		@manager.spawn_application('stub/minimal-railsapp')
	end
end

