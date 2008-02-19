$LOAD_PATH << "#{File.dirname(__FILE__)}/../lib"
require 'mod_rails/spawn_manager'
require 'mod_rails/message_channel'
require 'mod_rails/utils'
require 'abstract_server_spec'
include ModRails
include ModRails::Utils

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
				channel.write("spawn_application", "stub/railsapp", "", "")
				pid = channel.read[0]
				io = channel.recv_io
				io.close
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
	
	#it "should be able to spawn a usable application" do
	#	@manager.spawn_
	#end
end

