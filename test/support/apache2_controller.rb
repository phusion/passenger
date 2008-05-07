require 'erb'
require 'fileutils'
require 'passenger/platform_info'

class Apache2Controller
	include PlatformInfo
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
	
	def initialize(options = nil)
		set(options) if options
		@port = 64506
		@vhosts = []
		@server_root = File.expand_path('tmp.apache2')
		@passenger_root = File.expand_path(File.dirname(__FILE__) + "/../..")
		@mod_passenger = File.expand_path(File.dirname(__FILE__) + "/../../ext/apache2/mod_passenger.so")
	end
	
	def set(options)
		options.each_pair do |key, value|
			instance_variable_set("@#{key}", value)
		end
	end
	
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
		
		if !system(HTTPD, "-f", "#{@server_root}/httpd.conf", "-k", "start")
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
		File.chmod(0666, *Dir["#{@server_root}/*"]) rescue nil
	end
	
	def graceful_restart
		write_config_file
		if !system(HTTPD, "-f", "#{@server_root}/httpd.conf", "-k", "graceful")
			raise "Cannot restart Apache."
		end
	end
	
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
			FileUtils.rm_r(@server_root)
		end
	end
	
	def add_vhost(domain, document_root)
		vhost = VHost.new(domain, document_root)
		if block_given?
			yield vhost
		end
		vhosts << vhost
	end
	
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
		@@modules_dir ||= `#{APXS2} -q LIBEXECDIR`.strip
	end
	
	def builtin_modules
		@@builtin_modules ||= `#{HTTPD} -l`.split("\n").grep(/\.c$/).map do |line|
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
