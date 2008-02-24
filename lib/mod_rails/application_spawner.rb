require 'socket'
require 'etc'
require 'mod_rails/abstract_server'
require 'mod_rails/application'
require 'mod_rails/utils'
require 'mod_rails/request_handler'
module ModRails # :nodoc

# This class is capable of spawns instances of a single Ruby on Rails application.
# It does so by preloading as much of the application's code as possible, then creating
# instances of the application using what is already preloaded. This makes it spawning
# application instances very fast, except for the first spawn.
#
# Use multiple instances of ApplicationSpawner if you need to spawn multiple different
# Ruby on Rails applications.
class ApplicationSpawner < AbstractServer
	include Utils
	
	ROOT_UID = 0
	ROOT_GID = 0
	
	class SpawnError < StandardError
	end
	
	# An attribute, used internally. This should not be used outside Passenger.
	attr_accessor :time

	# _app_root_ is the root directory of this application, i.e. the directory
	# that contains 'app/', 'public/', etc. If given an invalid directory,
	# or a directory that doesn't appear to be a Rails application root directory,
	# then an ArgumentError will be raised.
	#
	# If _lower_privilege_ is true, then ApplicationSpawner will attempt to
	# switch to the user who owns the application's <tt>config/environment.rb</tt>,
	# and to the default group of that user.
	#
	# If that user doesn't exist on the system, or if that user is root,
	# then ApplicationSpawner will attempt to switch to the username given by
	# _lowest_user_ (and to the default group of that user).
	# If _lowest_user_ doesn't exist either, or if switching user failed
	# (because the current process does not have the privilege to do so),
	# then ApplicationSpawner will continue without reporting an error.
	def initialize(app_root, lower_privilege = true, lowest_user = "nobody")
		super()
		@app_root = normalize_path(app_root)
		@lower_privilege = lower_privilege
		@lowest_user = lowest_user
		self.time = Time.now
		assert_valid_app_root(@app_root)
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
		server.write("spawn_application")
		pid, socket_name = server.read
		owner_pipe = server.recv_io
		return Application.new(@app_root, pid, socket_name, owner_pipe)
	rescue SystemCallError, IOError, SocketError
		raise SpawnError, "Unable to spawn the application: application died unexpectedly during initialization."
	end

protected
	# Overrided method.
	def before_fork # :nodoc:
		if GC.cow_friendly?
			# Garbage collect to so that the child process doesn't have to
			# do that (to prevent making pages dirty).
			GC.start
		end
	end
	
	# Overrided method.
	def initialize_server # :nodoc:
		$0 = "Passenger ApplicationSpawner: #{@app_root}"
		Dir.chdir(@app_root)
		lower_privilege! if @lower_privilege
		preload_application
	end
	
private
	def lower_privilege!
		stat = File.stat("config/environment.rb")
		if !switch_to_user(stat.uid)
			switch_to_user(@lowest_user)
		end
	end

	def switch_to_user(user)
		begin
			if user.is_a?(String)
				pw = Etc.getpwnam(user)
				uid = pw.uid
				gid = pw.gid
			else
				uid = user
				gid = Etc.getpwuid(uid).gid
			end
		rescue
			return false
		end
		if uid == ROOT_UID
			return false
		else
			Process::Sys.setgid(gid)
			Process::Sys.setuid(uid)
			return true
		end
	end

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
						start_request_handler
					rescue SignalException => signal
						if e.message != RequestHandler::HARD_TERMINATION_SIGNAL &&
						   e.message != RequestHandler::SOFT_TERMINATION_SIGNAL
							print_exception('application', e)
						end
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
	
	def start_request_handler
		$0 = "Rails: #{@app_root}"
		reader, writer = IO.pipe
		begin
			handler = RequestHandler.new(reader)
			client.write(Process.pid, handler.socket_name)
			client.send_io(writer)
			writer.close
			client.close
			handler.main_loop
		ensure
			client.close rescue nil
			writer.close rescue nil
			handler.cleanup rescue nil
		end
	end
end

end # module ModRails
