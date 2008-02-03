require 'mod_rails/message_channel'
require 'mod_rails/cgi_fixed'

module ModRails # :nodoc:

class RequestHandler
	SERVER_TERMINATION_SIGNAL = "SIGTERM"

	def initialize(listen_socket)
		@listen_socket = listen_socket
		@listen_channel = MessageChannel.new(listen_socket)
		@previous_signal_handlers = {}
	end
	
	def main_loop
		reset_signal_handlers
		begin
			done = false
			while !done
				done = accept_next_request
			end
		rescue EOFError
			# Exit loop.
		rescue SignalException => signal
			if signal.message != SERVER_TERMINATION_SIGNAL
				raise
			end
		ensure
			revert_signal_handlers
		end
	end
	
	def accept_next_request
		if @listen_socket.read(1).nil?
			return true
		end

		reader1, writer1 = IO.pipe
		reader2, writer2 = IO.pipe
		@listen_channel.send_io(reader1)
		@listen_channel.send_io(writer2)
		reader1.close
		writer2.close
		process_request(reader2, writer1)
		return false
	end

private
	def reset_signal_handlers
		Signal.list.each_key do |signal|
			begin
				prev_handler = trap(signal, 'DEFAULT')
				if prev_handler != 'DEFAULT'
					@previous_signal_handlers[signal] = prev_handler
				end
			rescue ArgumentError
				# Signal cannot be trapped; ignore it.
			end
		end
		prev_handler = trap('HUP', 'IGNORE')
	end
	
	def revert_signal_handlers
		@previous_signal_handlers.each_pair do |signal, handler|
			trap(signal, handler)
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
	
	def process_request(reader, writer)
		headers_list = reader.read.split("\0")
		reader.close
		
		headers = {}
		i = 0
		while i < headers_list.size
			headers[headers_list[i]] = headers_list[i + 1]
			i += 2
		end
		
		cgi = CGIFixed.new(headers, StringIO.new(""), STDERR)
		::Dispatcher.dispatch(cgi, ::ActionController::CgiRequest::DEFAULT_SESSION_OPTIONS,
			ResponseSender.new(writer))
		writer.close
	end
	
	if false
		def process_request(reader, writer)
			headers = reader.read.split("\0")
			content = "hello <b>world</b>!<br>\n"
			content << "env = #{RAILS_ENV}<br>\n"
			content << "pid = #{$$}<br>\n"
			content << "rand = #{rand}<br>\n"
			i = 0
			while i < headers.size
				content << "<tt>"
				content << headers[i]
				content << "="
				content << headers[i + 1]
				content << "</tt><br>\n"
				i += 2
			end
			reader.close
			
			writer.write(
				"Status: 200 OK\r\n" <<
				"Content-Type: text/html\r\n" <<
				"X-Foo: bar\r\n" <<
				"Content-Length: #{content.size}\r\n" <<
				"\r\n")
			writer.write(content)
			writer.close
		end
	end
end

end # module ModRails