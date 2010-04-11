# encoding: binary
#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2008, 2009 Phusion
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
require 'stringio'
require 'phusion_passenger/packaging'
require 'phusion_passenger/exceptions'
if !defined?(RUBY_ENGINE) || RUBY_ENGINE == "ruby"
	require 'phusion_passenger/native_support'
end

module PhusionPassenger

# Utility functions.
module Utils
protected
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
	
	# Assert that +app_root+ is a valid Ruby on Rails application root.
	# Raises InvalidPath if that is not the case.
	def assert_valid_app_root(app_root)
		assert_valid_directory(app_root)
		assert_valid_file("#{app_root}/config/environment.rb")
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
			
			case hash[:class]
			when AppInitError.to_s
				exception_class = AppInitError
			when FrameworkInitError.to_s
				exception_class = FrameworkInitError
			else
				exception_class = InitializationError
			end
			return exception_class.new(hash[:message], child_exception)
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
	def print_exception(current_location, exception, destination = STDERR)
		if !exception.is_a?(SystemExit)
			destination.puts(exception.backtrace_string(current_location))
			destination.flush if destination.respond_to?(:flush)
		end
	end
	
	def setup_bundler_support
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
		#
		# So the strategy is as follows:

		# If the Bundler lock environment file exists then load that. If it
		# exists then there's a 99.9% chance that loading it is the correct
		# thing to do.
		if File.exist?('.bundle/environment.rb')
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
			# However older versions of Rails don't so loading boot.rb might
			# not be the correct thing to do. To be on the safe side we
			# call Bundler.setup ourselves; if this isn't the correct thing
			# to do after all then there's always the load_path_setup_file
			# option.
			require 'rubygems'
			require 'bundler'
			Bundler.setup
		end

		# Bundler might remove Phusion Passenger from the load path in its zealous
		# attempt to un-require RubyGems, so here we put Phusion Passenger back
		# into the load path.
		if $LOAD_PATH.first != LIBDIR
			$LOAD_PATH.unshift(LIBDIR)
			$LOAD_PATH.uniq!
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
				print_exception(current_location.to_s, e)
			ensure
				exit!
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
			if ENV['TESTING_PASSENGER'] == '1'
				print_exception(self.class.to_s, e)
			end
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
	
	# Lower the current process's privilege to the owner of the given file.
	# No exceptions will be raised in the event that privilege lowering fails.
	def lower_privilege(filename, lowest_user = "nobody")
		stat = File.lstat(filename)
		begin
			if !switch_to_user(stat.uid)
				switch_to_user(lowest_user)
			end
		rescue Errno::EPERM
			# No problem if we were unable to switch user.
		end
	end

	def switch_to_user(user)
		begin
			if user.is_a?(String)
				pw = Etc.getpwnam(user)
				username = user
				uid = pw.uid
				gid = pw.gid
			else
				pw = Etc.getpwuid(user)
				username = pw.name
				uid = user
				gid = pw.gid
			end
		rescue
			return false
		end
		if uid == 0
			return false
		else
			NativeSupport.switch_user(username, uid, gid)
			ENV['HOME'] = pw.dir
			return true
		end
	end
	
	def to_boolean(value)
		return !(value.nil? || value == false || value == "false")
	end
	
	def sanitize_spawn_options(options)
		defaults = {
			"lower_privilege" => true,
			"lowest_user"     => "nobody",
			"environment"     => "production",
			"app_type"        => "rails",
			"spawn_method"    => "smart-lv2",
			"framework_spawner_timeout" => -1,
			"app_spawner_timeout"       => -1,
			"print_exceptions" => true
		}
		options = defaults.merge(options)
		options["lower_privilege"]           = to_boolean(options["lower_privilege"])
		options["framework_spawner_timeout"] = options["framework_spawner_timeout"].to_i
		options["app_spawner_timeout"]       = options["app_spawner_timeout"].to_i
		# Force this to be a boolean for easy use with Utils#unmarshal_and_raise_errors.
		options["print_exceptions"]          = to_boolean(options["print_exceptions"])
		return options
	end
	
	@@passenger_tmpdir = nil
	
	def passenger_tmpdir(create = true)
		PhusionPassenger::Utils.passenger_tmpdir(create)
	end
	
	# Returns the directory in which to store Phusion Passenger-specific
	# temporary files. If +create+ is true, then this method creates the
	# directory if it doesn't exist.
	def self.passenger_tmpdir(create = true)
		dir = @@passenger_tmpdir
		if dir.nil? || dir.empty?
			dir = "#{Dir.tmpdir}/passenger.#{Process.pid}"
			@@passenger_tmpdir = dir
		end
		if create && !File.exist?(dir)
			# This is a very minimal implementation of the function
			# passengerCreateTempDir() in Utils.cpp. This implementation
			# is only meant to make the unit tests pass. For production
			# systems one should pre-create the temp directory with
			# passengerCreateTempDir().
			system("mkdir", "-p", "-m", "u=wxs,g=wx,o=wx", dir)
			system("mkdir", "-p", "-m", "u=wxs,g=wx,o=wx", "#{dir}/backends")
		end
		return dir
	end
	
	def self.passenger_tmpdir=(dir)
		@@passenger_tmpdir = dir
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
			"(#{self}) (process #{$$}):\n" <<
			"\tfrom " << backtrace.join("\n\tfrom ")
	end
end

class ConditionVariable
	# This is like ConditionVariable.wait(), but allows one to wait a maximum
	# amount of time. Returns true if this condition was signaled, false if a
	# timeout occurred.
	def timed_wait(mutex, secs)
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
		if defined?(RUBY_ENGINE) && RUBY_ENGINE == "jruby"
			if secs > 0
				return wait(mutex, secs)
			else
				return wait(mutex)
			end
		else
			require 'timeout' unless defined?(Timeout)
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
		require 'timeout' unless defined?(Timeout)
		if secs > 100000000
			# See the corresponding note for timed_wait().
			secs = 100000000
		end
		if defined?(RUBY_ENGINE) && RUBY_ENGINE == "jruby"
			if secs > 0
				if !wait(mutex, secs)
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
		# Send an IO object (i.e. a file descriptor) over this IO channel.
		# This only works if this IO channel is a Unix socket.
		#
		# Raises SystemCallError if something went wrong.
		def send_io(io)
			PhusionPassenger::NativeSupport.send_fd(self.fileno, io.fileno)
		end
	
		# Receive an IO object (i.e. a file descriptor) from this IO channel.
		# This only works if this IO channel is a Unix socket.
		#
		# Raises SystemCallError if something went wrong.
		def recv_io(klass = IO)
			return klass.for_fd(PhusionPassenger::NativeSupport.recv_fd(self.fileno))
		end
	end
	
	def close_on_exec!
		if defined?(Fcntl::F_SETFD)
			fcntl(Fcntl::F_SETFD, Fcntl::FD_CLOEXEC)
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

# Ruby's implementation of UNIXSocket#recv_io and UNIXSocket#send_io
# are broken on 64-bit FreeBSD 7, OpenBSD and x86_64/ppc64 OS X. So we override them
# with our own implementation.
if RUBY_PLATFORM =~ /freebsd/ || RUBY_PLATFORM =~ /openbsd/ || (RUBY_PLATFORM =~ /darwin/ && RUBY_PLATFORM !~ /universal/)
	require 'socket'
	UNIXSocket.class_eval do
		def recv_io(klass = IO)
			super
		end

		def send_io(io)
			super
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
