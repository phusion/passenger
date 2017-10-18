#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2014-2017 Phusion Holding B.V.
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

PhusionPassenger.require_passenger_lib 'ruby_core_enhancements'
PhusionPassenger.require_passenger_lib 'standalone/config_options_list'

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
        config_file_dir = File.dirname(File.absolute_logical_path(filename))
        config.each_pair do |key, val|
          key = key.to_sym
          spec_item = CONFIG_NAME_INDEX[key]
          if spec_item
            begin
              result[key] = parse_config_value(spec_item, val, config_file_dir)
            rescue ConfigLoadError => e
              raise ConfigLoadError, "cannot parse config file #{filename} " \
                "(error in config option '#{key}': #{e.message})"
            end
          else
            STDERR.puts "*** WARNING: #{filename}: configuration key '#{key}' is not supported"
            result[key] = val
          end
        end

        result
      end

      def load_env_config!
        begin
          load_env_config
        rescue ConfigUtils::ConfigLoadError => e
          abort "*** ERROR: #{e.message}"
        end
      end

      def load_env_config
        config = {}
        pwd = Dir.logical_pwd

        ENV.each_pair do |name, value|
          next if name !~ /^PASSENGER_(.+)/
          key = $1.downcase.to_sym
          spec_item = CONFIG_NAME_INDEX[key]
          next if !spec_item
          next if !config_type_supported_in_envvar?(spec_item[:type])
          next if value.empty?

          begin
            config[key] = parse_config_value(spec_item, value, pwd)
          rescue ConfigLoadError => e
            raise ConfigLoadError, "cannot parse environment variable '#{name}' " \
              "(#{e.message})"
          end
        end

        config
      end

      def add_option_parser_options_from_config_spec(parser, spec, options)
        spec.each do |spec_item|
          next if spec_item[:cli].nil?
          args = []

          if spec_item[:short_cli]
            args << spec_item[:short_cli]
          end

          args << make_long_cli_switch(spec_item)

          if type = determine_cli_switch_type(spec_item)
            args << type
          end

          args << format_cli_switch_description(spec_item)

          cli_parser = make_cli_switch_parser(parser, spec_item, options)

          parser.on(*args, &cli_parser)
        end
      end

      # We want the command line options to override the options in the local
      # config file, but the local config file could only be parsed when the
      # command line options have been parsed. This method remerges all the
      # config options from different sources so that options are overridden
      # according to the following order:
      #
      # - CONFIG_DEFAULTS
      # - global config file
      # - local config file
      # - environment variables
      # - command line options
      def remerge_all_config(global_options, local_options, env_options, parsed_options)
        CONFIG_DEFAULTS.merge(remerge_all_config_without_defaults(
          global_options, local_options, env_options, parsed_options))
      end

      def remerge_all_config_without_defaults(global_options, local_options, env_options, parsed_options)
        global_options.
          merge(local_options).
          merge(env_options).
          merge(parsed_options)
      end

      def find_pid_and_log_file(execution_root, options)
        if !options[:socket_file].nil?
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

    private
      def config_type_supported_in_envvar?(type)
        type == :string || type == :integer || type == :boolean ||
          type == :path || type == :hostname
      end

      def parse_config_value(spec_item, value, base_dir)
        if parser = spec_item[:config_value_parser]
          return parser.call(value, base_dir)
        end

        case spec_item[:type]
        when :string
          value.to_s
        when :integer
          value = value.to_i
          if spec_item[:min] && value < spec_item[:min]
            raise ConfigLoadError, "value must be greater than or equal to #{spec_item[:min]}"
          else
            value
          end
        when :boolean
          value = value.to_s.downcase
          value == 'true' || value == 'yes' || value == 'on' || value == '1'
        when :path
          File.absolute_logical_path(value.to_s, base_dir)
        when :array
          if value.is_a?(Array)
            value
          else
            raise ConfigLoadError, "array expected"
          end
        when :map
          if value.is_a?(Hash)
            value
          else
            raise ConfigLoadError, "map expected"
          end
        when :hostname
          begin
            resolve_hostname(value)
          rescue SocketError => e
            raise ConfigLoadError, "hostname #{value} cannot be resolved: #{e}"
          end
        else
          raise ArgumentError, "Unsupported type #{spec_item[:type]}"
        end
      end

      def make_long_cli_switch(spec_item)
        case spec_item[:type]
        when :string, :integer, :path, :array, :map, :hostname
          "#{spec_item[:cli]} #{spec_item[:type_desc]}"
        when :boolean
          spec_item[:cli]
        else
          raise ArgumentError,
            "Cannot create long CLI switch for type #{spec_item[:type]}"
        end
      end

      def determine_cli_switch_type(spec_item)
        case spec_item[:type]
        when :string, :path, :array, :map, :hostname
          String
        when :integer
          Integer
        when :boolean
          nil
        else
          raise ArgumentError,
            "Cannot determine CLI switch type for #{spec_item[:type]}"
        end
      end

      def format_cli_switch_description(spec_item)
        desc = spec_item[:desc]
        return '(no description)' if desc.nil?
        result = desc.gsub("\n", "\n" + ' ' * 37)
        result.gsub!('%DEFAULT%', (spec_item[:default] || 'N/A').to_s)
        result
      end

      def make_cli_switch_parser(parser, spec_item, options)
        if cli_parser = spec_item[:cli_parser]
          lambda do |value|
            cli_parser.call(options, value)
          end
        elsif spec_item[:type] == :integer
          lambda do |value|
            if spec_item[:min] && value < spec_item[:min]
              abort "*** ERROR: you may only specify for #{spec_item[:cli]} " \
                "a number greater than or equal to #{spec_item[:min]}"
            end
            options[spec_item[:name]] = value
          end
        elsif spec_item[:type] == :path
          lambda do |value|
            options[spec_item[:name]] = File.absolute_logical_path(value,
              Dir.logical_pwd)
          end
        elsif spec_item[:type] == :boolean
          lambda do |value|
            options[spec_item[:name]] = true
          end
        elsif spec_item[:type] == :hostname
          lambda do |value|
            begin
              options[spec_item[:name]] = resolve_hostname(value)
            rescue SocketError => e
              abort "*** ERROR: the hostname passed to #{spec_item[:cli]}, #{value}, cannot be resolved: #{e}"
            end
          end
        else
          lambda do |value|
            options[spec_item[:name]] = value
          end
        end
      end

      def resolve_hostname(hostname)
        # We resolve the hostname into an IP address during configuration loading
        # because different components in the system (Nginx, Passenger core) may
        # resolve hostnames differently. If a hostname resolves to multiple addresses
        # (for example, to an IPv6 and an IPv4 address) then different components may
        # pick a different address as 'winner'. By resolving the hostname here, we
        # guarantee consistent behavior.
        #
        # Furthermore, `rails server` defaults to setting the hostname to 'localhost'.
        # But the user almost certainly doesn't explicitly want it to resolve to an IPv6
        # address because most tools work better with IPv4 and because `http://[::1]:3000`
        # just looks weird. So we special case 'localhost' and resolve it to 127.0.0.1.
        if hostname.downcase == 'localhost'
          '127.0.0.1'
        else
          Socket.getaddrinfo(hostname, nil).first[3]
        end
      end
    end

  end # module Standalone
end # module PhusionPassenger
