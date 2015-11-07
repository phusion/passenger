#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2014-2015 Phusion Holding B.V.
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

PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'platform_info/ruby'
PhusionPassenger.require_passenger_lib 'ruby_core_enhancements'

module PhusionPassenger
  module Standalone

    module ConfigUtils
      extend self    # Make methods available as class methods.

      def self.included(klass)
        # When included into another class, make sure that Utils
        # methods are made private.
        public_instance_methods(false).each do |method_name|
          klass.send(:private, method_name)
        end
      end

      class ConfigLoadError < StandardError
      end

      DEFAULTS = {
        :address           => '0.0.0.0',
        :port              => 3000,
        :environment       => ENV['RAILS_ENV'] || ENV['RACK_ENV'] || ENV['NODE_ENV'] ||
          ENV['PASSENGER_APP_ENV'] || 'development',
        :spawn_method      => PlatformInfo.ruby_supports_fork? ? DEFAULT_SPAWN_METHOD : 'direct',
        :engine            => 'nginx',
        :nginx_version     => PREFERRED_NGINX_VERSION,
        :log_level         => DEFAULT_LOG_LEVEL,
        :auto              => !STDIN.tty? || !STDOUT.tty?,
        :ctls              => [],
        :envvars           => {}
      }.freeze

      def global_config_file_path
        @global_config_file_path ||= File.join(PhusionPassenger.home_dir,
          USER_NAMESPACE_DIRNAME, "standalone", "config.json")
      end

      def load_local_config_file_from_app_dir_param!(argv)
        if argv.empty?
          app_dir = Dir.logical_pwd
        elsif argv.size == 1
          app_dir = argv[0]
        end
        local_options = {}
        if app_dir
          begin
            ConfigUtils.load_local_config_file!(app_dir, local_options)
          rescue ConfigUtils::ConfigLoadError => e
            abort "*** ERROR: #{e.message}"
          end
        end
        local_options
      end

      def load_local_config_file!(app_dir, options)
        config_file = File.join(app_dir, "Passengerfile.json")
        if !File.exist?(config_file)
          config_file = File.join(app_dir, "passenger-standalone.json")
        end
        if File.exist?(config_file)
          local_options = load_config_file(config_file)
          options.merge!(local_options)
        end
      end

      def load_config_file(filename)
        if !defined?(PhusionPassenger::Utils::JSON)
          PhusionPassenger.require_passenger_lib 'utils/json'
        end
        begin
          data = File.open(filename, "r:utf-8") do |f|
            f.read
          end
        rescue SystemCallError => e
          raise ConfigLoadError, "cannot load config file #{filename} (#{e})"
        end

        begin
          config = PhusionPassenger::Utils::JSON.parse(data)
        rescue => e
          raise ConfigLoadError, "cannot parse config file #{filename} (#{e})"
        end
        if !config.is_a?(Hash)
          raise ConfigLoadError, "cannot parse config file #{filename} (it does not contain an object)"
        end

        result = {}
        config.each_pair do |key, val|
          result[key.to_sym] = val
        end

        resolve_config_file_paths(result, filename)

        result
      end

      # Absolutize relative paths. Make them relative to the config file in which
      # it's specified.
      def resolve_config_file_paths(config_file_options, config_filename)
        options = config_file_options
        config_file_dir = File.dirname(File.absolute_logical_path(config_filename))

        keys = [:socket_file, :ssl_certificate, :ssl_certificate_key, :log_file,
          :pid_file, :instance_registry_dir, :data_buffer_dir, :meteor_app_settings,
          :rackup, :startup_file, :static_files_dir, :restart_dir,
          :nginx_config_template]
        keys.each do |key|
          if filename = options[key]
            options[key] = File.expand_path(filename, config_file_dir)
          end
        end
      end

      # We want the command line options to override the options in the local
      # config file, but the local config file could only be parsed when the
      # command line options have been parsed. This method remerges all the
      # config options from different sources so that options are overriden
      # according to the following order:
      #
      # - ConfigUtils::DEFAULTS
      # - global config file
      # - local config file
      # - command line options
      def remerge_all_config(global_options, local_options, parsed_options)
        ConfigUtils::DEFAULTS.merge(remerge_all_config_without_defaults(
          global_options, local_options, parsed_options))
      end

      def remerge_all_config_without_defaults(global_options, local_options, parsed_options)
        global_options.
          merge(local_options).
          merge(parsed_options)
      end

      def find_pid_and_log_file(execution_root, options)
        if options[:socket_file]
          pid_basename = 'passenger.pid'
          log_basename = 'passenger.log'
        else
          pid_basename = "passenger.#{options[:port]}.pid"
          log_basename = "passenger.#{options[:port]}.log"
        end
        if File.directory?("#{execution_root}/tmp/pids")
          options[:pid_file] ||= "#{execution_root}/tmp/pids/#{pid_basename}"
        else
          options[:pid_file] ||= "#{execution_root}/#{pid_basename}"
        end
        if File.directory?("#{execution_root}/log")
          options[:log_file] ||= "#{execution_root}/log/#{log_basename}"
        else
          options[:log_file] ||= "#{execution_root}/#{log_basename}"
        end
      end
    end

  end # module Standalone
end # module PhusionPassenger
