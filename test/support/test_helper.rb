require 'fileutils'
require 'resolv'
require 'net/http'
require 'uri'
require 'support/multipart'

# Module containing helper methods, to be included in unit tests.
module TestHelper
	######## Stub helpers ########
	
	STUB_TEMP_DIR = 'tmp.stub'
	
	class Stub
		attr_reader :app_root
		
		def initialize(name, app_root)
			@name = name
			@app_root = app_root
		end
		
		def destroy
			FileUtils.rm_rf(@app_root)
		end
		
		def public_file(name)
			return File.read("#{@app_root}/public/#{name}")
		end
	end
	
	def setup_stub(name, dir = STUB_TEMP_DIR)
		FileUtils.rm_rf(dir)
		FileUtils.mkdir_p(dir)
		FileUtils.cp_r("stub/#{name}/.", dir)
		system("chmod", "-R", "a+rw", dir)
		return Stub.new(name, dir)
	end
	
	# Setup a stub, yield the given block, then destroy the stub.
	def use_stub(name, dir = STUB_TEMP_DIR)
		stub = setup_stub(name, dir)
		yield stub
	ensure
		stub.destroy
	end
	
	class RailsStub < Stub
		def environment_rb
			return "#{@app_root}/config/environment.rb"
		end
		
		def use_vendor_rails(name)
			FileUtils.mkdir_p("#{@app_root}/vendor/rails")
			FileUtils.cp_r("stub/vendor_rails/#{name}/.", "#{@app_root}/vendor/rails")
		end
		
		def dont_use_vendor_rails
			FileUtils.rm_rf("#{@app_root}/vendor/rails")
		end
	end
	
	def setup_rails_stub(name, dir = STUB_TEMP_DIR)
		FileUtils.rm_rf(dir)
		FileUtils.mkdir_p(dir)
		FileUtils.cp_r("stub/rails_apps/#{name}/.", dir)
		FileUtils.mkdir_p("#{dir}/log")
		system("chmod", "-R", "a+rw", dir)
		return RailsStub.new(name, dir)
	end
	
	def teardown_rails_stub
		FileUtils.rm_rf(STUB_TEMP_DIR)
	end
	
	def use_rails_stub(name, dir = STUB_TEMP_DIR)
		stub = setup_rails_stub(name, dir)
		yield stub
	ensure
		stub.destroy
	end
	
	
	######## HTTP helpers ########
	# Before using these methods, one must set the '@server' instance variable
	# and implement the start_web_server_if_necessary method.
	
	def get(uri)
		if @server.nil?
			raise "You must set the '@server' instance variable before get() can be used. For example, @server = 'http://mydomain.test/'"
		end
		start_web_server_if_necessary
		return Net::HTTP.get(URI.parse("#{@server}#{uri}"))
	end
	
	def get_response(uri)
		if @server.nil?
			raise "You must set the '@server' instance variable before get() can be used. For example, @server = 'http://mydomain.test/'"
		end
		start_web_server_if_necessary
		return Net::HTTP.get_response(URI.parse("#{@server}#{uri}"))
	end
	
	def post(uri, params = {})
		if @server.nil?
			raise "You must set the '@server' instance variable before get() can be used. For example, @server = 'http://mydomain.test/'"
		end
		start_web_server_if_necessary
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
	
	def check_hosts_configuration
		begin
			ok = Resolv.getaddress("passenger.test") == "127.0.0.1"
		rescue Resolv::ResolvError, ArgumentError
			# There's a bug in Ruby 1.8.6-p287's resolv.rb library, which causes
			# an ArgumentError to be raised instead of ResolvError when resolving
			# failed.
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
end

File.class_eval do
	def self.prepend(filename, data)
		original_content = File.read(filename)
		File.open(filename, 'w') do |f|
			f.write(data)
			f.write(original_content)
		end
	end
	
	def self.append(filename, data)
		File.open(filename, 'a') do |f|
			f.write(data)
		end
	end

	def self.write(filename, content = nil)
		if block_given?
			content = yield File.read(filename)
		end
		File.open(filename, 'w') do |f|
			f.write(content)
		end
	end
end

