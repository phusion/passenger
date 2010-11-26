# encoding: binary
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

require 'rubygems'
require 'thread'
if (!defined?(RUBY_ENGINE) || RUBY_ENGINE == "ruby") && RUBY_VERSION < "1.8.7"
	require 'fastthread'
end
require 'pathname'
require 'etc'
require 'fcntl'
require 'tempfile'
require 'timeout'
require 'stringio'
require 'phusion_passenger/exceptions'
require 'phusion_passenger/native_support'

module PhusionPassenger

# Utility functions.
module Utils
protected
	def private_class_method(name)
		metaclass = class << self; self; end
		metaclass.send(:private, name)
	end
	
	# Return the canonicalized version of +path+. This path is guaranteed to
	# to be "normal", i.e. it doesn't contain stuff like ".." or "/",
	# and it fully resolves symbolic links.
	#
	# Raises SystemCallError if something went wrong. Raises ArgumentError
	# if +path+ is nil. Raises InvalidPath if +path+ does not appear
	# to be a valid path.
	def canonicalize_path(path)
		raise ArgumentError, "The 'path' argument may not be nil" if path.nil?
		return Pathname.new(path).realpath.to_s
	rescue Errno::ENOENT => e
		raise InvalidAPath, e.message
	end
	
	# Assert that +path+ is a directory. Raises +InvalidPath+ if it isn't.
	def assert_valid_directory(path)
		if !File.directory?(path)
			raise InvalidPath, "'#{path}' is not a valid directory."
		end
	end
	
	# Assert that +path+ is a file. Raises +InvalidPath+ if it isn't.
	def assert_valid_file(path)
		if !File.file?(path)
			raise InvalidPath, "'#{path}' is not a valid file."
		end
	end
	
	# Assert that +username+ is a valid username. Raises
	# ArgumentError if that is not the case.
	def assert_valid_username(username)
		# If username does not exist then getpwnam() will raise an ArgumentError.
		username && Etc.getpwnam(username)
	end
	
	# Assert that +groupname+ is a valid group name. Raises
	# ArgumentError if that is not the case.
	def assert_valid_groupname(groupname)
		# If groupname does not exist then getgrnam() will raise an ArgumentError.
		groupname && Etc.getgrnam(groupname)
	end
	
	# Generate a long, cryptographically secure random ID string, which
	# is also a valid filename.
	def generate_random_id(method)
		case method
		when :base64
			data = [File.read("/dev/urandom", 64)].pack('m')
			data.gsub!("\n", '')
			data.gsub!("+", '')
			data.gsub!("/", '')
			data.gsub!(/==$/, '')
			return data
		when :hex
			return File.read("/dev/urandom", 64).unpack('H*')[0]
		else
			raise ArgumentError, "Invalid method #{method.inspect}"
		end
	end
	
	def close_all_io_objects_for_fds(file_descriptors_to_leave_open)
		ObjectSpace.each_object(IO) do |io|
			begin
				if !file_descriptors_to_leave_open.include?(io.fileno) && !io.closed?
					io.close
				end
			rescue
			end
		end
	end
	
	def marshal_exception(exception)
		data = {
			:message => exception.message,
			:class => exception.class.to_s,
			:backtrace => exception.backtrace
		}
		if exception.is_a?(InitializationError)
			data[:is_initialization_error] = true
			if exception.child_exception
				data[:child_exception] = marshal_exception(exception.child_exception)
				child_exception = exception.child_exception
				exception.child_exception = nil
				data[:exception] = Marshal.dump(exception)
				exception.child_exception = child_exception
			end
		else
			begin
				data[:exception] = Marshal.dump(exception)
			rescue ArgumentError, TypeError
				e = UnknownError.new(exception.message, exception.class.to_s,
							exception.backtrace)
				data[:exception] = Marshal.dump(e)
			end
		end
		return Marshal.dump(data)
	end
	
	def unmarshal_exception(data)
		hash = Marshal.load(data)
		if hash[:is_initialization_error]
			if hash[:child_exception]
				child_exception = unmarshal_exception(hash[:child_exception])
			else
				child_exception = nil
			end
			
			exception = Marshal.load(hash[:exception])
			exception.child_exception = child_exception
			return exception
		else
			begin
				return Marshal.load(hash[:exception])
			rescue ArgumentError, TypeError
				return UnknownError.new(hash[:message], hash[:class], hash[:backtrace])
			end
		end
	end
	
	# Print the given exception, including the stack trace, to STDERR.
	#
	# +current_location+ is a string which describes where the code is
	# currently at. Usually the current class name will be enough.
	def print_exception(current_location, exception, destination = nil)
		if !exception.is_a?(SystemExit)
			data = exception.backtrace_string(current_location)
			if defined?(DebugLogging) && self.is_a?(DebugLogging)
				error(data)
			else
				destination ||= STDERR
				destination.puts(data)
				destination.flush if destination.respond_to?(:flush)
			end
		end
	end
	
	# Prepare an application process using rules for the given spawn options.
	# This method is to be called before loading the application code.
	#
	# +startup_file+ is the application type's startup file, e.g.
	# "config/environment.rb" for Rails apps and "config.ru" for Rack apps.
	# See SpawnManager#spawn_application for options.
	#
	# This function may modify +options+. The modified options are to be
	# passed to the request handler.
	def prepare_app_process(startup_file, options)
		options["app_root"] = canonicalize_path(options["app_root"])
		Dir.chdir(options["app_root"])
		
		lower_privilege(startup_file, options)
		path, is_parent = check_directory_tree_permissions(options["app_root"])
		if path
			username = Etc.getpwuid(Process.euid).name
			groupname = Etc.getgrgid(Process.egid).name
			message = "This application process is currently running as " +
				"user '#{username}' and group '#{groupname}' and must be " +
				"able to access its application root directory " +
				"'#{options["app_root"]}'. "
			if is_parent
				message << "However the parent directory '#{path}' " +
					"has wrong permissions, thereby preventing " +
					"this process from accessing its application " +
					"root directory. Please fix the permissions " +
					"of the directory '#{path}' first."
			else
				message << "However this directory is not accessible " +
					"because it has wrong permissions. Please fix " +
					"these permissions first."
			end
			raise(message)
		end
		
		ENV["RAILS_ENV"] = ENV["RACK_ENV"] = options["environment"]
		
		base_uri = options["base_uri"]
		if base_uri && !base_uri.empty? && base_uri != "/"
			ENV["RAILS_RELATIVE_URL_ROOT"] = base_uri
			ENV["RACK_BASE_URI"] = base_uri
		end
		
		encoded_environment_variables = options["environment_variables"]
		if encoded_environment_variables
			env_vars_string = encoded_environment_variables.unpack("m").first
			env_vars_array  = env_vars_string.split("\0", -1)
			env_vars_array.pop
			env_vars = Hash[*env_vars_array]
			env_vars.each_pair do |key, value|
				ENV[key] = value
			end
		end
		
		# Instantiate the analytics logger if requested. Can be nil.
		require 'phusion_passenger/analytics_logger'
		options["analytics_logger"] = AnalyticsLogger.new_from_options(options)
		
		# Make sure RubyGems uses any new environment variable values
		# that have been set now (e.g. $HOME, $GEM_HOME, etc) and that
		# it is able to detect newly installed gems.
		Gem.clear_paths
		
		# Because spawned app processes exit using #exit!, #at_exit
		# blocks aren't called. Here we ninja patch Kernel so that
		# we can call #at_exit blocks during app process shutdown.
		class << Kernel
			def passenger_call_at_exit_blocks
				@passenger_at_exit_blocks ||= []
				@passenger_at_exit_blocks.reverse_each do |block|
					block.call
				end
			end
			
			def passenger_at_exit(&block)
				@passenger_at_exit_blocks ||= []
				@passenger_at_exit_blocks << block
				return block
			end
		end
		Kernel.class_eval do
			def at_exit(&block)
				return Kernel.passenger_at_exit(&block)
			end
		end
		
		
		# Rack::ApplicationSpawner depends on the 'rack' library, but the app
		# might want us to use a bundled version instead of a
		# gem/apt-get/yum/whatever-installed version. Therefore we must setup
		# the correct load paths before requiring 'rack'.
		#
		# The most popular tool for bundling dependencies is Bundler. Bundler
		# works as follows:
		# - If the bundle is locked then a file .bundle/environment.rb exists
		#   which will setup the load paths.
		# - If the bundle is not locked then the load paths must be set up by
		#   calling Bundler.setup.
		# - Rails 3's boot.rb automatically loads .bundle/environment.rb or
		#   calls Bundler.setup if that's not available.
		# - Other Rack apps might not have a boot.rb but we still want to setup
		#   Bundler.
		# - Some Rails 2 apps might have explicitly added Bundler support.
		#   These apps call Bundler.setup in their preinitializer.rb.
		#
		# So the strategy is as follows:
		
		# Our strategy might be completely unsuitable for the app or the
		# developer is using something other than Bundler, so we let the user
		# manually specify a load path setup file.
		if options["load_path_setup_file"]
			require File.expand_path(options["load_path_setup_file"])
		
		# The app developer may also override our strategy with this magic file.
		elsif File.exist?('config/setup_load_paths.rb')
			require File.expand_path('config/setup_load_paths')
		
		# If the Bundler lock environment file exists then load that. If it
		# exists then there's a 99.9% chance that loading it is the correct
		# thing to do.
		elsif File.exist?('.bundle/environment.rb')
			require File.expand_path('.bundle/environment')
		
		# If the Bundler environment file doesn't exist then there are two
		# possibilities:
		# 1. Bundler is not used, in which case we don't have to do anything.
		# 2. Bundler *is* used, but the gems are not locked and we're supposed
		#    to call Bundler.setup.
		#
		# The existence of Gemfile indicates whether (2) is true:
		elsif File.exist?('Gemfile')
			# In case of Rails 3, config/boot.rb already calls Bundler.setup.
			# However older versions of Rails may not so loading boot.rb might
			# not be the correct thing to do. To be on the safe side we
			# call Bundler.setup ourselves; calling Bundler.setup twice is
			# harmless. If this isn't the correct thing to do after all then
			# there's always the load_path_setup_file option and
			# setup_load_paths.rb.
			require 'rubygems'
			require 'bundler'
			Bundler.setup
		end
		
		# Bundler might remove Phusion Passenger from the load path in its zealous
		# attempt to un-require RubyGems, so here we put Phusion Passenger back
		# into the load path. This must be done before loading the app's startup
		# file because the app might require() Phusion Passenger files.
		if $LOAD_PATH.first != LIBDIR
			$LOAD_PATH.unshift(LIBDIR)
			$LOAD_PATH.uniq!
		end
		
		
		# !!! NOTE !!!
		# If the app is using Bundler then any dependencies required past this
		# point must be specified in the Gemfile. Like ruby-debug if debugging is on...
		
		if options["debugger"]
			require 'ruby-debug'
			if !Debugger.respond_to?(:ctrl_port)
				raise "Your version of ruby-debug is too old. Please upgrade to the latest version."
			end
			Debugger.start_remote('127.0.0.1', [0, 0])
			Debugger.start
		end
		
		PhusionPassenger._spawn_options = options
	end
	
	# This method is to be called after loading the application code but
	# before forking a worker process.
	def after_loading_app_code(options)
		# Even though prepare_app_process() restores the Phusion Passenger
		# load path after setting up Bundler, the app itself might also
		# remove Phusion Passenger from the load path for whatever reason,
		# so here we restore the load path again.
		if $LOAD_PATH.first != LIBDIR
			$LOAD_PATH.unshift(LIBDIR)
			$LOAD_PATH.uniq!
		end
		
		# Post-install framework extensions. Possibly preceded by a call to
		# PhusionPassenger.install_framework_extensions!
		require 'rails/version' if defined?(::Rails) && !defined?(::Rails::VERSION)
		if defined?(::Rails) && ::Rails::VERSION::MAJOR <= 2
			require 'phusion_passenger/classic_rails_extensions/init'
			ClassicRailsExtensions.init!(options)
			# Rails 3 extensions are installed by
			# PhusionPassenger.install_framework_extensions!
		end
		
		PhusionPassenger._spawn_options = nil
	end
	
	# To be called before the request handler main loop is entered, but after the app
	# startup file has been loaded. This function will fire off necessary events
	# and perform necessary preparation tasks.
	#
	# +forked+ indicates whether the current worker process is forked off from
	# an ApplicationSpawner that has preloaded the app code.
	# +options+ are the spawn options that were passed.
	def before_handling_requests(forked, options)
		if forked && options["analytics_logger"]
			options["analytics_logger"].clear_connection
		end
		
		# If we were forked from a preloader process then clear or
		# re-establish ActiveRecord database connections. This prevents
		# child processes from concurrently accessing the same
		# database connection handles.
		if forked && defined?(::ActiveRecord::Base)
			if ::ActiveRecord::Base.respond_to?(:clear_all_connections!)
				::ActiveRecord::Base.clear_all_connections!
			elsif ::ActiveRecord::Base.respond_to?(:clear_active_connections!)
				::ActiveRecord::Base.clear_active_connections!
			elsif ::ActiveRecord::Base.respond_to?(:connected?) &&
			      ::ActiveRecord::Base.connected?
				::ActiveRecord::Base.establish_connection
			end
		end
		
		# Fire off events.
		PhusionPassenger.call_event(:starting_worker_process, forked)
		if options["pool_account_username"] && options["pool_account_password_base64"]
			password = options["pool_account_password_base64"].unpack('m').first
			PhusionPassenger.call_event(:credentials,
				options["pool_account_username"], password)
		else
			PhusionPassenger.call_event(:credentials, nil, nil)
		end
	end
	
	# To be called after the request handler main loop is exited. This function
	# will fire off necessary events perform necessary cleanup tasks.
	def after_handling_requests
		PhusionPassenger.call_event(:stopping_worker_process)
		Kernel.passenger_call_at_exit_blocks
	end
	
	def get_socket_address_type(address)
		if address =~ %r{^unix:.}
			return :unix
		elsif address =~ %r{^tcp://.}
			return :tcp
		else
			return :unknown
		end
	end
	
	def connect_to_server(address)
		case get_socket_address_type(address)
		when :unix
			return UNIXSocket.new(address.sub(/^unix:/, ''))
		when :tcp
			host, port = address.sub(%r{^tcp://}, '').split(':', 2)
			port = port.to_i
			return TCPSocket.new(host, port)
		else
			raise ArgumentError, "Unknown socket address type for '#{address}'."
		end
	end
	
	def local_socket_address?(address)
		case get_socket_address_type(address)
		when :unix
			return true
		when :tcp
			host, port = address.sub(%r{^tcp://}, '').split(':', 2)
			return host == "127.0.0.1" || host == "::1" || host == "localhost"
		else
			raise ArgumentError, "Unknown socket address type for '#{address}'."
		end
	end
	
	# Fork a new process and run the given block inside the child process, just like
	# fork(). Unlike fork(), this method is safe, i.e. there's no way for the child
	# process to escape the block. Any uncaught exceptions in the child process will
	# be printed to standard output, citing +current_location+ as the source.
	# Futhermore, the child process will exit by calling Kernel#exit!, thereby
	# bypassing any at_exit or ensure blocks.
	#
	# If +double_fork+ is true, then the child process will fork and immediately exit.
	# This technique can be used to avoid zombie processes, at the expense of not
	# being able to waitpid() the second child.
	def safe_fork(current_location = self.class, double_fork = false)
		pid = fork
		if pid.nil?
			has_exception = false
			begin
				if double_fork
					pid2 = fork
					if pid2.nil?
						srand
						yield
					end
				else
					srand
					yield
				end
			rescue Exception => e
				has_exception = true
				print_exception(current_location.to_s, e)
			ensure
				exit!(has_exception ? 1 : 0)
			end
		else
			if double_fork
				Process.waitpid(pid) rescue nil
				return pid
			else
				return pid
			end
		end
	end
	
	# Checks whether the given process exists.
	def process_is_alive?(pid)
		begin
			Process.kill(0, pid)
			return true
		rescue Errno::ESRCH
			return false
		rescue SystemCallError => e
			return true
		end
	end
	module_function :process_is_alive?
	
	class PseudoIO
		def initialize(sink)
			@sink = sink || File.open("/dev/null", "w")
			@buffer = StringIO.new
		end
		
		def done!
			result = @buffer.string
			@buffer = nil
			return result
		end
		
		def method_missing(*args, &block)
			@buffer.send(*args, &block) if @buffer && args.first != :reopen
			return @sink.send(*args, &block)
		end
		
		def respond_to?(symbol, include_private = false)
			return @sink.respond_to?(symbol, include_private)
		end
	end
	
	# Run the given block. A message will be sent through +channel+ (a
	# MessageChannel object), telling the remote side whether the block
	# raised an exception, called exit(), or succeeded.
	#
	# If _sink_ is non-nil, then every operation on $stderr/STDERR inside
	# the block will be performed on _sink_ as well. If _sink_ is nil
	# then all operations on $stderr/STDERR inside the block will be
	# silently discarded, i.e. if one writes to $stderr/STDERR then nothing
	# will be actually written to the console.
	# 
	# Returns whether the block succeeded, i.e. whether it didn't raise an
	# exception.
	#
	# Exceptions are not propagated, except SystemExit and a few
	# non-StandardExeption classes such as SignalException. Of the
	# exceptions that are propagated, only SystemExit will be reported.
	def report_app_init_status(channel, sink = STDERR)
		begin
			old_global_stderr = $stderr
			old_stderr = STDERR
			stderr_output = ""
			
			pseudo_stderr = PseudoIO.new(sink)
			Object.send(:remove_const, 'STDERR') rescue nil
			Object.const_set('STDERR', pseudo_stderr)
			$stderr = pseudo_stderr
			
			begin
				yield
			ensure
				Object.send(:remove_const, 'STDERR') rescue nil
				Object.const_set('STDERR', old_stderr)
				$stderr = old_global_stderr
				stderr_output = pseudo_stderr.done!
			end
			
			channel.write('success')
			return true
		rescue StandardError, ScriptError, NoMemoryError => e
			channel.write('exception')
			channel.write_scalar(marshal_exception(e))
			channel.write_scalar(stderr_output)
			return false
		rescue SystemExit => e
			channel.write('exit')
			channel.write_scalar(marshal_exception(e))
			channel.write_scalar(stderr_output)
			raise
		end
	end
	
	# Receive status information that was sent to +channel+ by
	# report_app_init_status. If an error occured according to the
	# received information, then an appropriate exception will be
	# raised.
	#
	# If <tt>print_exception</tt> evaluates to true, then the
	# exception message and the backtrace will also be printed.
	# Where it is printed to depends on the type of
	# <tt>print_exception</tt>:
	# - If it responds to #puts, then the exception information will
	#   be printed using this method.
	# - If it responds to #to_str, then the exception information
	#   will be appended to the file whose filename equals the return
	#   value of the #to_str call.
	# - Otherwise, it will be printed to STDERR.
	#
	# Raises:
	# - AppInitError: this class wraps the exception information
	#   received through the channel.
	# - IOError, SystemCallError, SocketError: these errors are
	#   raised if an error occurred while receiving the information
	#   through the channel.
	def unmarshal_and_raise_errors(channel, print_exception = nil, app_type = "rails")
		args = channel.read
		if args.nil?
			raise EOFError, "Unexpected end-of-file detected."
		end
		status = args[0]
		if status == 'exception'
			child_exception = unmarshal_exception(channel.read_scalar)
			stderr = channel.read_scalar
			exception = AppInitError.new(
				"Application '#{@app_root}' raised an exception: " <<
				"#{child_exception.class} (#{child_exception.message})",
				child_exception,
				app_type,
				stderr.empty? ? nil : stderr)
		elsif status == 'exit'
			child_exception = unmarshal_exception(channel.read_scalar)
			stderr = channel.read_scalar
			exception = AppInitError.new("Application '#{@app_root}' exited during startup",
				child_exception, app_type, stderr.empty? ? nil : stderr)
		else
			exception = nil
		end
		
		if print_exception && exception
			if print_exception.respond_to?(:puts)
				print_exception(self.class.to_s, child_exception, print_exception)
			elsif print_exception.respond_to?(:to_str)
				filename = print_exception.to_str
				File.open(filename, 'a') do |f|
					print_exception(self.class.to_s, child_exception, f)
				end
			else
				print_exception(self.class.to_s, child_exception)
			end
		end
		raise exception if exception
	end
	
	# No-op, hook for unit tests.
	def self.lower_privilege_called
	end
	
	# Lowers the current process's privilege based on the documented rules for
	# the "user", "group", "default_user" and "default_group" options.
	def lower_privilege(startup_file, options)
		Utils.lower_privilege_called
		return if Process.euid != 0
		
		if options["default_user"] && !options["default_user"].empty?
			default_user = options["default_user"]
		else
			default_user = "nobody"
		end
		if options["default_group"] && !options["default_group"].empty?
			default_group = options["default_group"]
		else
			default_group = Etc.getgrgid(Etc.getpwnam(default_user).gid).name
		end

		if options["user"] && !options["user"].empty?
			begin
				user_info = Etc.getpwnam(options["user"])
			rescue ArgumentError
				user_info = nil
			end
		else
			uid = File.lstat(startup_file).uid
			begin
				user_info = Etc.getpwuid(uid)
			rescue ArgumentError
				user_info = nil
			end
		end
		if !user_info || user_info.uid == 0
			begin
				user_info = Etc.getpwnam(default_user)
			rescue ArgumentError
				user_info = nil
			end
		end

		if options["group"] && !options["group"].empty?
			if options["group"] == "!STARTUP_FILE!"
				gid = File.lstat(startup_file).gid
				begin
					group_info = Etc.getgrgid(gid)
				rescue ArgumentError
					group_info = nil
				end
			else
				begin
					group_info = Etc.getgrnam(options["group"])
				rescue ArgumentError
					group_info = nil
				end
			end
		elsif user_info
			begin
				group_info = Etc.getgrgid(user_info.gid)
			rescue ArgumentError
				group_info = nil
			end
		else
			group_info = nil
		end
		if !group_info || group_info.gid == 0
			begin
				group_info = Etc.getgrnam(default_group)
			rescue ArgumentError
				group_info = nil
			end
		end

		if !user_info
			raise SecurityError, "Cannot determine a user to lower privilege to"
		end
		if !group_info
			raise SecurityError, "Cannot determine a group to lower privilege to"
		end

		NativeSupport.switch_user(user_info.name, user_info.uid, group_info.gid)
		ENV['USER'] = user_info.name
		ENV['HOME'] = user_info.dir
	end
	
	# Checks the permissions of all parent directories of +dir+ as
	# well as +dir+ itself.
	#
	# +dir+ must be a canonical path.
	#
	# If one of the parent directories has wrong permissions, causing
	# +dir+ to be inaccessible by the current process, then this function
	# returns [path, true] where +path+ is the path of the top-most
	# directory with wrong permissions.
	# 
	# If +dir+ itself is not executable by the current process then
	# this function returns [dir, false].
	#
	# Otherwise, nil is returned.
	def check_directory_tree_permissions(dir)
		components = dir.split("/")
		components.shift
		i = 0
		# We can't use File.readable() and friends here because they
		# don't always work right with ACLs. Instead of we use 'real'
		# checks.
		while i < components.size
			path = "/" + components[0..i].join("/")
			begin
				File.stat(path)
			rescue Errno::EACCES
				return [File.dirname(path), true]
			end
			i += 1
		end
		begin
			Dir.chdir(dir) do
				return nil
			end
		rescue Errno::EACCES
			return [dir, false]
		end
	end
	
	# Returns a string which reports the backtraces for all threads,
	# or if that's not supported the backtrace for the current thread.
	def global_backtrace_report
		if Kernel.respond_to?(:caller_for_all_threads)
			output = "========== Process #{Process.pid}: backtrace dump ==========\n"
			caller_for_all_threads.each_pair do |thread, stack|
				output << ("-" * 60) << "\n"
				output << "# Thread: #{thread.inspect}, "
				if thread == Thread.main
					output << "[main thread], "
				end
				if thread == Thread.current
					output << "[current thread], "
				end
				output << "alive = #{thread.alive?}\n"
				output << ("-" * 60) << "\n"
				output << "    " << stack.join("\n    ")
				output << "\n\n"
			end
		else
			output = "========== Process #{Process.pid}: backtrace dump ==========\n"
			output << ("-" * 60) << "\n"
			output << "# Current thread: #{Thread.current.inspect}\n"
			output << ("-" * 60) << "\n"
			output << "    " << caller.join("\n    ")
		end
		return output
	end
	
	def to_boolean(value)
		return !(value.nil? || value == false || value == "false")
	end
	
	def sanitize_spawn_options(options)
		defaults = {
			"app_type"         => "rails",
			"environment"      => "production",
			"spawn_method"     => "smart-lv2",
			"framework_spawner_timeout" => -1,
			"app_spawner_timeout"       => -1,
			"print_exceptions" => true
		}
		options = defaults.merge(options)
		options["app_group_name"]            = options["app_root"] if !options["app_group_name"]
		options["framework_spawner_timeout"] = options["framework_spawner_timeout"].to_i
		options["app_spawner_timeout"]       = options["app_spawner_timeout"].to_i
		if options.has_key?("print_framework_loading_exceptions")
			options["print_framework_loading_exceptions"] = to_boolean(options["print_framework_loading_exceptions"])
		end
		# Force this to be a boolean for easy use with Utils#unmarshal_and_raise_errors.
		options["print_exceptions"]          = to_boolean(options["print_exceptions"])
		
		options["analytics"]                 = to_boolean(options["analytics"])
		options["show_version_in_header"]    = to_boolean(options["show_version_in_header"])
		
		# Smart spawning is not supported when using ruby-debug.
		options["debugger"]     = to_boolean(options["debugger"])
		options["spawn_method"] = "conservative" if options["debugger"]
		
		return options
	end
	
	if defined?(PhusionPassenger::NativeSupport)
		# Split the given string into an hash. Keys and values are obtained by splitting the
		# string using the null character as the delimitor.
		def split_by_null_into_hash(data)
			return PhusionPassenger::NativeSupport.split_by_null_into_hash(data)
		end
	else
		NULL = "\0".freeze
		
		def split_by_null_into_hash(data)
			args = data.split(NULL, -1)
			args.pop
			return Hash[*args]
		end
	end
	
	####################################
end

end # module PhusionPassenger

class Exception
	def backtrace_string(current_location = nil)
		if current_location.nil?
			location = nil
		else
			location = "in #{current_location} "
		end
		return "*** Exception #{self.class} #{location}" <<
			"(#{self}) (process #{$$}, thread #{Thread.current}):\n" <<
			"\tfrom " << backtrace.join("\n\tfrom ")
	end
end

class ConditionVariable
	# This is like ConditionVariable.wait(), but allows one to wait a maximum
	# amount of time. Returns true if this condition was signaled, false if a
	# timeout occurred.
	def timed_wait(mutex, secs)
		ruby_engine = defined?(RUBY_ENGINE) ? RUBY_ENGINE : "ruby"
		if secs > 100000000
			# NOTE: If one calls timeout() on FreeBSD 5 with an
			# argument of more than 100000000, then MRI will become
			# stuck in an infite loop, blocking all threads. It seems
			# that MRI uses select() to implement sleeping.
			# I think that a value of more than 100000000 overflows
			# select()'s data structures, causing it to behave incorrectly.
			# So we just make sure we can't sleep more than 100000000
			# seconds.
			secs = 100000000
		end
		if ruby_engine == "jruby"
			if secs > 0
				return wait(mutex, secs)
			else
				return wait(mutex)
			end
		elsif RUBY_VERSION >= '1.9.2'
			if secs > 0
				t1 = Time.now
				wait(mutex, secs)
				t2 = Time.now
				return t2.to_f - t1.to_f < secs
			else
				wait(mutex)
				return true
			end
		else
			if secs > 0
				Timeout.timeout(secs) do
					wait(mutex)
				end
			else
				wait(mutex)
			end
			return true
		end
	rescue Timeout::Error
		return false
	end
	
	# This is like ConditionVariable.wait(), but allows one to wait a maximum
	# amount of time. Raises Timeout::Error if the timeout has elapsed.
	def timed_wait!(mutex, secs)
		ruby_engine = defined?(RUBY_ENGINE) ? RUBY_ENGINE : "ruby"
		if secs > 100000000
			# See the corresponding note for timed_wait().
			secs = 100000000
		end
		if ruby_engine == "jruby"
			if secs > 0
				if !wait(mutex, secs)
					raise Timeout::Error, "Timeout"
				end
			else
				wait(mutex)
			end
		elsif RUBY_VERSION >= '1.9.2'
			if secs > 0
				t1 = Time.now
				wait(mutex, secs)
				t2 = Time.now
				if t2.to_f - t1.to_f >= secs
					raise Timeout::Error, "Timeout"
				end
			else
				wait(mutex)
			end
		else
			if secs > 0
				Timeout.timeout(secs) do
					wait(mutex)
				end
			else
				wait(mutex)
			end
		end
		return nil
	end
end

class IO
	if defined?(PhusionPassenger::NativeSupport)
		# Writes all of the strings in the +components+ array into the given file
		# descriptor using the +writev()+ system call. Unlike IO#write, this method
		# does not require one to concatenate all those strings into a single buffer
		# in order to send the data in a single system call. Thus, #writev is a great
		# way to perform zero-copy I/O.
		#
		# Unlike the raw writev() system call, this method ensures that all given
		# data is written before returning, by performing multiple writev() calls
		# and whatever else is necessary.
		#
		#   io.writev(["hello ", "world", "\n"])
		def writev(components)
			return PhusionPassenger::NativeSupport.writev(fileno, components)
		end
		
		# Like #writev, but accepts two arrays. The data is written in the given order.
		#
		#   io.writev2(["hello ", "world", "\n"], ["another ", "message\n"])
		def writev2(components, components2)
			return PhusionPassenger::NativeSupport.writev2(fileno,
				components, components2)
		end
		
		# Like #writev, but accepts three arrays. The data is written in the given order.
		#
		#   io.writev3(["hello ", "world", "\n"],
		#     ["another ", "message\n"],
		#     ["yet ", "another ", "one", "\n"])
		def writev3(components, components2, components3)
			return PhusionPassenger::NativeSupport.writev3(fileno,
				components, components2, components3)
		end
	end
	
	if defined?(Fcntl::F_SETFD)
		def close_on_exec!
			fcntl(Fcntl::F_SETFD, Fcntl::FD_CLOEXEC)
		end
	else
		def close_on_exec!
		end
	end
end

module Signal
	# Like Signal.list, but only returns signals that we can actually trap.
	def self.list_trappable
		ruby_engine = defined?(RUBY_ENGINE) ? RUBY_ENGINE : "ruby"
		case ruby_engine
		when "ruby"
			result = Signal.list
			result.delete("ALRM")
			result.delete("VTALRM")
		when "jruby"
			result = Signal.list
			result.delete("QUIT")
			result.delete("ILL")
			result.delete("FPE")
			result.delete("KILL")
			result.delete("SEGV")
			result.delete("USR1")
		else
			result = Signal.list
		end
		
		# Don't touch SIGCHLD no matter what! On OS X waitpid() will
		# malfunction if SIGCHLD doesn't have a correct handler.
		result.delete("CLD")
		result.delete("CHLD")
		
		# Other stuff that we don't want to trap no matter which
		# Ruby engine.
		result.delete("STOP")
		
		return result
	end
end

module Process
	def self.timed_waitpid(pid, max_time)
		done = false
		start_time = Time.now
		while Time.now - start_time < max_time && !done
			done = Process.waitpid(pid, Process::WNOHANG)
			sleep 0.1 if !done
		end
		return !!done
	rescue Errno::ECHILD
		return true
	end
end

# MRI's implementations of UNIXSocket#recv_io and UNIXSocket#send_io
# are broken on 64-bit FreeBSD 7, OpenBSD and x86_64/ppc64 OS X. So
# we override them with our own implementation.
ruby_engine = defined?(RUBY_ENGINE) ? RUBY_ENGINE : "ruby"
if ruby_engine == "ruby" && defined?(PhusionPassenger::NativeSupport) && (
  RUBY_PLATFORM =~ /freebsd/ ||
  RUBY_PLATFORM =~ /openbsd/ ||
  (RUBY_PLATFORM =~ /darwin/ && RUBY_PLATFORM !~ /universal/)
)
	require 'socket'
	UNIXSocket.class_eval do
		def recv_io(klass = IO)
			return klass.for_fd(PhusionPassenger::NativeSupport.recv_fd(self.fileno))
		end
		
		def send_io(io)
			PhusionPassenger::NativeSupport.send_fd(self.fileno, io.fileno)
		end
	end
end

module GC
	if !respond_to?(:copy_on_write_friendly?)
		# Checks whether the current Ruby interpreter's garbage
		# collector is copy-on-write friendly.
		def self.copy_on_write_friendly?
			return false
		end
	end
end
