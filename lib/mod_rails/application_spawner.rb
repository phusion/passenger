require 'socket'
require 'mod_rails/abstract_server'
require 'mod_rails/application'
require 'mod_rails/utils'
require 'mod_rails/request_handler'
module ModRails # :nodoc

# TODO: implement user switching

# This class is capable of spawns instances of a single Ruby on Rails application.
# It does so by preloading as much of the application's code as possible, then creating
# instances of the application using what is already preloaded. This makes it spawning
# application instances very fast, except for the first spawn.
#
# Use multiple instances of ApplicationSpawner if you need to spawn multiple different
# Ruby on Rails applications.
class ApplicationSpawner < AbstractServer
	include Utils
	
	class SpawnError < StandardError
	end
	
	class UserChangeError < StandardError
	end
	
	# An attribute, used internally. This should not be used outside mod_rails.
	attr_accessor :time

	# _app_root_ is the root directory of this application, i.e. the directory
	# that contains 'app/', 'public/', etc. If given an invalid directory,
	# or a directory that doesn't appear to be a Rails application root directory,
	# then an ArgumentError will be raised.
	#
	# You may optionally specify _user_ and _group_. If specified, ApplicationSpawner will
	# switch very spawned instance to the given user and group. This only works if
	# ApplicationSpawner is running as root (or has the capability to change the
	# current process's user).
	# If given an invalid user or group, then an ArgumentError will be raised.
	# If the current process's user/group cannot be changed, then a UserChangeError
	# will be raised.
	def initialize(app_root, user = nil, group = nil)
		super()
		@app_root = normalize_path(app_root)
		@user = user
		@group = group
		self.time = Time.now
		assert_valid_app_root(@app_root)
		assert_valid_username(@user) unless @user.nil?
		assert_valid_groupname(@group) unless @group.nil?
		define_message_handler(:spawn_application, :handle_spawn_application)
	end
	
	# Spawn an instance of the RoR application. When successful, an Application object
	# will be returned, which represents the spawned RoR application.
	#
	# If the ApplicationSpawner server hasn't already been started, a ServerNotStarted
	# will be raised.
	# If the RoR application failed to start, then a SpawnError will be raised. The
	# application's exception message will be printed to standard error.
	def spawn_application
		send_to_server("spawn_application")
		pid = recv_from_server
		listen_socket = recv_io_from_server
		return Application.new(@app_root, pid, listen_socket)
	rescue SystemCallError, IOError, SocketError
		raise SpawnError, "Unable to spawn the application: application died unexpectedly during initialization."
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
		Dir.chdir(@app_root)
		preload_application
	end
	
private
	def preload_application
		require 'config/environment'
		require_dependency 'application'
		Dir.glob('app/{models,controllers,helpers}/*.rb').each do |file|
			require normalize_path(file)
		end
	end

	def handle_spawn_application
		# Double fork to prevent zombie processes.
		pid = fork do
			begin
				pid = fork do
					begin
						send_to_client(Process.pid)
						
						socket1, socket2 = UNIXSocket.pair
						send_io_to_client(socket1)
						socket1.close
						@child_socket.close
						
						RequestHandler.new(socket2).main_loop
						socket2.close
					rescue Exception => e
						print_exception('application', e)
					ensure
						exit!
					end
				end
			rescue Exception => e
				print_exception(self.class.to_s, e)
			ensure
				exit!
			end
		end
		Process.waitpid(pid)
	end
end

end # module ModRails