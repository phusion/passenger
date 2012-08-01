# encoding: binary
#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010, 2011, 2012 Phusion
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
require 'phusion_passenger/native_support'

module PhusionPassenger

# Utility functions.
module Utils
	extend self    # Make methods available as class methods.
	
	def self.included(klass)
		# When included into another class, make sure that Utils
		# methods are made private.
		public_instance_methods(false).each do |method_name|
			klass.send(:private, method_name)
		end
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

	def require_option(hash, key)
		if hash.has_key?(key)
			return hash[key]
		else
			raise ArgumentError, "Option #{key.inspect} required"
		end
	end

	def install_options_as_ivars(object, options, *keys)
		keys.each do |key|
			object.instance_variable_set("@#{key}", options[key])
		end
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
