require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'phusion_passenger/app_process'
require 'phusion_passenger/spawn_manager'

require 'ruby/abstract_server_spec'
require 'ruby/rails/minimal_spawner_spec'
require 'ruby/rails/spawner_privilege_lowering_spec'
require 'ruby/rails/spawner_error_handling_spec'

# TODO: test whether SpawnManager restarts FrameworkSpawner if it crashed

describe SpawnManager do
	include TestHelper
	
	before :each do
		@manager = SpawnManager.new
		@stub = setup_rails_stub('foobar')
		@stub.use_vendor_rails('minimal')
		@server = @manager
		@server.start
	end
	
	after :each do
		@manager.cleanup
		@stub.destroy
	end
	
	describe "smart spawning" do
		before :each do
			@spawn_method = "smart"
		end
		
		it_should_behave_like "an AbstractServer"
	end
	
	describe "conservative spawning" do
		before :each do
			@spawn_method = "conservative"
		end
		
		it_should_behave_like "an AbstractServer"
	end
	
	def spawn_arbitrary_application
		@manager.spawn_application(@stub.app_root, true, CONFIG['lowest_user'],
			"production", @spawn_method)
	end
end

describe SpawnManager do
	include TestHelper
	
	before :each do
		@manager = SpawnManager.new
		@stub = setup_rails_stub('foobar')
		@stub.use_vendor_rails('minimal')
	end
	
	after :each do
		@manager.cleanup
		@stub.destroy
	end
	
	it "can spawn when the server's not running" do
		app = @manager.spawn_application("app_root" => @stub.app_root,
			"lowest_user" => CONFIG['lowest_user'])
		app.close
	end
	
	it "can spawn when the server's running synchronously" do
		a, b = UNIXSocket.pair
		pid = fork do
			begin
				a.close
				sleep(1) # Give @manager the chance to start.
				channel = MessageChannel.new(b)
				channel.write("spawn_application",
					"app_root", @stub.app_root,
					"lowest_user", CONFIG['lowest_user'])
				channel.read
				app_process = AppProcess.read_from_channel(channel)
				app_process.close
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
	
	it "doesn't crash upon spawning an application that doesn't specify its Rails version" do
		File.write(@stub.environment_rb) do |content|
			content.sub(/^RAILS_GEM_VERSION = .*$/, '')
		end
		@stub.dont_use_vendor_rails
		@manager.spawn_application("app_root" => @stub.app_root,
			"lowest_user" => CONFIG['lowest_user']).close
	end
	
	it "properly reloads applications that do not specify a Rails version" do
		File.write(@stub.environment_rb) do |content|
			content.sub(/^RAILS_GEM_VERSION = .*$/, '')
		end
		@stub.dont_use_vendor_rails
		@manager.reload(@stub.app_root)
		spawners = @manager.instance_eval { @spawners }
		spawners.synchronize do
			spawners.should be_empty
		end
	end
end

describe SpawnManager do
	include TestHelper

	it "can spawn a Rack application" do
		use_stub('rack') do |stub|
			@manager = SpawnManager.new
			begin
				app = @manager.spawn_application(
					"app_root" => stub.app_root,
					"app_type" => "rack",
					"lowest_user" => CONFIG['lowest_user'])
				app.close
			rescue => e
				puts e
				puts e.child_exception.backtrace
			ensure
				@manager.cleanup
			end
		end
	end
end

describe SpawnManager do
	include TestHelper
	
	before :each do
		@spawn_method = "smart"
	end
	
	describe "smart spawning" do
		it_should_behave_like "a minimal spawner"
	end
	
	describe "smart-lv2 spawning" do
		before :each do
			@spawn_method = "smart-lv2"
		end
		
		it_should_behave_like "a minimal spawner"
	end
	
	describe "conservative spawning" do
		before :each do
			@spawn_method = "conservative"
		end
		
		it_should_behave_like "a minimal spawner"
	end
	
	it_should_behave_like "handling errors in application initialization"
	it_should_behave_like "handling errors in framework initialization"
	
	def spawn_stub_application(stub, extra_options = {})
		spawner = SpawnManager.new
		begin
			options = {
				"app_root" => stub.app_root,
				"spawn_method" => @spawn_method,
				"lowest_user" => CONFIG['lowest_user']
			}.merge(extra_options)
			return spawner.spawn_application(options)
		ensure
			spawner.cleanup
		end
	end
	
	def load_nonexistant_framework(extra_options = {})
		# Prevent detect_framework_version from raising VersionNotFound
		AppProcess.should_receive(:detect_framework_version).
			at_least(:once).
			with(an_instance_of(String)).
			and_return("1.9.827")
		File.write(@stub.environment_rb, "RAILS_GEM_VERSION = '1.9.827'")
		@stub.dont_use_vendor_rails
		return spawn_stub_application(@stub, extra_options)
	end
end
