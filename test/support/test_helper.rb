require 'fileutils'
require 'resolv'
require 'net/http'
require 'uri'
require 'support/multipart'

# Module containing helper methods, to be included in unit tests.
module TestHelper
	######## Stub helpers ########
	
	class Stub
		attr_reader :app_root
		attr_reader :full_app_root
		
		def self.use(name, app_root = nil)
			stub = new(name, app_root)
			begin
				yield stub
			ensure
				stub.destroy
			end
		end
		
		def initialize(name, app_root = nil)
			@name = name
			if !File.exist?(stub_source_dir)
				raise Errno::ENOENT, "Stub '#{name}' not found."
			end
			
			if app_root
				@app_root = app_root
			else
				identifier = name.gsub('/', '-')
				@app_root = "tmp.#{identifier}.#{object_id}"
			end
			@full_app_root = File.expand_path(@app_root)
			remove_dir_tree(@full_app_root)
			FileUtils.mkdir_p(@full_app_root)
			copy_stub_contents
			system("chmod", "-R", "a+rw", @full_app_root)
		end
		
		def reset
			# Empty directory without removing the directory itself,
			# allowing processes with this directory as current working
			# directory to continue to function properly.
			files = Dir["#{@full_app_root}/*"]
			files |= Dir["#{@full_app_root}/.*"]
			files.delete("#{@full_app_root}/.")
			files.delete("#{@full_app_root}/..")
			FileUtils.chmod_R(0777, files)
			FileUtils.rm_rf(files)
			
			copy_stub_contents
			system("chmod", "-R", "a+rw", @full_app_root)
		end
		
		def move(new_app_root)
			File.rename(@full_app_root, new_app_root)
			@app_root = new_app_root
			@full_app_root = File.expand_path(new_app_root)
		end
		
		def destroy
			remove_dir_tree(@full_app_root)
		end
		
		def full_app_root
			return File.expand_path(@app_root)
		end
		
		def public_file(name)
			return File.read("#{@full_app_root}/public/#{name}")
		end
	
	private
		def stub_source_dir
			return "stub/#{@name}"
		end
		
		def copy_stub_contents
			FileUtils.cp_r("#{stub_source_dir}/.", @full_app_root)
		end
	end
	
	class RailsStub < Stub
		def self.use(name, app_root = nil)
			stub = new(name, app_root)
			begin
				yield stub
			ensure
				stub.destroy
			end
		end
		
		def startup_file
			return environment_rb
		end
		
		def environment_rb
			return "#{@full_app_root}/config/environment.rb"
		end
		
		def use_vendor_rails(name)
			FileUtils.mkdir_p("#{@full_app_root}/vendor/rails")
			FileUtils.cp_r("stub/vendor_rails/#{name}/.", "#{@full_app_root}/vendor/rails")
		end
		
		def dont_use_vendor_rails
			remove_dir_tree("#{@full_app_root}/vendor/rails")
		end
		
	private
		def stub_source_dir
			return "stub/rails_apps/#{@name}"
		end
		
		def copy_stub_contents
			super
			FileUtils.mkdir_p("#{@full_app_root}/log")
		end
	end
	
	class RackStub < Stub
		def startup_file
			return "#{@full_app_root}/config.ru"
		end
	end
	
	def describe_each_rails_version(&block)
		if ENV['ONLY_RAILS_VERSION']
			versions = [ENV['ONLY_RAILS_VERSION']]
		else
			versions = Dir.entries("stub/rails_apps").grep(/^\d+\.\d+$/)
			if RUBY_VERSION >= '1.9.0'
				# Only Rails >= 2.3 is compatible with Ruby 1.9.
				versions.reject! do |version|
					version < '2.3'
				end
			end
		end
		versions.sort.each do |version|
			klass = describe("Rails #{version}", &block)
			klass.send(:define_method, :rails_version) do
				version
			end
		end
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
			ok = ok && Resolv.getaddress("1.passenger.test") == "127.0.0.1"
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
				"127.0.0.1 passenger.test\n" <<
				"127.0.0.1 mycook.passenger.test\n" <<
				"127.0.0.1 zsfa.passenger.test\n" <<
				"127.0.0.1 norails.passenger.test\n" <<
				"127.0.0.1 1.passenger.test 2.passenger.test 3.passenger.test\n" <<
				"127.0.0.1 4.passenger.test 5.passenger.test 6.passenger.test\n" <<
				"127.0.0.1 7.passenger.test 8.passenger.test 9.passenger.test\n"
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
	
	
	######## Other helpers ########
	
	def when_user_switching_possible
		if Process.euid == 0
			yield
		end
	end
	
	def retry_with_time_limit(seconds, interval = 0.2)
		deadline = Time.now + seconds
		while Time.now < deadline
			if yield
				return
			else
				sleep(interval)
			end
		end
		raise "Time limit exceeded"
	end
	
	def eventually(deadline_duration = 1, check_interval = 0.05)
		deadline = Time.now + deadline_duration
		while Time.now < deadline
			if yield
				return
			else
				sleep(check_interval)
			end
		end
		raise "Time limit exceeded"
	end
	
	def remove_dir_tree(dir)
		# FileUtils.chmod_R is susceptible to race conditions:
		# if another thread/process deletes a file just before
		# chmod_R has chmodded it, then chmod_R will raise an error.
		# Keep trying until a real error has been reached or until
		# chmod_R is done.
		done = false
		while !done
			begin
				FileUtils.chmod_R(0777, dir)
				done = true
			rescue Errno::ENOENT
				done = !File.exist?(dir)
			end
		end
		FileUtils.rm_rf(dir)
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
	
	def self.touch(filename, timestamp = nil)
		File.open(filename, 'w').close
		File.utime(timestamp, timestamp, filename) if timestamp
	end
end

