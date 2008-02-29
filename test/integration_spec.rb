require 'net/http'
require 'uri'
require 'resolv'
require 'support/config'
require 'support/apache2_config_writer'
require 'mod_rails/platform_info'

include PlatformInfo

shared_examples_for "MyCook(tm) beta" do
	it "should be possible to fetch static assets" do
		get('/images/rails.png').should == public_file('images/rails.png')
	end
	
	it "should support page caching" do
		get('/welcome/cached').should =~ /This is the cached version/
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
	
	before :each do
		@server = "http://mycook.passenger.test:64506"
	end
	
	it_should_behave_like "MyCook(tm) beta"
	
	def get(uri, options = {})
		defaults = { :server => @server }
		options = defaults.merge(options)
		return Net::HTTP.get(URI.parse("#{options[:server]}#{uri}"))
	end
	
	def get_response(uri, options = {})
		defaults = { :server => @server }
		options = defaults.merge(options)
		return Net::HTTP.get_response(URI.parse("#{options[:server]}#{uri}"))
	end
	
	def post(uri, params, options = {})
		defaults = { :server => @server }
		options = defaults.merge(options)
		return Net::HTTP.post_form(URI.parse("#{options[:server]}#{uri}"), params).body
	end
	
	def public_file(name)
		return File.read("stub/mycook/public/#{name}")
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
		system("#{HTTPD} -f stub/apache2/httpd.conf -k stop >/dev/null 2>/dev/null")
		sleep(0.5)
		system("rm -f stub/apache2/*.{log,pid,lock}")
		if !system("#{HTTPD} -f stub/apache2/httpd.conf -k start")
			raise "Could not start a test Apache server"
		end
	end
	
	def stop_apache
		system("#{HTTPD} -f stub/apache2/httpd.conf -k stop")
		system("rm -f stub/apache2/*.{log,pid,lock}")
	end
end
