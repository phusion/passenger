require 'fileutils'
require 'resolv'
require 'net/http'
require 'uri'
require 'support/multipart'
PhusionPassenger.require_passenger_lib 'debug_logging'
PhusionPassenger.require_passenger_lib 'platform_info/ruby'

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
			return File.binread("#{@full_app_root}/public/#{name}")
		end
	
	private
		def stub_source_dir
			return "stub/#{@name}"
		end
		
		def copy_stub_contents
			FileUtils.cp_r("#{stub_source_dir}/.", @full_app_root)
		end
	end
	
	class ClassicRailsStub < Stub
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
		
	private
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

	class PythonStub < Stub
		def startup_file
			return "#{@full_app_root}/passenger_wsgi.py"
		end
	end

	class NodejsStub < Stub
		def startup_file
			return "#{@full_app_root}/app.js"
		end
	end
	
	def describe_rails_versions(matcher, &block)
		if ENV['ONLY_RAILS_VERSION'] && !ENV['ONLY_RAILS_VERSION'].empty?
			found_versions = [ENV['ONLY_RAILS_VERSION']]
		else
			found_versions = Dir.entries("stub/rails_apps").grep(/^\d+\.\d+$/)
			if RUBY_VERSION >= '1.9.0'
				# Only Rails >= 2.3 is compatible with Ruby 1.9.
				found_versions.reject! do |version|
					version < '2.3'
				end
			elsif RUBY_VERSION <= '1.8.6'
				# Rails >= 3 dropped support for 1.8.6 and older.
				found_versions.reject! do |version|
					version >= '3.0'
				end
			end
		end
		
		case matcher
		when /^<= (.+)$/
			max_version = $1
			found_versions.reject! do |version|
				version > max_version
			end
		when /^>= (.+)$/
			min_version = $1
			found_versions.reject! do |version|
				version < min_version
			end
		when /^= (.+)$/
			exact_version = $1
			found_versions.reject! do |version|
				version != exact_version
			end
		else
			raise ArgumentError, "Unknown matcher string '#{matcher}'"
		end
		
		found_versions.sort.each do |version|
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
					"  dscacheutil -flushcache"
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
	
	alias when_running_as_root when_user_switching_possible
	
	def when_not_running_as_root
		if Process.euid != 0
			yield
		end
	end
	
	def eventually(deadline_duration = 2, check_interval = 0.05)
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
	
	def should_never_happen(deadline_duration = 1, check_interval = 0.05)
		deadline = Time.now + deadline_duration
		while Time.now < deadline
			if yield
				raise "That which shouldn't happen happened anyway"
			else
				sleep(check_interval)
			end
		end
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
	
	def spawn_process(*args)
		args.map! do |arg|
			arg.to_s
		end
		if Process.respond_to?(:spawn)
			return Process.spawn(*args)
		else
			return fork do
				exec(*args)
			end
		end
	end
	
	# Run a script in a Ruby subprocess. *args are program arguments to
	# pass to the script. Returns the script's stdout output.
	def run_script(code, *args)
		stdin_child, stdin_parent = IO.pipe
		stdout_parent, stdout_child = IO.pipe
		program_args = [PhusionPassenger::PlatformInfo.ruby_command, "-e",
			"eval(STDIN.read, binding, '(script)', 0)",
			PhusionPassenger::LIBDIR, *args]
		if Process.respond_to?(:spawn)
			program_args << {
				STDIN  => stdin_child,
				STDOUT => stdout_child,
				STDERR => STDERR,
				:close_others => true
			}
			pid = Process.spawn(*program_args)
		else
			pid = fork do
				stdin_parent.close
				stdout_parent.close
				STDIN.reopen(stdin_child)
				STDOUT.reopen(stdout_child)
				stdin_child.close
				stdout_child.close
				exec(*program_args)
			end
		end
		stdin_child.close
		stdout_child.close
		stdin_parent.write(
			%Q[require(ARGV.shift + "/phusion_passenger")
			#{code}])
		stdin_parent.close
		result = stdout_parent.read
		stdout_parent.close
		Process.waitpid(pid)
		return result
	rescue Exception
		Process.kill('SIGKILL', pid) if pid
		raise
	ensure
		[stdin_child, stdout_child, stdin_parent, stdout_parent].each do |io|
			io.close if io && !io.closed?
		end
		begin
			Process.waitpid(pid) if pid
		rescue Errno::ECHILD, Errno::ESRCH
		end
	end
	
	def spawn_logging_agent(dump_file, password)
		passenger_tmpdir = PhusionPassenger::Utils.passenger_tmpdir
		socket_filename = "#{passenger_tmpdir}/logging.socket"
		pid = spawn_process("#{PhusionPassenger.agents_dir}/PassengerLoggingAgent",
			"passenger_root", PhusionPassenger.source_root,
			"log_level", PhusionPassenger::DebugLogging.log_level,
			"analytics_dump_file", dump_file,
			"analytics_log_user",  CONFIG['normal_user_1'],
			"analytics_log_group", CONFIG['normal_group_1'],
			"analytics_log_permissions", "u=rwx,g=rwx,o=rwx",
			"logging_agent_address", "unix:#{socket_filename}",
			"logging_agent_password", password,
			"logging_agent_admin_address", "unix:#{socket_filename}_admin",
			"admin_tool_status_password", password)
		eventually do
			File.exist?(socket_filename)
		end
		return [pid, socket_filename, "unix:#{socket_filename}"]
	rescue Exception => e
		if pid
			Process.kill('KILL', pid)
			Process.waitpid(pid)
		end
		raise e
	end
	
	def flush_logging_agent(password, socket_address)
		PhusionPassenger.require_passenger_lib 'message_client' if !defined?(PhusionPassenger::MessageClient)
		client = PhusionPassenger::MessageClient.new("logging", password, socket_address)
		begin
			client.write("flush")
			client.read
		ensure
			client.close
		end
	end
	
	def inspect_server(name)
		instance = PhusionPassenger::AdminTools::ServerInstance.list.first
		if name
			instance.connect(:passenger_status) do
				return instance.send(name)
			end
		else
			return instance
		end
	end

	if "".respond_to?(:force_encoding)
		def binary_string(str)
			return str.force_encoding("binary")
		end
	else
		def binary_string(str)
			return str
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
	
	def self.touch(filename, timestamp = nil)
		File.open(filename, 'w').close
		File.utime(timestamp, timestamp, filename) if timestamp
	end
	
	def self.binread(filename)
		return File.read(filename)
	end if !respond_to?(:binread)
end

