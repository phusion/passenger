require 'support/config'
require 'support/test_helper'
require 'passenger/application_spawner'

require 'minimal_spawner_spec'
require 'spawn_server_spec'
require 'spawner_privilege_lowering_spec'
require 'spawner_error_handling_spec'

include Passenger

# TODO: write unit test which checks whether setting ENV['RAILS_ENV'] in environment.rb is respected (issue #6)

describe ApplicationSpawner do
	include TestHelper

	before :each do
		ENV['RAILS_ENV'] = 'production'
		@stub = setup_rails_stub('foobar')
		@app_root = @stub.app_root
		@spawner = ApplicationSpawner.new(@app_root)
		@spawner.start
		@server = @spawner
	end
	
	after :each do
		@spawner.stop
		teardown_rails_stub
	end
	
	it_should_behave_like "a minimal spawner"
	it_should_behave_like "a spawn server"
	
	def spawn_arbitrary_application
		@spawner.spawn_application
	end
end

describe ApplicationSpawner do
	include TestHelper
	
	it_should_behave_like "handling errors in application initialization"
	
	def spawn_application(app_root)
		@spawner = ApplicationSpawner.new(app_root)
		begin
			@spawner.start
			return @spawner.spawn_application
		ensure
			@spawner.stop rescue nil
		end
	end
end

if Process.euid == ApplicationSpawner::ROOT_UID
	describe "ApplicationSpawner privilege lowering support" do
		include TestHelper
		
		it_should_behave_like "a spawner that supports lowering of privileges"
		
		def spawn_stub_application(options = {})
			options = {
				:lower_privilege => true,
				:lowest_user => CONFIG['lowest_user']
			}.merge(options)
			@spawner = ApplicationSpawner.new(@stub.app_root,
				options[:lower_privilege],
				options[:lowest_user])
			@spawner.start
			begin
				app = @spawner.spawn_application
				yield app
			ensure
				app.close if app
				@spawner.stop
			end
		end
	end
end
