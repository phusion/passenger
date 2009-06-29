#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2008, 2009 Phusion
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

require 'rubygems'
require 'phusion_passenger/abstract_server'
require 'phusion_passenger/abstract_server_collection'
require 'phusion_passenger/railz/application_spawner'
require 'phusion_passenger/exceptions'
require 'phusion_passenger/constants'
require 'phusion_passenger/utils'
module PhusionPassenger
module Railz

# This class is capable of spawning Ruby on Rails application instances
# quickly. This is done by preloading the Ruby on Rails framework into memory,
# before spawning the application instances.
#
# A single FrameworkSpawner instance can only hold a single Ruby on Rails
# framework version. So be careful when using FrameworkSpawner: the applications
# that you spawn through it must require the same RoR version. To handle multiple
# RoR versions, use multiple FrameworkSpawner instances.
#
# FrameworkSpawner uses ApplicationSpawner internally.
#
# *Note*: FrameworkSpawner may only be started asynchronously with AbstractServer#start.
# Starting it synchronously with AbstractServer#start_synchronously has not been tested.
class FrameworkSpawner < AbstractServer
	include Utils
	
	# This exception means that the FrameworkSpawner server process exited unexpectedly.
	class Error < AbstractServer::ServerError
	end
	
	# Creates a new instance of FrameworkSpawner.
	#
	# Valid options are:
	# - <tt>:version</tt>: The Ruby on Rails version to use. It is not checked whether
	#   this version is actually installed.
	# - <tt>:vendor</tt>: The directory to the vendor Rails framework to use. This is
	#   usually something like "/webapps/foo/vendor/rails".
	# - <tt>:print_framework_loading_exceptions</tt>:
	#   Whether exceptions that have occurred while loading the Ruby on Rails framework
	#   should be printed to STDERR. The default is true.
	#
	# It is not allowed to specify both +version+ and +vendor+.
	#
	# All other options will be passed on to ApplicationSpawner and RequestHandler.
	#
	# Note that the specified Rails framework will be loaded during the entire life time
	# of the FrameworkSpawner server. If you wish to reload the Rails framework's code,
	# then restart the server by calling AbstractServer#stop and AbstractServer#start.
	def initialize(options = {})
		if !options.respond_to?(:'[]')
			raise ArgumentError, "The 'options' argument not seem to be an options hash"
		end
		@version = options[:version]
		@vendor  = options[:vendor]
		if options.has_key?(:print_framework_loading_exceptions)
			@print_framework_loading_exceptions = options[:print_framework_loading_exceptions]
		else
			@print_framework_loading_exceptions = true
		end
		if !@version && !@vendor
			raise ArgumentError, "Either the 'version' or the 'vendor' option must specified"
		elsif @version && @vendor
			raise ArgumentError, "It is not allowed to specify both the 'version' and the 'vendor' options"
		end
		
		super()
		self.max_idle_time = DEFAULT_FRAMEWORK_SPAWNER_MAX_IDLE_TIME
		define_message_handler(:spawn_application, :handle_spawn_application)
		define_message_handler(:reload, :handle_reload)
	end
	
	# Overrided from AbstractServer#start.
	#
	# May raise these additional exceptions:
	# - FrameworkInitError: An error occurred while loading the specified Ruby on Rails framework.
	# - FrameworkSpawner::Error: The FrameworkSpawner server exited unexpectedly.
	def start
		super
		begin
			result = server.read
			if result.nil?
				raise Error, "The framework spawner server exited unexpectedly."
			else
				status = result[0]
			end
			if status == 'exception'
				child_exception = unmarshal_exception(server.read_scalar)
				stop
				if @version
					message = "Could not load Ruby on Rails framework version #{@version}: " <<
						"#{child_exception.class} (#{child_exception.message})"
				else
					message = "Could not load Ruby on Rails framework at '#{@vendor}': " <<
						"#{child_exception.class} (#{child_exception.message})"
				end
				options = { :vendor => @vendor, :version => @version }
				if @print_framework_loading_exceptions
					print_exception(self.class.to_s, child_exception)
				end
				raise FrameworkInitError.new(message, child_exception, options)
			end
		rescue IOError, SystemCallError, SocketError
			stop
			raise Error, "The framework spawner server exited unexpectedly"
		end
	end
	
	# Spawn a RoR application using the Ruby on Rails framework
	# version associated with this FrameworkSpawner.
	# When successful, an Application object will be returned, which represents
	# the spawned RoR application.
	#
	# All options accepted by ApplicationSpawner.new and RequestHandler.new are accepted.
	#
	# FrameworkSpawner will internally cache the code of applications, in order to
	# speed up future spawning attempts. This implies that, if you've changed
	# the application's code, you must do one of these things:
	# - Restart this FrameworkSpawner by calling AbstractServer#stop, then AbstractServer#start.
	# - Reload the application by calling reload with the correct app_root argument.
	#
	# Raises:
	# - AbstractServer::ServerNotStarted: The FrameworkSpawner server hasn't already been started.
	# - InvalidPath: +app_root+ doesn't appear to be a valid Ruby on Rails application root.
	# - AppInitError: The application raised an exception or called exit() during startup.
	# - ApplicationSpawner::Error: The ApplicationSpawner server exited unexpectedly.
	# - FrameworkSpawner::Error: The FrameworkSpawner server exited unexpectedly.
	def spawn_application(app_root, options = {})
		assert_valid_app_root(app_root)
		options = sanitize_spawn_options(options)
		options["app_root"] = app_root
		# No need for the ApplicationSpawner to print exceptions. All
		# exceptions raised by the ApplicationSpawner are sent back here,
		# so we just need to decide here whether we want to print it.
		print_exceptions = options["print_exceptions"]
		options["print_exceptions"] = false
		
		begin
			server.write("spawn_application", *options.to_a.flatten)
			result = server.read
			if result.nil?
				raise IOError, "Connection closed"
			end
			if result[0] == 'exception'
				e = unmarshal_exception(server.read_scalar)
				if print_exceptions && e.respond_to?(:child_exception) && e.child_exception
					print_exception(self.class.to_s, e.child_exception)
				elsif print_exceptions
					print_exception(self.class.to_s, e)
				end
				raise e
			else
				pid, listen_socket_name, socket_type = server.read
				if pid.nil?
					raise IOError, "Connection closed"
				end
				owner_pipe = server.recv_io
				return Application.new(app_root, pid, listen_socket_name,
					socket_type, owner_pipe)
			end
		rescue SystemCallError, IOError, SocketError => e
			raise Error, "The framework spawner server exited unexpectedly"
		end
	end
	
	# Remove the cached application instances at the given application root.
	# If nil is specified as application root, then all cached application
	# instances will be removed, no matter the application root.
	#
	# <b>Long description:</b>
	# Application code might be cached in memory by a FrameworkSpawner. But
	# once it a while, it will be necessary to reload the code for an
	# application, such as after deploying a new version of the application.
	# This method makes sure that any cached application code is removed, so
	# that the next time an application instance is spawned, the application
	# code will be freshly loaded into memory.
	#
	# Raises:
	# - ArgumentError: +app_root+ doesn't appear to be a valid Ruby on Rails
	#   application root.
	# - FrameworkSpawner::Error: The FrameworkSpawner server exited unexpectedly.
	def reload(app_root = nil)
		if app_root.nil?
			server.write("reload")
		else
			server.write("reload", app_root)
		end
	rescue SystemCallError, IOError, SocketError
		raise Error, "The framework spawner server exited unexpectedly"
	end

