require 'support/config'
require 'passenger/spawn_manager'
require 'passenger/message_channel'
require 'passenger/utils'
require 'abstract_server_spec'
require 'minimal_spawner_spec'
require 'spawner_privilege_lowering_spec'
require 'spawner_error_handling_spec'
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
				channel.read
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
	
	it "should not crash upon spawning an application that doesn't specify its Rails version" do
		@manager.spawn_application('stub/railsapp-without-version-spec').close
	end
	
	it "should properly reload applications that do not specify a Rails version" do
		@manager.spawn_application('stub/railsapp-without-version-spec').close
		@manager.reload('stub/railsapp-without-version-spec')
		spawners = @manager.instance_eval { @spawners }
		spawners.should be_empty
	end
	
	def spawn_application
		@manager.spawn_application('stub/minimal-railsapp')
	end
end

describe SpawnManager do
	it_should_behave_like "handling errors in application initialization"
	it_should_behave_like "handling errors in framework initialization"
	
	def spawn_application(app_root)
		spawner = SpawnManager.new
		begin
			return spawner.spawn_application(app_root)
		ensure
			spawner.cleanup
		end
	end
	
	def load_nonexistant_framework
		Application.instance_eval do
			alias orig_detect_framework_version detect_framework_version
			def detect_framework_version(app_root)
				return "1.9.827"
			end
		end
		begin
			return spawn_application('stub/broken-railsapp4')
		ensure
			Application.instance_eval do
				alias detect_framework_version orig_detect_framework_version
			end
		end
	end
end
