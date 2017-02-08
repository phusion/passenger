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

module PhusionPassenger
  module Standalone

    module ControlUtils
      extend self    # Make methods available as class methods.

      def self.included(klass)
        # When included into another class, make sure that Utils
        # methods are made private.
        public_instance_methods(false).each do |method_name|
          klass.send(:private, method_name)
        end
      end

      def require_daemon_controller
        return if defined?(PhusionPassenger::DaemonController)
        PhusionPassenger.require_passenger_lib 'vendor/daemon_controller'
      end

      def warn_pid_file_not_found(options)
        if options[:pid_file]
          STDERR.puts "According to the PID file '#{options[:pid_file]}',"
          STDERR.puts "#{PROGRAM_NAME} Standalone doesn't seem to be running."
        else
          STDERR.puts "#{PROGRAM_NAME} Standalone doesn't seem to be running, " +
            "because its PID file"
          STDERR.puts "could not be found."
        end
        STDERR.puts
        STDERR.puts "If you know that #{PROGRAM_NAME} Standalone *is* running then one of these"
        STDERR.puts "might be the cause of this error:"
        STDERR.puts
        STDERR.puts " * The #{PROGRAM_NAME} Standalone instance that you want to stop isn't running"
        STDERR.puts "   on port #{options[:port]}, but on another port. If this is the case then you"
        STDERR.puts "   should specify the right port with --port."
        STDERR.puts "   If the instance is listening on a Unix socket file instead of a TCP port,"
        STDERR.puts "   then please specify the PID file's filename with --pid-file."
        STDERR.puts " * The instance that you want to stop has stored its PID file in a non-standard"
        STDERR.puts "   location. In this case please specify the right PID file with --pid-file."
      end
    end

  end
end
