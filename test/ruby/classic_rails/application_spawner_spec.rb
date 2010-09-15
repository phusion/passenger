require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
require 'phusion_passenger/classic_rails/application_spawner'

require 'ruby/shared/abstract_server_spec'
require 'ruby/shared/spawners/spawn_server_spec'
require 'ruby/shared/spawners/spawner_spec'
require 'ruby/shared/spawners/preloading_spawner_spec'
require 'ruby/shared/spawners/non_preloading_spawner_spec'
require 'ruby/shared/spawners/classic_rails/spawner_spec'
require 'ruby/shared/spawners/classic_rails/lack_of_rails_gem_version_spec'
require 'ruby/shared/rails/analytics_logging_extensions_spec'

module PhusionPassenger

describe ClassicRails::ApplicationSpawner do
	include SpawnerSpecHelper
	
	after :each do
		@spawner.stop if @spawner && @spawner.started?
	end
	
	describe "conservative spawning" do
		def spawn_some_application(extra_options = {})
			stub = register_stub(RailsStub.new("#{rails_version}/empty"))
			yield stub if block_given?
			
			default_options = {
				"app_root"     => stub.app_root,
				"default_user" => CONFIG['default_user']
			}
			options = default_options.merge(extra_options)
			app = ClassicRails::ApplicationSpawner.spawn_application(options)
			return register_app(app)
		end
		
		describe_rails_versions('<= 2.3') do
			it_should_behave_like "a spawner"
			it_should_behave_like "a spawner that does not preload app code"
			it_should_behave_like "a Rails spawner"
			include_shared_example_group "a Rails app that lacks RAILS_GEM_VERSION"
			include_shared_example_group "analytics logging extensions for Rails"
		end
	end
	
	describe "smart spawning" do
		def server
			return spawner
		end
		
		def spawner
			@spawner ||= begin
				stub = register_stub(RailsStub.new("#{rails_version}/empty"))
				spawner = ClassicRails::ApplicationSpawner.new("app_root" => stub.app_root)
				spawner.start
				spawner
			end
		end
		
		def spawn_some_application(extra_options = {})
			stub = register_stub(RailsStub.new("#{rails_version}/empty"))
			yield stub if block_given?
			
			default_options = {
				"app_root"     => stub.app_root,
				"default_user" => CONFIG['default_user']
			}
			options = default_options.merge(extra_options)
			@spawner ||= begin
				spawner = ClassicRails::ApplicationSpawner.new(options)
				spawner.start
				spawner
			end
			app = @spawner.spawn_application(options)
			return register_app(app)
		end
		
		describe_rails_versions('<= 2.3') do
			it_should_behave_like "an AbstractServer"
			it_should_behave_like "a spawn server"
			it_should_behave_like "a spawner"
			it_should_behave_like "a spawner that preloads app code"
			it_should_behave_like "a Rails spawner"
			include_shared_example_group "a Rails app that lacks RAILS_GEM_VERSION"
			include_shared_example_group "analytics logging extensions for Rails"
		end
	end
end

end # module PhusionPassenger
