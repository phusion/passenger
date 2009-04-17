require 'support/config'
require 'support/test_helper'
require 'support/nginx_controller'

require 'integration_tests/mycook_spec'
require 'integration_tests/hello_world_rack_spec'
require 'integration_tests/hello_world_wsgi_spec'

describe "Phusion Passenger for Nginx" do
	include TestHelper
	
	before :all do
		if !CONFIG['nginx']
			STDERR.puts "*** ERROR: You must set the 'nginx' config option in test/config.yml."
			exit!(1)
		end
		
		check_hosts_configuration
		@nginx = NginxController.new("tmp.nginx")
		if Process.uid == 0
			@nginx.set(
				:www_user => CONFIG['normal_user_1'],
				:www_group => Etc.getgrgid(Etc.getpwnam(CONFIG['normal_user_1']).gid).name
			)
		end
		
		FileUtils.mkdir_p("tmp.nginx")
	end
	
	after :all do
		begin
			@nginx.stop
		ensure
			FileUtils.rm_rf("tmp.nginx")
		end
	end
	
	
	describe "MyCook(tm) beta running a root URI" do
		before :all do
			@server = "http://passenger.test:#{@nginx.port}"
			@stub = setup_rails_stub('mycook')
			@nginx.add_server do |server|
				server[:server_name] = "passenger.test"
				server[:root]        = File.expand_path("#{@stub.app_root}/public")
			end
			@nginx.start
		end
		
		after :all do
			@stub.destroy
		end
		
		it_should_behave_like "MyCook(tm) beta"
	end
	
	describe "MyCook(tm) beta running in a sub-URI" do
		before :all do
			@stub = setup_rails_stub('mycook')
			FileUtils.rm_rf('tmp.webdir')
			FileUtils.mkdir_p('tmp.webdir')
			FileUtils.cp_r('stub/zsfa/.', 'tmp.webdir')
			FileUtils.ln_sf(File.expand_path(@stub.app_root) + "/public", 'tmp.webdir/mycook')
			
			@nginx.add_server do |server|
				server[:server_name] = "passenger.test"
				server[:root]        = File.expand_path("tmp.webdir")
				server[:passenger_base_uri] = "/mycook"
			end
			@nginx.start
		end
		
		after :all do
			FileUtils.rm_rf('tmp.webdir')
			@stub.destroy
		end
		
		before :each do
			@server = "http://passenger.test:#{@nginx.port}/mycook"
		end
		
		it_should_behave_like "MyCook(tm) beta"
		
		it "does not interfere with the root website" do
			@server = "http://passenger.test:#{@nginx.port}"
			get('/').should =~ /Zed, you rock\!/
		end
	end
	
	
	##### Helper methods #####
	
	def start_web_server_if_necessary
		if !@nginx.running?
			@nginx.start
		end
	end
end
