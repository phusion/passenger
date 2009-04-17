require 'socket'
require 'fileutils'
require 'support/config'
require 'support/test_helper'
require 'support/apache2_controller'
require 'phusion_passenger/platform_info'

require 'integration_tests/mycook_spec'
require 'integration_tests/hello_world_rack_spec'
require 'integration_tests/hello_world_wsgi_spec'

# TODO: test the 'RailsUserSwitching' and 'RailsDefaultUser' option.
# TODO: test custom page caching directory

describe "mod_passenger running in Apache 2" do
	include TestHelper
	
	before :all do
		check_hosts_configuration
		@apache2 = Apache2Controller.new
		if Process.uid == 0
			@apache2.set(
				:www_user => CONFIG['normal_user_1'],
				:www_group => Etc.getgrgid(Etc.getpwnam(CONFIG['normal_user_1']).gid).name
			)
		end
	end
	
	after :all do
		@apache2.stop
	end
	
	describe ": MyCook(tm) beta running on root URI" do
		before :all do
			@web_server_supports_chunked_transfer_encoding = true
			@server = "http://passenger.test:#{@apache2.port}"
			@stub = setup_rails_stub('mycook')
			@apache2 << "RailsMaxPoolSize 1"
			@apache2.set_vhost("passenger.test", File.expand_path("#{@stub.app_root}/public"))
			@apache2.start
		end
		
		after :all do
			@stub.destroy
		end
		
		it_should_behave_like "MyCook(tm) beta"
		
		it "doesn't block Rails while an upload is in progress" do
			get('/') # Force spawning so that the timeout below is enough.
			
			socket = TCPSocket.new('passenger.test', @apache2.port)
			begin
				socket.write("POST / HTTP/1.1\r\n")
				socket.write("Host: passenger.test\r\n")
			
				upload_data = File.read("stub/upload_data.txt")
				size_of_first_half = upload_data.size / 2
			
				socket.write(upload_data[0..size_of_first_half])
				socket.flush
				
				Timeout.timeout(10) do
					get('/').should =~ /Welcome to MyCook/
				end
			ensure
				socket.close rescue nil
			end
		end
		
		it "doesn't block Rails while a large number of uploads are in progress" do
			get('/') # Force spawning so that the timeout below is enough.
			sockets = []
			
			upload_data = File.read("stub/upload_data.txt")
			size_of_first_half = upload_data.size / 2
			
			begin
				10.times do |i|
					socket = TCPSocket.new('passenger.test', @apache2.port)
					sockets << socket
					socket.write("POST / HTTP/1.1\r\n")
					socket.write("Host: passenger.test\r\n")
					socket.write(upload_data[0..size_of_first_half])
					socket.flush
				end
				Timeout.timeout(10) do
					get('/').should =~ /Welcome to MyCook/
				end
			ensure
				sockets.each do |socket|
					socket.close rescue nil
				end
			end
		end
	end
	
	describe ": MyCook(tm) beta running in a sub-URI" do
		before :all do
			@web_server_supports_chunked_transfer_encoding = true
			@stub = setup_rails_stub('mycook')
			FileUtils.rm_rf('tmp.webdir')
			FileUtils.mkdir_p('tmp.webdir')
			FileUtils.cp_r('stub/zsfa/.', 'tmp.webdir')
			FileUtils.ln_sf(File.expand_path(@stub.app_root) + "/public", 'tmp.webdir/mycook')
			
			@apache2.set_vhost('passenger.test', File.expand_path('tmp.webdir')) do |vhost|
				vhost << "RailsBaseURI /mycook"
			end
			@apache2.start
		end
		
		after :all do
			FileUtils.rm_rf('tmp.webdir')
			@stub.destroy
		end
		
		before :each do
			@server = "http://passenger.test:#{@apache2.port}/mycook"
		end
		
		it_should_behave_like "MyCook(tm) beta"
		
		it "does not interfere with the root website" do
			@server = "http://passenger.test:#{@apache2.port}"
			get('/').should =~ /Zed, you rock\!/
		end
	end
	
	describe "configuration options" do
		before :all do
			@apache2 << "PassengerMaxPoolSize 3"
			
			@stub = setup_rails_stub('mycook')
			rails_dir = File.expand_path(@stub.app_root) + "/public"
			
			@apache2.set_vhost('mycook.passenger.test', rails_dir)
			
			@apache2.set_vhost('norails.passenger.test', rails_dir) do |vhost|
				vhost << "RailsAutoDetect off"
			end
			
			@stub2 = setup_rails_stub('foobar', 'tmp.stub2')
			rails_dir = File.expand_path(@stub2.app_root) + "/public"
			@apache2.set_vhost('passenger.test', rails_dir) do |vhost|
				vhost << "RailsEnv development"
				vhost << "RailsSpawnMethod conservative"
				vhost << "PassengerUseGlobalQueue on"
				vhost << "PassengerRestartDir #{rails_dir}"
			end
			@apache2.start
		end
		
		after :all do
			@stub.destroy
			@stub2.destroy
		end
		
		it "ignores the Rails application if RailsAutoDetect is off" do
			@server = "http://norails.passenger.test:#{@apache2.port}"
			get('/').should_not =~ /MyCook/
		end
		
		specify "setting RailsAutoDetect for one virtual host should not interfere with others" do
			@server = "http://mycook.passenger.test:#{@apache2.port}"
			get('/').should =~ /MyCook/
		end
		
		specify "RailsEnv is per-virtual host" do
			@server = "http://mycook.passenger.test:#{@apache2.port}"
			get('/welcome/rails_env').should == "production"
			
			@server = "http://passenger.test:#{@apache2.port}"
			get('/foo/rails_env').should == "development"
		end
		
		it "supports conservative spawning" do
			@server = "http://passenger.test:#{@apache2.port}"
			get('/foo/backtrace').should_not =~ /framework_spawner/
		end
		
		specify "RailsSpawnMethod spawning is per-virtual host" do
			@server = "http://mycook.passenger.test:#{@apache2.port}"
			get('/welcome/backtrace').should =~ /application_spawner/
		end
		
		it "looks for restart.txt in the directory specified by PassengerRestartDir" do
			@server = "http://passenger.test:#{@apache2.port}"
			controller = "#{@stub2.app_root}/app/controllers/bar_controller.rb"
			restart_file = "#{@stub2.app_root}/public/restart.txt"
			begin
				File.open(controller, 'w') do |f|
					f.write(%Q{
						class BarController < ApplicationController
							def index
								render :text => 'hello world'
							end
						end
					})
				end
				
				File.open(restart_file, 'w').close
				get('/bar').should == "hello world"
				
				File.open(controller, 'w') do |f|
					f.write(%Q{
						class BarController < ApplicationController
							def index
								render :text => 'oh hai'
							end
						end
					})
				end
				
				now = Time.now
				File.open(restart_file, 'w').close
				File.utime(now - 10, now - 10, restart_file)
				get('/bar').should == "oh hai"
			ensure
				File.unlink(controller) rescue nil
				File.unlink(restart_file) rescue nil
			end
		end
		
		describe "PassengerUseGlobalQueue" do
			after :each do
				# Restart Apache to reset the application pool's state.
				@apache2.start
			end
			
			it "is off by default" do
				@server = "http://mycook.passenger.test:#{@apache2.port}"
				
				# Spawn the application.
				get('/')
				
				threads = []
				# Reserve all application pool slots.
				3.times do |i|
					thread = Thread.new do
						File.unlink("#{@stub.app_root}/#{i}.txt") rescue nil
						get("/welcome/sleep_until_exists?name=#{i}.txt")
					end
					threads << thread
				end
				
				# Wait until all application instances are waiting
				# for the quit file.
				while !File.exist?("#{@stub.app_root}/waiting_0.txt") ||
				      !File.exist?("#{@stub.app_root}/waiting_1.txt") ||
				      !File.exist?("#{@stub.app_root}/waiting_2.txt")
					sleep 0.1
				end
				
				# While all slots are reserved, make two more requests.
				first_request_done = false
				second_request_done = false
				thread = Thread.new do
					get("/")
					first_request_done = true
				end
				threads << thread
				thread = Thread.new do
					get("/")
					second_request_done = true
				end
				threads << thread
				
				# These requests should both block.
				sleep 0.5
				first_request_done.should be_false
				second_request_done.should be_false
				
				# One of the requests should still be blocked
				# if one application instance frees up.
				File.open("#{@stub.app_root}/2.txt", 'w')
				begin
					Timeout.timeout(5) do
						while !first_request_done && !second_request_done
							sleep 0.1
						end
					end
				rescue Timeout::Error
				end
				(first_request_done || second_request_done).should be_true
				
				File.open("#{@stub.app_root}/0.txt", 'w')
				File.open("#{@stub.app_root}/1.txt", 'w')
				File.open("#{@stub.app_root}/2.txt", 'w')
				threads.each do |thread|
					thread.join
				end
			end
			
			it "works and is per-virtual host" do
				@server = "http://passenger.test:#{@apache2.port}"
				
				# Spawn the application.
				get('/')
				
				threads = []
				# Reserve all application pool slots.
				3.times do |i|
					thread = Thread.new do
						File.unlink("#{@stub2.app_root}/#{i}.txt") rescue nil
						get("/foo/sleep_until_exists?name=#{i}.txt")
					end
					threads << thread
				end
				
				# Wait until all application instances are waiting
				# for the quit file.
				while !File.exist?("#{@stub2.app_root}/waiting_0.txt") ||
				      !File.exist?("#{@stub2.app_root}/waiting_1.txt") ||
				      !File.exist?("#{@stub2.app_root}/waiting_2.txt")
					sleep 0.1
				end
				
				# While all slots are reserved, make two more requests.
				first_request_done = false
				second_request_done = false
				thread = Thread.new do
					get("/")
					first_request_done = true
				end
				threads << thread
				thread = Thread.new do
					get("/")
					second_request_done = true
				end
				threads << thread
				
				# These requests should both block.
				sleep 0.5
				first_request_done.should be_false
				second_request_done.should be_false
				
				# Both requests should be processed if one application instance frees up.
				File.open("#{@stub2.app_root}/2.txt", 'w')
				begin
					Timeout.timeout(5) do
						while !first_request_done || !second_request_done
							sleep 0.1
						end
					end
				rescue Timeout::Error
				end
				first_request_done.should be_true
				second_request_done.should be_true
				
				File.open("#{@stub2.app_root}/0.txt", 'w')
				File.open("#{@stub2.app_root}/1.txt", 'w')
				File.open("#{@stub2.app_root}/2.txt", 'w')
				threads.each do |thread|
					thread.join
				end
			end
		end
		
		describe "PassengerAppRoot" do	    
			before :all do
				@stub3 = setup_rails_stub('mycook', 'tmp.stub3')
				doc_root = File.expand_path(@stub3.app_root) + "/sites/some.site/public"
				@apache2.set_vhost('passenger.test', doc_root) do |vhost|
					vhost << "PassengerAppRoot #{File.expand_path(@stub3.app_root).inspect}"
				end
				@apache2.start
			end

			after :all do
				@stub3.destroy
			end

			it "supports page caching on non-index URIs" do
				@server = "http://passenger.test:#{@apache2.port}"
				get('/welcome/cached.html').should =~ %r{This is the cached version of some.site/public/welcome/cached}
			end

			it "supports page caching on index URIs" do
				@server = "http://passenger.test:#{@apache2.port}"
				get('/uploads.html').should =~ %r{This is the cached version of some.site/public/uploads}
			end

			it "works as a rails application" do
				@server = "http://passenger.test:#{@apache2.port}"
				result = get('/welcome/parameters_test?hello=world&recipe[name]=Green+Bananas')
				result.should =~ %r{<hello>world</hello>}
				result.should =~ %r{<recipe>}
				result.should =~ %r{<name>Green Bananas</name>}
			end
		end
		
		####################################
	end
	
	describe "error handling" do
		before :all do
			FileUtils.rm_rf('tmp.webdir')
			FileUtils.mkdir_p('tmp.webdir')
			@webdir = File.expand_path('tmp.webdir')
			@apache2.set_vhost('passenger.test', @webdir) do |vhost|
				vhost << "RailsBaseURI /app-with-nonexistant-rails-version/public"
				vhost << "RailsBaseURI /app-that-crashes-during-startup/public"
				vhost << "RailsBaseURI /app-with-crashing-vendor-rails/public"
			end
			@apache2.start
		end
		
		after :all do
			FileUtils.rm_rf('tmp.webdir')
		end
		
		before :each do
			@server = "http://zsfa.passenger.test:64506"
			@error_page_signature = /<meta name="generator" content="Phusion Passenger">/
		end
		
		it "displays an error page if the Rails application requires a nonexistant Rails version" do
			use_rails_stub('foobar', "#{@webdir}/app-with-nonexistant-rails-version") do |stub|
				File.write(stub.environment_rb) do |content|
					content.sub(/^RAILS_GEM_VERSION = .*$/, "RAILS_GEM_VERSION = '1.9.1234'")
				end
				get("/app-with-nonexistant-rails-version/public").should =~ @error_page_signature
			end
		end
		
		it "displays an error page if the Rails application crashes during startup" do
			use_rails_stub('foobar', "#{@webdir}/app-that-crashes-during-startup") do |stub|
				File.prepend(stub.environment_rb, "raise 'app crash'")
				result = get("/app-that-crashes-during-startup/public")
				result.should =~ @error_page_signature
				result.should =~ /app crash/
			end
		end
		
		it "displays an error page if the Rails application's vendor'ed Rails crashes" do
			use_rails_stub('foobar', "#{@webdir}/app-with-crashing-vendor-rails") do |stub|
				stub.use_vendor_rails('minimal')
				File.append("#{stub.app_root}/vendor/rails/railties/lib/initializer.rb",
					"raise 'vendor crash'")
				result = get("/app-with-crashing-vendor-rails/public")
				result.should =~ @error_page_signature
				result.should =~ /vendor crash/
			end
		end
	end
	
	describe "Rack application running in root URI" do
		before :all do
			@stub = setup_stub('rack')
			@apache2.set_vhost('passenger.test', File.expand_path(@stub.app_root) + "/public")
			@apache2.start
			@server = "http://passenger.test:#{@apache2.port}"
		end
		
		after :all do
			@stub.destroy
		end
		
		it_should_behave_like "HelloWorld Rack application"
	end
	
	describe "Rack application running in sub-URI" do
		before :all do
			FileUtils.rm_rf('tmp.webdir')
			FileUtils.mkdir_p('tmp.webdir')
			@stub = setup_stub('rack')
			@apache2.set_vhost('passenger.test', File.expand_path('tmp.webdir')) do |vhost|
				FileUtils.ln_s(File.expand_path(@stub.app_root) + "/public", 'tmp.webdir/rack')
				vhost << "RackBaseURI /rack"
			end
			@apache2.start
			@server = "http://passenger.test:#{@apache2.port}/rack"
		end
		
		after :all do
			@stub.destroy
			FileUtils.rm_rf('tmp.webdir')
		end
		
		it_should_behave_like "HelloWorld Rack application"
	end
	
	describe "WSGI application running in root URI" do
		before :all do
			@stub = setup_stub('wsgi')
			@apache2.set_vhost('passenger.test', File.expand_path(@stub.app_root) + "/public")
			@apache2.start
			@server = "http://passenger.test:#{@apache2.port}"
		end
		
		after :all do
			@stub.destroy
		end
		
		it_should_behave_like "HelloWorld WSGI application"
	end
	
	##### Helper methods #####
	
	def start_web_server_if_necessary
		if !@apache2.running?
			@apache2.start
		end
	end
end
