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

require 'erb'
require 'etc'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/ruby'
PhusionPassenger.require_passenger_lib 'standalone/control_utils'
PhusionPassenger.require_passenger_lib 'utils/tmpio'
PhusionPassenger.require_passenger_lib 'utils/shellwords'
PhusionPassenger.require_passenger_lib 'utils/json'

module PhusionPassenger
  module Standalone
    class StartCommand

      module NginxEngine
      private
        def start_engine_real
          write_nginx_config_file(nginx_config_path)
          maybe_debug_nginx_config(nginx_config_path)
          test_nginx_config(nginx_config_path, 'nginx.conf')

          Standalone::ControlUtils.require_daemon_controller
          @engine = DaemonController.new(build_daemon_controller_options)

          begin
            @engine.start
          rescue DaemonController::AlreadyStarted
            begin
              pid = @engine.pid
            rescue SystemCallError, IOError
              pid = nil
            end
            if @can_remove_working_dir
              FileUtils.remove_entry_secure(@working_dir)
              @can_remove_working_dir = false
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
        rescue Errno::ECONNREFUSED, Errno::ECONNRESET, Errno::ENOENT
        end


        def maybe_debug_nginx_config(path)
          if @options[:debug_nginx_config]
            File.open(path, 'rb') do |f|
              puts(f.read)
            end
            exit
          end
        end

        def test_nginx_config(path, file)
          command = "#{Shellwords.escape @nginx_binary}" \
            " -c #{Shellwords.escape path}" \
            " -p #{Shellwords.escape @working_dir}" \
            " -t"
          output = `#{command} 2>&1`
          if $? && $?.exitstatus != 0
            output.gsub!(path, file)
            output = PlatformInfo.send(:reindent, output, 4)

            message = "*** ERROR: the Nginx configuration that #{PROGRAM_NAME}" \
              " Standalone generated internally contains problems. The error " \
              "message returned by the Nginx engine is:\n\n" \
              "#{output}\n\n"
            debug_log_file = Utils::TmpIO.new('passenger-standalone',
              :suffix => '.log', :binary => true, :unlink_immediately => false)
            begin
              File.open(path, 'rb') do |f|
                debug_log_file.write(f.read)
              end
            ensure
              debug_log_file.close
            end
            if @options[:nginx_config_template] && file == 'nginx.conf'
              message << "This probably means that you have a problem in your " \
                "Nginx configuration template. Please fix your template.\n\n" \
                "Tip: to debug your template, re-run #{SHORT_PROGRAM_NAME} " \
                "Standalone with the `--debug-nginx-config` option. This " \
                "allows you to see how the final Nginx config file looks like."
            else
              message << "This probably means that you have found a bug in " \
                "#{PROGRAM_NAME} Standalone. Please report this bug to our " \
                "Github issue tracker: https://github.com/phusion/passenger/issues\n\n" \
                "In the bug report, please include this error message, as " \
                "well as the contents of the file #{debug_log_file.path}"
            end

            abort(message)
          end
        end

        def build_daemon_controller_options
          if @options[:socket_file]
            ping_spec = [:unix, @options[:socket_file]]
          else
            ping_spec = [:tcp, @options[:address], @options[:port]]
          end
          return {
            :identifier    => 'Nginx',
            :start_command => "#{Shellwords.escape @nginx_binary} " +
              "-c #{Shellwords.escape nginx_config_path} " +
              "-p #{Shellwords.escape @working_dir}",
            :stop_command => "#{Shellwords.escape @nginx_binary} " +
              "-c #{Shellwords.escape nginx_config_path} " +
              "-p #{Shellwords.escape @working_dir} " +
              "-s quit",
            :ping_command  => ping_spec,
            :pid_file      => @options[:pid_file],
            :log_file      => @options[:log_file],
            :start_timeout => 25,
            :stop_timeout  => 60,
            :log_file_activity_timeout => 12,
            :dont_stop_if_pid_file_invalid => true
          }
        end

        def nginx_config_path
          return "#{@working_dir}/nginx.conf"
        end

        def write_nginx_config_file(path)
          File.open(path, 'w') do |f|
            f.chmod(0644)
            erb = ERB.new(File.read(nginx_config_template_filename), nil,
              "-", next_eoutvar)
            erb.filename = nginx_config_template_filename

            # The template requires some helper methods which are defined in start_command.rb.
            output = erb.result(get_binding)
            f.write(output)

            if debugging? && !@options[:debug_nginx_config]
              puts output
            end
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

        def next_eoutvar
          @next_eoutvar_index ||= 0
          @next_eoutvar_index += 1
          "_erbout#{@next_eoutvar_index}"
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

        def nginx_http_option(option_name)
          nginx_option(@options, option_name)
        end

        def nginx_option(options, option_name, nginx_config_name = nil)
          if options.is_a?(Symbol)
            # Support old syntax for backward compatibility:
            # nginx_option(nginx_config_name, option_name)
            nginx_config_name = options
            options = @options
          end

          if options.key?(option_name)
            nginx_config_name ||= begin
              if option_name.to_s =~ /^union_station_/
                option_name
              else
                "passenger_#{option_name}"
              end
            end
            value = options[option_name]
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

        # Method exists for backward compatibility with old Nginx config templates
        def boolean_config_value(val)
          val ? "on" : "off"
        end

        def json_config_value(value)
          value.is_a?(Hash) || value.is_a?(Array) ? Utils::JSON.generate(value) : value
        end

        def include_passenger_internal_template(name, indent = 0, fix_existing_indenting = true, the_binding = get_binding)
          path = "#{PhusionPassenger.resources_dir}/templates/standalone/#{name}"
          erb = ERB.new(File.read(path), nil, "-", next_eoutvar)
          erb.filename = path
          result = erb.result(the_binding)

          if fix_existing_indenting
            # Remove extraneous indenting by 'if' blocks
            # and collapse multiple empty newlines
            result.gsub!(/;[\n ]+/, ";\n")
          end

          # Set indenting
          result.gsub!(/^/, " " * indent)
          result.gsub!(/\A +/, '')

          result
        end

        def current_user
          Etc.getpwuid(Process.uid).name
        end

        def get_binding
          binding
        end

        def default_group_for(username)
          user = Etc.getpwnam(username)
          group = Etc.getgrgid(user.gid)
          return group.name
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
