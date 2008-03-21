require 'net/http'
require 'uri'
require 'resolv'
require 'socket'
require 'timeout'
require 'support/config'
require 'support/multipart'
require 'support/apache2_config_writer'
require 'passenger/platform_info'

include PlatformInfo

shared_examples_for "MyCook(tm) beta" do
	it "should be possible to fetch static assets" do
		get('/images/rails.png').should == public_file('images/rails.png')
	end
	
	it "should support page caching on non-index URIs" do
		get('/welcome/cached').should =~ %r{This is the cached version of /welcome/cached}
	end
	
	it "should support page caching on index URIs" do
		get('/uploads').should =~ %r{This is the cached version of /uploads}
	end
	
	it "should not use page caching if the HTTP request is not GET" do
		post('/welcome/cached').should =~ %r{This content should never be displayed}
	end
	
	it "should not be interfered by Rails's default .htaccess dispatcher rules" do
		# Already being tested by all the other tests.
	end
	
	it "should be possible to GET a regular Rails page" do
		get('/').should =~ /Welcome to MyCook/
	end
	
	it "should be possible to pass GET parameters to a Rails page" do
		result = get('/welcome/parameters_test?hello=world&recipe[name]=Green+Bananas')
		result.should =~ %r{<hello>world</hello>}
		result.should =~ %r{<recipe>}
		result.should =~ %r{<name>Green Bananas</name>}
	end
	
	it "should be possible to POST to a Rails page" do
		result = post('/recipes', {
			'recipe[name]' => 'Banana Pancakes',
			'recipe[instructions]' => 'Call 0900-BANANAPANCAKES'
		})
		result.should =~ %r{HTTP method: post}
		result.should =~ %r{Name: Banana Pancakes}
		result.should =~ %r{Instructions: Call 0900-BANANAPANCAKES}
	end
	
	it "should be possible to upload a file" do
		rails_png = File.open("#{@app_root}/public/images/rails.png", 'rb')
		params = {
			'upload[name1]' => 'Kotonoha',
			'upload[name2]' => 'Sekai',
			'upload[data]' => rails_png
		}
		begin
			response = post('/uploads', params)
			rails_png.rewind
			response.should ==
				"name 1 = Kotonoha\n" <<
				"name 2 = Sekai\n" <<
				"data = " << rails_png.read
		ensure
			rails_png.close
		end
	end
	
	it "should properly handle custom headers" do
		response = get_response('/welcome/headers_test')
		response["X-Foo"].should == "Bar"
	end
	
	it "should support restarting via restart.txt" do
		begin
			controller = "#{@app_root}/app/controllers/test_controller.rb"
			restart_file = "#{@app_root}/tmp/restart.txt"
			
			File.open(controller, 'w') do |f|
				f.write %q{
					class TestController < ApplicationController
						layout nil
						def index
							render :text => "foo"
						end
					end
				}
			end
			File.open(restart_file, 'w') do end
			get('/test').should == "foo"
		
			File.open(controller, 'w') do |f|
				f.write %q{
					class TestController < ApplicationController
						layout nil
						def index
							render :text => "bar"
						end
					end
				}
			end

			File.open(restart_file, 'w') do end
			get('/test').should == 'bar'
		ensure
			File.unlink(controller) rescue nil
			File.unlink(restart_file) rescue nil
		end
	end
	
	it "should not make the web server crash if the app crashes" do
		post('/welcome/terminate')
		sleep(0.25) # Give the app the time to terminate itself.
		get('/').should =~ /Welcome to MyCook/
	end
	
	if Process.uid == 0
		it "should be running as unprivileged user" do
			post('/welcome/touch')
			begin
				stat = File.stat("#{@app_root}/public/touch.txt")
				stat.uid.should_not == 0
				stat.gid.should_not == 0
			ensure
				File.unlink("#{@app_root}/public/touch.txt") rescue nil
			end
		end
	end
end

