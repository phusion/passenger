require 'erb'
require 'fileutils'
PhusionPassenger.require_passenger_lib 'platform_info/apache'
PhusionPassenger.require_passenger_lib 'platform_info/ruby'

# A class for starting, stopping and restarting Apache, and for manipulating
# its configuration file. This is used by the integration tests.
#
# Before a test begins, the test instructs Apache2Controller to create an Apache
# configuration folder, which contains an Apache configuration file and other
# configuration resources that Apache needs. The Apache configuration file is
# created from a template (see Apache2Controller::STUB_DIR).
# The test can define configuration customizations. For example, it can tell
# Apache2Controller to add configuration options, virtual host definitions, etc.
#
# After the configuration folder has been created, Apache2Controller will start
# Apache. After Apache has been started, the test will be run. Apache2Controller
# will stop Apache after the test is done.
#
# Apache2Controller ensures that starting, stopping and restarting are not prone
# to race conditions. For example, it ensures that when #start returns, Apache
# really is listening on its server socket instead of still initializing.
#
# == Usage
#
# Suppose that you want to test a hypothetical "AlwaysPrintHelloWorld"
# Apache configuration option. Then you can write the following test:
#
#   apache = Apache2Controller.new
#   
#   # Add a configuration option to the configuration file.
#   apache << "AlwaysPrintHelloWorld on"
#   
#   # Write configuration file and start Apache with that configuration file.
#   apache.start
#   
#   begin
#       response_body = http_get("http://localhost:#{apache.port}/some/url")
#       response_body.should == "hello world!"
#   ensure
#       apache.stop
#   end
class Apache2Controller
	include PhusionPassenger
	STUB_DIR = File.expand_path(File.dirname(__FILE__) + "/../stub/apache2")
	
	class VHost
		attr_accessor :domain
		attr_accessor :document_root
		attr_accessor :additional_configs
		
		def initialize(domain, document_root)
			@domain = domain
			@document_root = document_root
			@additional_configs = []
		end
		
		def <<(config)
			@additional_configs << config
		end
	end
	
	attr_accessor :port
	attr_accessor :vhosts
	attr_reader :server_root
	
	def initialize(options = nil)
		set(options) if options
		@port = 64506
		@vhosts = []
		@extra = []
		@server_root = File.expand_path('tmp.apache2')
		@passenger_root = File.expand_path(PhusionPassenger.source_root)
		@mod_passenger = PhusionPassenger.apache2_module_path
	end
	
	def set(options)
		options.each_pair do |key, value|
			instance_variable_set("@#{key}", value)
		end
	end
	
	# Create an Apache configuration folder and start Apache on that
	# configuration folder. This method does not return until Apache
	# has done initializing.
	#
	# If Apache is already started, this this method will stop Apache first.
	def start
		if running?
			stop
		else
			File.unlink("#{@server_root}/httpd.pid") rescue nil
		end
		
		if File.exist?(@server_root)
			FileUtils.rm_r(@server_root)
		end
		FileUtils.mkdir_p(@server_root)
		write_config_file
		FileUtils.cp("#{STUB_DIR}/mime.types", @server_root)
		
		if !system(PlatformInfo.httpd, "-f", "#{@server_root}/httpd.conf", "-k", "start")
			raise "Could not start an Apache server."
		end
		
		begin
			# Wait until the PID file has been created.
			Timeout::timeout(20) do
				while !File.exist?("#{@server_root}/httpd.pid")
					sleep(0.1)
				end
			end
			# Wait until Apache is listening on the server port.
			Timeout::timeout(7) do
				done = false
				while !done
					begin
						socket = TCPSocket.new('localhost', @port)
						socket.close
						done = true
					rescue Errno::ECONNREFUSED
						sleep(0.1)
					end
				end
			end
		rescue Timeout::Error
			raise "Could not start an Apache server."
		end
		Dir["#{@server_root}/*"].each do |filename|
			if File.file?(filename)
				File.chmod(0666, filename)
			end
		end
	end
	
	def graceful_restart
		write_config_file
		if !system(PlatformInfo.httpd, "-f", "#{@server_root}/httpd.conf", "-k", "graceful")
			raise "Cannot restart Apache."
		end
	end
	
	# Stop Apache and delete its configuration folder. This method waits
	# until Apache is done with its shutdown procedure.
	#
	# This method does nothing if Apache is already stopped.
	def stop
		pid_file = "#{@server_root}/httpd.pid"
		if File.exist?(pid_file)
			begin
				pid = File.read(pid_file).strip.to_i
				Process.kill('SIGTERM', pid)
			rescue Errno::ESRCH
				# Looks like a stale pid file.
				FileUtils.rm_r(@server_root)
				return
			end
		end
		begin
			# Wait until the PID file is removed.
			Timeout::timeout(17) do
				while File.exist?(pid_file)
					sleep(0.1)
				end
			end
			# Wait until the server socket is closed.
			Timeout::timeout(7) do
				done = false
				while !done
					begin
						socket = TCPSocket.new('localhost', @port)
						socket.close
						sleep(0.1)
					rescue SystemCallError
						done = true
					end
				end
			end
		rescue Timeout::Error
			raise "Unable to stop Apache."
		end
		if File.exist?(@server_root)
			FileUtils.chmod_R(0777, @server_root)
			FileUtils.rm_r(@server_root)
		end
	end
	
	# Define a virtual host configuration block for the Apache configuration
	# file. If there was already a vhost definition with the same domain name,
	# then it will be overwritten.
	#
	# The given document root will be created if it doesn't exist.
	def set_vhost(domain, document_root)
		FileUtils.mkdir_p(document_root)
		vhost = VHost.new(domain, document_root)
		if block_given?
			yield vhost
		end
		vhosts.reject! {|host| host.domain == domain}
		vhosts << vhost
	end
	
	# Checks whether this Apache instance is running.
	def running?
		if File.exist?("#{@server_root}/httpd.pid")
			pid = File.read("#{@server_root}/httpd.pid").strip
			begin
				Process.kill(0, pid.to_i)
				return true
			rescue Errno::ESRCH
				return false
			rescue SystemCallError
				return true
			end
		else
			return false
		end
	end
	
	# Defines a configuration snippet to be added to the Apache configuration file.
	def <<(line)
		@extra << line
	end

private
	def get_binding
		return binding
	end
	
	def write_config_file
		template = ERB.new(File.read("#{STUB_DIR}/httpd.conf.erb"))
		File.open("#{@server_root}/httpd.conf", 'w') do |f|
			f.write(template.result(get_binding))
		end
	end
	
	def modules_dir
		@@modules_dir ||= `#{PlatformInfo.apxs2} -q LIBEXECDIR`.strip
	end
	
	def builtin_modules
		@@builtin_modules ||= `#{PlatformInfo.httpd} -l`.split("\n").grep(/\.c$/).map do |line|
			line.strip
		end
	end
	
	def has_builtin_module?(name)
		return builtin_modules.include?(name)
	end
	
	def has_module?(name)
		return File.exist?("#{modules_dir}/#{name}")
	end
	
	def we_are_root?
		return Process.uid == 0
	end
end
