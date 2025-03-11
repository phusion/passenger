#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2015-2025 Asynchronous B.V.
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

PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'platform_info/ruby'

# This file contains a specification of all supported Passenger Standalone
# configuration options. The specifications are spread over multiple constants,
# one for each category. The command line parser for `passenger start` is
# automatically generated from these specifications. The configuration file
# parser and the environment variable parser also use these specifications.
#
# A specification is an array of hashes. The following keys are supported:
#
# - :name
#   The name of the configuration option. If you omit this option, while
#   simultaneously setting the `:cli` option, then it means this configuration
#   option is available as a command line option only.
#
# - :type
#   (default: :string)
#   The value type. Supported types are: :string, :integer, :boolean, :path,
#   :array, :map, :hostname.
#   This is used for determining a default parser and for checking the value.
#
# - :type_desc
#   (default: automatically inferred)
#   A description of the value type, to be used in command line option help
#   messages. For example, the `:address` option should have the type desc
#   `HOST` so that the help message `--address HOST` is generated. Boolean
#   options have a `type_desc` of nil.
#
# - :cli
#   (default: automatically inferred)
#   The name of the corresponding command line option. If not specified, then
#   a default one will be automatically generated form the configuration option
#   name. For example, `:ssl_certificate_path` becomes `--ssl-certificate-path`.
#   If set to nil, then it means there is no corresponding command line option.
#
# - :cli_parser
#   (default: automatically inferred)
#   Command line options are parsed based on the configuration option type.
#   If you want to parse it differently, then you can set this option to a
#   lambda to specify a custom parser.
#
# - :short_cli
#   The name of the corresponding short command line option, if any. For
#   example, the `:address` option should have `short_cli` set to `-a`.
#
# - :desc
#   A description to be displayed in command line options help message.
#
# - :default
#   The default value.
#
# - :min
#   If :type is :integer, then this specifies the minimum accepted value.
#   Has no meaing if :type is not :integer.
#   This value is used by the default CLI parser to check whether the
#   passed argument is acceptable.

