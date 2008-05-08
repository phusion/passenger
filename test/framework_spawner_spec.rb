require 'support/config'
require 'support/test_helper'

require 'minimal_spawner_spec'
require 'spawn_server_spec'
require 'spawner_privilege_lowering_spec'
require 'spawner_error_handling_spec'
include Passenger

# TODO: test whether FrameworkSpawner restarts ApplicationSpawner if it crashed

describe FrameworkSpawner do
	include TestHelper
	
	before :each do
		@stub = setup_rails_stub('foobar')
		if use_vendor_rails?
			@stub.use_vendor_rails('minimal')
			@spawner = FrameworkSpawner.new(:vendor => "#{@stub.app_root}/vendor/rails")
		else
			version = Application.detect_framework_version(@stub.app_root)
			@spawner = FrameworkSpawner.new(:version => version)
		end
		@spawner.start
		@server = @spawner
	end
	
	after :each do
		@spawner.stop
		@stub.destroy
	end
	
	describe "situations in which Rails is loaded via the gem" do
		def use_vendor_rails?
			false
		end
		
		it_should_behave_like "a spawn server"
	end
	
	describe "situations in which Rails is loaded via vendor folder" do
		def use_vendor_rails?
			true
		end
		
		it_should_behave_like "a spawn server"
	end
	
	def spawn_arbitrary_application
		@spawner.spawn_application(@stub.app_root)
	end
end

describe FrameworkSpawner do
	include TestHelper
	
	describe "situations in which Rails is loaded via the gem" do
		def use_vendor_rails?
			false
		end
		
		it_should_behave_like "a minimal spawner"
		it_should_behave_like "handling errors in application initialization"
		it_should_behave_like "handling errors in framework initialization"
	end
	
	describe "situations in which Rails is loaded via vendor folder" do
		def use_vendor_rails?
			true
		end
		
		it_should_behave_like "a minimal spawner"
		it_should_behave_like "handling errors in framework initialization"
	end
	
	def spawn_stub_application(stub)
		if use_vendor_rails?
			stub.use_vendor_rails('minimal')
		end
		version = Application.detect_framework_version(stub.app_root)
		if version == :vendor
			options = { :vendor => "#{stub.app_root}/vendor/rails" }
		else
			options = { :version => version }
		end
		spawner = FrameworkSpawner.new(options)
		spawner.start
		begin
			return spawner.spawn_application(stub.app_root)
		ensure
			spawner.stop
		end
	end
	
	def load_nonexistant_framework
		spawner = FrameworkSpawner.new(:version => "1.9.827")
		begin
			spawner.start
		ensure
			spawner.stop rescue nil
		end
	end
end

Process.euid == ApplicationSpawner::ROOT_UID &&
describe("FrameworkSpawner privilege lowering support") do
	include TestHelper
	
	it_should_behave_like "a spawner that supports lowering of privileges"
	
	def spawn_stub_application(options = {})
		options = {
			:lower_privilege => true,
			:lowest_user => CONFIG['lowest_user']
		}.merge(options)
		@stub.use_vendor_rails('minimal')
		@spawner = FrameworkSpawner.new(:vendor =>
			"#{@stub.app_root}/vendor/rails")
		@spawner.start
		begin
			app = @spawner.spawn_application(@stub.app_root,
				options[:lower_privilege],
				options[:lowest_user])
			yield app
		ensure
			app.close if app
			@spawner.stop
		end
	end
end
