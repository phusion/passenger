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
	
	def connect_http
		addr, port = @request_handler.server_sockets[:http][0].split(/:/)
		port = port.to_i
		return TCPSocket.new(addr, port)
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
	
	specify "the main socket rejects headers that are too large" do
		@request_handler.stderr = StringIO.new
		@request_handler.start_main_loop_thread
		begin
			client = connect
			client.sync = true
			block = lambda do
				data = "REQUEST_METHOD\0/"
				data << "x" * (AbstractRequestHandler::MAX_HEADER_SIZE * 2)
				data << "\0"
				MessageChannel.new(client).write_scalar(data)
			end
			block.should raise_error(Errno::EPIPE)
			@request_handler.stderr.string.should_not be_empty
		ensure
			client.close rescue nil
		end
	end
	
	specify "the main socket rejects unauthenticated connections, if a connect password is supplied" do
		@request_handler.stderr = StringIO.new
		@request_handler.connect_password = "1234"
		@request_handler.start_main_loop_thread
		begin
			client = connect
			channel = MessageChannel.new(client)
			channel.write_scalar("REQUEST_METHOD\0PING\0")
			client.read.should == ""
		ensure
			client.close rescue nil
		end
		begin
			client = connect
			channel = MessageChannel.new(client)
			channel.write_scalar("REQUEST_METHOD\0PING\0PASSENGER_CONNECT_PASSWORD\0001234\0")
			client.read.should == "pong"
		ensure
			client.close rescue nil
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
	
	specify "the HTTP socket rejects headers that are too large" do
		@request_handler.stderr = StringIO.new
		@request_handler.start_main_loop_thread
		begin
			client = connect_http
			client.sync = true
			block = lambda do
				client.write("GET /")
				client.write("x" * AbstractRequestHandler::MAX_HEADER_SIZE)
				sleep 0.01 # Context switch
				client.write("x" * AbstractRequestHandler::MAX_HEADER_SIZE)
				client.write(" HTTP/1.1\r\n")
			end
			block.should raise_error(SystemCallError)
			@request_handler.stderr.string.should_not be_empty
		ensure
			client.close rescue nil
		end
	end
	
	specify "the HTTP socket rejects unauthenticated connections, if a connect password is supplied" do
		@request_handler.stderr = StringIO.new
		@request_handler.connect_password = "1234"
		@request_handler.start_main_loop_thread
		begin
			client = connect_http
			client.write("PING / HTTP/1.1\r\n")
			client.write("\r\n")
			client.read.should == ""
		ensure
			client.close rescue nil
		end
		begin
			client = connect_http
			client.write("PING / HTTP/1.1\r\n")
			client.write("X-Passenger-Connect-Password: 1234\r\n")
			client.write("\r\n")
			client.read.should == "pong"
		ensure
			client.close rescue nil
		end
	end
	
	describe "HTTP parsing" do
		before :each do
			@request_handler.start_main_loop_thread
			addr, port = @request_handler.server_sockets[:http][0].split(/:/)
			port = port.to_i
			@client = TCPSocket.new(addr, port)
			@client.sync = true
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
		
		it "correct parses HTTP requests that come in arbitrary chunks" do
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
				headers["HTTP_PLUS_SOME"].should be_nil
			end
			
			@client.write("POST /fo")
			sleep 0.001
			@client.write("o/bar?hello=world&a=b+c HT")
			sleep 0.001
			@client.write("TP/1.1\r")
			sleep 0.001
			@client.write("\nHost: foo.com")
			sleep 0.001
			@client.write("\r\n")
			sleep 0.001
			@client.write("X-Foo-Bar: baz\r\n")
			sleep 0.001
			@client.write("Content-Len")
			sleep 0.001
			@client.write("gth: 10\r\nContent-Type: text/pla")
			sleep 0.001
			@client.write("in\r\n\r")
			sleep 0.001
			@client.write("\nPlus-Some: garbage data that should be ignored.")
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
