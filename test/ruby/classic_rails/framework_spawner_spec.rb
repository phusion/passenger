require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
require 'phusion_passenger/app_process'
require 'phusion_passenger/classic_rails/framework_spawner'

require 'ruby/shared/abstract_server_spec'
require 'ruby/shared/spawners/spawn_server_spec'
require 'ruby/shared/spawners/spawner_spec'
require 'ruby/shared/spawners/reload_single_spec'
require 'ruby/shared/spawners/reload_all_spec'
require 'ruby/shared/spawners/classic_rails/spawner_spec'
require 'ruby/shared/spawners/classic_rails/framework_spawner_spec'

# TODO: test whether FrameworkSpawner restarts ApplicationSpawner if it crashed

module PhusionPassenger

describe ClassicRails::FrameworkSpawner do
	include SpawnerSpecHelper
	
	after :each do
		@spawner.stop if @spawner && @spawner.started?
	end
	
	def server
		return spawner
	end
	
	def spawner
		@spawner ||= begin
			stub = register_stub(RailsStub.new("#{rails_version}/empty"))
			yield stub if block_given?
			
			framework_version = AppProcess.detect_framework_version(stub.app_root)
			spawner = ClassicRails::ApplicationSpawner.new(
				"framework_version" => framework_version,
				"app_root" => stub.app_root)
			spawner.start
			spawner
		end
	end
	
	def spawn_stub_application(stub, extra_options = {})
		framework_version = AppProcess.detect_framework_version(stub.app_root)
		default_options = {
			"framework_version" => framework_version,
			"app_root"     => stub.app_root,
			"default_user" => CONFIG['default_user']
		}
		options = default_options.merge(extra_options)
		@spawner ||= begin
			spawner = ClassicRails::FrameworkSpawner.new(options)
			spawner.start
			spawner
		end
		app = @spawner.spawn_application(options)
		return register_app(app)
	end
	
	def spawn_some_application(extra_options = {})
		stub = register_stub(RailsStub.new("#{rails_version}/empty"))
		yield stub if block_given?
		return spawn_stub_application(stub, extra_options)
	end
	
	def use_some_stub
		RailsStub.use("#{rails_version}/empty") do |stub|
			yield stub
		end
	end
	
	def load_nonexistant_framework(options = {})
		spawner = ClassicRails::FrameworkSpawner.new(options.merge(
			"framework_version" => "1.9.827"))
		begin
			spawner.start
		ensure
			spawner.stop rescue nil
		end
	end
	
	describe_rails_versions('<= 2.3') do
		it_should_behave_like "an AbstractServer"
		it_should_behave_like "a spawn server"
		it_should_behave_like "a spawner"
		it_should_behave_like "a Rails spawner"
		it_should_behave_like "a ClassicRails::FrameworkSpawner"
		it_should_behave_like "a Rails spawner that supports #reload(app_group_name)"
		it_should_behave_like "a Rails spawner that supports #reload()"
	end
end

end # module PhusionPassenger
