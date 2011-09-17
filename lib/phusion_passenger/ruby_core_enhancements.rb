# encoding: binary
#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010, 2011 Phusion
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
require 'socket'
require 'thread'
if (!defined?(RUBY_ENGINE) || RUBY_ENGINE == "ruby") && RUBY_VERSION < "1.8.7"
	require 'fastthread'
end
require 'phusion_passenger/native_support'

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
