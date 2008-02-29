require 'net/http'
require 'uri'
require 'resolv'
require 'timeout'
require 'support/config'
require 'support/apache2_config_writer'
require 'mod_rails/platform_info'

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
	
	it "should be possible to upload a file"
	
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
end

describe "mod_passenger running in Apache 2" do
	# TODO: test all of these with and without subdir
	
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
			if !File.exist?("stub/zsfa/mycook")
				File.symlink("../mycook/public", "stub/zsfa/mycook")
			end
			if !File.exist?("stub/zsfa/foo")
				File.symlink("../railsapp/public", "stub/zsfa/foo")
			end
		end
		
		it_should_behave_like "MyCook(tm) beta"
	end
	
	##### Helper methods #####
	
	def get(uri)
		return Net::HTTP.get(URI.parse("#{@server}#{uri}"))
	end
	
	def get_response(uri)
		return Net::HTTP.get_response(URI.parse("#{@server}#{uri}"))
	end
	
	def post(uri, params)
		return Net::HTTP.post_form(URI.parse("#{@server}#{uri}"), params).body
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
				"  127.0.0.1 zsfa.passenger.test"
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
		if !system("#{HTTPD} -f stub/apache2/httpd.conf -k start")
			raise "Could not start a test Apache server"
		end
		Timeout::timeout(5) do
			while !File.exist?("stub/apache2/httpd.pid")
				sleep(0.25)
			end
		end
	end
	
	def stop_apache
		begin
			pid = File.read('stub/apache2/httpd.pid').strip.to_i
			Process.kill('SIGTERM', pid)
		rescue
		end
		Timeout::timeout(5) do
			while File.exist?("stub/apache2/httpd.pid")
				sleep(0.25)
			end
		end
		File.unlink(*Dir["stub/apache2/*.{log,lock}"]) rescue nil
	end
end
