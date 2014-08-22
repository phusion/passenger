#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2013 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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
require 'optparse'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'utils'
PhusionPassenger.require_passenger_lib 'standalone/utils'

module PhusionPassenger
module Standalone

class Command
	DEFAULT_OPTIONS = {
		:address       => '0.0.0.0',
		:port          => 3000,
		:environment   => ENV['RAILS_ENV'] || ENV['RACK_ENV'] || ENV['NODE_ENV'] || ENV['PASSENGER_APP_ENV'] || 'development',
		:max_pool_size => 6,
		:min_instances => 1,
		:spawn_method  => Kernel.respond_to?(:fork) ? 'smart' : 'direct',
		:concurrency_model => DEFAULT_CONCURRENCY_MODEL,
		:thread_count  => DEFAULT_THREAD_COUNT,
		:nginx_version => PREFERRED_NGINX_VERSION
	}.freeze

	include Utils

	def self.show_in_command_list
		return true
	end

	def self.description
		return nil
	end

	def initialize(args)
		@args = args.dup
		@original_args = args.dup
		@options = DEFAULT_OPTIONS.dup
	end

private
	def require_daemon_controller
		if !defined?(DaemonController)
			begin
				require 'daemon_controller'
				begin
					require 'daemon_controller/version'
					too_old = DaemonController::VERSION_STRING < '1.1.0'
				rescue LoadError
					too_old = true
				end
				if too_old
					PhusionPassenger.require_passenger_lib 'platform_info/ruby'
					gem_command = PlatformInfo.gem_command(:sudo => true)
					error "Your version of daemon_controller is too old. " <<
					      "You must install 1.1.0 or later. Please upgrade:\n\n" <<

					      " #{gem_command} uninstall FooBarWidget-daemon_controller\n" <<
					      " #{gem_command} install daemon_controller",
					      :wrap => false
					exit 1
				end
			rescue LoadError
				PhusionPassenger.require_passenger_lib 'platform_info/ruby'
				gem_command = PlatformInfo.gem_command(:sudo => true)
				error "Please install daemon_controller first:\n\n" <<
				      " #{gem_command} install daemon_controller"
				exit 1
			end
		end
	end

	def require_erb
		require 'erb' unless defined?(ERB)
	end

	def require_etc
		require 'etc' unless defined?(Etc)
	end

	def require_optparse
		require 'optparse' unless defined?(OptionParser)
	end

	def require_app_finder
		PhusionPassenger.require_passenger_lib 'standalone/app_finder' unless defined?(AppFinder)
	end

	def debugging?
		return ENV['PASSENGER_DEBUG'] && !ENV['PASSENGER_DEBUG'].empty?
	end

	def parse_options!(command_name, description = nil)
		require_etc
		help = false

		home_dir = PhusionPassenger::Utils.home_dir
		global_config_file = File.join(home_dir, USER_NAMESPACE_DIRNAME, "standalone", "config")
		if File.exist?(global_config_file)
			PhusionPassenger.require_passenger_lib 'standalone/config_file' unless defined?(ConfigFile)
			global_options = ConfigFile.new(:global_config, global_config_file).options
			@options.merge!(global_options)
		end

		require_optparse
		parser = OptionParser.new do |opts|
			opts.banner = "Usage: passenger #{command_name} [options]"
			opts.separator description if description
			opts.separator " "
			yield opts
			opts.on("-h", "--help", "Show this help message") do
				help = true
			end
		end
		parser.parse!(@args)
		if help
			puts parser
			exit 0
		end
	end

	def error(message, options = {})
		wrap = options.fetch(:wrap, true)
		if message =~ /\n/
			if wrap
				processed_message = wrap_desc(message, 80, 0)
			else
				processed_message = message
			end
			STDERR.puts("*** ERROR ***\n" << processed_message)
		else
			if wrap
				processed_message = wrap_desc("*** ERROR: #{message}", 80, 0)
			else
				processed_message = "*** ERROR: #{message}"
			end
			STDERR.puts(processed_message)
		end
		@plugin.call_hook(:error, message) if @plugin
	end

	# Word wrap the given option description text so that it is formatted
	# nicely in the --help output.
	def wrap_desc(description_text, max_width = 43, newline_prefix_size = 37)
		line_prefix = "\n" << (' ' * newline_prefix_size)
		result = description_text.gsub(/(.{1,#{max_width}})( +|$\n?)|(.{1,#{max_width}})/, "\\1\\3#{line_prefix}")
		result.strip!
		return result
	end

	def ensure_directory_exists(dir)
		if !File.exist?(dir)
			require_file_utils
			FileUtils.mkdir_p(dir)
		end
	end

	def determine_various_resource_locations(create_subdirs = true)
		require_app_finder
		if @options[:socket_file]
			pid_basename = "passenger.pid"
			log_basename = "passenger.log"
		else
			pid_basename = "passenger.#{@options[:port]}.pid"
			log_basename = "passenger.#{@options[:port]}.log"
		end
		if @args.empty?
			if AppFinder.looks_like_app_directory?(".")
				@options[:pid_file] ||= File.expand_path("tmp/pids/#{pid_basename}")
				@options[:log_file] ||= File.expand_path("log/#{log_basename}")
				if create_subdirs
					ensure_directory_exists(File.dirname(@options[:pid_file]))
					ensure_directory_exists(File.dirname(@options[:log_file]))
				end
			else
				@options[:pid_file] ||= File.expand_path(pid_basename)
				@options[:log_file] ||= File.expand_path(log_basename)
			end
		else
			@options[:pid_file] ||= File.expand_path(File.join(@args[0], pid_basename))
			@options[:log_file] ||= File.expand_path(File.join(@args[0], log_basename))
		end
	end

	def write_nginx_config_file
		PhusionPassenger.require_passenger_lib 'platform_info/ruby'
		PhusionPassenger.require_passenger_lib 'utils/tmpio'
		# @temp_dir may already be set because we're redeploying
		# using Mass Deployment.
		@temp_dir ||= PhusionPassenger::Utils.mktmpdir(
			"passenger-standalone.")
		@config_filename = "#{@temp_dir}/config"
		location_config_filename = "#{@temp_dir}/locations.ini"
		File.chmod(0755, @temp_dir)
		begin
			Dir.mkdir("#{@temp_dir}/logs")
		rescue Errno::EEXIST
		end

		locations_ini_fields =
			PhusionPassenger::REQUIRED_LOCATIONS_INI_FIELDS +
			PhusionPassenger::OPTIONAL_LOCATIONS_INI_FIELDS -
			[:agents_dir, :lib_dir]

		File.open(location_config_filename, 'w') do |f|
			f.puts '[locations]'
			f.puts "natively_packaged=#{PhusionPassenger.natively_packaged?}"
			if PhusionPassenger.natively_packaged?
				f.puts "native_packaging_method=#{PhusionPassenger.native_packaging_method}"
			end
			f.puts "lib_dir=#{@runtime_locator.find_lib_dir}"
			f.puts "agents_dir=#{@runtime_locator.find_agents_dir}"
			locations_ini_fields.each do |field|
				value = PhusionPassenger.send(field)
				f.puts "#{field}=#{value}" if value
			end
		end
		puts File.read(location_config_filename) if debugging?

		File.open(@config_filename, 'w') do |f|
			f.chmod(0644)
			require_erb
			erb = ERB.new(File.read(nginx_config_template_filename), nil, "-")
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

	def determine_nginx_start_command
		if @options[:nginx_bin]
			nginx_bin = @options[:nginx_bin]
		else
			nginx_bin = @runtime_locator.find_nginx_binary
		end
		return "#{nginx_bin} -c '#{@config_filename}' -p '#{@temp_dir}/'"
	end

	# Returns the port on which to ping Nginx.
	def nginx_ping_port
		if @options[:ping_port]
			return @options[:ping_port]
		else
			return @options[:port]
		end
	end

	def create_nginx_controller(extra_options = {})
		require_daemon_controller
		require 'socket' unless defined?(UNIXSocket)
		require 'thread' unless defined?(Mutex)
		if @options[:socket_file]
			ping_spec = [:unix, @options[:socket_file]]
		else
			ping_spec = [:tcp, @options[:address], nginx_ping_port]
		end
		opts = {
			:identifier    => 'Nginx',
			:before_start  => method(:write_nginx_config_file),
			:start_command => method(:determine_nginx_start_command),
			:ping_command  => ping_spec,
			:pid_file      => @options[:pid_file],
			:log_file      => @options[:log_file],
			:timeout       => 25
		}
		@nginx = DaemonController.new(opts.merge(extra_options))
		@nginx_mutex = Mutex.new
	end
end

end # module Standalone
end # module PhusionPassenger
