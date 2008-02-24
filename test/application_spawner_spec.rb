$LOAD_PATH << "#{File.dirname(__FILE__)}/../lib"
require 'mod_rails/application_spawner'
require 'abstract_server_spec'
include ModRails

describe ApplicationSpawner do
	before :each do
		@spawner = ApplicationSpawner.new('stub/railsapp')
		@spawner.start
		@server = @spawner
	end
	
	after :each do
		@spawner.stop
	end
	
	it_should_behave_like "AbstractServer"
	
	it "should be able to spawn our stub application" do
		app = @spawner.spawn_application
		app.pid.should_not == 0
		app.app_root.should_not be_nil
		app.close
	end
	
	it "should be able to spawn an arbitary number of applications" do
		last_pid = 0
		4.times do
			app = @spawner.spawn_application
			app.pid.should_not == last_pid
			app.app_root.should_not be_nil
			last_pid = app.pid
			app.close
		end
	end
	
	it "should raise a SpawnError if something went wrong" do
		pid = @spawner.instance_eval { @pid }
		Process.kill('SIGABRT', pid)
		spawning = lambda { @spawner.spawn_application }
		spawning.should raise_error(ApplicationSpawner::SpawnError)
	end
	
	it "should work correctly after a restart, if something went wrong" do
		pid = @spawner.instance_eval { @pid }
		Process.kill('SIGABRT', pid)
		spawning = lambda { @spawner.spawn_application }
		spawning.should raise_error(ApplicationSpawner::SpawnError)
		
		@spawner.stop
		@spawner.start
		app = @spawner.spawn_application
		app.pid.should_not == 0
		app.app_root.should_not be_nil
		app.close
	end
	
	it "should be able to spawn an application as a different user" do
		violated "not implemented yet"
	end
	
	it "should be able to spawn an application as a different group" do
		violated "not implemented yet"
	end
	
	it "should be able to spawn an application as a different user and group" do
		violated "not implemented yet"
	end
end
