require 'mod_rails/message_channel'
# NOTE: we make use of pipes instead of Unix sockets, because
# experimentation has shown that pipes are slightly faster.
module ModRails # :nodoc:

class RequestHandler
	class Messages
		def self.read(io)
			buffer = ''
			while buffer.size < 2
				buffer << io.readpartial(2 - buffer.size)
			end
			chunk_size = buffer.unpack('n')[0]
			if chunk_size == 0
				return nil
			else
				buffer = ''
				while buffer.size < chunk_size
					buffer << io.readpartial(chunk_size - buffer.size)
				end
				return buffer
			end
		end
		
		def self.write(io, data)
			io.write([data.size].pack('n') << data)
			io.flush
		end
	end

	def initialize(reader, writer)
		@reader = reader
		@writer = writer
		@reader_channel = MessageChannel.new(reader)
		@previous_signal_handlers = {}
	end
	
	def main_loop
		reset_signal_handlers
		begin
			done = false
			while !done
				begin
					process_next_request
				rescue EOFError
					done = true
				end
			end
		ensure
			revert_signal_handlers
		end
	end
	
	def process_next_request
		content = "hello <b>world</b>!<br>\n"

		STDERR.puts "--- #{$$} BEGIN"
		done = false
		while !done
			header, value = @reader_channel.read
			if !header.empty?
				content << "<tt>#{header} = #{value}</tt><br>\n"
			end
			done = header.empty?
		end
		
		write_chunk("Status: 200 OK\r\n")
		write_chunk("Content-Type: text/html\r\n")
		write_chunk("Content-Length: #{content.size}\r\n")
		write_chunk("\r\n")
		write_chunk(content)
		write_chunk("")
		STDERR.puts "--- #{$$} END"
		STDERR.flush
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
	
	def read_chunk
		return Messages.read(@reader)
	end
	
	def write_chunk(data)
		Messages.write(@writer, data)
	end
end

end # module ModRails