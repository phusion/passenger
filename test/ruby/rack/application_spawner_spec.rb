require 'support/config'
require 'support/test_helper'
require 'passenger/rack/application_spawner'

describe Passenger::Rack::ApplicationSpawner do
	include TestHelper
	
	before :each do
		@stub = setup_stub('rack')
	end
	
	after :each do
		@stub.destroy
	end
	
	it "can spawn our stub application" do
		spawn(@stub.app_root).close
	end
	
	it "propagates exceptions in application startup" do
		File.prepend("#{@stub.app_root}/config.ru", "raise StandardError, 'foo'\n")
		spawn = lambda { spawn(@stub.app_root) }
		spawn.should raise_error(StandardError)
	end
	
	it "lowers privilege to the owner of config.ru" do
		system("chown", "-R", CONFIG['normal_user_1'], @stub.app_root)
		File.prepend("#{@stub.app_root}/config.ru", %q{
			File.new('touch.txt', 'w').close
		})
		spawn(@stub.app_root).close
		config_ru_owner = File.stat("#{@stub.app_root}/config.ru").uid
		touch_txt_owner = File.stat("#{@stub.app_root}/touch.txt").uid
		config_ru_owner.should == touch_txt_owner
	end if Process.euid == 0
	
	def spawn(*args)
		Passenger::Rack::ApplicationSpawner.spawn_application(*args)
	end
end