describe "mod_passenger running in Apache 2" do
	before :all do
		check_hosts_configuration
		Apache2ConfigWriter.new.write
		start_apache
	end
	
	after :all do
		stop_apache
	end
	
	describe ": MyCook(tm) beta running on root URI" do
		before :each do
			@server = "http://mycook.passenger.test:64506"
			@app_root = "stub/mycook"
		end
		
		it_should_behave_like "MyCook(tm) beta"
	end
	
	describe ": MyCook(tm) beta running in a sub-URI" do
		before :each do
			@server = "http://zsfa.passenger.test:64506/mycook"
			@app_root = "stub/mycook"
			File.unlink("stub/zsfa/mycook") rescue nil
			File.symlink("../mycook/public", "stub/zsfa/mycook")
		end
		
		after :each do
			File.unlink("stub/zsfa/mycook") rescue nil
		end
		
		it_should_behave_like "MyCook(tm) beta"
	end
	
	describe ": railsapp running in a sub-URI" do
		before :each do
			@server = "http://zsfa.passenger.test:64506/foo"
			@app_root = "stub/railsapp"
			File.unlink("stub/zsfa/foo") rescue nil
			File.symlink("../railsapp/public", "stub/zsfa/foo")
		end
		
		after :each do
			File.unlink("stub/zsfa/foo") rescue nil
		end
		
		it "should respond to /foo/new" do
			get('/foo/new').should == 'hello world'
		end
		
		it "should not interfere with the ZSFA website" do
			@server = "http://zsfa.passenger.test:64506"
			get('/').should =~ /Zed, you rock\!/
		end
	end
	
	describe "configuration options" do
		it "should ignore the Rails application if RailsAutoDetect is off" do
			@server = "http://norails.passenger.test:64506"
			get('/').should_not =~ /MyCook/
		end
		
		it "setting RailsAutoDetect for one virtual host should not interfere with others" do
			# Already covered by other tests.
		end
	end
	
	describe "error handling" do
		before :each do
			File.unlink("stub/zsfa/app-with-nonexistant-rails-version") rescue nil
			File.unlink("stub/zsfa/app-that-crashes-during-startup") rescue nil
			File.unlink("stub/zsfa/app-with-crashing-vendor-rails") rescue nil
			File.symlink("../broken-railsapp4/public", "stub/zsfa/app-with-nonexistant-rails-version")
			File.symlink("../broken-railsapp/public", "stub/zsfa/app-that-crashes-during-startup")
			File.symlink("../broken-railsapp5/public", "stub/zsfa/app-with-crashing-vendor-rails")
			@server = "http://zsfa.passenger.test:64506"
			@error_page_signature = /<meta name="generator" content="Phusion Passenger">/
		end
		
		after :each do
			File.unlink("stub/zsfa/app-with-nonexistant-rails-version") rescue nil
			File.unlink("stub/zsfa/app-that-crashes-during-startup") rescue nil
			File.unlink("stub/zsfa/app-with-crashing-vendor-rails") rescue nil
		end
		
		it "should display an error page if the Rails application requires a nonexistant Rails version" do
			get("/app-with-nonexistant-rails-version/").should =~ @error_page_signature
		end
		
		it "should display an error page if the Rails application crashes during startup" do
			get("/app-that-crashes-during-startup/").should =~ @error_page_signature
		end
		
		it "should display an error page if the Rails application's vendor'ed Rails crashes" do
			get("/app-with-crashing-vendor-rails/").should =~ @error_page_signature
		end
	end
	
	##### Helper methods #####
	
	def get(uri)
		return Net::HTTP.get(URI.parse("#{@server}#{uri}"))
	end
	
	def get_response(uri)
		return Net::HTTP.get_response(URI.parse("#{@server}#{uri}"))
	end
	
	def post(uri, params = {})
		url = URI.parse("#{@server}#{uri}")
		if params.values.any? { |x| x.respond_to?(:read) }
			mp = Multipart::MultipartPost.new
			query, headers = mp.prepare_query(params)
			Net::HTTP.start(url.host, url.port) do |http|
				return http.post(url.path, query, headers).body
			end
		else
			return Net::HTTP.post_form(url, params).body
		end
	end
	
	def public_file(name)
		return File.read("#{@app_root}/public/#{name}")
	end
	
	def check_hosts_configuration
		begin
			ok = Resolv.getaddress("passenger.test") == "127.0.0.1"
		rescue Resolv::ResolvError
			ok = false
		end
		if !ok
			message = "To run the integration test, you must update " <<
				"your hosts file.\n" <<
				"Please add these to your /etc/hosts:\n\n" <<
				"  127.0.0.1 passenger.test\n" <<
				"  127.0.0.1 mycook.passenger.test\n" <<
				"  127.0.0.1 zsfa.passenger.test\n" <<
				"  127.0.0.1 norails.passenger.test\n"
			if RUBY_PLATFORM =~ /darwin/
				message << "\n\nThen run:\n\n" <<
					"  lookupd -flushcache      (OS X Tiger)\n\n" <<
					"-OR-\n\n" <<
					"  dscacheutil -flushcache  (OS X Leopard)"
			end
			STDERR.puts "---------------------------"
			STDERR.puts message
			exit!
		end
	end
	
	def start_apache
		if File.exist?("stub/apache2/httpd.pid")
			stop_apache
		end
		config_file = File.expand_path("stub/apache2/httpd.conf")
		if !system(HTTPD, "-f", config_file, "-k", "start")
			raise "Could not start a test Apache server"
		end
		begin
			# Wait until the PID file has been created.
			Timeout::timeout(15) do
				while !File.exist?("stub/apache2/httpd.pid")
					sleep(0.25)
				end
			end
			# Wait until Apache is listening on the server port.
			Timeout::timeout(5) do
				done = false
				while !done
					begin
						socket = TCPSocket.new('localhost', 64506)
						socket.close
						done = true
					rescue Errno::ECONNREFUSED
						sleep(0.25)
					end
				end
			end
		rescue Timeout::Error
			raise "Unable to start Apache."
		end
		File.chmod(0666, *Dir['stub/apache2/*.{log,lock,pid}']) rescue nil
		File.chmod(0777, *Dir['stub/mycook/{public,log}']) rescue nil
	end
	
	def stop_apache
		File.chmod(0666, *Dir['stub/apache2/*.{log,lock,pid}']) rescue nil
		begin
			pid = File.read('stub/apache2/httpd.pid').strip.to_i
			Process.kill('SIGTERM', pid)
		rescue
		end
		begin
			# Wait until the PID file is removed.
			Timeout::timeout(15) do
				while File.exist?("stub/apache2/httpd.pid")
					sleep(0.25)
				end
			end
			# Wait until the server socket is closed.
			Timeout::timeout(5) do
				done = false
				while !done
					begin
						socket = TCPSocket.new('localhost', 64506)
						socket.close
						sleep(0.25)
					rescue SystemCallError
						done = true
					end
				end
			end
		rescue Timeout::Error
			raise "Unable to stop Apache."
		end
	end
end
