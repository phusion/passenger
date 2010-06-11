# encoding: binary
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

require 'phusion_passenger'
require 'phusion_passenger/abstract_server'
require 'phusion_passenger/abstract_server_collection'
require 'phusion_passenger/constants'
require 'phusion_passenger/utils'

# Define a constant with a name that's unlikely to clash with anything the
# application defines, so that they can detect whether they're running under
# Phusion Passenger.
IN_PHUSION_PASSENGER = true

module PhusionPassenger

# The spawn manager is capable of spawning Ruby on Rails or Rack application
# instances. It acts like a simple fascade for the rest of the spawn manager
# system.
#
# *Note*: SpawnManager may only be started synchronously with
# AbstractServer#start_synchronously. Starting asynchronously has not been
# tested. Don't forget to call cleanup after the server's main loop has
# finished.
#
# == Ruby on Rails optimizations
#
# Spawning a Ruby on Rails application is usually slow. But SpawnManager
# will preload and cache Ruby on Rails frameworks, as well as application
# code, so subsequent spawns will be very fast.
#
# Internally, SpawnManager uses ClassicRails::FrameworkSpawner to preload and cache
# Ruby on Rails frameworks. ClassicRails::FrameworkSpawner, in turn, uses
# ClassicRails::ApplicationSpawner to preload and cache application code.
#
# In case you're wondering why the namespace is "ClassicRails" and not "Rails":
# it's to work around an obscure bug in ActiveSupport's Dispatcher.
class SpawnManager < AbstractServer
	include Utils
	
	def initialize(options = {})
		super("", "")
		@options = options
		@spawners = AbstractServerCollection.new
		define_message_handler(:spawn_application, :handle_spawn_application)
		define_message_handler(:reload, :handle_reload)
		define_signal_handler('SIGHUP', :reload)
		
		# Start garbage collector in order to free up some existing
		# heap slots. This prevents the heap from growing unnecessarily
		# during the startup phase.
		GC.start
		if GC.copy_on_write_friendly?
			# Preload libraries for copy-on-write semantics.
			require 'base64'
			require 'phusion_passenger/app_process'
			require 'phusion_passenger/classic_rails/framework_spawner'
			require 'phusion_passenger/classic_rails/application_spawner'
			require 'phusion_passenger/rack/application_spawner'
			require 'phusion_passenger/html_template'
			require 'phusion_passenger/platform_info'
			require 'phusion_passenger/exceptions'
		end
	end
	
	# Spawns an application with the given spawn options. When successful, an
	# AppProcess object will be returned, which represents the spawned application
	# process.
	#
	# Most options are explained in PoolOptions.h.
	#
	# Mandatory options:
	# - 'app_root'
	#
	# Optional options:
	# - 'app_type'
	# - 'environment'
	# - 'spawn_method'
	# - 'user',
	# - 'group'
	# - 'default_user'
	# - 'default_group'
	# - 'framework_spawner_timeout'
	# - 'app_spawner_timeout'
	# - 'environment_variables': Environment variables which should be passed
	#   to the spawned application process. This is NULL-seperated string of
	#   key-value pairs, encoded in base64. The last byte in the unencoded
	#   data must be a NULL.
	# - 'base_uri'
	# - 'print_exceptions'
	#
	# <b>Exceptions:</b>
	# - InvalidPath: +app_root+ doesn't appear to be a valid Ruby on Rails application root.
	# - VersionNotFound: The Ruby on Rails framework version that the given application requires
	#   is not installed.
	# - AbstractServer::ServerError: One of the server processes exited unexpectedly.
	# - FrameworkInitError: The Ruby on Rails framework that the application requires could not be loaded.
	# - AppInitError: The application raised an exception or called exit() during startup.
	def spawn_application(options)
		if !options["app_root"]
			raise ArgumentError, "The 'app_root' option must be given."
		end
		options = sanitize_spawn_options(options)
		
		case options["app_type"]
		when "rails"
			if !defined?(ClassicRails::FrameworkSpawner)
				require 'phusion_passenger/classic_rails/framework_spawner'
				require 'phusion_passenger/classic_rails/application_spawner'
			end
			return spawn_rails_application(options)
		when "rack"
			if !defined?(Rack::ApplicationSpawner)
				require 'phusion_passenger/rack/application_spawner'
			end
			return spawn_rack_application(options)
		when "wsgi"
			if !defined?(WSGI::ApplicationSpawner)
				require 'phusion_passenger/wsgi/application_spawner'
			end
			return WSGI::ApplicationSpawner.spawn_application(options)
		else
			raise ArgumentError, "Unknown 'app_type' value '#{options["app_type"]}'."
		end
	end
	
	# Remove the cached application instances at the given group name.
	# If nil is specified as group name, then all cached application
	# instances will be removed, no matter the group name.
	#
	# <b>Long description:</b>
	# Application code might be cached in memory. But once it a while, it will
	# be necessary to reload the code for an application, such as after
	# deploying a new version of the application. This method makes sure that
	# any cached application code is removed, so that the next time an
	# application instance is spawned, the application code will be freshly
	# loaded into memory.
	#
	# Raises AbstractServer::SpawnError if something went wrong.
	def reload(app_group_name = nil)
		@spawners.synchronize do
			if app_group_name
				# Stop and delete associated ApplicationSpawner.
				@spawners.delete("app:#{app_group_name}")
				# Propagate reload command to associated FrameworkSpawner.
				@spawners.each do |spawner|
					if spawner.respond_to?(:reload)
						spawner.reload(app_group_name)
					end
				end
			else
				# Stop and delete all spawners.
				@spawners.clear
			end
		end
	end
	
	# Cleanup resources. Should be called when this SpawnManager is no longer needed.
	def cleanup
		@spawners.cleanup
	end