module PhusionPassenger
  module Standalone
    # Server configuration options
    SERVER_CONFIG_SPEC = [
      {
        :name      => :address,
        :type      => :hostname,
        :type_desc => 'HOST',
        :short_cli => '-a',
        :default   => '0.0.0.0',
        :desc      => "Bind to the given address.\n" \
                      "Default: %DEFAULT%"
      },
      {
        :name      => :port,
        :type      => :integer,
        :short_cli => '-p',
        :default   => 3000,
        :desc      => 'Use the given port number. Default: %DEFAULT%'
      },
      {
        :name      => :socket_file,
        :type      => :path,
        :type_desc => 'FILE',
        :cli       => '--socket',
        :short_cli => '-S',
        :desc      => "Bind to Unix domain socket instead of TCP\n" \
                      'socket'
      },
      {
        :name      => :socket_backlog,
        :type      => :integer,
        :cli       => '--socket-backlog',
        :desc      => "Override size of the socket backlog.\n" \
                      "Default: #{DEFAULT_SOCKET_BACKLOG}"
      },
      {
        :name      => :ssl,
        :type      => :boolean,
        :desc      => "Enable SSL support (Nginx\n" \
                      'engine only)'
      },
      {
        :name      => :ssl_certificate,
        :type      => :path,
        :desc      => "Specify the SSL certificate path\n" \
                      '(Nginx engine only)'
      },
      {
        :name      => :ssl_certificate_key,
        :type      => :path,
        :desc      => "Specify the SSL key path (Nginx\n" \
                      'engine only)'
      },
      {
        :name      => :ssl_port,
        :type      => :integer,
        :type_desc => 'PORT',
        :desc      => "Listen for SSL on this port, while\n" \
                      "listening for HTTP on the normal port\n" \
                      '(Nginx engine only)'
      },
      {
        :name      => :daemonize,
        :type      => :boolean,
        :short_cli => '-d',
        :desc      => 'Daemonize into the background'
      },
      {
        :name      => :user,
        :type_desc => 'USERNAME',
        :desc      => "User to run as. Ignored unless\n" \
                      'running as root'
      },
      {
        :name      => :log_file,
        :type      => :path,
        :desc      => "Where to write log messages. Default:\n" \
                      'console, or /dev/null when daemonized'
      },
      {
        :name      => :pid_file,
        :type      => :path,
        :desc      => 'Where to store the PID file'
      },
      {
        :name      => :stop_timeout,
        :type      => :integer,
        :default   => 60,
        :desc      => "How long in seconds to wait for the HTTP engine to gracefully shutdown,\nbefore killing it.\nDefault: %DEFAULT%"
      },
      {
        :name      => :instance_registry_dir,
        :type      => :path,
        :desc      => 'Use the given instance registry directory'
      },
      {
        :name      => :data_buffer_dir,
        :type      => :path,
        :desc      => 'Use the given data buffer directory'
      },
      {
        :name      => :core_file_descriptor_ulimit,
        :type      => :integer,
        :desc      => "Set custom file descriptor ulimit for the\n" \
                      "#{SHORT_PROGRAM_NAME} core"
      }
    ]

    # Application loading configuration options
    APPLICATION_LOADING_CONFIG_SPECS = [
      {
        :name      => :environment,
        :type_desc => 'NAME',
        :short_cli => '-e',
        :default   => ENV['RAILS_ENV'] || ENV['RACK_ENV'] || ENV['NODE_ENV'] ||
          ENV['PASSENGER_APP_ENV'] || 'development',
        :desc      => "Web framework environment. Default:\n" \
                      "%DEFAULT%"
      },
      {
        :name      => :ruby,
        :type_desc => 'FILENAME',
        :desc      => "Executable to use for Ruby apps.\n" \
                      "Default: #{PlatformInfo.ruby_command}\n" \
                      "(current context)"
      },
      {
        :name      => :python,
        :type_desc => 'FILENAME',
        :desc      => 'Executable to use for Python apps'
      },
      {
        :name      => :nodejs,
        :type_desc => 'FILENAME',
        :desc      => 'Executable to use for Node.js apps'
      },
      {
        :name      => :meteor_app_settings,
        :type      => :path,
        :type_desc => 'FILENAME',
        :desc      => "Settings file to use for (development mode)\n" \
                      'Meteor apps'
      },
      {
        :type      => :path,
        :type_desc => 'FILENAME',
        :cli       => '--rackup',
        :short_cli => '-R',
        :cli_parser => lambda do |options, value|
          options[:app_type] = 'rack'
          options[:startup_file] = File.absolute_logical_path(value,
            Dir.logical_pwd)
        end,
        :desc      => "Consider application a Ruby app, and use\n" \
                      'the given rackup file'
      },
      {
        :name      => :app_type,
        :type_desc => 'NAME',
        :desc      => 'Force app to be detected as the given type'
      },
      {
        :name      => :startup_file,
        :type      => :path,
        :type_desc => 'FILENAME',
        :desc      => 'Force given startup file to be used'
      },
      {
        :name      => :app_start_command,
        :type_desc => 'COMMAND',
        :desc      => 'Force the app to be started through this command'
      },
      {
        :name      => :app_support_kuria_protocol,
        :type      => :boolean,
        :desc      => 'Force the app to be recognized as a Kuria app',
      },
      {
        :name      => :spawn_method,
        :type_desc => 'NAME',
        :default   => PlatformInfo.ruby_supports_fork? ? DEFAULT_SPAWN_METHOD : 'direct',
        :desc      => 'The spawn method to use. Default: see docs'
      },
      {
        :name      => :direct_instance_request_address,
        :type      => :hostname,
        :type_desc => 'HOST',
        :default   => '127.0.0.1',
        :desc      => "The address that Passenger binds to in order to allow sending\nHTTP requests to individual application processes.\nDefault: %DEFAULT%"
      },
      {
        :name      => :static_files_dir,
        :type      => :path,
        :desc      => "Specify the static files dir (Nginx engine\n" \
                      'only)'
      },
      {
        :name      => :restart_dir,
        :type      => :path,
        :desc      => 'Specify the restart dir'
      },
      {
        :name      => :friendly_error_pages,
        :type      => :boolean,
        :desc      => 'Turn on friendly error pages'
      },
      {
        :name      => :custom_error_page,
        :type      => :path,
        :desc      => 'Path to html file to use for Passenger generated error pages'
      },
      {
        :type      => :boolean,
        :cli       => '--no-friendly-error-pages',
        :cli_parser => lambda do |options, value|
          options[:friendly_error_pages] = false
        end,
        :desc      => 'Turn off friendly error pages'
      },
      {
        :name      => :load_shell_envvars,
        :type      => :boolean,
        # The Standalone mode is primarily used for serving a single app (except
        # when in mass deployment mode), so load_shell_envvars is disabled by
        # default. However, it's enabled by default in the Core, so we need to
        # explicitly set it to disabled here.
        :default   => false,
        :desc      => "Load shell startup files before loading\n" \
                      'application'
      },
      {
        :name      => :preload_bundler,
        :type      => :boolean,
        :default   => false,
        :desc      => "Tell Ruby to load the bundler gem before loading the application"
      },
      {
        :name      => :app_file_descriptor_ulimit,
        :type      => :integer,
        :desc      => "Set custom file descriptor ulimit for the\n" \
                      "application"
      },
      {
        :name      => :debugger,
        :type      => :boolean,
        :desc      => 'Enable debugger support'
      },
      {
        :name      => :envvars,
        :type      => :map,
        :type_desc => 'NAME=VALUE',
        :default   => {},
        :cli       => '--envvar',
        :cli_parser => lambda do |options, value|
          if value !~ /=.+/
            abort "*** ERROR: invalid --envvar format: #{value}"
          end
          key, real_value = value.split('=', 2)
          options[:envvars] ||= {}
          options[:envvars][key] = real_value
        end,
        :desc      => 'Pass environment variable to application'
      }
    ]

    # Process management configuration options
    PROCESS_MANAGEMENT_CONFIG_SPECS = [
      {
        :name      => :max_pool_size,
        :type      => :integer,
        :min       => 1,
        :desc      => "Maximum number of application processes.\n" \
                      "Default: #{DEFAULT_MAX_POOL_SIZE}"
      },
      {
        :name      => :min_instances,
        :type      => :integer,
        :min       => 0,
        :desc      => "Minimum number of processes per\n" \
                      'application. Default: 1'
      },
      {
        :name      => :pool_idle_time,
        :type      => :integer,
        :type_desc => 'SECONDS',
        :desc      => "Maximum time that processes may be idle.\n" \
                      "Default: #{DEFAULT_POOL_IDLE_TIME}"
      },
      {
        :name      => :max_preloader_idle_time,
        :type      => :integer,
        :type_desc => 'SECONDS',
        :desc      => "Maximum time that preloader processes may\n" \
                      "be idle. A value of 0 means that preloader\n" \
                      "processes never timeout. Default: #{DEFAULT_MAX_PRELOADER_IDLE_TIME}"
      },
      {
        :name      => :force_max_concurrent_requests_per_process,
        :type      => :integer,
        :desc      => "Force #{SHORT_PROGRAM_NAME} to believe that an\n" \
                      "application process can handle the given\n" \
                      "number of concurrent requests per process"
      },
      {
        :name      => :start_timeout,
        :type      => :integer,
        :type_desc => 'SECONDS',
        :desc      => "The maximum time an application process may\n" \
                      "take to start up, after which it will be killed\n" \
                      "with SIGKILL, and logged with an error.\nDefault: #{DEFAULT_START_TIMEOUT / 1000}"
      },
      {
        :name      => :app_connect_timeout,
        :type      => :integer,
        :type_desc => 'SECONDS',
        :desc      => "The maximum time an application process may\n" \
                      "take to accept a socket connection\n" \
                      "Default: #{DEFAULT_CONNECT_TIMEOUT}"
      },
      {
        :name      => :concurrency_model,
        :type_desc => 'NAME',
        :desc      => "The concurrency model to use, either\n" \
                      "'process' or 'thread' (Enterprise only).\n" \
                      "Default: #{DEFAULT_CONCURRENCY_MODEL}"
      },
      {
        :name      => :thread_count,
        :type      => :integer,
        :desc      => "The number of threads to use when using\n" \
                      "the 'thread' concurrency model (Enterprise\n" \
                      "only). Default: #{DEFAULT_APP_THREAD_COUNT}"
      },
      {
        :name      => :memory_limit,
        :type      => :integer,
        :type_desc => 'MB',
        :desc      => "Restart application processes that go over\n" \
                      "the given memory limit (Enterprise only)"
      },
      {
        :name      => :rolling_restarts,
        :type      => :boolean,
        :desc      => "Enable rolling restarts (Enterprise only)"
      },
      {
        :name      => :resist_deployment_errors,
        :type      => :boolean,
        :desc      => "Enable deployment error resistance\n" \
                      '(Enterprise only)'
      }
    ]

    # Request handling configuration options
    REQUEST_HANDLING_CONFIG_SPECS = [
      {
        :name      => :max_requests,
        :type      => :integer,
        :min       => 0,
        :desc      => "Restart application processes that have handled\n" \
                      "the specified maximum number of requests"
      },
      {
        :name      => :max_request_time,
        :type      => :integer,
        :type_desc => 'SECONDS',
        :min       => 0,
        :desc      => "Abort requests that take too much time\n" \
                      '(Enterprise only)'
      },
      {
        :name      => :max_request_queue_size,
        :type      => :integer,
        :min       => 0,
        :desc      => "Specify request queue size. Default: #{DEFAULT_MAX_REQUEST_QUEUE_SIZE}"
      },
      {
        :name      => :sticky_sessions,
        :type      => :boolean,
        :desc      => 'Enable sticky sessions'
      },
      {
        :name      => :sticky_sessions_cookie_name,
        :type_desc => 'NAME',
        :desc      => "Cookie name to use for sticky sessions.\n" \
                      "Default: #{DEFAULT_STICKY_SESSIONS_COOKIE_NAME}"
      },
      {
        :name      => :sticky_sessions_cookie_attributes,
        :type_desc => "'NAME1=VALUE1; NAME2'",
        :desc      => "The attributes to use for the sticky session cookie.\n" \
                      "Default: #{DEFAULT_STICKY_SESSIONS_COOKIE_ATTRIBUTES}"
      },
      {
        :name      => :vary_turbocache_by_cookie,
        :type_desc => 'NAME',
        :desc      => "Vary the turbocache by the cookie of the\n" \
                      'given name'
      },
      {
        :name      => :turbocaching,
        :type      => :boolean,
        :cli       => nil
      },
      {
        :type      => :boolean,
        :cli       => '--disable-turbocaching',
        :desc      => 'Disable turbocaching',
        :cli_parser => lambda do |options, value|
          options[:turbocaching] = false
        end
      },
      {
        :name      => :old_routing,
        :type      => :boolean,
        :desc      => 'Revert to old routing algorithm'
      },
      {
        :name      => :unlimited_concurrency_paths,
        :type      => :array,
        :type_desc => 'URI-PATH',
        :cli       => '--unlimited-concurrency-path',
        :desc      => "Specify URI path which supports unlimited\n" \
                      "concurrency. Specify multiple times for\n" \
                      "multiple paths",
        :default   => [],
        :cli_parser => lambda do |options, value|
          options[:unlimited_concurrency_paths] ||= []
          options[:unlimited_concurrency_paths] << value
        end
      },
      {
        :name      => :abort_websockets_on_process_shutdown,
        :type      => :boolean,
        :cli       => nil
      },
      {
        :type      => :boolean,
        :cli       => '--no-abort-websockets-on-process-shutdown',
        :desc      => "Do not abort WebSocket connections on\n" \
                      'process restart',
        :cli_parser => lambda do |options, value|
          options[:abort_websockets_on_process_shutdown] = false
        end
      }
    ]

    # Nginx engine configuration options
    NGINX_ENGINE_CONFIG_SPECS = [
      {
        :name      => :nginx_bin,
        :type      => :path,
        :type_desc => 'FILENAME',
        :desc      => 'Nginx binary to use as core'
      },
      {
        :name      => :nginx_version,
        :type_desc => 'VERSION',
        :default   => PREFERRED_NGINX_VERSION,
        :desc      => "Nginx version to use as core.\n" \
                      "Default: #{PREFERRED_NGINX_VERSION}"
      },
      {
        :name      => :nginx_tarball,
        :type      => :path,
        :type_desc => 'FILENAME',
        :desc      => "If Nginx needs to be installed, then the\n" \
                      "given tarball will be used instead of\n" \
                      "downloading from the Internet"
      },
      {
        :name      => :nginx_config_template,
        :type      => :path,
        :type_desc => 'FILENAME',
        :desc      => "The template to use for generating the\n" \
                      'Nginx config file'
      },
      {
        :name      => :debug_nginx_config,
        :type      => :boolean,
        :desc      => 'Print Nginx config template and exit'
      }
    ]

    # Advanced configuration options
    ADVANCED_CONFIG_SPECS = [
      {
        :name      => :disable_security_update_check,
        :type      => :boolean,
        :desc      => "Disable check for security updates"
      },
      {
        :name      => :security_update_check_proxy,
        :type_desc => 'NAME',
        :desc      => "Use HTTP/SOCKS proxy for the security update check"
      },
      {
        :name      => :disable_anonymous_telemetry,
        :type      => :boolean,
        :desc      => "Disable anonymous telemetry collection"
      },
      {
        :name      => :anonymous_telemetry_proxy,
        :type_desc => 'NAME',
        :desc      => "Use HTTP/SOCKS proxy for anonymous telemetry collection"
      },
      {
        :name      => :engine,
        :type_desc => 'NAME',
        :default   => 'nginx',
        :desc      => "Underlying HTTP engine to use. Available\n" \
                      "options: nginx (default), builtin"
      },
      {
        :name      => :log_level,
        :type      => :integer,
        :default   => DEFAULT_LOG_LEVEL,
        :desc      => "Log level to use. Default: #{DEFAULT_LOG_LEVEL}"
      },
      {
        :name      => :disable_log_prefix,
        :type      => :boolean,
        :desc      => "Disable prefixing log statements with PID and channel."
      },
      {
        :name      => :auto,
        :type      => :boolean,
        :default   => !STDIN.tty? || !STDOUT.tty?,
        :desc      => "Run in non-interactive mode. Default when\n" \
                      'stdin or stdout is not a TTY'
      },
      {
        :name      => :ctls,
        :type      => :array,
        :type_desc => 'NAME=VALUE',
        :default   => [],
        :cli_parser => lambda do |options, value|
          if value !~ /=.+/
            abort "*** ERROR: invalid --ctl format: #{value}"
          end
          options[:ctls] ||= []
          options[:ctls] << value
        end
      },
      {
        :name      => :binaries_url_root,
        :type_desc => 'URL',
        :desc      => "If Nginx needs to be installed, then the\n" \
                      "specified URL will be checked for binaries\n" \
                      'prior to a local build'
      },
      {
        :name      => :runtime_check_only,
        :type      => :boolean,
        :desc      => "Quit after checking whether the\n" \
                      "#{PROGRAM_NAME} Standalone runtime files\n" \
                      'are installed'
      },
      {
        :name      => :dont_install_runtime,
        :type      => :boolean,
        :cli       => '--no-install-runtime',
        :desc      => 'Abort if runtime must be installed'
      },
      {
        :name      => :dont_compile_runtime,
        :type      => :boolean,
        :cli       => '--no-compile-runtime',
        :desc      => 'Abort if runtime must be compiled'
      }
    ]

    CONFIG_SPECS = [
      SERVER_CONFIG_SPEC,
      APPLICATION_LOADING_CONFIG_SPECS,
      PROCESS_MANAGEMENT_CONFIG_SPECS,
      REQUEST_HANDLING_CONFIG_SPECS,
      NGINX_ENGINE_CONFIG_SPECS,
      ADVANCED_CONFIG_SPECS
    ]

    # Maps configuration options to their default value. Automatically
    # set by code later in this file.
    #
    # To inspect the value of this array, run:
    #
    #   ./dev/runner -r standalone/config_options_list -r pp \
    #     'pp Standalone::CONFIG_DEFAULTS; nil'
    CONFIG_DEFAULTS = {}

    # Indexes all configuration specification items by name.
    #
    # To inspect the value of this array, run:
    #
    #   ./dev/runner -r standalone/config_options_list -r pp \
    #     'pp Standalone::CONFIG_NAME_INDEX; nil'
    CONFIG_NAME_INDEX = {}


    ############


    # Apply transformations on the specification constants, e.g. set default values.

    make_default_type_desc_value = lambda do |spec_item|
      case spec_item[:type]
      when :string
        'STRING'
      when :integer
        'NUMBER'
      when :path
        'PATH'
      when :array
        'ARRAY'
      when :map
        'MAP'
      when :hostname
        'HOSTNAME'
      else
        nil
      end
    end

    make_default_cli_value = lambda do |spec_item|
      '--' + spec_item[:name].to_s.gsub('_', '-')
    end

    CONFIG_SPECS.each do |spec|
      spec.each do |spec_item|
        spec_item[:type] ||= :string
        spec_item[:type_desc] ||= make_default_type_desc_value.call(spec_item)
        if spec_item[:name]
          if !spec_item.key?(:cli)
            spec_item[:cli] = make_default_cli_value.call(spec_item)
          end
          if spec_item.key?(:default)
            CONFIG_DEFAULTS[spec_item[:name]] = spec_item[:default]
          end
          CONFIG_NAME_INDEX[spec_item[:name]] = spec_item
        end
      end
    end
  end
end
