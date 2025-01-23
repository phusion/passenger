# encoding: utf-8
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2014-2025 Asynchronous B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Asynchronous B.V.
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

require 'etc'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'standalone/control_utils'
PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'
PhusionPassenger.require_passenger_lib 'utils/shellwords'
PhusionPassenger.require_passenger_lib 'utils/json'

module PhusionPassenger
  module Standalone
    class StartCommand

      module BuiltinEngine
      private
        def start_engine_real
          Standalone::ControlUtils.require_daemon_controller
          @engine = DaemonController.new(build_daemon_controller_options)
          start_engine_no_create
        end

        def wait_until_engine_has_exited
          read_and_delete_report_file!
          if PlatformInfo.supports_flock?
            lock = DaemonController::LockFile.new(@watchdog_lock_file_path)
            lock.shared_lock do
              # Do nothing
            end
          else
            pid = @engine.pid
            while process_is_alive?(pid)
              sleep 1
            end
          end
        end


        def start_engine_no_create
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
            abort "Could not start the server engine:\n#{e}"
          end
        end

        def build_daemon_controller_options
          ping_spec = lambda do
            File.exist?(report_file_path) &&
              File.stat(report_file_path).size > 0
          end

          command = ""

          if !@options[:envvars].empty?
            command = "env "
            @options[:envvars].each_pair do |name, value|
              command << "#{Shellwords.escape name}=#{Shellwords.escape value} "
            end
          end

          command << "#{@agent_exe} watchdog";
          command << " --passenger-root #{Shellwords.escape PhusionPassenger.install_spec}"
          command << " --daemonize"
          command << " --no-user-switching"
          command << " --no-delete-pid-file"
          command << " --cleanup-pidfile #{Shellwords.escape @working_dir}/temp_dir_toucher.pid"
          command << " --report-file #{Shellwords.escape @working_dir}/report.json"
          command << " --ctl prestart_urls=#{Shellwords.escape prestart_urls_json}"
          add_param(command, :user, "--user")
          add_param(command, :log_file, "--log-file")
          add_param(command, :pid_file, "--pid-file")
          add_param(command, :instance_registry_dir, "--instance-registry-dir")
          add_param(command, :spawn_dir, "--spawn-dir")
          add_param(command, :data_buffer_dir, "--data-buffer-dir")
          add_param(command, :log_level, "--log-level")
          add_flag_param(command, :disable_log_prefix, "--disable-log-prefix")
          @options[:ctls].each do |ctl|
            command << " --ctl #{Shellwords.escape ctl}"
          end
          if @options[:user]
            command << " --default-user #{Shellwords.escape @options[:user]}"
          else
            user  = Etc.getpwuid(Process.uid).name
            begin
              group = Etc.getgrgid(Process.gid)
            rescue ArgumentError
              # Do nothing. On Heroku, it's normal that the group
              # database is broken.
            else
              command << " --default-group #{Shellwords.escape group.name}"
            end
            command << " --default-user #{Shellwords.escape user}"
          end

          # Arguments for supervised agent are between --BC/--EC and --BU/--EU
          command << " --BC"
          # The builtin engine cannot be used in combination with Mass Deployment,
          # so we know @apps always has 1 app.
          command << " --listen #{listen_address(@apps[0])}"
          command << " --no-graceful-exit"
          add_param(command, :socket_backlog, "--socket-backlog")
          add_param(command, :environment, "--environment")
          add_param(command, :app_type, "--app-type")
          add_param(command, :startup_file, "--startup-file")
          add_param(command, :app_start_command, "--app-start-command")
          add_param(command, :spawn_method, "--spawn-method")
          add_param(command, :restart_dir, "--restart-dir")
          add_param(command, :custom_error_page, "--custom-error-page")
          if @options.has_key?(:friendly_error_pages)
            if @options[:friendly_error_pages]
              command << " --force-friendly-error-pages"
            else
              command << " --disable-friendly-error-pages"
            end
          end
          if @options[:turbocaching] == false
            command << " --disable-turbocaching"
          end
          add_flag_param(command, :old_routing, "--old-routing")
          if @options[:abort_websockets_on_process_shutdown] == false
            command << " --no-abort-websockets-on-process-shutdown"
          end
          add_param(command, :force_max_concurrent_requests_per_process, "--force-max-concurrent-requests-per-process")
          add_flag_param(command, :load_shell_envvars, "--load-shell-envvars")
          add_flag_param(command, :preload_bundler, "--preload-bundler")
          add_param(command, :max_pool_size, "--max-pool-size")
          add_param(command, :min_instances, "--min-instances")
          add_param(command, :pool_idle_time, "--pool-idle-time")
          add_param(command, :max_preloader_idle_time, "--max-preloader-idle-time")
          add_param(command, :max_request_queue_size, "--max-request-queue-size")
          add_enterprise_param(command, :concurrency_model, "--concurrency-model")
          add_enterprise_param(command, :thread_count, "--app-thread-count")
          add_param(command, :max_requests, "--max-requests")
          add_enterprise_param(command, :max_request_time, "--max-request-time")
          add_enterprise_param(command, :memory_limit, "--memory-limit")
          add_enterprise_flag_param(command, :rolling_restarts, "--rolling-restarts")
          add_enterprise_flag_param(command, :resist_deployment_errors, "--resist-deployment-errors")
          add_enterprise_flag_param(command, :debugger, "--debugger")
          add_flag_param(command, :sticky_sessions, "--sticky-sessions")
          add_param(command, :vary_turbocache_by_cookie, "--vary-turbocache-by-cookie")
          add_param(command, :sticky_sessions_cookie_name, "--sticky-sessions-cookie-name")
          add_param(command, :sticky_sessions_cookie_attributes, "--sticky-sessions-cookie-attributes")
          add_param(command, :ruby, "--ruby")
          add_param(command, :python, "--python")
          add_param(command, :nodejs, "--nodejs")
          add_param(command, :meteor_app_settings, "--meteor-app-settings")
          add_param(command, :core_file_descriptor_ulimit, "--core-file-descriptor-ulimit")
          add_param(command, :app_file_descriptor_ulimit, "--app-file-descriptor-ulimit")
          add_flag_param(command, :disable_security_update_check, "--disable-security-update-check")
          add_param(command, :security_update_check_proxy, "--security-update-check-proxy")
          add_flag_param(command, :disable_anonymous_telemetry, "--disable-anonymous-telemetry")
          add_param(command, :anonymous_telemetry_proxy, "--anonymous-telemetry-proxy")

          command << " #{Shellwords.escape(@apps[0][:root])}"

          command << " --EC --BU"
          add_param(command, :core_file_descriptor_ulimit, "--core-file-descriptor-ulimit")

          return {
            :identifier    => "#{AGENT_EXE} watchdog",
            :start_command => command,
            :ping_command  => ping_spec,
            :pid_file      => @options[:pid_file],
            :log_file      => @options[:log_file],
            :start_timeout => 25,
            :stop_timeout  => @options[:stop_timeout]
          }
        end

        def listen_address(options = @options, for_ping_port = false)
          if options[:socket_file]
            return "unix:#{options[:socket_file]}"
          else
            return "tcp://" + compose_ip_and_port(options[:address], options[:port])
          end
        end

        def prestart_urls_json
          PhusionPassenger::Utils::JSON.generate([listen_url(@apps[0])])
        end

        def add_param(command, option_name, param_name)
          if value = @options[option_name]
            command << " #{param_name} #{Shellwords.escape value.to_s}"
          end
        end

        def add_flag_param(command, option_name, param_name)
          if value = @options[option_name]
            command << " #{param_name}"
          end
        end

        def add_enterprise_param(command, option_name, param_name)
          if value = @options[option_name]
            abort "The '#{option_name}' feature is only available in #{PROGRAM_NAME} " +
              "Enterprise. You are currently running the open source #{PROGRAM_NAME}. " +
              "Please learn more about and/or buy #{PROGRAM_NAME} Enterprise at " +
              "https://www.phusionpassenger.com/features#premium-features"
          end
        end

        def add_enterprise_flag_param(command, option_name, param_name)
          if value = @options[option_name]
            abort "The '#{option_name}' feature is only available in #{PROGRAM_NAME} " +
              "Enterprise. You are currently running the open source #{PROGRAM_NAME}. " +
              "Please learn more about and/or buy #{PROGRAM_NAME} Enterprise at " +
              "https://www.phusionpassenger.com/features#premium-features"
          end
        end

        def report_file_path
          @report_file_path ||= "#{@working_dir}/report.json"
        end

        def read_and_delete_report_file!
          report = File.open(report_file_path, "r:utf-8") do |f|
            Utils::JSON.parse(f.read)
          end
          # The report file may contain sensitive information, so delete it.
          File.unlink(report_file_path)
          @watchdog_lock_file_path = report["instance_dir"] + "/lock"
        end

        def process_is_alive?(pid)
          begin
            Process.kill(0, pid)
            true
          rescue Errno::ESRCH
            false
          rescue SystemCallError => e
            true
          end
        end

        #####################
      end # module BuiltinEngine

    end # module StartCommand
  end # module Standalone
end # module PhusionPassenger
