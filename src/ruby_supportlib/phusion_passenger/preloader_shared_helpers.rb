# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2011-2014 Phusion Holding B.V.
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

require 'socket'
require 'tmpdir'
PhusionPassenger.require_passenger_lib 'utils'
PhusionPassenger.require_passenger_lib 'native_support'

module PhusionPassenger

  # Provides shared functions for preloader apps.
  module PreloaderSharedHelpers
    extend self

    def init(options)
      if !Kernel.respond_to?(:fork)
        message = "Smart spawning is not available on this Ruby " +
          "implementation because it does not support `Kernel.fork`. "
        if ENV['SERVER_SOFTWARE'].to_s =~ /nginx/i
          message << "Please set `passenger_spawn_method` to `direct`."
        else
          message << "Please set `PassengerSpawnMethod` to `direct`."
        end
        raise(message)
      end
      return options
    end

    def accept_and_process_next_client(server_socket)
      original_pid = Process.pid
      client = server_socket.accept
      client.binmode
      begin
        command = client.readline
      rescue EOFError
        return nil
      end
      if command !~ /\n\Z/
        STDERR.puts "Command must end with a newline"
      elsif command == "spawn\n"
        while client.readline != "\n"
          # Do nothing.
        end

        # Improve copy-on-write friendliness.
        GC.start

        pid = fork
        if pid.nil?
          $0 = "#{$0} (forking...)"
          client.puts "OK"
          client.puts Process.pid
          client.flush
          client.sync = true
          return [:forked, client]
        elsif defined?(NativeSupport)
          NativeSupport.detach_process(pid)
        else
          Process.detach(pid)
        end
      else
        STDERR.puts "Unknown command '#{command.inspect}'"
      end
      return nil
    ensure
      if client && Process.pid == original_pid
        begin
          client.close
        rescue Errno::EINVAL
          # Work around OS X bug.
          # https://code.google.com/p/phusion-passenger/issues/detail?id=854
        end
      end
    end

    def run_main_loop(options)
      $0 = "Passenger AppPreloader: #{options['app_root']}"
      client = nil
      original_pid = Process.pid

      if defined?(NativeSupport)
        unix_path_max = NativeSupport::UNIX_PATH_MAX
      else
        unix_path_max = options.fetch('UNIX_PATH_MAX', 100).to_i
      end
      if options['socket_dir']
        socket_dir = options['socket_dir']
        socket_prefix = "preloader"
      else
        socket_dir = Dir.tmpdir
        socket_prefix = "PsgPreloader"
      end

      socket_filename = nil
      server = nil
      Utils.retry_at_most(128, Errno::EADDRINUSE) do
        socket_filename = "#{socket_dir}/#{socket_prefix}.#{rand(0xFFFFFFFF).to_s(36)}"
        socket_filename = socket_filename.slice(0, unix_path_max - 10)
        server = UNIXServer.new(socket_filename)
      end
      server.close_on_exec!
      File.chmod(0600, socket_filename)

      # Update the dump information just before telling the preloader that we're
      # ready because the Passenger core will read and memorize this information.
      LoaderSharedHelpers.dump_all_information(options)

      puts "!> Ready"
      puts "!> socket: unix:#{socket_filename}"
      puts "!> "

      while true
        # We call ::select just in case someone overwrites the global select()
        # function by including ActionView::Helpers in the wrong place.
        # https://code.google.com/p/phusion-passenger/issues/detail?id=915
        ios = Kernel.select([server, STDIN])[0]
        if ios.include?(server)
          result, client = accept_and_process_next_client(server)
          if result == :forked
            STDIN.reopen(client)
            STDOUT.reopen(client)
            STDOUT.sync = true
            client.close
            return :forked
          end
        end
        if ios.include?(STDIN)
          if STDIN.tty?
            begin
              # Prevent bash from exiting when we press Ctrl-D.
              STDIN.read_nonblock(1)
            rescue Errno::EAGAIN
              # Do nothing.
            end
          end
          break
        end
      end
      return nil
    ensure
      server.close if server
      if original_pid == Process.pid
        File.unlink(socket_filename) rescue nil
      end
    end
  end

end # module PhusionPassenger
