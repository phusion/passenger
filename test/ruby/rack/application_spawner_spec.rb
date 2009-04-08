require 'support/config'
require 'support/test_helper'
require 'phusion_passenger/rack/application_spawner'

describe PhusionPassenger::Rack::ApplicationSpawner do
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
	
	it "calls the starting_worker_process event after config.ru has been loaded" do
	  File.append("#{@stub.app_root}/config.ru", %q{
			PhusionPassenger.on_event(:starting_worker_process) do
				File.append("rackresult.txt", "worker_process_started\n")
			end
			File.append("rackresult.txt", "end of config.ru\n");
		})
		spawn(@stub.app_root).close
		spawn(@stub.app_root).close
		
		# Give some time for the starting_worker_process hook to be executed.
		sleep 0.2
		
		contents = File.read("#{@stub.app_root}/rackresult.txt")
		contents.should == "end of config.ru\n" +
			"worker_process_started\n" +
			"end of config.ru\n" +
			"worker_process_started\n"
	end
	
	it "calls the stopping_worker_process event" do
	  File.append("#{@stub.app_root}/config.ru", %q{
			PhusionPassenger.on_event(:stopping_worker_process) do
				File.append("rackresult.txt", "worker_process_stopped\n")
			end
			File.append("rackresult.txt", "end of config.ru\n");
		})
		spawn(@stub.app_root).close
		spawn(@stub.app_root).close
		
		# Give some time for the starting_worker_process hook to be executed.
		sleep 0.2
		
		contents = File.read("#{@stub.app_root}/rackresult.txt")
		contents.should == "end of config.ru\n" +
			"worker_process_stopped\n" +
			"end of config.ru\n" +
			"worker_process_stopped\n"
	end	
	
	def spawn(app_root)
		PhusionPassenger::Rack::ApplicationSpawner.spawn_application(app_root,
			"lowest_user" => CONFIG['lowest_user'])
	end
end

