#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2014 Phusion
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
PhusionPassenger.require_passenger_lib 'plugin'
PhusionPassenger.require_passenger_lib 'standalone/command'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'

# We lazy load as many libraries as possible not only to improve startup performance,
# but also to ensure that we don't require libraries before we've passed the dependency
# checking stage of the runtime installer.

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

		PhusionPassenger.require_passenger_lib 'standalone/runtime_locator'
		@runtime_locator = RuntimeLocator.new(@options[:runtime_dir],
			@options[:nginx_version])
		ensure_runtime_installed
		set_stdout_stderr_binmode
		exit if @options[:runtime_check_only]
		require_app_finder
		@app_finder = AppFinder.new(@args, @options)
		@apps = @app_finder.scan
		@options = @app_finder.global_options
		sanity_check_server_options
		determine_various_resource_locations
		@plugin.call_hook(:found_apps, @apps)

		extra_controller_options = {}
		@plugin.call_hook(:before_creating_nginx_controller, extra_controller_options)
		create_nginx_controller(extra_controller_options)

		begin
			start_nginx
			show_intro_message
			if @options[:daemonize]
				if PlatformInfo.ruby_supports_fork?
					daemonize
				else
					daemonize_without_fork
				end
			end
			Thread.abort_on_exception = true
			@plugin.call_hook(:nginx_started, @nginx)
			########################
			########################
			touch_temp_dir_in_background
			watch_log_files_in_background if should_watch_logs?
			wait_until_nginx_has_exited if should_wait_until_nginx_has_exited?
		rescue Interrupt
			begin_shutdown
			stop_threads
			stop_nginx
			exit 2
		rescue SignalException => signal
			begin_shutdown
			stop_threads
			stop_nginx
			if signal.message == 'SIGINT' || signal.message == 'SIGTERM'
				exit 2
			else
				raise
			end
		rescue Exception => e
			begin_shutdown
			stop_threads
			stop_nginx
			raise
		ensure
			begin_shutdown
			begin
				stop_threads
			ensure
				finalize_shutdown
			end
		end
	ensure
		@plugin.call_hook(:cleanup)
	end

