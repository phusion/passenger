require 'socket'
require 'mod_rails/message_channel'
require 'mod_rails/utils'
module ModRails # :nodoc:

# An abstract base class for a server, with the following properties:
#
# - The server has exactly one client, and is connected to that client at all times. The server will
#   quit when the connection closes.
# - The server's main loop may be run in a child process (and so is asynchronous from the main process).
# - One can communicate with the server through discrete messages (as opposed to byte streams).
# - The server can pass file descriptors (IO objects) back to the client.
#
# A message is just an ordered list of strings. The first element in the message is the _message name_.
#
# The server will also reset all signal handlers (in the child process). That is, it will respond to
# all signals in the default manner. The only exception is SIGHUP, which is ignored. One may define
# additional signal handlers using define_signal_handler().
#
# Before an AbstractServer can be used, it must first be started by calling start(). When it is no
# longer needed, stop() should be called.
#
# Here's an example on using AbstractServer:
#
#  class MyServer < ModRails::AbstractServer
#     def initialize
#        super()
#        define_message_handler(:hello, :handle_hello)
#     end
#
#     def hello(first_name, last_name)
#        send_to_server('hello', first_name, last_name)
#        reply, pointless_number = recv_from_server
#        puts "The server said: #{reply}"
#        puts "In addition, it sent this pointless number: #{pointless_number}"
#     end
#
#  private
#     def handle_hello(first_name, last_name)
#        send_to_client("Hello #{first_name} #{last_name}, how are you?", 1234)
#     end
#  end
#  
#  server = MyServer.new
#  server.start
#  server.hello("Joe", "Dalton")
#  server.stop
class AbstractServer
	include Utils
	SERVER_TERMINATION_SIGNAL = "SIGTERM"

	# Raised when the server receives a message with an unknown message name.
	class UnknownMessage < StandardError
	end
	
	# Raised when a command is invoked that requires that the server is
	# not already started.
	class ServerAlreadyStarted < StandardError
	end
	
	# Raised when a command is invoked that requires that the server is
	# already started.
	class ServerNotStarted < StandardError
	end
	
	def initialize
		@done = false
		@message_handlers = {}
		@signal_handlers = {}
		@orig_signal_handlers = {}
	end
	
	# Start the server. This method does not block since the server runs
	# asynchronously from the current process.
	#
	# You may only call this method if the server is not already started.
	# Otherwise, a ServerAlreadyStarted will be raised.
	def start
		if !@parent_channel.nil?
			raise ServerAlreadyStarted, "Server is already started"
		end
	
		@parent_socket, @child_socket = UNIXSocket.pair
		before_fork
		@pid = fork do
			begin
				@parent_socket.close
				NativeSupport.close_all_file_descriptors([0, 1, 2, @child_socket.fileno])
				start_synchronously(@child_socket)
			rescue Interrupt
				# Do nothing.
			rescue SignalException => signal
				if signal.message == SERVER_TERMINATION_SIGNAL
					# Do nothing.
				else
					print_exception(self.class.to_s, signal)
				end
			rescue Exception => e
				print_exception(self.class.to_s, e)
			ensure
				exit!
			end
		end
		@child_socket.close
		@parent_channel = MessageChannel.new(@parent_socket)
	end
	
	# Start the server, but in the current process instead of in a child process.
	# This method blocks until the server's main loop has ended.
	#
	# _socket_ is the socket that the server should listen on. The server main
	# loop will end if the socket has been closed.
	#
	# All hooks will be called, except before_fork().
	def start_synchronously(socket)
		@child_socket = socket
		@child_channel = MessageChannel.new(socket)
		begin
			reset_signal_handlers
			initialize_server
			begin
				main_loop
			ensure
				finalize_server
			end
		ensure
			revert_signal_handlers
		end
	end
	
	# Stop the server. The server will quit as soon as possible. This method waits
	# until the server has been stopped.
	#
	# When calling this method, the server must already be started. If not, a
	# ServerNotStarted will be raised.
	def stop
		if @parent_channel.nil?
			raise ServerNotStarted, "Server is not started"
		end
		
		@parent_socket.close
		@parent_channel = nil
		Process.kill(SERVER_TERMINATION_SIGNAL, @pid) rescue nil
		Process.waitpid(@pid) rescue nil
	end
	
	# Return the PID of the started server. This is only valid if start() has been called.
	def server_pid
		return @pid
	end

protected
	# A hook which is called when the server is being started, just before forking a new process.
	# The default implementation does nothing, this method is supposed to be overrided by child classes.
	def before_fork
	end
	
	# A hook which is called when the server is being started. This is called in the child process,
	# before the main loop is entered.
	# The default implementation does nothing, this method is supposed to be overrided by child classes.
	def initialize_server
	end
	
	# A hook which is called when the server is being stopped. This is called in the child process,
	# after the main loop has been left.
	# The default implementation does nothing, this method is supposed to be overrided by child classes.
	def finalize_server
	end
	
	# Define a handler for a message. _message_name_ is the name of the message to handle,
	# and _handler_ is the name of a method to be called (this may either be a String or a Symbol).
	#
	# A message is just a list of strings, and so _handler_ will be called with the message as its
	# arguments, excluding the first element. See also the example in the class description.
	def define_message_handler(message_name, handler)
		@message_handlers[message_name.to_s] = handler
	end
	
	# Define a handler for a signal.
	def define_signal_handler(signal, handler)
		@signal_handlers[signal.to_s] = handler
	end
	
	# Return the communication channel with the server. This is a MessageChannel
	# object.
	#
	# Raises ServerNotStarted if the server hasn't been started yet.
	#
	# This method may only be called in the parent process, and not
	# in the started server process.
	def server
		if @parent_channel.nil?
			raise ServerNotStarted, "Server hasn't been started yet. Please call start() first."
		end
		return @parent_channel
	end
	
	# Return the communication channel with the client (i.e. the parent process
	# that started the server). This is a MessageChannel object.
	def client
		return @child_channel
	end
	
	# Tell the main loop to stop as soon as possible.
	def quit_main
		@done = true
	end

private
	# Reset all signal handlers to default. This is called in the child process,
	# before entering the main loop.
	def reset_signal_handlers
		Signal.list.each_key do |signal|
			begin
				@orig_signal_handlers[signal] = trap(signal, 'DEFAULT')
			rescue ArgumentError
				# Signal cannot be trapped; ignore it.
			end
		end
		@signal_handlers.each_pair do |signal, handler|
			trap(signal) do
				__send__(handler)
			end
		end
		trap('HUP', 'IGNORE')
	end
	
	def revert_signal_handlers
		@orig_signal_handlers.each_pair do |signal, handler|
			trap(signal, handler)
		end
		@orig_signal_handlers.clear
	end
	
	# The server's main loop. This is called in the child process.
	# The main loop's main function is to read messages from the socket,
	# and letting registered message handlers handle each message.
	# Use define_message_handler() to register a message handler.
	#
	# If an unknown message is encountered, UnknownMessage will be raised.
	def main_loop
		channel = MessageChannel.new(@child_socket)
		while !@done
			begin
				name, *args = channel.read
				if name.nil?
					@done = true
				elsif @message_handlers.has_key?(name)
					__send__(@message_handlers[name], *args)
				else
					raise UnknownMessage, "Unknown message '#{name}' received."
				end
			rescue Interrupt
				@done = true
			rescue SignalException => signal
				if signal.message == SERVER_TERMINATION_SIGNAL
					@done = true
				else
					raise
				end
			end
		end
	end
end

end # module ModRails
