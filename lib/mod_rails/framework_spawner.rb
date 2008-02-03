require 'rubygems'
require 'socket'
require 'pathname'
require 'mod_rails/abstract_server'
require 'mod_rails/application_spawner'
require 'mod_rails/utils'
require 'mod_rails/core_extensions'
module ModRails # :nodoc:

# TODO: check whether Rails version is supported
# TODO: check whether preloading Rails was successful

class FrameworkSpawner < AbstractServer
	APP_SPAWNER_CLEAN_INTERVAL = 125
	APP_SPAWNER_MAX_IDLE_TIME = 120

	include Utils
	
	# An attribute, used internally. This should not be used outside mod_rails.
	attr_accessor :time

	# Creates a new instance of FrameworkSpawner.
	#
	# _version_ is the Ruby on Rails version to use. If this version is not
	# installed (through RubyGems), then an ArgumentError will be raised.
	# _version_ may also be nil. In this case, it is unspecified which Rails
	# version will be loaded. The choice will depend on RubyGems.
	#
	# Note that this Rails version will be loaded during the entire life time
	# of the FrameworkSpawner server. If you wish to reload the Rails framework's code,
	# then restart the server by calling stop() and start().
	def initialize(version = nil)
		super()
		@version = version
		if !version.nil?
			@gem = Gem.cache.search('rails', "=#{version}.0").sort_by { |g| g.version.version }.last
			if @gem.nil?
				raise ArgumentError, "Ruby on Rails version #{version} is not installed."
			end
		else
			@gem = nil
		end
		define_message_handler(:spawn_application, :handle_spawn_application)
		define_message_handler(:reload, :handle_reload)
	end
	
	# Spawn a RoR application using the Ruby on Rails framework
	# version associated with this FrameworkSpawner.
	# When successful, an Application object will be returned, which represents
	# the spawned RoR application.
	#
	# FrameworkSpawner will internally use ApplicationSpawner, and cache ApplicationSpawner
	# objects for a while. As a result, spawning an instance of a RoR application for the
	# first time will be relatively slow, but following attempts will be very fast, as long
	# as the cache idle timeout hasn't been reached.
	#
	# This also implies that the application's code will be cached in memory. If you've
	# changed the application's code, you must do one of these things:
	# - Restart this FrameworkSpawner by calling stop(), then start().
	# - Reload the application by calling reload().
	#
	# If the FrameworkSpawner server hasn't already been started, a ServerNotStarted
	# will be raised.
	# If the RoR application failed to start (which may be a problem in the application,
	# or a problem in the Ruby on Rails framework), then a ApplicationSpawner::SpawnError
	# will be raised. The application's exception message will be printed to standard
	# error.
	def spawn_application(app_root, user = nil, group = nil)
		app_root = normalize_path(app_root)
		assert_valid_app_root(app_root)
		assert_valid_username(user) unless user.nil?
		assert_valid_groupname(group) unless group.nil?
		begin
			send_to_server("spawn_application", app_root, user, group)
			pid = recv_from_server
			listen_socket = recv_io_from_server
			return Application.new(app_root, pid, listen_socket)
		rescue SystemCallError, IOError, SocketError
			raise ApplicationSpawner::SpawnError, "Unable to spawn the application: " <<
				"either the Ruby on Rails framework failed to load, " <<
				"or the application died unexpectedly during initialization."
		end
	end
	
	def reload(app_root = nil)
		if app_root.nil?
			send_to_server("reload")
		else
			send_to_server("reload", normalize_path(app_root))
		end
	rescue Errno::EPIPE, Errno::EBADF, IOError, SocketError
		raise IOError, "Cannot send reload command to the framework spawner server."
	end

protected
	# Overrided method.
	def before_fork
		if GC.cow_friendly?
			# Garbage collect to so that the child process doesn't have to
			# do that (to prevent making pages dirty).
			GC.start
		end
	end

	# Overrided method.
	def initialize_server
		preload_rails
		@spawners = {}
		@spawners_lock = Mutex.new
		@spawners_cond = ConditionVariable.new
		@spawners_cleaner = Thread.new do
			begin
				spawners_cleaner_main_loop
			rescue Exception => e
				print_exception(self.class.to_s, e)
			end
		end
	end
	
	# Overrided method.
	def finalize_server
		@spawners_lock.synchronize do
			@spawners_cond.signal
		end
		@spawners_cleaner.join
		@spawners.each_value do |spawner|
			spawner.stop
		end
	end

private
	def preload_rails
		if @gem
			gem 'rails', "=#{@version}"
			require "#{@gem.full_gem_path}/lib/initializer"
		else
			gem 'rails'
			require 'initializer'
		end
		require 'active_support'
		require 'active_record'
		require 'action_controller'
		require 'action_view'
		require 'action_pack'
		require 'action_mailer'
		require 'dispatcher'
		require 'ruby_version_check'
		if Rails::VERSION::MAJOR >= 2
			require 'active_resource'
		else
			require 'action_web_service'
		end
	end

	def handle_spawn_application(app_root, user, group)
		user = nil if user && user.empty?
		group = nil if group && group.empty?
		@spawners_lock.synchronize do
			spawner = @spawners[app_root]
			if spawner.nil?
				spawner = ApplicationSpawner.new(app_root, user, group)
				spawner.file_descriptors_to_close = [@child_socket.fileno]
				spawner.start
				@spawners[app_root] = spawner
			end
			spawner.time = Time.now
			app = spawner.spawn_application
			send_to_client(app.pid)
			send_io_to_client(app.listen_socket)
			app.close
		end
	end
	
	def handle_reload(app_root = nil)
		@spawners_lock.synchronize do
			if app_root.nil?
				@spawners.each_value do |spawner|
					spawner.stop
				end
				@spawners.clear
			else
				spawner = @spawners[app_root]
				if spawner
					spawner.stop
				end
				@spawners.delete(app_root)
			end
		end
	end
	
	# The main loop for the spawners cleaner thread.
	# This thread checks the spawners list every APP_SPAWNER_CLEAN_INTERVAL seconds,
	# and stops application spawners that have been idle for more than
	# APP_SPAWNER_MAX_IDLE_TIME seconds.
	def spawners_cleaner_main_loop
		@spawners_lock.synchronize do
			while true
				if @spawners_cond.timed_wait(@spawners_lock, APP_SPAWNER_CLEAN_INTERVAL)
					break
				else
					current_time = Time.now
					@spawners.keys.each do |key|
						spawner = @spawners[key]
						if current_time - spawner.time > APP_SPAWNER_MAX_IDLE_TIME
							spawner.stop
							@spawners.delete(key)
						end
					end
				end
			end
		end
	end
end

end # module ModRails