private
	def require_file_utils
		require 'fileutils' unless defined?(FileUtils)
	end

	def parse_my_options
		description = "Starts Phusion Passenger Standalone and serve one or more Ruby web applications."
		parse_options!("start [directory]", description) do |opts|
			opts.separator "Server options:"
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
			opts.on("--ssl",
				wrap_desc("Enable SSL support")) do
				@options[:ssl] = true
			end
			opts.on("--ssl-certificate PATH", String,
				wrap_desc("Specify the SSL certificate path")) do |val|
				@options[:ssl_certificate] = File.expand_path(val)
			end
			opts.on("--ssl-certificate-key PATH", String,
				wrap_desc("Specify the SSL key path")) do |val|
				@options[:ssl_certificate_key] = File.expand_path(val)
			end
			opts.on("--ssl-port PORT", Integer,
				wrap_desc("Listen for SSL on this port, while listening for HTTP on the normal port")) do |val|
				@options[:ssl_port] = val
			end
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
			opts.on("--temp-dir PATH", String,
				wrap_desc("Use the given temp dir")) do |value|
				ENV['TMPDIR'] = value
				@options[:temp_dir] = value
			end

			opts.separator ""
			opts.separator "Application loading options:"
			opts.on("-e", "--environment ENV", String,
				wrap_desc("Framework environment (default: #{@options[:environment]})")) do |value|
				@options[:environment] = value
			end
			opts.on("-R", "--rackup FILE", String,
				wrap_desc("Consider application a Ruby Rack app, and use the given rackup file")) do |value|
				@options[:app_type] = "rack"
				@options[:startup_file] = value
			end
			opts.on("--app-type NAME", String,
				wrap_desc("Force app to be detected as the given type")) do |value|
				@options[:app_type] = value
			end
			opts.on("--startup-file FILENAME", String,
				wrap_desc("Force given startup file to be used")) do |value|
				@options[:startup_file] = value
			end
			opts.on("--spawn-method NAME", String,
				wrap_desc("The spawn method to use (default: #{@options[:spawn_method]})")) do |value|
				@options[:spawn_method] = value
			end
			opts.on("--static-files-dir PATH", String,
				wrap_desc("Specify the static files dir")) do |val|
				@options[:static_files_dir] = File.expand_path(val)
			end
			opts.on("--restart-dir PATH", String,
				wrap_desc("Specify the restart dir")) do |val|
				@options[:restart_dir] = File.expand_path(val)
			end
			opts.on("--friendly-error-pages",
				wrap_desc("Turn on friendly error pages")) do
				@options[:friendly_error_pages] = true
			end
			opts.on("--no-friendly-error-pages",
				wrap_desc("Turn off friendly error pages")) do
				@options[:friendly_error_pages] = false
			end
			opts.on("--load-shell-envvars",
				wrap_desc("Load shell startup files before loading application")) do
				@options[:load_shell_envvars] = true
			end

			opts.separator ""
			opts.separator "Process management options:"
			opts.on("--max-pool-size NUMBER", Integer,
				wrap_desc("Maximum number of application processes (default: #{@options[:max_pool_size]})")) do |value|
				@options[:max_pool_size] = value
			end
			opts.on("--min-instances NUMBER", Integer,
				wrap_desc("Minimum number of processes per application (default: #{@options[:min_instances]})")) do |value|
				@options[:min_instances] = value
			end
			opts.on("--concurrency-model NAME", String,
				wrap_desc("The concurrency model to use, either 'process' or 'thread' (default: #{@options[:concurrency_model]}) (Enterprise only)")) do |value|
				@options[:concurrency_model] = value
			end
			opts.on("--thread-count NAME", Integer,
				wrap_desc("The number of threads to use when using the 'thread' concurrency model (default: #{@options[:thread_count]}) (Enterprise only)")) do |value|
				@options[:thread_count] = value
			end
			opts.on("--rolling-restarts",
				wrap_desc("Enable rolling restarts (Enterprise only)")) do
				@options[:rolling_restarts] = true
			end
			opts.on("--resist-deployment-errors",
				wrap_desc("Enable deployment error resistance (Enterprise only)")) do
				@options[:resist_deployment_errors] = true
			end

			opts.separator ""
			opts.separator "Request handling options:"
			opts.on("--sticky-sessions",
				wrap_desc("Enable sticky sessions")) do
				@options[:sticky_sessions] = true
			end
			opts.on("--sticky-sessions-cookie-name", String,
				wrap_desc("Cookie name to use for sticky sessions (default: #{DEFAULT_STICKY_SESSIONS_COOKIE_NAME})")) do |val|
				@options[:sticky_sessions_cookie_name] = val
			end

			opts.separator ""
			opts.separator "Union Station options:"
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
			opts.separator "Advanced options:"
			opts.on("--ping-port NUMBER", Integer,
				wrap_desc("Use the given port number for checking whether Nginx is alive (default: same as the normal port)")) do |value|
				@options[:ping_port] = value
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
			opts.on("--nginx-config-template FILENAME", String,
				wrap_desc("The template to use for generating the Nginx config file")) do |value|
				@options[:nginx_config_template] = File.expand_path(value)
			end
			opts.on("--binaries-url-root URL", String,
				wrap_desc("If Nginx needs to be installed, then the specified URL will be " +
				          "checked for binaries prior to a local build.")) do |value|
				@options[:binaries_url_root] = value
			end
			opts.on("--no-download-binaries",
				wrap_desc("Never download binaries")) do
				@options[:download_binaries] = false
			end
			opts.on("--runtime-dir DIRECTORY", String,
				wrap_desc("Directory to use for Phusion Passenger Standalone runtime files")) do |value|
				@options[:runtime_dir] = File.expand_path(value)
			end
			opts.on("--runtime-check-only",
				wrap_desc("Quit after checking whether the Phusion Passenger Standalone runtime files are installed")) do
				@options[:runtime_check_only] = true
			end
			opts.on("--no-compile-runtime",
				wrap_desc("Abort if runtime must be compiled")) do
				@options[:dont_compile_runtime] = true
			end

			@plugin.call_hook(:parse_options, opts)
			opts.separator ""
		end
		@plugin.call_hook(:done_parsing_options)
	end

	def sanity_check_server_options
		if @options[:tcp_explicitly_given] && @options[:socket_file]
			error "You cannot specify both --address/--port and --socket. Please choose either one."
			exit 1
		end
		if @options[:ssl] && !@options[:ssl_certificate]
			error "You specified --ssl. Please specify --ssl-certificate as well."
			exit 1
		end
		if @options[:ssl] && !@options[:ssl_certificate_key]
			error "You specified --ssl. Please specify --ssl-certificate-key as well."
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
				PhusionPassenger.require_passenger_lib 'platform_info/ruby'
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

	if defined?(RUBY_ENGINE) && RUBY_ENGINE == "jruby"
		require 'java'

		def check_port(host_name, port)
			channel = java.nio.channels.SocketChannel.open
			begin
				address = java.net.InetSocketAddress.new(host_name, port)
				channel.configure_blocking(false)
				if channel.connect(address)
					return true
				end

				deadline = Time.now.to_f + 0.1
				done = false
				while true
					begin
						if channel.finish_connect
							return true
						end
					rescue java.net.ConnectException => e
						if e.message =~ /Connection refused/i
							return false
						else
							throw e
						end
					end

					# Not done connecting and no error.
					sleep 0.01
					if Time.now.to_f >= deadline
						return false
					end
				end
			ensure
				channel.close
			end
		end
	else
		def check_port_with_protocol(address, port, protocol)
			begin
				socket = Socket.new(protocol, Socket::Constants::SOCK_STREAM, 0)
				sockaddr = Socket.pack_sockaddr_in(port, address)
				begin
					socket.connect_nonblock(sockaddr)
				rescue Errno::ENOENT, Errno::EINPROGRESS, Errno::EAGAIN, Errno::EWOULDBLOCK
					if select(nil, [socket], nil, 0.1)
						begin
							socket.connect_nonblock(sockaddr)
						rescue Errno::EISCONN
						rescue Errno::EINVAL
							if PlatformInfo.os_name =~ /freebsd/i
								raise Errno::ECONNREFUSED
							else
								raise
							end
						end
					else
						raise Errno::ECONNREFUSED
					end
				end
				return true
			rescue Errno::ECONNREFUSED
				return false
			ensure
				socket.close if socket && !socket.closed?
			end
		end

		def check_port(address, port)
			begin
				check_port_with_protocol(address, port, Socket::Constants::AF_INET)
			rescue Errno::EAFNOSUPPORT
				check_port_with_protocol(address, port, Socket::Constants::AF_INET6)
			end
		end
	end

	def check_port_availability
		if !@options[:socket_file] && check_port(@options[:address], @options[:port])
			error "The address #{@options[:address]}:#{@options[:port]} is already " <<
			      "in use by another process, perhaps another Phusion Passenger " <<
			      "Standalone instance.\n\n" <<
			      "If you want to run this Phusion Passenger Standalone instance on " <<
			      "another port, use the -p option, like this:\n\n" <<
			      "  passenger start -p #{@options[:port] + 1}"
			exit 1
		end
	end

	def should_watch_logs?
		return !@options[:daemonize] && @options[:log_file] != "/dev/null"
	end

	def should_wait_until_nginx_has_exited?
		return !@options[:daemonize] || @app_finder.multi_mode?
	end

	# Returns the URL that Nginx will be listening on.
	def listen_url
		if @options[:socket_file]
			return @options[:socket_file]
		else
			if @options[:ssl] && !@options[:ssl_port]
				scheme = "https"
			else
				scheme = "http"
			end
			result = "#{scheme}://"
			if @options[:port] == 80
				result << @options[:address]
			else
				result << compose_ip_and_port(@options[:address], @options[:port])
			end
			result << "/"
			return result
		end
	end

	def install_runtime(runtime_locator)
		PhusionPassenger.require_passenger_lib 'standalone/runtime_installer'
		installer = RuntimeInstaller.new(
			:targets     => runtime_locator.install_targets,
			:support_dir => runtime_locator.support_dir_install_destination,
			:nginx_dir   => runtime_locator.nginx_binary_install_destination,
			:lib_dir     => runtime_locator.find_lib_dir || runtime_locator.support_dir_install_destination,
			:nginx_version     => @options[:nginx_version],
			:nginx_tarball     => @options[:nginx_tarball],
			:binaries_url_root => @options[:binaries_url_root],
			:download_binaries => @options.fetch(:download_binaries, true),
			:dont_compile_runtime => @options[:dont_compile_runtime],
			:plugin      => @plugin)
		return installer.run
	end

	def ensure_runtime_installed
		if @runtime_locator.everything_installed?
			if !File.exist?(@runtime_locator.find_nginx_binary)
				error "The web helper binary '#{@runtime_locator.find_nginx_binary}' does not exist."
				exit 1
			end
		else
			if !@runtime_locator.find_support_dir && PhusionPassenger.natively_packaged?
				error "Your Phusion Passenger Standalone installation is broken: the support " +
					"files could not be found. Please reinstall Phusion Passenger Standalone. " +
					"If this problem persists, please contact your packager."
				exit 1
			end
			install_runtime(@runtime_locator) || exit(1)
			@runtime_locator.reload
		end
	end

	def set_stdout_stderr_binmode
		# We already set STDOUT and STDERR to binmode in bin/passenger, which
		# fixes https://github.com/phusion/passenger-ruby-heroku-demo/issues/11.
		# However RuntimeInstaller sets them to UTF-8, so here we set them back.
		STDOUT.binmode
		STDERR.binmode
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
		puts "Environment: #{@options[:environment]}"
		puts "Accessible via: #{listen_url}"

		puts
		if @options[:daemonize]
			puts "Serving in the background as a daemon."
		else
			puts "You can stop Phusion Passenger Standalone by pressing Ctrl-C."
		end
		puts "Problems? Check #{STANDALONE_DOC_URL}#troubleshooting"
		puts "==============================================================================="
	end

	def daemonize_without_fork
		STDERR.puts "Unable to daemonize using the current Ruby interpreter " +
			"(#{PlatformInfo.ruby_command}) because it does not support forking."
		exit 1
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
			# tail bails out if the file doesn't exist, so wait until it exists.
			while !File.exist?(log_file)
				sleep 1
			end
			backward = 10
		end

		IO.popen("tail -f -n #{backward} \"#{log_file}\"", "rb") do |f|
			begin
				while true
					begin
						line = f.readline
						@console_mutex.synchronize do
							STDOUT.write(line)
							STDOUT.flush
						end
					rescue EOFError
						break
					end
				end
			ensure
				Process.kill('TERM', f.pid) rescue nil
			end
		end
	end

	def watch_log_files_in_background
		@apps.each do |app|
			thread = Thread.new do
				watch_log_file("#{app[:root]}/log/#{@options[:environment]}.log")
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

	def touch_temp_dir_in_background
		result = system("#{@runtime_locator.find_agents_dir}/TempDirToucher",
			@temp_dir,
			"--cleanup",
			"--daemonize",
			"--pid-file", "#{@temp_dir}/temp_dir_toucher.pid",
			"--log-file", @options[:log_file])
		if !result
			error "Cannot start #{@runtime_locator.find_agents_dir}/TempDirToucher"
			exit 1
		end
	end

	def begin_shutdown
		return if @shutting_down
		@shutting_down = 1
		trap("INT", &method(:signal_during_shutdown))
		trap("TERM", &method(:signal_during_shutdown))
	end

	def finalize_shutdown
		@shutting_down = nil
		trap("INT", "DEFAULT")
		trap("TERM", "DEFAULT")
	end

	def signal_during_shutdown(signal)
		if @shutting_down == 1
			@shutting_down += 1
			puts "Ignoring signal #{signal} during shutdown. Send it again to force exit."
		else
			exit!(1)
		end
	end

	def stop_touching_temp_dir_in_background
		if @toucher
			begin
				Process.kill('TERM', @toucher.pid)
			rescue Errno::ESRCH, Errno::ECHILD
			end
			@toucher.close
		end
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
			return compose_ip_and_port(options[:address], port)
		end
	end

	def nginx_listen_address_with_ssl_port(options = @options)
		if options[:socket_file]
			return "unix:" + File.expand_path(options[:socket_file])
		else
			return compose_ip_and_port(options[:address], options[:ssl_port])
		end
	end

	def compose_ip_and_port(ip, port)
		if ip =~ /:/
			# IPv6
			return "[#{ip}]:#{port}"
		else
			return "#{ip}:#{port}"
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
