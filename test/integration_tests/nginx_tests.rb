require File.expand_path(File.dirname(__FILE__) + "/spec_helper")
require 'support/nginx_controller'
require 'integration_tests/mycook_spec'
require 'integration_tests/cgi_environment_spec'
require 'integration_tests/hello_world_rack_spec'
require 'integration_tests/hello_world_wsgi_spec'

describe "Phusion Passenger for Nginx" do
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
	
	before :each do
		File.open("test.log", "a") do |f|
			# Make sure that all Nginx log output is prepended by the test description
			# so that we know which messages are associated with which tests.
			f.puts "\n#### #{self.class.description} : #{description}"
		end
	end
	
	
	describe "MyCook(tm) beta running a root URI" do
		before :all do
			@server = "http://1.passenger.test:#{@nginx.port}"
			@base_uri = ""
			@stub = RailsStub.new('2.3/mycook')
			@nginx.add_server do |server|
				server[:server_name] = "1.passenger.test"
				server[:root]        = "#{@stub.full_app_root}/public"
			end
			@nginx.start
		end
		
		after :all do
			@stub.destroy
		end
		
		before :each do
			@stub.reset
		end
		
		it_should_behave_like "MyCook(tm) beta"
		include_shared_example_group "CGI environment variables compliance"
	end
	
	describe "MyCook(tm) beta running in a sub-URI" do
		before :all do
			@base_uri = "/mycook"
			@stub = RailsStub.new('2.3/mycook')
			FileUtils.rm_rf('tmp.webdir')
			FileUtils.mkdir_p('tmp.webdir')
			FileUtils.cp_r('stub/zsfa/.', 'tmp.webdir')
			FileUtils.ln_sf(@stub.full_app_root + "/public", 'tmp.webdir/mycook')
			
			@nginx.add_server do |server|
				server[:server_name] = "1.passenger.test"
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
			@server = "http://1.passenger.test:#{@nginx.port}/mycook"
			@stub.reset
		end
		
		it_should_behave_like "MyCook(tm) beta"
		include_shared_example_group "CGI environment variables compliance"
		
		it "does not interfere with the root website" do
			@server = "http://1.passenger.test:#{@nginx.port}"
			get('/').should =~ /Zed, you rock\!/
		end
	end
	
	describe "Rack application running in root URI" do
		before :all do
			@server = "http://passenger.test:#{@nginx.port}"
			@stub = RackStub.new('rack')
			@nginx.add_server do |server|
				server[:server_name] = "passenger.test"
				server[:root]        = "#{@stub.full_app_root}/public"
			end
			@nginx.start
		end
		
		after :all do
			@stub.destroy
		end
		
		before :each do
			@stub.reset
		end
		
		it_should_behave_like "HelloWorld Rack application"
	end
	
	describe "Rack application running in sub-URI" do
		before :all do
			FileUtils.rm_rf('tmp.webdir')
			FileUtils.mkdir_p('tmp.webdir')
			@stub = RackStub.new('rack')
			@nginx.add_server do |server|
				FileUtils.ln_s(@stub.full_app_root + "/public", 'tmp.webdir/rack')
				server[:server_name] = "passenger.test"
				server[:root]        = File.expand_path('tmp.webdir')
				server[:passenger_base_uri] = "/rack"
			end
			@nginx.start
			@server = "http://passenger.test:#{@nginx.port}/rack"
		end
		
		after :all do
			@stub.destroy
			FileUtils.rm_rf('tmp.webdir')
		end
		
		before :each do
			@stub.reset
		end
		
		it_should_behave_like "HelloWorld Rack application"
	end
	
	describe "Rack application running within Rails directory structure" do
		before :all do
			@server = "http://passenger.test:#{@nginx.port}"
			@stub = RailsStub.new('2.3/mycook')
			FileUtils.cp_r("stub/rack/.", @stub.app_root)
			@nginx.add_server do |server|
			server[:server_name] = "passenger.test"
			server[:root]        = "#{@stub.full_app_root}/public"
			end
			@nginx.start
		end
		
		after :all do
			@stub.destroy
		end
		
		before :each do
			@stub.reset
			FileUtils.cp_r("stub/rack/.", @stub.app_root)
		end
		
		it_should_behave_like "HelloWorld Rack application"
	end
	
	describe "various features" do
		before :all do
			@server = "http://passenger.test:#{@nginx.port}"
			@stub = RackStub.new('rack')
			@nginx.add_server do |server|
				server[:server_name] = "passenger.test"
				server[:root]        = "#{@stub.full_app_root}/public"
				server << %q{
					location /crash_without_friendly_error_page {
						passenger_friendly_error_pages off;
					}
				}
			end
			@nginx.add_server do |server|
				server[:server_name] = "1.passenger.test"
				server[:root]        = "#{@stub.full_app_root}/public"
				server[:passenger_app_group_name] = "secondary"
				server[:passenger_show_version_in_header] = "off"
			end
			@nginx.start
		end
		
		after :all do
			@stub.destroy
		end
		
		before :each do
			@stub.reset
			@error_page_signature = /<meta name="generator" content="Phusion Passenger">/
			File.touch("#{@stub.app_root}/tmp/restart.txt", 1 + rand(100000))
		end
		
		it "sets ENV['SERVER_SOFTWARE']" do
			File.write("#{@stub.app_root}/config.ru", %q{
				server_software = ENV['SERVER_SOFTWARE']
				app = lambda do |env|
					[200, { "Content-Type" => "text/plain" }, [server_software]]
				end
				run app
			})
			get('/').should =~ /nginx/i
		end
		
		it "displays a friendly error page if the application fails to spawn" do
			File.write("#{@stub.app_root}/config.ru", %q{
				raise "my error"
			})
			data = get('/')
			data.should =~ /#{@error_page_signature}/
			data.should =~ /my error/
		end
		
		it "doesn't display a friendly error page if the application fails to spawn but passenger_friendly_error_pages is off" do
			File.write("#{@stub.app_root}/config.ru", %q{
				raise "my error"
			})
			data = get('/crash_without_friendly_error_page')
			data.should_not =~ /#{@error_page_signature}/
			data.should_not =~ /my error/
		end
		
		it "appends an X-Powered-By header containing the Phusion Passenger version number" do
			response = get_response('/')
			response["X-Powered-By"].should include("Phusion Passenger")
			response["X-Powered-By"].should include(PhusionPassenger::VERSION_STRING)
		end
		
		it "omits the version number in X-Powered-By when passenger_show_version_in_header is off" do
			@server = "http://1.passenger.test:#{@nginx.port}/"
			response = get_response('/')
			response["X-Powered-By"].should include("Phusion Passenger")
			response["X-Powered-By"].should_not include(PhusionPassenger::VERSION_STRING)
		end
	end
	
	
	##### Helper methods #####
	
	def start_web_server_if_necessary
		if !@nginx.running?
			@nginx.start
		end
	end
end
