#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.
require 'socket'
require 'thread'
require 'etc'
require 'phusion_passenger'
require 'phusion_passenger/plugin'
require 'phusion_passenger/standalone/command'

# We lazy load as many libraries as possible not only to improve startup performance,
# but also to ensure that we don't require libraries before we've passed the dependency
# checking stage of the runtime installer.
#
# IMPORTANT: do not directly or indirectly require native_support; we can't compile
# it yet until we have a compiler, and the runtime installer is supposed to check whether
# a compiler is installed.

module PhusionPassenger
module Standalone

class StartCommand < Command
	def self.description
		return "Start Phusion Passenger Standalone."
	end
	
	def initialize(args)
		super(args)
		@console_mutex = Mutex.new
		@termination_pipe = IO.pipe
		@threads = []
		@interruptable_threads = []
		@plugin = PhusionPassenger::Plugin.new('standalone/start_command', self, @options)
	end
	
	def run
		parse_my_options
		sanity_check_options
		
		ensure_nginx_installed
		require_file_tail if should_watch_logs?
		determine_various_resource_locations
		require_app_finder
		@app_finder = AppFinder.new(@args, @options)
		@apps = @app_finder.scan
		@plugin.call_hook(:found_apps, @apps)
		
		extra_controller_options = {}
		@plugin.call_hook(:before_creating_nginx_controller, extra_controller_options)
		create_nginx_controller(extra_controller_options)
		
		begin
			start_nginx
			show_intro_message
			daemonize if @options[:daemonize]
			Thread.abort_on_exception = true
			@plugin.call_hook(:nginx_started, @nginx)
			########################
			########################
			watch_log_files_in_background if should_watch_logs?
			wait_until_nginx_has_exited
		rescue Interrupt
			stop_threads
			stop_nginx
			exit 2
		rescue SignalException => signal
			stop_threads
			stop_nginx
			if signal.message == 'SIGINT' || signal.message == 'SIGTERM'
				exit 2
			else
				raise
			end
		rescue Exception => e
			stop_threads
			stop_nginx
			raise
		ensure
			stop_threads
		end
	ensure
		if @temp_dir
			FileUtils.rm_rf(@temp_dir) rescue nil
		end
		@plugin.call_hook(:cleanup)
	end

