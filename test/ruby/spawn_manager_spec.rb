require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'phusion_passenger/app_process'
require 'phusion_passenger/spawn_manager'

require 'ruby/shared/spawners/spawner_spec'
require 'ruby/shared/spawners/reload_single_spec'
require 'ruby/shared/spawners/reload_all_spec'
require 'ruby/shared/spawners/classic_rails/spawner_spec'
require 'ruby/shared/spawners/classic_rails/lack_of_rails_gem_version_spec'

# TODO: test whether SpawnManager restarts the subspawner if it crashed

module PhusionPassenger

describe SpawnManager do
	include SpawnerSpecHelper
	
	after :each do
		@spawner.cleanup if @spawner
	end
	
	describe_rails_versions('<= 2.3') do
		def spawner
			@spawner ||= SpawnManager.new
		end
		
		def spawn_stub_application(stub, extra_options = {})
			default_options = {
				"app_root"     => stub.app_root,
				"spawn_method" => @spawn_method,
				"default_user" => CONFIG['default_user']
			}
			options = default_options.merge(extra_options)
			app = spawner.spawn_application(options)
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
		
		# def load_nonexistant_framework(extra_options = {})
		# 	# Prevent detect_framework_version from raising VersionNotFound
		# 	AppProcess.should_receive(:detect_framework_version).
		# 		at_least(:once).
		# 		with(an_instance_of(String)).
		# 		and_return("1.9.827")
		# 	stub = register_stub(RailsStub.new("#{rails_version}/empty"))
		# 	File.write(stub.environment_rb) do |content|
		# 		content.sub(/^RAILS_GEM_VERSION = .*$/, "RAILS_GEM_VERSION = '1.9.827'")
		# 	end
		# 	return spawn_stub_application(stub, extra_options)
		# end
		
		describe "smart spawning" do
			before :each do
				@spawn_method = "smart"
			end
			
			it_should_behave_like "a spawner"
			it_should_behave_like "a Rails spawner"
			it_should_behave_like "a Rails spawner that supports #reload(app_group_name)"
			it_should_behave_like "a Rails spawner that supports #reload()"
			include_shared_example_group "a Rails app that lacks RAILS_GEM_VERSION"
		end
		
		describe "smart-lv2 spawning" do
			before :each do
				@spawn_method = "smart-lv2"
			end
			
			it_should_behave_like "a spawner"
			it_should_behave_like "a Rails spawner"
			it_should_behave_like "a Rails spawner that supports #reload(app_group_name)"
			it_should_behave_like "a Rails spawner that supports #reload()"
			include_shared_example_group "a Rails app that lacks RAILS_GEM_VERSION"
		end
		
		describe "conservative spawning" do
			before :each do
				@spawn_method = "conservative"
			end
			
			it_should_behave_like "a spawner"
			it_should_behave_like "a Rails spawner"
			it_should_behave_like "a Rails spawner that supports #reload()"
			include_shared_example_group "a Rails app that lacks RAILS_GEM_VERSION"
		end
	end
	
	describe "Rack" do
		def spawn_some_application(extra_options = {})
			stub = register_stub(RackStub.new("rack"))
			yield stub if block_given?
			
			default_options = {
				"app_root"     => stub.app_root,
				"app_type"     => "rack",
				"spawn_method" => @spawn_method,
				"default_user" => CONFIG['default_user']
			}
			options = default_options.merge(extra_options)
			@spawner ||= SpawnManager.new
			app = @spawner.spawn_application(options)
			return register_app(app)
		end
		
		describe "smart spawning" do
			before :each do
				@spawn_method = "smart"
			end
			
			it_should_behave_like "a spawner"
		end
		
		describe "conservative spawning" do
			before :each do
				@spawn_method = "conservative"
			end
			
			it_should_behave_like "a spawner"
		end
	end
end

end # module PhusionPassenger
