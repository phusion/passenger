require 'erb'
begin
	require 'daemon_controller'
rescue LoadError
	STDERR.puts "*** ERROR: daemon_controller is not installed. Please install with: "
	STDERR.puts
	STDERR.puts "  gem install daemon_controller"
	STDERR.puts
	exit!(1)
end

PhusionPassenger.require_passenger_lib 'platform_info/ruby'

class NginxController
	PlatformInfo = PhusionPassenger::PlatformInfo
	TEMPLATE_DIR = File.expand_path(File.dirname(__FILE__) + "/../stub/nginx")
	PORT = 64507
	
	def initialize(root_dir)
		root_dir     = File.expand_path(root_dir)
		@passenger_root = PhusionPassenger.source_root
		@nginx_root  = root_dir
		@port        = PORT
		@config_file = "#{root_dir}/nginx.conf"
		@pid_file    = "#{root_dir}/nginx.pid"
		@log_file    = "#{root_dir}/error.log"
		@controller  = DaemonController.new(
			:identifier    => 'Nginx',
			:start_command => "#{CONFIG['nginx']} -c '#{@config_file}'",
			:ping_command  => [:tcp, '127.0.0.1', PORT],
			:pid_file      => @pid_file,
			:log_file      => @log_file,
			:timeout       => 25,
			:before_start  => method(:write_nginx_config_files)
		)
		
		@servers = []
		@max_pool_size = 1
	end
	
	def set(options)
		options.each_pair do |key, value|
			instance_variable_set("@#{key}", value)
		end
	end
	
	def start
		stop
		@controller.start
	end
	
	def stop
		@controller.stop
		# On OS X, the Nginx server socket may linger around for a while
		# after Nginx shutdown, despite Nginx setting SO_REUSEADDR.
		sockaddr = Socket.pack_sockaddr_in(PORT, '127.0.0.1')
		eventually(30) do
			!@controller.send(:ping_socket, Socket::Constants::AF_INET, sockaddr)
		end
	end
	
	def running?
		return @controller.running?
	end
	
	def port
		return @port
	end
	
	def add_server
		server = Server.new
		yield server
		@servers << server
	end

private
	class Server
		attr_accessor :values
		attr_accessor :extra
		
		def initialize
			@values = { :passenger_enabled => "on" }
		end
		
		def [](key)
			return @values[key]
		end
		
		def []=(key, value)
			@values[key] = value
		end
		
		def <<(text)
			@extra = text
		end
	end

	def write_nginx_config_files
		template = ERB.new(File.read("#{TEMPLATE_DIR}/nginx.conf.erb"))
		File.write(@config_file, template.result(get_binding))
	end
	
	def get_binding
		return binding
	end
end
