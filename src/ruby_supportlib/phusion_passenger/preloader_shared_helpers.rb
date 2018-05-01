# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2011-2017 Phusion Holding B.V.
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

require 'tmpdir'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'utils'
PhusionPassenger.require_passenger_lib 'native_support'

module PhusionPassenger

  # Provides shared functions for preloader apps.
  module PreloaderSharedHelpers
    extend self

    def init(main_app)
      options = LoaderSharedHelpers.init(main_app)

      $0 = "#{SHORT_PROGRAM_NAME} AppPreloader: #{options['app_root']}"

      if !Kernel.respond_to?(:fork)
        message = "Smart spawning is not available on this Ruby " +
          "implementation because it does not support `Kernel.fork`. "
        case options['integration_mode']
        when 'nginx'
          message << "Please set `passenger_spawn_method` to `direct`."
        when 'apache'
          message << "Please set `PassengerSpawnMethod` to `direct`."
        else
          message << "Please set `spawn_method` to `direct`."
        end
        raise(message)
      end

      options
    end

    def accept_and_process_next_client(server_socket)
      client = server_socket.accept
      client.binmode
      begin
        line = client.readline
      rescue EOFError
        return nil
      end

      begin
        doc = Utils::JSON.parse(line)
      rescue RuntimeError => e
        client.write(Utils::JSON.generate(
          :result => 'error',
          :message => "JSON parse error: #{e}"
        ))
      end

      if doc['command'] == 'spawn'
        handle_spawn_command(client, doc)
      else
        client.write(Utils::JSON.generate(
          :result => 'error',
          :message => "Unknown command #{doc['command'].inspect}"
        ))
        nil
      end
    ensure
      if client
        begin
          client.close
        rescue Errno::EINVAL
          # Work around OS X bug.
          # https://code.google.com/p/phusion-passenger/issues/detail?id=854
        end
      end
    end

    def handle_spawn_command(client, doc)
      work_dir = doc['work_dir']
      LoaderSharedHelpers.record_journey_step_end('PRELOADER_PREPARATION',
        'STEP_PERFORMED', work_dir)
      LoaderSharedHelpers.record_journey_step_begin('PRELOADER_FORK_SUBPROCESS',
        'STEP_IN_PROGRESS', work_dir)

      # Improve copy-on-write friendliness.
      GC.start

      begin
        pid = fork
      rescue SystemCallError => e
        LoaderSharedHelpers.record_journey_step_end('PRELOADER_FORK_SUBPROCESS',
          'STEP_ERRORED', work_dir)
        raise e
      end

      if pid.nil?
        begin
          $0 = "#{$0} (forking...)"
          LoaderSharedHelpers.record_journey_step_end('PRELOADER_FORK_SUBPROCESS',
            'STEP_PERFORMED', work_dir)
          LoaderSharedHelpers.run_block_and_record_step_progress('PRELOADER_SEND_RESPONSE', work_dir) do
            client.write(Utils::JSON.generate(
              :result => 'ok',
              :pid => Process.pid
            ))
          end
          LoaderSharedHelpers.record_journey_step_end('PRELOADER_FINISH',
            'STEP_PERFORMED', work_dir)
          [:forked, work_dir]
        rescue Exception => e
          STDERR.puts("Error: #{e}\n#{e.backtrace.join("\n")}")
          exit!(1)
        end
      elsif defined?(NativeSupport)
        NativeSupport.detach_process(pid)
      else
        Process.detach(pid)
      end
    end

    def advertise_sockets(_options, server)
      json = {
        :sockets => [
          {
            :name => 'main',
            :address => "unix:#{server[1]}",
            :protocol => 'preloader',
            :concurrency => 1
          }
        ]
      }

      File.open(ENV['PASSENGER_SPAWN_WORK_DIR'] + '/response/properties.json', 'w') do |f|
        f.write(PhusionPassenger::Utils::JSON.generate(json))
      end
    end

    def run_main_loop(server, options)
      server_socket, socket_filename = server
      original_pid = Process.pid

      while true
        # We call ::select just in case someone overwrites the global select()
        # function by including ActionView::Helpers in the wrong place.
        # https://code.google.com/p/phusion-passenger/issues/detail?id=915
        ios = Kernel.select([server_socket, STDIN])[0]
        if ios.include?(server_socket)
          result, subprocess_work_dir = accept_and_process_next_client(server_socket)
          if result == :forked
            return subprocess_work_dir
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
      nil
    ensure
      server_socket.close if server_socket
      if original_pid == Process.pid
        File.unlink(socket_filename) rescue nil
      end
    end
  end

end # module PhusionPassenger
