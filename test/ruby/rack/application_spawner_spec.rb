require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
require 'phusion_passenger/rack/application_spawner'

require 'ruby/shared/spawners/spawner_spec'

describe Rack::ApplicationSpawner do
	include SpawnerSpecHelper
	
	def spawn_some_application(extra_options = {})
		stub = register_stub(RackStub.new('rack'))
		yield stub if block_given?
		
		defaults = {
			"app_root"    => stub.app_root,
			"lowest_user" => CONFIG['lowest_user']
		}
		options = defaults.merge(extra_options)
		app = Rack::ApplicationSpawner.spawn_application(options)
		return register_app(app)
	end
	
	it_should_behave_like "a spawner"
end
