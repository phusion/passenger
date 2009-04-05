require 'support/config'
require 'support/test_helper'
require 'phusion_passenger/railz/application_spawner'

require 'ruby/rails/minimal_spawner_spec'
require 'ruby/spawn_server_spec'
require 'ruby/rails/spawner_privilege_lowering_spec'
require 'ruby/rails/spawner_error_handling_spec'

include PhusionPassenger
include PhusionPassenger::Railz

describe ApplicationSpawner do
	include TestHelper
	
	before :each do
		@stub = setup_rails_stub('foobar')
		@spawner = ApplicationSpawner.new(@stub.app_root,
			"lowest_user" => CONFIG['lowest_user'])
		@spawner.start
		@server = @spawner
	end
	
	after :each do
		@spawner.stop
		@stub.destroy
	end
	
	it_should_behave_like "a spawn server"
	
	def spawn_arbitrary_application
		@spawner.spawn_application
	end
end

describe ApplicationSpawner do
	include TestHelper
	
	describe "smart spawning" do
		it_should_behave_like "a minimal spawner"
		it_should_behave_like "handling errors in application initialization"
		
		it "calls the starting_worker_process event, with forked=true, after a new worker process has been forked off" do
			use_rails_stub('foobar') do |stub|
				File.append(stub.environment_rb, %q{
					PhusionPassenger.on_event(:starting_worker_process) do |forked|
						File.append("result.txt", "forked = #{forked}\n")
					end
					File.append("result.txt", "end of environment.rb\n");
				})
				
				spawner = ApplicationSpawner.new(stub.app_root,
					"lowest_user" => CONFIG['lowest_user'])
				spawner.start
				begin
					spawner.spawn_application.close
					spawner.spawn_application.close
				ensure
					spawner.stop
				end
				
				# Give some time for the starting_worker_process hook to be executed.
				sleep 0.2
				
				contents = File.read("#{stub.app_root}/result.txt")
				contents.should == "end of environment.rb\n" +
					"forked = true\n" +
					"forked = true\n"
			end
		end
		
		def spawn_stub_application(stub)
			@spawner = ApplicationSpawner.new(stub.app_root,
				"lowest_user" => CONFIG['lowest_user'])
			begin
				@spawner.start
				return @spawner.spawn_application
			ensure
				@spawner.stop rescue nil
			end
		end
	end
	
	describe "conservative spawning" do
		it_should_behave_like "a minimal spawner"
		it_should_behave_like "handling errors in application initialization"
		
		it "calls the starting_worker_process event, with forked=true, after environment.rb has been loaded" do
			use_rails_stub('foobar') do |stub|
				File.append(stub.environment_rb, %q{
					PhusionPassenger.on_event(:starting_worker_process) do |forked|
						File.append("result.txt", "forked = #{forked}\n")
					end
					File.append("result.txt", "end of environment.rb\n");
				})
				spawn_stub_application(stub).close
				spawn_stub_application(stub).close
				
				# Give some time for the starting_worker_process hook to be executed.
				sleep 0.2
				
				contents = File.read("#{stub.app_root}/result.txt")
				contents.should == "end of environment.rb\n" +
					"forked = false\n" +
					"end of environment.rb\n" +
					"forked = false\n"
			end
		end
		
		def spawn_stub_application(stub)
			@spawner = ApplicationSpawner.new(stub.app_root,
				"lowest_user" => CONFIG['lowest_user'])
			return @spawner.spawn_application!
		end
	end
end

Process.euid == ApplicationSpawner::ROOT_UID &&
describe("ApplicationSpawner privilege lowering support") do
	include TestHelper
	
	describe "regular spawning" do
		it_should_behave_like "a spawner that supports lowering of privileges"
	
		def spawn_stub_application(options = {})
			options = {
				"lower_privilege" => true,
				"lowest_user" => CONFIG['lowest_user']
			}.merge(options)
			@spawner = ApplicationSpawner.new(@stub.app_root, options)
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
	
	describe "conservative spawning" do
		it_should_behave_like "a spawner that supports lowering of privileges"
	
		def spawn_stub_application(options = {})
			options = {
				"lower_privilege" => true,
				"lowest_user" => CONFIG['lowest_user']
			}.merge(options)
			@spawner = ApplicationSpawner.new(@stub.app_root, options)
			begin
				app = @spawner.spawn_application!
				yield app
			ensure
				app.close if app
			end
		end
	end
end
