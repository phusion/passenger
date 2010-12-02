#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010 Phusion
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
require 'phusion_passenger'
require 'phusion_passenger/standalone/utils'

module PhusionPassenger
module Standalone

class Command
	DEFAULT_OPTIONS = {
		:address       => '0.0.0.0',
		:port          => 3000,
		:env           => ENV['RAILS_ENV'] || ENV['RACK_ENV'] || 'development',
		:max_pool_size => 6,
		:min_instances => 1,
		:spawn_method  => 'smart-lv2',
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
					too_old = DaemonController::VERSION_STRING < '0.2.5'
				rescue LoadError
					too_old = true
				end
				if too_old
					error "Your version of daemon_controller is too old. " <<
					      "You must install 0.2.5 or later. Please upgrade:\n\n" <<
					      
					      " sudo gem uninstall FooBarWidget-daemon_controller\n" <<
					      " sudo gem install daemon_controller"
					exit 1
				end
			rescue LoadError
				error "Please install daemon_controller first:\n\n" <<
				      " sudo gem install daemon_controller"
				exit 1
			end
		end
	end
	
	def require_erb
		require 'erb' unless defined?(ERB)
	end
	
	def require_optparse
		require 'optparse' unless defined?(OptionParser)
	end
	
	def require_app_finder
		require 'phusion_passenger/standalone/app_finder' unless defined?(AppFinder)
	end
	
	def debugging?
		return ENV['PASSENGER_DEBUG'] && !ENV['PASSENGER_DEBUG'].empty?
	end
	
	def parse_options!(command_name, description = nil)
		help = false
		
		global_config_file = File.join(ENV['HOME'], LOCAL_DIR, "standalone", "config")
		if File.exist?(global_config_file)
			require 'phusion_passenger/standalone/config_file' unless defined?(ConfigFile)
			global_options = ConfigFile.new(:global_config, global_config_file).options
			@options.merge!(global_options)
		end
		
		require_optparse
		parser = OptionParser.new do |opts|
			opts.banner = "Usage: passenger #{command_name} [options]"
			opts.separator description if description
			opts.separator " "
			opts.separator "Options:"
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
	
	def error(message)
		if message =~ /\n/
			STDERR.puts("*** ERROR ***\n" << wrap_desc(message, 80, 0))
		else
			STDERR.puts(wrap_desc("*** ERROR: #{message}", 80, 0))
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
		require 'phusion_passenger/platform_info/ruby'
		ensure_directory_exists(@temp_dir)
		
		File.open(@config_filename, 'w') do |f|
			f.chmod(0644)
			template_filename = File.join(TEMPLATES_DIR, "standalone", "config.erb")
			require_erb
			erb = ERB.new(File.read(template_filename))
			
			if debugging?
				passenger_root = SOURCE_ROOT
			else
				passenger_root = passenger_support_files_dir
			end
			# The template requires some helper methods which are defined in start_command.rb.
			output = erb.result(binding)
			f.write(output)
			puts output if debugging?
		end
	end
	
	def determine_nginx_start_command
		if @options[:nginx_bin]
			nginx_bin = @options[:nginx_bin]
		else
			nginx_bin = "#{nginx_dir}/sbin/nginx"
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
	
	def ping_nginx
		require 'socket' unless defined?(UNIXSocket)
		if @options[:socket_file]
			UNIXSocket.new(@options[:socket_file])
		else
			TCPSocket.new(@options[:address], nginx_ping_port)
		end
	end
	
	def create_nginx_controller(extra_options = {})
		require_daemon_controller
		@temp_dir        = "/tmp/passenger-standalone.#{$$}"
		@config_filename = "#{@temp_dir}/config"
		opts = {
			:identifier    => 'Nginx',
			:before_start  => method(:write_nginx_config_file),
			:start_command => method(:determine_nginx_start_command),
			:ping_command  => method(:ping_nginx),
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
