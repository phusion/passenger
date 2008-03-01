require 'support/config'
require 'passenger/application_spawner'
require 'minimal_spawner_spec'
require 'spawn_server_spec'
require 'spawner_privilege_lowering_spec'
include Passenger

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
	
	it_should_behave_like "a minimal spawner"
	it_should_behave_like "a spawn server"
	
	def spawn_application
		@spawner.spawn_application
	end
end

if Process.euid == ApplicationSpawner::ROOT_UID
	describe "ApplicationSpawner privilege lowering support" do
		before :all do
			@test_app = "stub/railsapp"
			ENV['RAILS_ENV'] = 'production'
		end
	
		it_should_behave_like "a spawner that supports lowering of privileges"
		
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
