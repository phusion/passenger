require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
require 'phusion_passenger/app_process'
require 'phusion_passenger/railz/framework_spawner'

require 'ruby/shared/abstract_server_spec'
require 'ruby/shared/spawners/spawn_server_spec'
require 'ruby/shared/spawners/spawner_spec'
require 'ruby/shared/spawners/rails/spawner_spec'

describe Railz::ApplicationSpawner do
	include SpawnerSpecHelper
	
	after :each do
		@spawner.stop if @spawner && @spawner.started?
	end
	
	def server
		return spawner
	end
	
	def spawner
		@spawner ||= begin
			stub = register_stub(RailsStub.new('foobar'))
			yield stub if block_given?
			
			framework_version = AppProcess.detect_framework_version(stub.app_root)
			spawner = Railz::ApplicationSpawner.new(
				:version => framework_version,
				"app_root" => stub.app_root)
			spawner.start
			spawner
		end
	end
	
	def spawn_some_application(extra_options = {})
		stub = register_stub(RailsStub.new('foobar'))
		yield stub if block_given?
		
		framework_version = AppProcess.detect_framework_version(stub.app_root)
		default_options = {
			:version      => framework_version,
			"app_root"    => stub.app_root,
			"lowest_user" => CONFIG['lowest_user']
		}
		options = default_options.merge(extra_options)
		@spawner ||= begin
			spawner = Railz::FrameworkSpawner.new(options)
			spawner.start
			spawner
		end
		app = @spawner.spawn_application(options)
		return register_app(app)
	end
	
	it_should_behave_like "an AbstractServer"
	it_should_behave_like "a spawn server"
	it_should_behave_like "a spawner"
	it_should_behave_like "a Rails spawner"
end