private
	def spawn_rails_application(options)
		app_root       = options["app_root"]
		app_group_name = options["app_group_name"]
		spawn_method   = options["spawn_method"]
		spawner        = nil
		create_spawner = nil
		key            = nil
		
		case spawn_method
		when nil, "", "smart", "smart-lv2"
			if spawn_method != "smart-lv2"
				framework_version = AppProcess.detect_framework_version(app_root)
			end
			if framework_version.nil? || framework_version == :vendor
				key = "app:#{app_group_name}"
				create_spawner = proc do
					ClassicRails::ApplicationSpawner.new(@options.merge(options))
				end
				spawner_timeout = options["app_spawner_timeout"]
			else
				key = "version:#{framework_version}"
				create_spawner = proc do
					options["framework_version"] = framework_version
					ClassicRails::FrameworkSpawner.new(@options.merge(options))
				end
				spawner_timeout = options["framework_spawner_timeout"]
			end
			
			@spawners.synchronize do
				spawner = @spawners.lookup_or_add(key) do
					spawner = create_spawner.call
					if spawner_timeout != -1
						spawner.max_idle_time = spawner_timeout
					end
					spawner.start
					spawner
				end
				begin
					return spawner.spawn_application(options)
				rescue AbstractServer::ServerError
					@spawners.delete(key)
					raise
				end
			end
		else
			return ClassicRails::ApplicationSpawner.spawn_application(
				@options.merge(options))
		end
	end
	
	def spawn_rack_application(options)
		app_group_name = options["app_group_name"]
		spawn_method   = options["spawn_method"]
		spawner        = nil
		create_spawner = nil
		key            = nil
		
		case spawn_method
		when nil, "", "smart", "smart-lv2"
			@spawners.synchronize do
				key = "app:#{app_group_name}"
				spawner = @spawners.lookup_or_add(key) do
					spawner_timeout = options["app_spawner_timeout"]
					spawner = Rack::ApplicationSpawner.new(
						@options.merge(options))
					if spawner_timeout != -1
						spawner.max_idle_time = spawner_timeout
					end
					spawner.start
					spawner
				end
				begin
					return spawner.spawn_application(options)
				rescue AbstractServer::ServerError
					@spawners.delete(key)
					raise
				end
			end
		else
			return Rack::ApplicationSpawner.spawn_application(
				@options.merge(options))
		end
	end
	
	def handle_spawn_application(client, *options)
		options     = sanitize_spawn_options(Hash[*options])
		app_process = nil
		app_root    = options["app_root"]
		app_type    = options["app_type"]
		begin
			app_process = spawn_application(options)
		rescue AbstractServer::ServerError => e
			send_error_page(client, 'general_error', :error => e)
		rescue VersionNotFound => e
			send_error_page(client, 'version_not_found', :error => e, :app_root => app_root)
		rescue AppInitError => e
			if database_error?(e)
				send_error_page(client, 'database_error', :error => e,
					:app_root => app_root, :app_name => app_name(app_type),
					:app_type => app_type)
			elsif load_error?(e)
				# A source file failed to load, maybe because of a
				# missing gem. If that's the case then the sysadmin
				# will install probably the gem. So we clear RubyGems's
				# cache so that it can detect new gems.
				Gem.clear_paths
				send_error_page(client, 'load_error', :error => e, :app_root => app_root,
					:app_name => app_name(app_type))
			elsif e.child_exception.is_a?(SystemExit)
				send_error_page(client, 'app_exited_during_initialization', :error => e,
					:app_root => app_root, :app_name => app_name(app_type))
			else
				send_error_page(client, 'app_init_error', :error => e,
					:app_root => app_root, :app_name => app_name(app_type))
			end
		rescue FrameworkInitError => e
			send_error_page(client, 'framework_init_error', :error => e)
		end
		if app_process
			begin
				client.write('ok')
				app_process.write_to_channel(client)
			rescue Errno::EPIPE
				# The Apache module may be interrupted during a spawn command,
				# in which case it will close the connection. We ignore this error.
			ensure
				app_process.close
			end
		end
	end
	
	def handle_reload(client, app_group_name)
		reload(app_group_name)
	end
	
	def send_error_page(channel, template_name, options = {})
		require 'phusion_passenger/html_template' unless defined?(HTMLTemplate)
		if !defined?(PlatformInfo)
			require 'phusion_passenger/platform_info'
			require 'phusion_passenger/platform_info/ruby'
		end
		options["enterprisey"] = File.exist?("#{SOURCE_ROOT}/enterprisey.txt") ||
			File.exist?("/etc/passenger_enterprisey.txt")
		data = HTMLTemplate.new(template_name, options).result
		channel.write('error_page')
		channel.write_scalar(data)
	end
	
	def database_error?(e)
		return ( defined?(Mysql::Error) && e.child_exception.is_a?(Mysql::Error) ) ||
		       ( e.child_exception.is_a?(UnknownError) &&
		           (
		               e.child_exception.real_class_name =~ /^ActiveRecord/ ||
		               e.child_exception.real_class_name =~ /^Mysql::/
		           )
		       )
	end
	
	def load_error?(e)
		return e.child_exception.is_a?(LoadError) || (
		           e.child_exception.is_a?(UnknownError) &&
		           e.child_exception.real_class_name == "MissingSourceFile"
		)
	end
	
	def app_name(app_type)
		if app_type == "rails"
			return "Ruby on Rails"
		else
			return "Ruby (Rack)"
		end
	end
end

end # module PhusionPassenger
