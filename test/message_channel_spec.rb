$LOAD_PATH << "#{File.dirname(__FILE__)}/../lib"
require 'socket'
require 'mod_rails/message_channel'
include ModRails

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
		
		it "should be able to read a single written array message" do
			@writer.write("hello")
			@reader.read.should == ["hello"]
		end
		
		it "should be able to handle array messages that contain spaces" do
			@writer.write("hello world", "! ")
			@reader.read.should == ["hello world", "! "]
		end
		
		it "should be able to handle array messages that have only a single empty string" do
			@writer.write("")
			@reader.read.should == [""]
		end
		
		it "should be able to handle array messages with empty arguments" do
			@writer.write("hello", "", "world")
			@reader.read.should == ["hello", "", "world"]
			
			@writer.write("")
			@reader.read.should == [""]
			
			@writer.write(nil, "foo")
			@reader.read.should == ["", "foo"]
		end
		
		it "should properly detect end-of-file when reading an array message" do
			@writer.close
			@reader.read.should be_nil
		end
		
		it "should be able to read a single written scalar message" do
			@writer.write_scalar("hello world")
			@reader.read_scalar.should == "hello world"
		end
		
		it "should be able to handle empty scalar messages" do
			@writer.write_scalar("")
			@reader.read_scalar.should == ""
		end
		
		it "should properly detect end-of-file when reading a scalar message" do
			@writer.close
			@reader.read_scalar.should be_nil
		end
	end
	
	describe "scenarios with 2 channels and 2 concurrent processes" do
		after :each do
			@parent_socket.close
			Process.waitpid(@pid)
		end
		
		it "both processes should be able to read and write a single array message" do
			spawn_process do
				x = @channel.read
				@channel.write("#{x}!")
			end
			@channel.write("hello")
			@channel.read.should == ["hello!"]
		end
		
		it "should be able to handle scalar messages with arbitrary binary data" do
			garbage_files = ["garbage1.dat", "garbage2.dat", "garbage3.dat"]
			spawn_process do
				garbage_files.each do |name|
					data = File.read("stub/#{name}")
					@channel.write_scalar(data)
				end
			end
			
			garbage_files.each do |name|
				data = File.read("stub/#{name}")
				@channel.read_scalar.should == data
			end
		end
		
		it "should support IO object (file descriptor) passing" do
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
		
		it "should support large amounts of data" do
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
		
		it "should have stream properties" do
			violated "test not implemented yet"
		end
		
		def spawn_process
			@parent_socket, @child_socket = UNIXSocket.pair
			@pid = fork do
				@parent_socket.close
				@channel = MessageChannel.new(@child_socket)
				begin
					yield
				rescue Exception => e
					STDERR.puts("*** Exception in child process: #{e}")
					STDERR.flush
				ensure
					@child_socket.close
					exit!
				end
			end
			@child_socket.close
			@channel = MessageChannel.new(@parent_socket)
		end
	end
end

