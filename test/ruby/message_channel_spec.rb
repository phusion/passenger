require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'socket'
PhusionPassenger.require_passenger_lib 'message_channel'

module PhusionPassenger

describe MessageChannel do
	describe "scenarios with a single channel" do
		before :each do
			@reader_pipe, @writer_pipe = IO.pipe
			@reader = MessageChannel.new(@reader_pipe)
			@writer = MessageChannel.new(@writer_pipe)
		end
		
		after :each do
			@reader_pipe.close unless @reader_pipe.closed?
			@writer_pipe.close unless @writer_pipe.closed?
		end
		
		it "can read a single written array message" do
			@writer.write("hello")
			@reader.read.should == ["hello"]
		end
		
		it "can handle array messages that contain spaces" do
			@writer.write("hello world", "! ")
			@reader.read.should == ["hello world", "! "]
		end
		
		it "can handle array messages that have only a single empty string" do
			@writer.write("")
			@reader.read.should == [""]
		end
		
		it "can handle array messages with empty arguments" do
			@writer.write("hello", "", "world")
			@reader.read.should == ["hello", "", "world"]
			
			@writer.write("")
			@reader.read.should == [""]
			
			@writer.write(nil, "foo")
			@reader.read.should == ["", "foo"]
		end
		
		it "properly detects end-of-file when reading an array message" do
			@writer.close
			@reader.read.should be_nil
		end
		
		specify "#read_hash works" do
			@writer.write("hello", "world")
			@reader.read_hash.should == { "hello" => "world" }
			
			@writer.write("hello", "world", "foo", "bar", "", "...")
			@reader.read_hash.should == { "hello" => "world", "foo" => "bar", "" => "..." }
		end
		
		specify "#read_hash throws an exception if the array message doesn't have an even number of items" do
			@writer.write("foo")
			lambda { @reader.read_hash }.should raise_error(MessageChannel::InvalidHashError)
			
			@writer.write("foo", "bar", "baz")
			lambda { @reader.read_hash }.should raise_error(MessageChannel::InvalidHashError)
		end
		
		it "can read a single written scalar message" do
			@writer.write_scalar("hello world")
			@reader.read_scalar.should == "hello world"
		end
		
		it "can handle empty scalar messages" do
			@writer.write_scalar("")
			@reader.read_scalar.should == ""
		end
		
		it "properly detects end-of-file when reading a scalar message" do
			@writer.close
			@reader.read_scalar.should be_nil
		end
		
		it "puts the data into the given buffer" do
			buffer = ''
			@writer.write_scalar("x" * 100)
			result = @reader.read_scalar(buffer)
			result.object_id.should == buffer.object_id
			buffer.should == "x" * 100
		end
		
		it "raises SecurityError when a received scalar message's size is larger than a specified maximum" do
			@writer.write_scalar(" " * 100)
			lambda { @reader.read_scalar('', 99) }.should raise_error(SecurityError)
		end
	end
	
	describe "scenarios with 2 channels and 2 concurrent processes" do
		after :each do
			@parent_socket.close
			Process.waitpid(@pid) rescue nil
		end
		
		def spawn_process
			@parent_socket, @child_socket = UNIXSocket.pair
			@pid = fork do
				@parent_socket.close
				@channel = MessageChannel.new(@child_socket)
				begin
					yield
				rescue Exception => e
					print_exception("child", e)
				ensure
					@child_socket.close
					exit!
				end
			end
			@child_socket.close
			@channel = MessageChannel.new(@parent_socket)
		end
		
		it "both processes can read and write a single array message" do
			spawn_process do
				x = @channel.read
				@channel.write("#{x[0]}!")
			end
			@channel.write("hello")
			@channel.read.should == ["hello!"]
		end
		
		it "can handle scalar messages with arbitrary binary data" do
			garbage_files = ["garbage1.dat", "garbage2.dat", "garbage3.dat"]
			spawn_process do
				garbage_files.each do |name|
					data = File.binread("stub/#{name}")
					@channel.write_scalar(data)
				end
			end
			
			garbage_files.each do |name|
				data = File.binread("stub/#{name}")
				@channel.read_scalar.should == data
			end
		end
		
		it "supports IO object (file descriptor) passing" do
			spawn_process do
				writer = @channel.recv_io
				writer.write("it works")
				writer.close
			end
			reader, writer = IO.pipe
			@channel.send_io(writer)
			writer.close
			reader.read.should == "it works"
			reader.close
		end
		
		it "supports large amounts of data" do
			iterations = 1000
			blob = "123" * 1024
			spawn_process do
				iterations.times do |i|
					@channel.write(blob)
				end
			end
			iterations.times do
				@channel.read.should == [blob]
			end
		end
		
		it "has stream properties" do
			garbage = File.binread("stub/garbage1.dat")
			spawn_process do
				@channel.write("hello", "world")
				@channel.write_scalar(garbage)
				@channel.send_io(STDIN)
				@channel.write_scalar(":-)")
				
				a = @channel.read_scalar
				b = @channel.read
				b << a
				@channel.write(*b)
			end
			@channel.read.should == ["hello", "world"]
			@channel.read_scalar.should == garbage
			@channel.recv_io.close
			@channel.read_scalar.should == ":-)"
			
			@channel.write_scalar("TASTE MY WRATH! ULTIMATE SWORD TECHNIQUE!! DRAGON'S BREATH SL--")
			@channel.write("Uhm, watch your step.", "WAAHH?!", "Calm down, Motoko!!")
			@channel.read.should == ["Uhm, watch your step.", "WAAHH?!", "Calm down, Motoko!!",
				"TASTE MY WRATH! ULTIMATE SWORD TECHNIQUE!! DRAGON'S BREATH SL--"]
		end
	end
end

end # module PhusionPassenger
