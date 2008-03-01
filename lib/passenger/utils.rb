require 'rubygems'
require 'pathname'
require 'etc'
require 'thread'
require 'fastthread'
require 'timeout'
require File.expand_path("#{File.dirname(__FILE__)}/../../ext/passenger/native_support.so")

module Passenger

module Utils
protected
	# Return the absolute version of +path+. This path is guaranteed to
	# to be "normal", i.e. it doesn't contain stuff like ".." or "/",
	# and it correctly respects symbolic links.
	#
	# Raises SystemCallError if something went wrong.
	def normalize_path(path)
		return Pathname.new(path).realpath.to_s
	end
	
	def assert_valid_app_root(app_root)
		assert_valid_directory(app_root)
		assert_valid_file("#{app_root}/config/environment.rb")
	end
	
	# Assert that +path+ is a directory. Raises +ArgumentError+ if it isn't.
	def assert_valid_directory(path)
		if !File.directory?(path)
			raise ArgumentError, "'#{path}' is not a valid directory."
		end
	end
	
	# Assert that +path+ is a file. Raises +ArgumentError+ if it isn't.
	def assert_valid_file(path)
		if !File.file?(path)
			raise ArgumentError, "'#{path}' is not a valid file."
		end
	end
	
	def assert_valid_username(username)
		# If username does not exist then getpwnam() will raise an ArgumentError.
		username && Etc.getpwnam(username)
	end
	
	def assert_valid_groupname(groupname)
		# If groupname does not exist then getgrnam() will raise an ArgumentError.
		groupname && Etc.getgrnam(groupname)
	end
	
	def print_exception(current_location, exception)
		if !exception.is_a?(SystemExit)
			STDERR.puts("** Exception #{exception.class} in #{current_location} " <<
				"(#{exception}) (process #{$$}):\n" <<
				"\tfrom " <<
				exception.backtrace.join("\n\tfrom "))
			STDERR.flush
		end
	end
end

end # module Passenger

class ConditionVariable
	# This is like ConditionVariable.wait(), but allows one to wait a maximum
	# amount of time. Returns true if this condition was signaled, false if a
	# timeout occurred.
	def timed_wait(mutex, secs)
		if secs > 0
			Timeout.timeout(secs) do
				wait(mutex)
			end
		else
			wait(mutex)
		end
		return true
	rescue Timeout::Error
		return false
	end
	
	# This is like ConditionVariable.wait(), but allows one to wait a maximum
	# amount of time. Raises Timeout::Error if the timeout has elapsed.
	def timed_wait!(mutex, secs)
		if secs > 0
			Timeout.timeout(secs) do
				wait(mutex)
			end
		else
			wait(mutex)
		end
	end
end

class IO
	# Send an IO object (i.e. a file descriptor) over this IO channel.
	# This only works if this IO channel is a Unix socket.
	#
	# Raises SystemCallError if something went wrong.
	def send_io(io)
		Passenger::NativeSupport.send_fd(self.fileno, io.fileno)
	end
	
	# Receive an IO object (i.e. a file descriptor) from this IO channel.
	# This only works if this IO channel is a Unix socket.
	#
	# Raises SystemCallError if something went wrong.
	def recv_io
		return IO.new(Passenger::NativeSupport.recv_fd(self.fileno))
	end
end

module GC
	if !respond_to?(:cow_friendly?)
		# Checks whether the current Ruby interpreter's garbage
		# collector is copy-on-write friendly.
		def self.cow_friendly?
			return false
		end
	end
end

