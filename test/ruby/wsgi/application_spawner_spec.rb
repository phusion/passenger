require 'support/config'
require 'support/test_helper'
require 'phusion_passenger/wsgi/application_spawner'
require 'phusion_passenger/utils'
require 'fileutils'
require 'tempfile'

include PhusionPassenger

describe PhusionPassenger::WSGI::ApplicationSpawner do
	include TestHelper
	include PhusionPassenger::Utils
	
	before :each do
		@old_passenger_tmpdir = Utils.passenger_tmpdir
		Utils.passenger_tmpdir = "#{Dir.tmpdir}/wsgi_test.tmp"
		@stub = setup_stub('wsgi')
		File.unlink("#{@stub.app_root}/passenger_wsgi.pyc") rescue nil
	end
	
	after :each do
		@stub.destroy
		FileUtils.chmod_R(0700, Utils.passenger_tmpdir)
		FileUtils.rm_rf(Utils.passenger_tmpdir)
		Utils.passenger_tmpdir = @old_passenger_tmpdir
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
		sleep 0.25 # Give it some time to terminate.
		File.chmod(0700, "#{passenger_tmpdir}/backends")
		Dir["#{passenger_tmpdir}/backends/wsgi_backend.*"].should be_empty
	end
	
	def spawn(app_root)
		PhusionPassenger::WSGI::ApplicationSpawner.spawn_application(app_root,
			true, CONFIG['lowest_user'])
	end
end

