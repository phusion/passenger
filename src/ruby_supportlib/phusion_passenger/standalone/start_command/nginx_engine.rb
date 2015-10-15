#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2015 Phusion Holding B.V.
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

require 'erb'
require 'etc'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'platform_info/ruby'
PhusionPassenger.require_passenger_lib 'standalone/control_utils'
PhusionPassenger.require_passenger_lib 'utils/tmpio'
PhusionPassenger.require_passenger_lib 'utils/shellwords'

module PhusionPassenger
  module Standalone
    class StartCommand

      module NginxEngine
      private
        def start_engine_real
          Standalone::ControlUtils.require_daemon_controller
          @engine = DaemonController.new(build_daemon_controller_options)
          write_nginx_config_file(nginx_config_path)

          begin
            @engine.start
          rescue DaemonController::AlreadyStarted
            begin
              pid = @engine.pid
            rescue SystemCallError, IOError
              pid = nil
            end
            if pid
              abort "#{PROGRAM_NAME} Standalone is already running on PID #{pid}."
            else
              abort "#{PROGRAM_NAME} Standalone is already running."
            end
          rescue DaemonController::StartError => e
            abort "Could not start the Nginx engine:\n#{e}"
          end
        end

        def wait_until_engine_has_exited
          # Since the engine is not our child process (it daemonizes)
          # we cannot use Process.waitpid to wait for it. A busy-sleep-loop with
          # Process.kill(0, pid) isn't very efficient. Instead we do this:
          #
          # Connect to the engine's server and wait until it disconnects the socket
          # because of timeout. Keep doing this until we can no longer connect.
          while true
            if @options[:socket_file]
              socket = UNIXSocket.new(@options[:socket_file])
            else
              socket = TCPSocket.new(@options[:address], @options[:port])
            end
            begin
              begin
                socket.read
              rescue SystemCallError, IOError, SocketError
              end
            ensure
              begin
                socket.close
              rescue SystemCallError, IOError, SocketError
              end
            end
          end
        rescue Errno::ECONNREFUSED, Errno::ECONNRESET
        end


        def build_daemon_controller_options
          if @options[:socket_file]
            ping_spec = [:unix, @options[:socket_file]]
          else
            ping_spec = [:tcp, @options[:address], @options[:port]]
          end
          return {
            :identifier    => 'Nginx',
            :start_command => "#{@nginx_binary} " +
              "-c #{Shellwords.escape nginx_config_path} " +
              "-p #{Shellwords.escape @working_dir}",
            :ping_command  => ping_spec,
            :pid_file      => @options[:pid_file],
            :log_file      => @options[:log_file],
            :timeout       => 25
          }
        end

        def nginx_config_path
          return "#{@working_dir}/nginx.conf"
        end

        def write_nginx_config_file(path)
          File.open(path, 'w') do |f|
            f.chmod(0644)
            erb = ERB.new(File.read(nginx_config_template_filename), nil, "-")
            erb.filename = nginx_config_template_filename
            current_user = Etc.getpwuid(Process.uid).name

            # The template requires some helper methods which are defined in start_command.rb.
            output = erb.result(binding)
            f.write(output)
            puts output if debugging?
          end
        end

        def nginx_config_template_filename
          if @options[:nginx_config_template]
            return @options[:nginx_config_template]
          else
            return File.join(PhusionPassenger.resources_dir,
              "templates", "standalone", "config.erb")
          end
        end

        def debugging?
          return ENV['PASSENGER_DEBUG'] && !ENV['PASSENGER_DEBUG'].empty?
        end

        #### Config file template helpers ####

        def nginx_listen_address(options = @options)
          if options[:socket_file]
            "unix:#{options[:socket_file]}"
          else
            compose_ip_and_port(options[:address], options[:port])
          end
        end

        def nginx_listen_address_with_ssl_port(options = @options)
          if options[:socket_file]
            "unix:#{options[:socket_file]}"
          else
            compose_ip_and_port(options[:address], options[:ssl_port])
          end
        end

        def default_group_for(username)
          user = Etc.getpwnam(username)
          group = Etc.getgrgid(user.gid)
          return group.name
        end

        def nginx_option(nginx_config_name, option_name)
          if @options.has_key?(option_name)
            value = @options[option_name]
            if value.is_a?(String)
              value = "'#{value}'"
            elsif value == true
              value = "on"
            elsif value == false
              value = "off"
            end
            "#{nginx_config_name} #{value};"
          end
        end

        def default_group_for(username)
          user = Etc.getpwnam(username)
          group = Etc.getgrgid(user.gid)
          return group.name
        end

        def boolean_config_value(val)
          return val ? "on" : "off"
        end

        def serialize_strset(*items)
          if "".respond_to?(:force_encoding)
            items = items.map { |x| x.force_encoding('binary') }
            null  = "\0".force_encoding('binary')
          else
            null  = "\0"
          end
          return [items.join(null)].pack('m*').gsub("\n", "").strip
        end

        #####################
      end # module NginxEngine

    end # module StartCommand
  end # module Standalone
end # module PhusionPassenger