protected
	# Overrided method.
	def before_fork # :nodoc:
		if GC.copy_on_write_friendly?
			# Garbage collect now so that the child process doesn't have to
			# do that (to prevent making pages dirty).
			GC.start
		end
	end

	# Overrided method.
	def initialize_server # :nodoc:
		$0 = "Passenger FrameworkSpawner: #{@version || @vendor}"
		@spawners = AbstractServerCollection.new
		begin
			preload_rails
		rescue StandardError, ScriptError, NoMemoryError => e
			client.write('exception')
			client.write_scalar(marshal_exception(e))
			return
		end
		client.write('success')
	end
	
	# Overrided method.
	def finalize_server # :nodoc:
		@spawners.cleanup
	end

private
	def preload_rails
		Object.const_set(:RAILS_ROOT, ".")
		if @version
			gem 'rails', "=#{@version}"
			require 'initializer'
		else
			$LOAD_PATH.unshift("#{@vendor}/railties/builtin/rails_info")
			Dir["#{@vendor}/*"].each do |entry|
				next unless File.directory?(entry)
				$LOAD_PATH.unshift("#{entry}/lib")
			end
			require "#{@vendor}/railties/lib/initializer"
		end
		require 'active_support'
		require 'active_record'
		require 'action_controller'
		require 'action_view'
		require 'action_pack'
		require 'action_mailer'
		require 'dispatcher'
		begin
			if ::Rails::VERSION::MAJOR >= 2
				require 'active_resource'
			else
				require 'action_web_service'
			end
			require 'ruby_version_check'
			require 'active_support/whiny_nil'
		rescue NameError
			# Rails < 1.1
			require 'action_web_service'
		end
		Object.send(:remove_const, :RAILS_ROOT)
	end

	def handle_spawn_application(*options)
		options = sanitize_spawn_options(Hash[*options])
		
		app = nil
		app_root = options["app_root"]
		@spawners.synchronize do
			begin
				spawner = @spawners.lookup_or_add(app_root) do
					spawner = ApplicationSpawner.new(app_root, options)
					if options["app_spawner_timeout"] && options["app_spawner_timeout"] != -1
						spawner.max_idle_time = options["app_spawner_timeout"]
					end
					spawner.start
					spawner
				end
			rescue InvalidPath, AppInitError, ApplicationSpawner::Error => e
				client.write('exception')
				client.write_scalar(marshal_exception(e))
				if e.respond_to?(:child_exception) && e.child_exception.is_a?(LoadError)
					# A source file failed to load, maybe because of a
					# missing gem. If that's the case then the sysadmin
					# will install probably the gem. So we clear RubyGems's
					# cache so that it can detect new gems.
					Gem.clear_paths
				end
				return
			end
			begin
				app = spawner.spawn_application
			rescue ApplicationSpawner::Error => e
				spawner.stop
				@spawners.delete(app_root)
				client.write('exception')
				client.write_scalar(marshal_exception(e))
				return
			end
		end
		client.write('success')
		client.write(app.pid, app.listen_socket_name, app.listen_socket_type)
		client.send_io(app.owner_pipe)
		app.close
	end
	
	def handle_reload(app_root = nil)
		@spawners.synchronize do
			if app_root
				@spawners.delete(app_root)
			else
				@spawners.clear
			end
		end
	end
end

end # module Railz
end # module PhusionPassenger
