require 'test/unit'
require 'socket'
require 'mod_rails/message_channel'

class MessageChannelTest < Test::Unit::TestCase
	def test_simple_read_write
		with_pipe_channel do
			@writer.write("hello")
			assert_equal(["hello"], @reader.read)
		end
	end
	
	def test_concurrent_read_write
		concurrent_test(
			:parent => proc do
				@channel.write("hello")
				assert_equal(["hello!"], @channel.read)
			end,
			:child => proc do
				x = @channel.read
				@channel.write("#{x}!")
			end
		)
	end
	
	def test_io_passing
		concurrent_test(
			:parent => proc do
				reader, writer = IO.pipe
				@channel.send_io(writer)
				writer.close
				assert_equal("it works", reader.read)
				reader.close
			end,
			:child => proc do
				writer = @channel.recv_io
				writer.write("it works")
				writer.close
			end
		)
	end
	
	def test_large_amount_of_data
		concurrent_test(
			:parent => proc do
				1000.times do
					assert_equal(["123"], @channel.read)
					io = @channel.recv_io
					io.close
					@channel.write(456)
					assert_equal(["456", "!"], @channel.read)
				end
			end,
			:child => proc do
				1000.times do |i|
					@channel.write(123)
					@channel.send_io(STDIN)
					x = @channel.read
					@channel.write(x, "!")
				end
			end
		)
	end

private
	def with_pipe_channel
		@reader_pipe, @writer_pipe = IO.pipe
		@reader = ModRails::MessageChannel.new(@reader_pipe)
		@writer = ModRails::MessageChannel.new(@writer_pipe)
		begin
			yield
		ensure
			@reader_pipe.close
			@writer_pipe.close
		end
	end
	
	def concurrent_test(args = {})
		@parent_socket, @child_socket = UNIXSocket.pair
		pid = fork do
			@parent_socket.close
			@channel = ModRails::MessageChannel.new(@child_socket)
			begin
				args[:child].call
			ensure
				@child_socket.close
			end
		end
		@child_socket.close
		@channel = ModRails::MessageChannel.new(@parent_socket)
		begin
			args[:parent].call
		ensure
			@parent_socket.close
		end
	end
end
