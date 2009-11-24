require 'support/config'
require 'support/test_helper'
require 'phusion_passenger/abstract_request_handler'

require 'fileutils'

include PhusionPassenger

describe AbstractRequestHandler do
	before :each do
		@old_passenger_tmpdir = Utils.passenger_tmpdir
		Utils.passenger_tmpdir = "abstract_request_handler_spec.tmp"
		@owner_pipe = IO.pipe
		@request_handler = AbstractRequestHandler.new(@owner_pipe[1])
		def @request_handler.process_request(*args)
			# Do nothing.
		end
	end
	
	after :each do
		@request_handler.cleanup
		@owner_pipe[0].close rescue nil
		Utils.passenger_tmpdir = @old_passenger_tmpdir
		FileUtils.chmod_R(0777, "abstract_request_handler_spec.tmp")
		FileUtils.rm_rf("abstract_request_handler_spec.tmp")
	end
	
	def connect
		if @request_handler.server_sockets[:main][1] == "unix"
			return UNIXSocket.new(@request_handler.server_sockets[:main][0])
		else
			addr, port = @request_handler.server_sockets[:main][0].split(/:/)
			return TCPSocket.new(addr, port.to_i)
		end
	end
	
	it "exits if the owner pipe is closed" do
		@request_handler.start_main_loop_thread
		@owner_pipe[0].close
		begin
			Timeout.timeout(5) do
				wait_until do
					!@request_handler.main_loop_running?
				end
			end
		rescue Timeout::Error
			violated
		end
	end
	
	it "ignores new connections that don't send any data" do
		def @request_handler.accept_connection
			return nil
		end
		@request_handler.start_main_loop_thread
		wait_until do
			@request_handler.iterations != 0
		end
		@request_handler.processed_requests.should == 0
	end
	
	it "creates a socket file in the Phusion Passenger temp folder, unless when using TCP sockets" do
		if @request_handler.server_sockets[:main][1] == "unix"
			File.chmod(0700, "abstract_request_handler_spec.tmp/backends")
			Dir["abstract_request_handler_spec.tmp/backends/*"].should_not be_empty
		end
	end
	
	it "accepts pings on the main server socket" do
		@request_handler.start_main_loop_thread
		client = connect
		begin
			channel = MessageChannel.new(client)
			channel.write_scalar("REQUEST_METHOD\0PING\0")
			client.read.should == "pong"
		ensure
			client.close
		end
	end
	
	it "accepts pings on the HTTP server socket" do
		@request_handler.start_main_loop_thread
		addr, port = @request_handler.server_sockets[:http][0].split(/:/)
		client = TCPSocket.new(addr, port.to_i)
		begin
			client.write("PING / HTTP/1.1\r\n")
			client.write("Host: foo.com\r\n\r\n")
			client.close_write
			client.read.should == "pong"
		ensure
			client.close
		end
	end
	
	describe "HTTP parsing" do
		before :each do
			@request_handler.start_main_loop_thread
			addr, port = @request_handler.server_sockets[:http][0].split(/:/)
			port = port.to_i
			@client = TCPSocket.new(addr, port)
		end
		
		after :each do
			@client.close
		end
		
		it "correctly parses HTTP requests without query string" do
			@request_handler.should_receive(:process_request).and_return do |headers, input, output, status_line_desired|
				headers["REQUEST_METHOD"].should == "POST"
				headers["SERVER_PROTOCOL"].should == "HTTP/1.1"
				headers["HTTP_HOST"].should == "foo.com"
				headers["HTTP_X_FOO_BAR"].should == "baz"
				headers["PATH_INFO"].should == "/foo/bar"
				headers["SCRIPT_NAME"].should == ""
				headers["QUERY_STRING"].should == ""
				headers["REQUEST_URI"].should == "/foo/bar"
				headers["HTTP_CONTENT_LENGTH"].should be_nil
				headers["HTTP_CONTENT_TYPE"].should be_nil
				headers["CONTENT_LENGTH"].should == "10"
				headers["CONTENT_TYPE"].should == "text/plain"
			end
			
			@client.write("POST /foo/bar HTTP/1.1\r\n")
			@client.write("Host: foo.com\r\n")
			@client.write("X-Foo-Bar: baz\r\n")
			@client.write("Content-Length: 10\r\n")
			@client.write("Content-Type: text/plain\r\n")
			@client.write("\r\n")
			@client.close_write
			@client.read
		end
		
		it "correctly parses HTTP requests with query string" do
			@request_handler.should_receive(:process_request).and_return do |headers, input, output, status_line_desired|
				headers["REQUEST_METHOD"].should == "POST"
				headers["SERVER_PROTOCOL"].should == "HTTP/1.1"
				headers["HTTP_HOST"].should == "foo.com"
				headers["HTTP_X_FOO_BAR"].should == "baz"
				headers["PATH_INFO"].should == "/foo/bar"
				headers["SCRIPT_NAME"].should == ""
				headers["QUERY_STRING"].should == "hello=world&a=b+c"
				headers["REQUEST_URI"].should == "/foo/bar?hello=world&a=b+c"
				headers["HTTP_CONTENT_LENGTH"].should be_nil
				headers["HTTP_CONTENT_TYPE"].should be_nil
				headers["CONTENT_LENGTH"].should == "10"
				headers["CONTENT_TYPE"].should == "text/plain"
			end
			
			@client.write("POST /foo/bar?hello=world&a=b+c HTTP/1.1\r\n")
			@client.write("Host: foo.com\r\n")
			@client.write("X-Foo-Bar: baz\r\n")
			@client.write("Content-Length: 10\r\n")
			@client.write("Content-Type: text/plain\r\n")
			@client.write("\r\n")
			@client.close_write
			@client.read
		end
	end
	
	specify "upon receiving a soft shutdown signal, the main loop quits a while after the last connection was accepted" do
		@request_handler.soft_termination_linger_time = 0.2
		@request_handler.start_main_loop_thread
		@request_handler.soft_shutdown
		
		# Each time we ping the server it should reset the exit time.
		deadline = Time.now + 0.6
		while Time.now < deadline
			@request_handler.should be_main_loop_running
			client = connect
			begin
				channel = MessageChannel.new(client)
				channel.write_scalar("REQUEST_METHOD\0PING\0")
				client.read
				sleep 0.02
			ensure
				client.close
			end
		end
		@request_handler.should be_main_loop_running
		
		# After we're done pinging it should quit within a short period of time.
		deadline = Time.now + 0.6
		while Time.now < deadline && @request_handler.main_loop_running?
			sleep 0.01
		end
		@request_handler.should_not be_main_loop_running
	end
	
	############################
	
	def wait_until
		while !yield
			sleep 0.01
		end
	end
end
