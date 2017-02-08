# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2017 Phusion Holding B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Phusion Holding B.V.
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

require 'base64'

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
      data = File.open("/dev/urandom", "rb") do |f|
        f.read(64)
      end
      case method
      when :base64
        data = base64(data)
        data.gsub!("+", '')
        data.gsub!("/", '')
        data.gsub!(/==$/, '')
        return data
      when :hex
        return data.unpack('H*')[0]
      else
        raise ArgumentError, "Invalid method #{method.inspect}"
      end
    end

    def retry_at_most(n, *exceptions)
      n.times do |i|
        begin
          return yield
        rescue *exceptions
          if i == n - 1
            raise
          end
        end
      end
    end

    # Print the given exception, including the stack trace, to STDERR.
    #
    # +current_location+ is a string which describes where the code is
    # currently at. Usually the current class name will be enough.
    # It may be nil.
    #
    # This method requires 'ruby_core_enhancements'. If 'debug_logging'
    # is loaded and included in the current module, it will use that for
    # logging.
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

    # A wrapper around Thread.new that installs a default exception handler.
    # If an uncaught exception is encountered, it will immediately log the
    # exception and abort the entire program.
    #
    # Thread#abort_on_exception is also supposed to do that, but the problem
    # is that it is implemented by forwarding the uncaught exception
    # to the main thread, which may not expect that particular exception
    # and may not handle it properly. The exception could be forwarded to
    # the main thread during any point of the main thread's execution.
    #
    # This method requires 'thread' and 'ruby_core_enhancements'.
    # If 'debug_logging' is loaded and included in the current module,
    # it will use that for logging.
    def create_thread_and_abort_on_exception(*args)
      Thread.new do
        Thread.current.abort_on_exception = true
        begin
          yield(*args)
        rescue SystemExit
          raise
        rescue Exception => e
          print_exception(nil, e)
          exit(1)
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

    if Base64.respond_to?(:strict_encode64)
      def base64(data)
        Base64.strict_encode64(data)
      end
    else
      # Base64-encodes the given data. Newlines are removed.
      # This is like `Base64.strict_encode64`, but also works
      # on Ruby 1.8 which doesn't have that method.
      def base64(data)
        result = Base64.encode64(data)
        result.delete!("\n")
        result
      end
    end

    # Returns a string which reports the backtraces for all threads,
    # or if that's not supported the backtrace for the current thread.
    def global_backtrace_report
      if Kernel.respond_to?(:caller_for_all_threads)
        all_thread_stacks = caller_for_all_threads
      elsif Thread.respond_to?(:list) && Thread.public_method_defined?(:backtrace)
        all_thread_stacks = {}
        Thread.list.each do |thread|
          all_thread_stacks[thread] = thread.backtrace
        end
      end

      output = "========== Process #{Process.pid}: backtrace dump ==========\n"
      if all_thread_stacks
        all_thread_stacks.each_pair do |thread, stack|
          if thread_name = thread[:name]
            thread_name = "(#{thread_name})"
          end
          stack ||= ["(empty)"]
          output << ("-" * 60) << "\n"
          output << "# Thread: #{thread.inspect}#{thread_name}, "
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
        output << ("-" * 60) << "\n"
        output << "# Current thread: #{Thread.current.inspect}\n"
        output << ("-" * 60) << "\n"
        output << "    " << caller.join("\n    ")
      end
      return output
    end

    ####################################
  end

end # module PhusionPassenger
