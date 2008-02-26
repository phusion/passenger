$LOAD_PATH << "#{File.dirname(__FILE__)}/../lib"
require 'mod_rails/application_spawner'
require 'abstract_server_spec'
require 'spawner_privilege_lowering_spec'
require 'support/config'
include ModRails

describe ApplicationSpawner do
	before :all do
		ENV['RAILS_ENV'] = 'production'
		@test_app = "stub/railsapp"
		Dir["#{@test_app}/log/*"].each do |file|
			File.chmod(0666, file) rescue nil
		end
		File.chmod(0777, "#{@test_app}/log") rescue nil
	end
	
	before :each do
		@spawner = ApplicationSpawner.new(@test_app)
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
end

if Process.euid == ApplicationSpawner::ROOT_UID
	describe "ApplicationSpawner privilege lowering support" do
		before :all do
			@test_app = "stub/railsapp"
			ENV['RAILS_ENV'] = 'production'
		end
	
		it_should_behave_like "spawner that supports lowering of privileges"
		
		def spawn_app(options = {})
			options = {
				:lower_privilege => true,
				:lowest_user => CONFIG['lowest_user']
			}.merge(options)
			@spawner = ApplicationSpawner.new(@test_app,
				options[:lower_privilege],
				options[:lowest_user])
			@spawner.start
			begin
				app = @spawner.spawn_application
				yield app
			ensure
				app.close
				@spawner.stop
			end
		end
	end
end