private
	def require_file_tail
		begin
			require 'file/tail'
		rescue LoadError
			error "Please install file-tail first: sudo gem install file-tail"
			exit 1
		end
	end
	
	def require_file_utils
		require 'fileutils' unless defined?(FileUtils)
	end
	
	def require_app_finder
		require 'phusion_passenger/standalone/app_finder' unless defined?(AppFinder)
	end
	
	def parse_my_options
		description = "Starts Phusion Passenger Standalone and serve one or more Ruby web applications."
		parse_options!("start [directory]", description) do |opts|
			opts.on("-a", "--address HOST", String,
				wrap_desc("Bind to HOST address (default: #{@options[:address]})")) do |value|
				@options[:address] = value
				@options[:tcp_explicitly_given] = true
			end
			opts.on("-p", "--port NUMBER", Integer,
				wrap_desc("Use the given port number (default: #{@options[:port]})")) do |value|
				@options[:port] = value
				@options[:tcp_explicitly_given] = true
			end
			opts.on("-S", "--socket FILE", String,
				wrap_desc("Bind to Unix domain socket instead of TCP socket")) do |value|
				@options[:socket_file] = value
			end
			
			opts.separator ""
			opts.on("-e", "--environment ENV", String,
				wrap_desc("Framework environment (default: #{@options[:env]})")) do |value|
				@options[:env] = value
			end
			opts.on("--max-pool-size NUMBER", Integer,
				wrap_desc("Maximum number of application processes (default: #{@options[:max_pool_size]})")) do |value|
				@options[:max_pool_size] = value
			end
			opts.on("--min-instances NUMBER", Integer,
				wrap_desc("Minimum number of processes per application (default: #{@options[:min_instances]})")) do |value|
				@options[:min_instances] = value
			end
			opts.on("--spawn-method NAME", String,
				wrap_desc("The spawn method to use (default: #{@options[:spawn_method]})")) do |value|
				@options[:spawn_method] = value
			end
			opts.on("--union-station-gateway HOST:PORT", String,
				wrap_desc("Specify Union Station Gateway host and port")) do |value|
				host, port = value.split(":", 2)
				port = port.to_i
				port = 443 if port == 0
				@options[:union_station_gateway_address] = host
				@options[:union_station_gateway_port] = port.to_i
			end
			opts.on("--union-station-key KEY", String,
				wrap_desc("Specify Union Station key")) do |value|
				@options[:union_station_key] = value
			end
			
			opts.separator ""
			opts.on("--ping-port NUMBER", Integer,
				wrap_desc("Use the given port number for checking whether Nginx is alive (default: same as the normal port)")) do |value|
				@options[:ping_port] = value
			end
			@plugin.call_hook(:parse_options, opts)
			
			opts.separator ""
			opts.on("-d", "--daemonize",
				wrap_desc("Daemonize into the background")) do
				@options[:daemonize] = true
			end
			opts.on("--user USERNAME", String,
				wrap_desc("User to run as. Ignored unless running as root.")) do |value|
				@options[:user] = value
			end
			opts.on("--log-file FILENAME", String,
				wrap_desc("Where to write log messages (default: console, or /dev/null when daemonized)")) do |value|
				@options[:log_file] = value
			end
			opts.on("--pid-file FILENAME", String,
				wrap_desc("Where to store the PID file")) do |value|
				@options[:pid_file] = value
			end
			opts.on("--nginx-bin FILENAME", String,
				wrap_desc("Nginx binary to use as core")) do |value|
				@options[:nginx_bin] = value
			end
			opts.on("--nginx-version VERSION", String,
				wrap_desc("Nginx version to use as core (default: #{@options[:nginx_version]})")) do |value|
				@options[:nginx_version] = value
			end
			opts.on("--nginx-tarball FILENAME", String,
				wrap_desc("If Nginx needs to be installed, then the given tarball will " +
				          "be used instead of downloading from the Internet")) do |value|
				@options[:nginx_tarball] = File.expand_path(value)
			end
		end
		@plugin.call_hook(:done_parsing_options)
	end
	
	def sanity_check_options
		if @options[:tcp_explicitly_given] && @options[:socket_file]
			error "You cannot specify both --address/--port and --socket. Please choose either one."
			exit 1
		end
		check_port_bind_permission_and_display_sudo_suggestion
		check_port_availability
	end
	
	# Most platforms don't allow non-root processes to bind to a port lower than 1024.
	# Check whether this is the case for the current platform and if so, tell the user
	# that it must re-run Phusion Passenger Standalone with sudo.
	def check_port_bind_permission_and_display_sudo_suggestion
		if !@options[:socket_file] && @options[:port] < 1024 && Process.euid != 0
			begin
				TCPServer.new('127.0.0.1', @options[:port]).close
			rescue Errno::EACCES
				require 'phusion_passenger/platform_info/ruby'
				myself = `whoami`.strip
				error "Only the 'root' user can run this program on port #{@options[:port]}. " <<
				      "You are currently running as '#{myself}'. Please re-run this program " <<
				      "with root privileges with the following command:\n\n" <<
				      
				      "  #{PlatformInfo.ruby_sudo_command} passenger start #{@original_args.join(' ')} --user=#{myself}\n\n" <<
				      
				      "Don't forget the '--user' part! That will make Phusion Passenger Standalone " <<
				      "drop root privileges and switch to '#{myself}' after it has obtained " <<
				      "port #{@options[:port]}."
				exit 1
			end
		end
	end
	
	def check_port_availability
		if !@options[:socket_file]
			begin
				TCPSocket.new(@options[:address], @options[:port]).close
				port_taken = true
			rescue SystemCallError
				port_taken = false
			end
			if port_taken
				error "The address #{@options[:address]}:#{@options[:port]} is already " <<
				      "in use by another process, perhaps another Phusion Passenger " <<
				      "Standalone instance.\n\n" <<
				      "If you want to run this Phusion Passenger Standalone instance on " <<
				      "another port, use the -p option, like this:\n\n" <<
				      "  passenger start -p #{@options[:port] + 1}"
				exit 1
			end
		end
	end
	
	def should_watch_logs?
		return !@options[:daemonize] && @options[:log_file] != "/dev/null"
	end
	
	def listening_on_unix_domain_socket?
		return !!@options[:socket_file]
	end
	
	# Returns the URL that Nginx will be listening on.
	def listen_url
		if @options[:socket_file]
			return @options[:socket_file]
		else
			result = "http://#{@options[:address]}"
			if @options[:port] != 80
				result << ":#{@options[:port]}"
			end
			result << "/"
			return result
		end
	end
	
	def install_runtime
		require 'phusion_passenger/standalone/runtime_installer'
		installer = RuntimeInstaller.new(
			:source_root => SOURCE_ROOT,
			:support_dir => passenger_support_files_dir,
			:nginx_dir   => nginx_dir,
			:version     => @options[:nginx_version],
			:tarball     => @options[:nginx_tarball],
			:plugin      => @plugin)
		installer.start
	end
	
	def passenger_support_files_dir
		return "#{@runtime_dir}/support"
	end
	
	def nginx_dir
		return "#{@runtime_dir}/nginx-#{@options[:nginx_version]}"
	end
	
	def ensure_nginx_installed
		if @options[:nginx_bin] && !File.exist?(@options[:nginx_bin])
			error "The given Nginx binary '#{@options[:nginx_bin]}' does not exist."
			exit 1
		end
		
		home           = Etc.getpwuid.dir
		@runtime_dir   = "#{GLOBAL_STANDALONE_RESOURCE_DIR}/#{runtime_version_string}"
		if !File.exist?("#{nginx_dir}/sbin/nginx")
			if Process.euid == 0
				install_runtime
			else
				@runtime_dir = "#{home}/#{LOCAL_STANDALONE_RESOURCE_DIR}/#{runtime_version_string}"
				if !File.exist?("#{nginx_dir}/sbin/nginx")
					install_runtime
				end
			end
		end
	end
	
	def ensure_directory_exists(dir)
		if !File.exist?(dir)
			require_file_utils
			FileUtils.mkdir_p(dir)
		end
	end
	
	def start_nginx
		begin
			@nginx.start
		rescue DaemonController::AlreadyStarted
			begin
				pid = @nginx.pid
			rescue SystemCallError, IOError
				pid = nil
			end
			if pid
				error "Phusion Passenger Standalone is already running on PID #{pid}."
			else
				error "Phusion Passenger Standalone is already running."
			end
			exit 1
		rescue DaemonController::StartError => e
			error "Could not start Passenger Nginx core:\n#{e}"
			exit 1
		end
	end
	
	def show_intro_message
		puts "=============== Phusion Passenger Standalone web server started ==============="
		puts "PID file: #{@options[:pid_file]}"
		puts "Log file: #{@options[:log_file]}"
		puts "Environment: #{@options[:env]}"
		
		puts "Accessible via: #{listen_url}"
		
		puts
		if @options[:daemonize]
			puts "Serving in the background as a daemon."
		else
			puts "You can stop Phusion Passenger Standalone by pressing Ctrl-C."
		end
		puts "==============================================================================="
	end
	
	def daemonize
		pid = fork
		if pid
			# Parent
			exit!(0)
		else
			# Child
			trap "HUP", "IGNORE"
			STDIN.reopen("/dev/null", "r")
			STDOUT.reopen(@options[:log_file], "a")
			STDERR.reopen(@options[:log_file], "a")
			STDOUT.sync = true
			STDERR.sync = true
			Process.setsid
		end
	end
	
	# Wait until the termination pipe becomes readable (a hint for threads
	# to shut down), or until the timeout has been reached. Returns true if
	# the termination pipe became readable, false if the timeout has been reached.
	def wait_on_termination_pipe(timeout)
		ios = select([@termination_pipe[0]], nil, nil, timeout)
		return !ios.nil?
	end
	
	def watch_log_file(log_file)
		if File.exist?(log_file)
			backward = 0
		else
			# File::Tail bails out if the file doesn't exist, so wait until it exists.
			while !File.exist?(log_file)
				sleep 1
			end
			backward = 10
		end
		
		File::Tail::Logfile.open(log_file, :backward => backward) do |log|
			log.interval = 0.1
			log.max_interval = 1
			log.tail do |line|
				@console_mutex.synchronize do
					STDOUT.write(line)
					STDOUT.flush
				end
			end
		end
	end
	
	def watch_log_files_in_background
		require_file_tail
		@apps.each do |app|
			thread = Thread.new do
				watch_log_file("#{app[:root]}/log/#{@options[:env]}.log")
			end
			@threads << thread
			@interruptable_threads << thread
		end
		thread = Thread.new do
			watch_log_file(@options[:log_file])
		end
		@threads << thread
		@interruptable_threads << thread
	end
	
	def wait_until_nginx_has_exited
		# Since Nginx is not our child process (it daemonizes or we daemonize)
		# we cannot use Process.waitpid to wait for it. A busy-sleep-loop with
		# Process.kill(0, pid) isn't very efficient. Instead we do this:
		#
		# Connect to Nginx and wait until Nginx disconnects the socket because of
		# timeout. Keep doing this until we can no longer connect.
		while true
			if @options[:socket_file]
				socket = UNIXSocket.new(@options[:socket_file])
			else
				socket = TCPSocket.new(@options[:address], nginx_ping_port)
			end
			begin
				socket.read rescue nil
			ensure
				socket.close rescue nil
			end
		end
	rescue Errno::ECONNREFUSED, Errno::ECONNRESET
	end
	
	def stop_nginx
		@console_mutex.synchronize do
			STDOUT.write("Stopping web server...")
			STDOUT.flush
			@nginx.stop
			STDOUT.puts " done"
			STDOUT.flush
		end
	end
	
	def stop_threads
		if !@termination_pipe[1].closed?
			@termination_pipe[1].write("x")
			@termination_pipe[1].close
		end
		@interruptable_threads.each do |thread|
			thread.terminate
		end
		@interruptable_threads = []
		@threads.each do |thread|
			thread.join
		end
		@threads = []
	end
	
	#### Config file template helpers ####
	
	def nginx_listen_address(options = @options, for_ping_port = false)
		if options[:socket_file]
			return "unix:" + File.expand_path(options[:socket_file])
		else
			if for_ping_port
				port = options[:ping_port]
			else
				port = options[:port]
			end
			return "#{options[:address]}:#{port}"
		end
	end
	
	def default_group_for(username)
		user = Etc.getpwnam(username)
		group = Etc.getgrgid(user.gid)
		return group.name
	end
	
	#################
end

end # module Standalone
end # module PhusionPassenger
