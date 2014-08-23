require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
PhusionPassenger.require_passenger_lib 'request_handler'
PhusionPassenger.require_passenger_lib 'request_handler/thread_handler'
PhusionPassenger.require_passenger_lib 'rack/thread_handler_extension'
PhusionPassenger.require_passenger_lib 'union_station/core'
PhusionPassenger.require_passenger_lib 'utils'

require 'fileutils'

module PhusionPassenger

describe RequestHandler do
	class DummyThreadHandler < RequestHandler::ThreadHandler
		def process_request(*args)
			# Do nothing.
		end
	end

	before :each do
		preinitialize if respond_to?(:preinitialize)
		@old_passenger_tmpdir = Utils.passenger_tmpdir
		Utils.passenger_tmpdir = "request_handler_spec.tmp"
		Utils.passenger_tmpdir
		@owner_pipe = IO.pipe
		@options ||= {}
		@thread_handler = Class.new(DummyThreadHandler)
		@options = {
			"app_group_name" => "foobar",
			"thread_handler" => @thread_handler,
			"generation_dir" => "request_handler_spec.tmp"
		}.merge(@options)
		@request_handler = RequestHandler.new(@owner_pipe[1], @options)
	end

	after :each do
		stop_request_handler
		Utils.passenger_tmpdir = @old_passenger_tmpdir
		FileUtils.chmod_R(0777, "request_handler_spec.tmp")
		FileUtils.rm_rf("request_handler_spec.tmp")
	end

	def stop_request_handler
		if @request_handler
			@request_handler.cleanup
			@owner_pipe[0].close rescue nil
			@request_handler = nil
		end
	end

	def connect(socket_name = :main)
		address = @request_handler.server_sockets[socket_name][:address]
		return Utils.connect_to_server(address)
	end

	def send_binary_request(socket, env)
		channel = MessageChannel.new(socket)
		data = ""
		env.each_pair do |key, value|
			data << key << "\0"
			data << value << "\0"
		end
		channel.write_scalar(data)
	end

	it "exits if the owner pipe is closed" do
		@request_handler.start_main_loop_thread
		@owner_pipe[0].close
		eventually do
			!@request_handler.main_loop_running?
		end
	end

	it "creates a socket file in the Phusion Passenger temp folder, unless when using TCP sockets" do
		if @request_handler.server_sockets[:main][1] == "unix"
			File.chmod(0700, "request_handler_spec.tmp/backends")
			Dir["request_handler_spec.tmp/backends/*"].should_not be_empty
		end
	end

	specify "the main socket rejects headers that are too large" do
		stderr = StringIO.new
		DebugLogging.log_level = 0
		DebugLogging.stderr_evaluator = lambda { stderr }
		@request_handler.start_main_loop_thread
		begin
			client = connect
			client.sync = true
			block = lambda do
				data = "REQUEST_METHOD\0/"
				data << "x" * (RequestHandler::ThreadHandler::MAX_HEADER_SIZE * 2)
				data << "\0"
				MessageChannel.new(client).write_scalar(data)
			end
			block.should raise_error(Errno::EPIPE)
			stderr.string.should_not be_empty
		ensure
			client.close rescue nil
		end
	end

	specify "the main socket rejects unauthenticated connections, if a connect password is supplied" do
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
		client = connect(:http)
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
		stderr = StringIO.new
		DebugLogging.log_level = 0
		DebugLogging.stderr_evaluator = lambda { stderr }
		@request_handler.start_main_loop_thread
		begin
			client = connect(:http)
			client.sync = true
			block = lambda do
				client.write("GET /")
				client.write("x" * RequestHandler::ThreadHandler::MAX_HEADER_SIZE)
				sleep 0.01 # Context switch
				client.write("x" * RequestHandler::ThreadHandler::MAX_HEADER_SIZE)
				client.write(" HTTP/1.1\r\n")
			end
			block.should raise_error(SystemCallError)
			stderr.string.should_not be_empty
		ensure
			client.close rescue nil
		end
	end

	specify "the HTTP socket rejects unauthenticated connections, if a connect password is supplied" do
		DebugLogging.log_level = -1
		@request_handler.connect_password = "1234"
		@request_handler.start_main_loop_thread
		begin
			client = connect(:http)
			client.write("PING / HTTP/1.1\r\n")
			client.write("\r\n")
			client.read.should == ""
		ensure
			client.close rescue nil
		end
		begin
			client = connect(:http)
			client.write("PING / HTTP/1.1\r\n")
			client.write("X-Passenger-Connect-Password: 1234\r\n")
			client.write("\r\n")
			client.read.should == "pong"
		ensure
			client.close rescue nil
		end
	end

	it "catches exceptions generated by the Rack application object" do
		@options["thread_handler"] = Class.new(RequestHandler::ThreadHandler) do
			include Rack::ThreadHandlerExtension
		end

		lambda_called = false

		# Here we test that the exception is not propagated to outside the request handler.
		DebugLogging.log_level = -2
		@options["app"] = lambda do |env|
			lambda_called = true
			raise "an error"
		end

		@request_handler = RequestHandler.new(@owner_pipe[1], @options)
		@request_handler.start_main_loop_thread
		client = connect
		begin
			send_binary_request(client,
				"REQUEST_METHOD" => "GET",
				"PATH_INFO" => "/")
			client.read
		ensure
			client.close
		end

		lambda_called.should == true
	end

	it "catches exceptions generated by the Rack body object" do
		@options["thread_handler"] = Class.new(RequestHandler::ThreadHandler) do
			include Rack::ThreadHandlerExtension
		end

		lambda_called = false

		# Here we test that the exception is not propagated to outside the request handler.
		DebugLogging.log_level = -2
		@options["app"] = lambda do |env|
			lambda_called = true
			body = Object.new
			def body.each
				raise "an error"
			end
			[200, { "Content-Type" => "text/plain" }, body]
		end

		@request_handler = RequestHandler.new(@owner_pipe[1], @options)
		@request_handler.start_main_loop_thread
		client = connect
		begin
			send_binary_request(client,
				"REQUEST_METHOD" => "GET",
				"PATH_INFO" => "/")
			client.read
		ensure
			client.close
		end

		lambda_called.should == true
	end

	it "allows the application to take over the socket completely through the full hijack API" do
		@options["thread_handler"] = Class.new(RequestHandler::ThreadHandler) do
			include Rack::ThreadHandlerExtension
		end

		lambda_called = false

		@options["app"] = lambda do |env|
			lambda_called = true
			env['rack.hijack?'].should be_true
			env['rack.hijack_io'].should be_nil
			env['rack.hijack'].call
			Thread.new do
				Thread.current.abort_on_exception = true
				sleep 0.1
				env['rack.hijack_io'].write("Hijacked response!")
				env['rack.hijack_io'].close
			end
		end

		@request_handler = RequestHandler.new(@owner_pipe[1], @options)
		@request_handler.start_main_loop_thread
		client = connect
		begin
			send_binary_request(client,
				"REQUEST_METHOD" => "GET",
				"PATH_INFO" => "/")
			sleep 0.1 # Give it some time to handle the request.
			stop_request_handler
			client.read.should == "Hijacked response!"
		ensure
			client.close
		end

		lambda_called.should == true
	end

	it "allows the application to take over the socket after sending headers through the partial hijack API" do
		@options["thread_handler"] = Class.new(RequestHandler::ThreadHandler) do
			include Rack::ThreadHandlerExtension
		end

		lambda_called = false
		hijack_callback_called = false

		@options["app"] = lambda do |env|
			lambda_called = true
			env['rack.hijack?'].should be_true
			env['rack.hijack_io'].should be_nil
			hijack_callback = lambda do |socket|
				hijack_callback_called = true
				env['rack.hijack_io'].should_not be_nil
				env['rack.hijack_io'].should == socket
				socket.write("Hijacked partial response!")
				socket.close
			end
			[200, { 'Content-Type' => 'text/html', 'rack.hijack' => hijack_callback }]
		end

		@request_handler = RequestHandler.new(@owner_pipe[1], @options)
		@request_handler.start_main_loop_thread
		client = connect
		begin
			send_binary_request(client,
				"REQUEST_METHOD" => "GET",
				"PATH_INFO" => "/")
			client.read.should ==
				"Status: 200\r\n" +
				"Content-Type: text/html\r\n" +
				"\r\n" +
				"Hijacked partial response!"
		ensure
			client.close
		end

		lambda_called.should == true
		hijack_callback_called.should == true
	end

	specify "requests with Content-Length are assumed to have a request body" do
		@options["thread_handler"] = Class.new(RequestHandler::ThreadHandler) do
			include Rack::ThreadHandlerExtension
		end

		lambda_called = false

		@options["app"] = lambda do |env|
			lambda_called = true
			env['rack.input'].read(3).should == "abc"
			[200, {}, ["ok"]]
		end

		@request_handler = RequestHandler.new(@owner_pipe[1], @options)
		@request_handler.start_main_loop_thread
		client = connect
		begin
			send_binary_request(client,
				"REQUEST_METHOD" => "GET",
				"PATH_INFO" => "/",
				"CONTENT_LENGTH" => "3")
			client.write("abc")
			client.close_write
			client.read.should ==
				"Status: 200\r\n" +
				"\r\n" +
				"ok"
		ensure
			client.close
		end

		lambda_called.should be_true
	end

	specify "requests with Transfer-Encoding chunked are assumed to have a request body" do
		@options["thread_handler"] = Class.new(RequestHandler::ThreadHandler) do
			include Rack::ThreadHandlerExtension
		end

		lambda_called = false

		@options["app"] = lambda do |env|
			lambda_called = true
			env['rack.input'].read(13).should ==
				"3\r\n" +
				"abc\r\n" +
				"0\r\n\r\n"
			[200, {}, ["ok"]]
		end

		@request_handler = RequestHandler.new(@owner_pipe[1], @options)
		@request_handler.start_main_loop_thread
		client = connect
		begin
			send_binary_request(client,
				"REQUEST_METHOD" => "GET",
				"PATH_INFO" => "/",
				"HTTP_TRANSFER_ENCODING" => "chunked")
			client.write(
				"3\r\n" +
				"abc\r\n" +
				"0\r\n\r\n")
			client.close_write
			client.read.should ==
				"Status: 200\r\n" +
				"\r\n" +
				"ok"
		ensure
			client.close
		end

		lambda_called.should be_true
	end

	specify "requests with neither Content-Length nor Transfer-Encoding are assumed to have no request body" do
		@options["thread_handler"] = Class.new(RequestHandler::ThreadHandler) do
			include Rack::ThreadHandlerExtension
		end

		lambda_called = false

		@options["app"] = lambda do |env|
			lambda_called = true
			env['rack.input'].read(1).should be_nil
			env['rack.input'].gets.should be_nil
			[200, {}, ["ok"]]
		end

		@request_handler = RequestHandler.new(@owner_pipe[1], @options)
		@request_handler.start_main_loop_thread
		client = connect
		begin
			send_binary_request(client,
				"REQUEST_METHOD" => "POST",
				"PATH_INFO" => "/")
			client.close_write
			client.read.should ==
				"Status: 200\r\n" +
				"\r\n" +
				"ok"
		ensure
			client.close
		end

		lambda_called.should be_true
	end

	describe "on requests that are not supposed to have a body" do
		before :each do
			@options["thread_handler"] = Class.new(RequestHandler::ThreadHandler) do
				include Rack::ThreadHandlerExtension
			end
		end

		it "allows reading from the client socket once the socket has been fully hijacked" do
			lambda_called = false

			@options["app"] = lambda do |env|
				lambda_called = true
				env['rack.hijack'].call
				io = env['rack.hijack_io']
				begin
					io.read.should == "hi"
					io.write("ok")
				ensure
					io.close
				end
			end

			@request_handler = RequestHandler.new(@owner_pipe[1], @options)
			@request_handler.start_main_loop_thread
			client = connect
			begin
				send_binary_request(client,
					"REQUEST_METHOD" => "GET",
					"PATH_INFO" => "/")
				client.write("hi")
				client.close_write
				client.read.should == "ok"
			ensure
				client.close
			end

			lambda_called.should be_true
		end

		it "allows reading from the client socket once the socket has been partially hijacked" do
			lambda_called = false

			@options["app"] = lambda do |env|
				block = lambda do |io|
					lambda_called = true
					begin
						io.read.should == "hi"
						io.write("ok")
					ensure
						io.close
					end
				end
				headers = { 'rack.hijack' => block }
				[200, headers, []]
			end

			@request_handler = RequestHandler.new(@owner_pipe[1], @options)
			@request_handler.start_main_loop_thread
			client = connect
			begin
				send_binary_request(client,
					"REQUEST_METHOD" => "GET",
					"PATH_INFO" => "/")
				client.write("hi")
				client.close_write
				client.read.should ==
					"Status: 200\r\n" +
					"\r\n" +
					"ok"
			ensure
				client.close
			end

			lambda_called.should be_true
		end
	end

	describe "if Union Station core is given" do
		def preinitialize
			if @agent_pid
				Process.kill('KILL', @agent_pid)
				Process.waitpid(@agent_pid)
			end
			@dump_file = "#{Utils.passenger_tmpdir}/log.txt"
			@logging_agent_password = "1234"
			@agent_pid, @socket_filename, @socket_address = spawn_logging_agent(@dump_file,
				@logging_agent_password)

			@union_station_core = UnionStation::Core.new(@socket_address, "logging",
				"1234", "localhost")
			@options = { "union_station_core" => @union_station_core }
		end

		after :each do
			if @agent_pid
				Process.kill('KILL', @agent_pid)
				Process.waitpid(@agent_pid)
			end
		end

		def base64(data)
			return [data].pack('m').gsub("\n", "")
		end

		it "makes the analytics log object available through the request env and a thread-local variable" do
			header_value = nil
			thread_value = nil
			@thread_handler.any_instance.should_receive(:process_request).and_return do |headers, connection, full_http_response|
				header_value = headers[UNION_STATION_REQUEST_TRANSACTION]
				thread_value = Thread.current[UNION_STATION_REQUEST_TRANSACTION]
			end
			@request_handler.start_main_loop_thread
			client = connect
			begin
				send_binary_request(client,
					"REQUEST_METHOD" => "GET",
					"PASSENGER_TXN_ID" => "1234-abcd",
					"PASSENGER_GROUP_NAME" => "foobar")
				client.read
			ensure
				client.close
			end
			header_value.should be_kind_of(UnionStation::Transaction)
			thread_value.should be_kind_of(UnionStation::Transaction)
			header_value.should == thread_value
		end

		it "logs uncaught exceptions for requests that have a transaction ID" do
			reraised = false
			@thread_handler.any_instance.should_receive(:process_request).and_return do |headers, connection, full_http_response|
				raise "something went wrong"
			end
			@thread_handler.any_instance.stub(:should_reraise_error?).and_return do |e|
				reraised = true
				e.message != "something went wrong"
			end
			@request_handler.start_main_loop_thread
			client = connect
			begin
				DebugLogging.log_level = -2
				send_binary_request(client,
					"REQUEST_METHOD" => "GET",
					"PASSENGER_TXN_ID" => "1234-abcd")
			ensure
				client.close
			end
			eventually(5) do
				flush_logging_agent(@logging_agent_password, @socket_address)
				if File.exist?(@dump_file)
					log_data = File.read(@dump_file)
				else
					log_data = ""
				end
				log_data.include?("Request transaction ID: 1234-abcd\n") &&
					log_data.include?("Message: " + base64("something went wrong")) &&
					log_data.include?("Class: RuntimeError") &&
					log_data.include?("Backtrace: ")
			end
			reraised.should be_true
		end
	end

	describe "HTTP parsing" do
		before :each do
			@request_handler.start_main_loop_thread
			@client = connect(:http)
			@client.sync = true
		end

		after :each do
			@client.close if @client
		end

		it "correctly parses HTTP requests without query string" do
			@thread_handler.any_instance.should_receive(:process_request).and_return do |headers, connection, full_http_response|
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
			@thread_handler.any_instance.should_receive(:process_request).and_return do |headers, connection, full_http_response|
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
			@thread_handler.any_instance.should_receive(:process_request).and_return do |headers, connection, full_http_response|
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

	############################
end

end # module PhusionPassenger
