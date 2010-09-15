require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
require 'phusion_passenger/wsgi/application_spawner'
require 'phusion_passenger/utils'
require 'fileutils'
require 'tempfile'

module PhusionPassenger

describe WSGI::ApplicationSpawner do
	include Utils
	
	before :each do
		@stub = Stub.new('wsgi')
		File.unlink("#{@stub.app_root}/passenger_wsgi.pyc") rescue nil
	end
	
	after :each do
		@stub.destroy
	end
	
	def spawn(app_root)
		WSGI::ApplicationSpawner.spawn_application(
			"app_root"     => app_root,
			"default_user" => CONFIG['default_user'])
	end
	
	it "can spawn our stub application" do
		spawn(@stub.app_root).close
	end
	
	it "creates a socket in Phusion Passenger's temp directory" do
		begin
			app = spawn(@stub.app_root)
			File.chmod(0700, "#{passenger_tmpdir}/backends")
			Dir["#{passenger_tmpdir}/backends/wsgi.*"].should have(1).item
		ensure
			app.close rescue nil
		end
	end
	
	specify "the backend process deletes its socket upon termination" do
		spawn(@stub.app_root).close
		File.chmod(0700, "#{passenger_tmpdir}/backends")
		eventually do
			Dir["#{passenger_tmpdir}/backends/wsgi.*"].empty?
		end
	end
end

end # module PhusionPassenger
