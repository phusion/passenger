# NOTE: we make use of pipes instead of Unix sockets, because
# experimentation has shown that pipes are slightly faster.
module ModRails # :nodoc:

class RequestHandler
	def initialize(reader_pipe, writer_pipe)
		@reader = reader_pipe
		@writer = writer_pipe
		@previous_signal_handlers = {}
	end
	
	def main_loop
		reset_signal_handlers
		begin
			done = false
			while !done
				begin
					puts "#{$$} received: " << @reader.readline
				rescue EOFError
					done = true
				end
			end
		ensure
			revert_signal_handlers
		end
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
		if prev_handler != 'IGNORE'
			@previous_signal_handlers['HUP'] = prev_handler
		end
	end
	
	def revert_signal_handlers
		@previous_signal_handlers.each_pair do |signal, handler|
			trap(signal, handler)
		end
	end
end

end # module ModRails