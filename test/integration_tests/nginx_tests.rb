require File.expand_path(File.dirname(__FILE__) + "/spec_helper")
require 'support/nginx_controller'
require 'integration_tests/shared/example_webapp_tests'

describe "Phusion Passenger for Nginx" do
	before :all do
		if !CONFIG['nginx']
			STDERR.puts "*** ERROR: You must set the 'nginx' config option in test/config.json."
			exit!(1)
		end

		check_hosts_configuration
		FileUtils.mkdir_p("tmp.nginx")
	end

	after :all do
		begin
			@nginx.stop if @nginx
		ensure
			FileUtils.rm_rf("tmp.nginx")
		end
	end

	before :each do
		File.open("test.log", "a") do |f|
			# Make sure that all Nginx log output is prepended by the test description
			# so that we know which messages are associated with which tests.
			f.puts "\n#### #{Time.now}: #{example.full_description}"
		end
	end

	def create_nginx_controller(options = {})
		@nginx = NginxController.new("tmp.nginx")
		if Process.uid == 0
			@nginx.set({
				:www_user => CONFIG['normal_user_1'],
				:www_group => Etc.getgrgid(Etc.getpwnam(CONFIG['normal_user_1']).gid).name
			}.merge(options))
		end
	end

	describe "a Ruby app running on the root URI" do
		before :all do
			create_nginx_controller
			@server = "http://1.passenger.test:#{@nginx.port}"
			@stub = RackStub.new('rack')
			@nginx.add_server do |server|
				server[:server_name] = "1.passenger.test"
				server[:root]        = "#{@stub.full_app_root}/public"
			end
			@nginx.start
		end

		after :all do
			@stub.destroy
			@nginx.stop if @nginx
		end

		before :each do
			@stub.reset
		end

		it_should_behave_like "an example web app"
	end

	describe "a Ruby app running in a sub-URI" do
		before :all do
			create_nginx_controller
			@server = "http://1.passenger.test:#{@nginx.port}/subapp"
			@stub = RackStub.new('rack')
			@nginx.add_server do |server|
				server[:server_name] = "1.passenger.test"
				server[:root]        = "#{PhusionPassenger.source_root}/test/stub"
				server << %Q{
					location ~ ^/subapp(/.*|$) {
						passenger_base_uri /subapp;
						alias #{@stub.full_app_root}/public$1;
						passenger_app_root #{@stub.full_app_root};
						passenger_enabled on;
					}
				}
			end
			@nginx.start
		end

		after :all do
			@stub.destroy
			@nginx.stop if @nginx
		end

		before :each do
			@stub.reset
		end

		it_should_behave_like "an example web app"

		it "does not interfere with the root website" do
			@server = "http://1.passenger.test:#{@nginx.port}"
			get('/').should == "This is the stub directory."
		end
	end

	describe "a Python app running on the root URI" do
		before :all do
			create_nginx_controller
			@server = "http://1.passenger.test:#{@nginx.port}"
			@stub = PythonStub.new('wsgi')
			@nginx.add_server do |server|
				server[:server_name] = "1.passenger.test"
				server[:root]        = "#{@stub.full_app_root}/public"
			end
			@nginx.start
		end

		after :all do
			@stub.destroy
			@nginx.stop if @nginx
		end

		before :each do
			@stub.reset
		end

		it_should_behave_like "an example web app"
	end

	describe "a Python app running in a sub-URI" do
		before :all do
			create_nginx_controller
			@server = "http://1.passenger.test:#{@nginx.port}/subapp"
			@stub = PythonStub.new('wsgi')
			@nginx.add_server do |server|
				server[:server_name] = "1.passenger.test"
				server[:root]        = "#{PhusionPassenger.source_root}/test/stub"
				server << %Q{
					location ~ ^/subapp(/.*|$) {
						passenger_base_uri /subapp;
						alias #{@stub.full_app_root}/public$1;
						passenger_app_root #{@stub.full_app_root};
						passenger_enabled on;
					}
				}
			end
			@nginx.start
		end

		after :all do
			@stub.destroy
			@nginx.stop if @nginx
		end

		before :each do
			@stub.reset
		end

		it_should_behave_like "an example web app"

		it "does not interfere with the root website" do
			@server = "http://1.passenger.test:#{@nginx.port}"
			get('/').should == "This is the stub directory."
		end
	end

	describe "various features" do
		before :all do
			create_nginx_controller
			@server = "http://1.passenger.test:#{@nginx.port}"
			@stub = RackStub.new('rack')
			@nginx.add_server do |server|
				server[:server_name] = "1.passenger.test"
				server[:root]        = "#{@stub.full_app_root}/public"
				server << %q{
					location /crash_without_friendly_error_page {
						passenger_friendly_error_pages off;
					}
				}
			end
			@nginx.add_server do |server|
				server[:server_name] = "2.passenger.test"
				server[:root]        = "#{@stub.full_app_root}/public"
				server[:passenger_app_group_name] = "secondary"
				server[:passenger_show_version_in_header] = "off"
			end
			@nginx.add_server do |server|
				server[:server_name] = "3.passenger.test"
				server[:root]        = "#{@stub.full_app_root}/public"
				server[:passenger_max_requests] = 3
			end
			@nginx.start
		end

		after :all do
			@stub.destroy
			@nginx.stop if @nginx
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
			@server = "http://2.passenger.test:#{@nginx.port}/"
			response = get_response('/')
			response["X-Powered-By"].should include("Phusion Passenger")
			response["X-Powered-By"].should_not include(PhusionPassenger::VERSION_STRING)
		end

		it "respawns the app after handling max_requests requests" do
			@server = "http://3.passenger.test:#{@nginx.port}/"
			pid = get("/pid")
			get("/pid").should == pid
			get("/pid").should == pid
			get("/pid").should_not == pid
		end
	end

	describe "oob work" do
		before :all do
			create_nginx_controller
			@server = "http://passenger.test:#{@nginx.port}"
			@stub = RackStub.new('rack')
			@nginx.set(:max_pool_size => 2)
			@nginx.add_server do |server|
				server[:server_name] = "passenger.test"
				server[:root]        = "#{@stub.full_app_root}/public"
			end
		end

		after :all do
			@stub.destroy
			@nginx.stop if @nginx
		end

		before :each do
			@stub.reset

			File.write("#{@stub.app_root}/config.ru", <<-RUBY)
				PhusionPassenger.on_event(:oob_work) do
					f = File.open("#{@stub.full_app_root}/oob_work.\#{$$}", 'w')
					f.close
					sleep 1
				end
				app = lambda do |env|
					if env['PATH_INFO'] == '/oobw'
						[200, { "Content-Type" => "text/html", "X-Passenger-Request-OOB-Work" => 'true' }, [$$]]
					else
						[200, { "Content-Type" => "text/html" }, [$$]]
					end
				end
				run app
			RUBY

			@nginx.start
		end

		it "invokes oobw when requested by the app process" do
			pid = get("/oobw")
			sleep 0.5 # wait for oobw callback to be invoked
			File.exists?("#{@stub.app_root}/oob_work.#{pid}").should == true
		end

		it "does not block client while invoking oob work" do
			get("/") # ensure there are spawned app processes
			t0 = Time.now
			get("/oobw")
			secs = Time.now - t0
			secs.should <= 0.1
		end
	end

	##### Helper methods #####

	def start_web_server_if_necessary
		if !@nginx.running?
			@nginx.start
		end
	end
end
