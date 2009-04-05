require 'support/config'
require 'support/test_helper'
require 'phusion_passenger/wsgi/application_spawner'
require 'phusion_passenger/utils'
require 'fileutils'
require 'tempfile'

describe PhusionPassenger::WSGI::ApplicationSpawner do
	include TestHelper
	include PhusionPassenger::Utils
	
	before :each do
		@old_instance_temp_dir = ENV['PASSENGER_INSTANCE_TEMP_DIR']
		ENV['PASSENGER_INSTANCE_TEMP_DIR'] = "#{Dir.tmpdir}/wsgi_test.tmp"
		@stub = setup_stub('wsgi')
		File.unlink("#{@stub.app_root}/passenger_wsgi.pyc") rescue nil
	end
	
	after :each do
		@stub.destroy
		FileUtils.chmod_R(0700, ENV['PASSENGER_INSTANCE_TEMP_DIR'])
		FileUtils.rm_rf(ENV['PASSENGER_INSTANCE_TEMP_DIR'])
		if @old_instance_temp_dir
			ENV['PASSENGER_INSTANCE_TEMP_DIR'] = @old_instance_temp_dir
		else
			ENV.delete('PASSENGER_INSTANCE_TEMP_DIR')
		end
	end
	
	it "can spawn our stub application" do
		spawn(@stub.app_root).close
	end
	
	it "creates a socket in Phusion Passenger's temp directory" do
		begin
			app = spawn(@stub.app_root)
			File.chmod(0700, "#{passenger_tmpdir}/backends")
			Dir["#{passenger_tmpdir}/backends/wsgi_backend.*"].should have(1).item
		ensure
			app.close rescue nil
		end
	end
	
	specify "the backend process deletes its socket upon termination" do
		spawn(@stub.app_root).close
		sleep 0.2 # Give it some time to terminate.
		File.chmod(0700, "#{passenger_tmpdir}/backends")
		Dir["#{passenger_tmpdir}/backends/wsgi_backend.*"].should be_empty
	end
	
	def spawn(app_root)
		PhusionPassenger::WSGI::ApplicationSpawner.spawn_application(app_root,
			true, CONFIG['lowest_user'])
	end
end

