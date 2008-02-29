require 'net/http'
require 'uri'
require 'resolv'
require 'support/config'
require 'support/apache2_config_writer'
require 'mod_rails/platform_info'

include PlatformInfo

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
	
	it "should ignore static assets" do
		get('/images/rails.png').should == File.read('stub/mycook/public/images/rails.png')
	end
	
	it "should support page caching" do
		# TODO
	end
	
	def get(uri, server = "http://passenger.test:64506")
		return Net::HTTP.get(URI.parse("#{server}#{uri}"))
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
