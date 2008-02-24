require 'timeout'
require 'mod_rails/message_channel'
require 'mod_rails/cgi_fixed'

module ModRails # :nodoc:

class RequestHandler
	# Signal which will cause the Rails application to exit immediately.
	HARD_TERMINATION_SIGNAL = "SIGTERM"
	# Signal which will cause the Rails application to exit as soon as it's done processing a request.
	SOFT_TERMINATION_SIGNAL = "SIGUSR1"
	
	# String constants which exist to relieve Ruby's garbage collector.
	IGNORE = 'IGNORE' # :nodoc:
	DEFAULT = 'DEFAULT' # :nodoc:
	CONTENT_LENGTH = 'CONTENT_LENGTH' # :nodoc:
	HTTP_CONTENT_LENGTH = 'HTTP_CONTENT_LENGTH' # :nodoc:

	def initialize(socket_filename, listen_socket)
		@socket_filename = socket_filename
		@listen_socket = listen_socket
		@previous_signal_handlers = {}
	end
	
	def main_loop
		reset_signal_handlers
		begin
			done = false
			while !done
				client = accept_connection_or_exit
				trap SOFT_TERMINATION_SIGNAL do
					done = true
				end
				process_request(client)
				trap SOFT_TERMINATION_SIGNAL, DEFAULT
			end
		rescue EOFError
			# Exit main loop.
		rescue Interrupt
			# Exit main loop.
		rescue SignalException => signal
			if signal.message != HARD_TERMINATION_SIGNAL &&
			   signal.message != SOFT_TERMINATION_SIGNAL
				raise
			end
		ensure
			revert_signal_handlers
		end
	end

private
	def reset_signal_handlers
		Signal.list.each_key do |signal|
			begin
				prev_handler = trap(signal, DEFAULT)
				if prev_handler != DEFAULT
					@previous_signal_handlers[signal] = prev_handler
				end
			rescue ArgumentError
				# Signal cannot be trapped; ignore it.
			end
		end
		prev_handler = trap('HUP', IGNORE)
	end
	
	def revert_signal_handlers
		@previous_signal_handlers.each_pair do |signal, handler|
			trap(signal, handler)
		end
	end
	
	def accept_connection_or_exit
		while true
			# TODO: poll some IO object in order to check whether the
			# owning process has exited.
			begin
				Timeout.timeout(60) do
					return @listen_socket.accept
				end
			rescue Timeout::Error
				# Do nothing.
			end
		end
	end
	
	class ResponseSender
		def initialize(io)
			@io = io
		end
		
		def write(block)
			@io.write(block)
		end
	end
	
	def process_request(socket)
		channel = MessageChannel.new(socket)
		headers_data = channel.read_scalar
		if headers_data.nil?
			socket.close
			return
		end
		
		headers = Hash[*headers_data.split("\0")]
		headers[CONTENT_LENGTH] = headers[HTTP_CONTENT_LENGTH]
		
		# TODO:
		# Uploaded files are apparently put in /tmp, but not as temp files.
		# That should be fixed.
		
		cgi = CGIFixed.new(headers, socket, ResponseSender.new(socket))
		::Dispatcher.dispatch(cgi, ::ActionController::CgiRequest::DEFAULT_SESSION_OPTIONS,
			cgi.stdoutput)
		socket.close
	end
end

end # module ModRails
