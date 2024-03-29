require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
PhusionPassenger.require_passenger_lib 'request_handler'
PhusionPassenger.require_passenger_lib 'request_handler/thread_handler'
PhusionPassenger.require_passenger_lib 'rack/thread_handler_extension'
PhusionPassenger.require_passenger_lib 'constants'
PhusionPassenger.require_passenger_lib 'utils'

require 'fileutils'
require 'tmpdir'

module PhusionPassenger

describe RequestHandler do
  class DummyThreadHandler < RequestHandler::ThreadHandler
    def process_request(*args)
      # Do nothing.
    end
  end

  before :each do
    @temp_dir = Dir.mktmpdir
    preinitialize if respond_to?(:preinitialize)
    @owner_pipe = IO.pipe
    @options ||= {}
    @thread_handler = Class.new(DummyThreadHandler)
    @options = {
      "app_group_name" => "foobar",
      "thread_handler" => @thread_handler,
      "socket_dir"     => @temp_dir,
      "keepalive"      => false
    }.merge(@options)
    @request_handler = RequestHandler.new(@owner_pipe[1], @options)
  end

  after :each do
    stop_request_handler
    if @temp_dir
      FileUtils.chmod_R(0777, @temp_dir)
      FileUtils.rm_rf(@temp_dir)
    end
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
      File.chmod(0700, "#{@temp_dir}/backends")
      Dir["#{@temp_dir}/backends/*"].should_not be_empty
    end
  end

  specify "the main socket rejects headers that are too large" do
    stderr = StringIO.new
    DebugLogging.log_level = DEFAULT_LOG_LEVEL
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
    DebugLogging.log_level = DEFAULT_LOG_LEVEL
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
        sleep 0.01 # Context switch
        client.write(" HTTP/1.1\r\n")
      end
      block.should raise_error(SystemCallError)
      stderr.string.should_not be_empty
    ensure
      client.close rescue nil
    end
  end

  specify "the HTTP socket rejects unauthenticated connections, if a connect password is supplied" do
    DebugLogging.log_level = LVL_ERROR
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
      env['rack.hijack?'].should be_truthy
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
      env['rack.hijack?'].should be_truthy
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
        "HTTP/1.1 200 Whatever\r\n" +
        "Content-Type: text/html\r\n" +
        "Connection: close\r\n" +
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
        "HTTP/1.1 200 Whatever\r\n" +
        "Content-Length: 2\r\n" +
        "Connection: close\r\n" +
        "\r\n" +
        "ok"
    ensure
      client.close
    end

    lambda_called.should be_truthy
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
        "HTTP/1.1 200 Whatever\r\n" +
        "Content-Length: 2\r\n" +
        "Connection: close\r\n" +
        "\r\n" +
        "ok"
    ensure
      client.close
    end

    lambda_called.should be_truthy
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
        "HTTP/1.1 200 Whatever\r\n" +
        "Content-Length: 2\r\n" +
        "Connection: close\r\n" +
        "\r\n" +
        "ok"
    ensure
      client.close
    end

    lambda_called.should be_truthy
  end

  describe "on requests that are not supposed to have a body" do
    before :each do
      @options["thread_handler"] = Class.new(RequestHandler::ThreadHandler) do
        include Rack::ThreadHandlerExtension
      end
    end

    it "doesn't allow reading from rack.input" do
      lambda_called = false

      @options["app"] = lambda do |env|
        lambda_called = true
        body = env['rack.input'].read.inspect
        [200, { "Content-Type" => "text/plain" }, [body]]
      end

      @request_handler = RequestHandler.new(@owner_pipe[1], @options)
      @request_handler.start_main_loop_thread
      client = connect
      begin
        send_binary_request(client,
          "REQUEST_METHOD" => "GET",
          "PATH_INFO" => "/")
        client.read.should ==
          "HTTP/1.1 200 Whatever\r\n" +
          "Content-Type: text/plain\r\n" +
          "Content-Length: 2\r\n" +
          "Connection: close\r\n" +
          "\r\n" +
          "\"\""
      ensure
        client.close
      end

      lambda_called.should be_truthy
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

      lambda_called.should be_truthy
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
          "HTTP/1.1 200 Whatever\r\n" +
          "Connection: close\r\n" +
          "\r\n" +
          "ok"
      ensure
        client.close
      end

      lambda_called.should be_truthy
    end
  end

  describe "when processing Rack responses" do
    def setup(&app)
      @options["thread_handler"] = Class.new(RequestHandler::ThreadHandler) do
        include Rack::ThreadHandlerExtension
      end
      @options["app"] = app
      @options["keepalive"] = true

      @request_handler = RequestHandler.new(@owner_pipe[1], @options)
      @request_handler.start_main_loop_thread
      @client = connect
    end

    after :each do
      @client.close if @client
    end

    class NonArrayBody
      def initialize(array)
        @array = array
      end

      def each(&block)
        @array.each(&block)
      end
    end

    context "with Content-Length" do
      context "and the response status code allows a body" do
        context "and the request is HEAD" do
          it "disallows Transfer-Encoding" do
            setup do |env|
                [200, { "Content-Length" => "2", "Transfer-Encoding" => "chunked" },
                  ["ok"]]
              end
              send_binary_request(@client,
                "REQUEST_METHOD" => "HEAD",
                "PATH_INFO" => "/")
              @client.close_write
              @client.read.should == ""
          end

          it "outputs Content-Length" do
            setup do |env|
              [200, { "Content-Length" => "2" }, ["ok"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "HEAD",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should ==
              "HTTP/1.1 200 Whatever\r\n" \
              "Content-Length: 2\r\n\r\n"
          end

          context "and the body is an Array" do
            it "does not check whether the body size matches Content-Length" do
              setup do |env|
                [200, { "Content-Length" => "2" }, ["okok"]]
              end
              send_binary_request(@client,
                "REQUEST_METHOD" => "HEAD",
                "PATH_INFO" => "/")
              @client.close_write
              @client.read.should ==
                "HTTP/1.1 200 Whatever\r\n" \
                "Content-Length: 2\r\n\r\n"
            end

            it "allows keepalive" do
              setup do |env|
                [200, { "Content-Length" => "2" }, ["ok"]]
              end
              send_binary_request(@client,
                "REQUEST_METHOD" => "HEAD",
                "PATH_INFO" => "/")
              @client.close_write
              @client.read.should ==
                "HTTP/1.1 200 Whatever\r\n" \
                "Content-Length: 2\r\n\r\n"
            end
          end

          context "and the body is not an Array" do
            it "allows keep-alive" do
              setup do |env|
                [200, { "Content-Length" => "2" }, NonArrayBody.new(["ok"])]
              end
              send_binary_request(@client,
                "REQUEST_METHOD" => "HEAD",
                "PATH_INFO" => "/")
              @client.close_write
              @client.read.should ==
                "HTTP/1.1 200 Whatever\r\n" \
                "Content-Length: 2\r\n\r\n"
            end
          end
        end

        context "and the request is not HEAD" do
          it "disallows Transfer-Encoding" do
            setup do |env|
                [200, { "Content-Length" => "2", "Transfer-Encoding" => "chunked" },
                  ["ok"]]
              end
              send_binary_request(@client,
                "REQUEST_METHOD" => "GET",
                "PATH_INFO" => "/")
              @client.close_write
              @client.read.should == ""
          end

          it "outputs Content-Length" do
            setup do |env|
              [200, { "Content-Length" => "2" }, ["ok"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "GET",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should ==
              "HTTP/1.1 200 Whatever\r\n" \
              "Content-Length: 2\r\n\r\n" \
              "ok"
          end

          context "and the body is an Array" do
            it "checks whether the body size matches Content-Length" do
              setup do |env|
                [200, { "Content-Length" => "2" }, ["okok"]]
              end
              send_binary_request(@client,
                "REQUEST_METHOD" => "GET",
                "PATH_INFO" => "/")
              @client.close_write
              @client.read.should == ""
            end

            it "allows keepalive" do
              setup do |env|
                [200, { "Content-Length" => "2" }, ["ok"]]
              end
              send_binary_request(@client,
                "REQUEST_METHOD" => "GET",
                "PATH_INFO" => "/")
              @client.close_write
              @client.read.should ==
                "HTTP/1.1 200 Whatever\r\n" \
                "Content-Length: 2\r\n\r\n" \
                "ok"
            end
          end

          context "and the body is not an Array" do
            it "does not allow keep-alive" do
              setup do |env|
                [200, { "Content-Length" => "2" }, NonArrayBody.new(["ok"])]
              end
              send_binary_request(@client,
                "REQUEST_METHOD" => "GET",
                "PATH_INFO" => "/")
              @client.close_write
              @client.read.should ==
                "HTTP/1.1 200 Whatever\r\n" \
                "Content-Length: 2\r\n" \
                "Connection: close\r\n\r\n" \
                "ok"
            end
          end
        end
      end

      context "and the response status code does not allow a body" do
        context "and the request is HEAD" do
          it "disallows Transfer-Encoding" do
            setup do |env|
              [204, { "Content-Length" => "2", "Transfer-Encoding" => "chunked" },
                ["ok"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "HEAD",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should == ""
          end

          it "outputs Content-Length, ignores the body and allows keep-alive" do
            setup do |env|
              [204, { "Content-Length" => "2" }, ["ok"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "HEAD",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should ==
              "HTTP/1.1 204 Whatever\r\n" \
              "Content-Length: 2\r\n\r\n"
          end

          it "does not check whether the body size matches Content-Length" do
            setup do |env|
              [204, { "Content-Length" => "2" }, ["okok"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "HEAD",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should ==
              "HTTP/1.1 204 Whatever\r\n" \
              "Content-Length: 2\r\n\r\n"
          end
        end

        context "and the request is not HEAD" do
          it "disallows Transfer-Encoding" do
            setup do |env|
              [204, { "Content-Length" => "2", "Transfer-Encoding" => "chunked" },
                ["ok"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "GET",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should == ""
          end

          it "outputs Content-Length, ignores the body and allows keep-alive" do
            setup do |env|
              [204, { "Content-Length" => "2" }, ["ok"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "GET",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should ==
              "HTTP/1.1 204 Whatever\r\n" \
              "Content-Length: 2\r\n\r\n"
          end

          it "does not check whether the body size matches Content-Length" do
            setup do |env|
              [204, { "Content-Length" => "2" }, ["okok"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "GET",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should ==
              "HTTP/1.1 204 Whatever\r\n" \
              "Content-Length: 2\r\n\r\n"
          end
        end
      end

      context "with X-Sendfile" do
        it "outputs the body and disallows keep-alive" do
          setup do |env|
            [200, { "Content-Length" => "2", "X-Sendfile" => "/foo" }, ["ok"]]
          end
          send_binary_request(@client,
            "REQUEST_METHOD" => "GET",
            "PATH_INFO" => "/")
          @client.close_write
          [
            "HTTP/1.1 200 Whatever\r\n" \
            "Content-Length: 2\r\n" \
            "X-Sendfile: /foo\r\n" \
            "Connection: close\r\n\r\n" \
            "ok",

            "HTTP/1.1 200 Whatever\r\n" \
            "X-Sendfile: /foo\r\n" \
            "Content-Length: 2\r\n" \
            "Connection: close\r\n\r\n" \
            "ok"
          ].should include(@client.read)
        end
      end

      context "with X-Accel-Redirect" do
        it "outputs the body and disallows keep-alive" do
          setup do |env|
            [200, { "Content-Length" => "2", "X-Accel-Redirect" => "/foo" }, ["ok"]]
          end
          send_binary_request(@client,
            "REQUEST_METHOD" => "GET",
            "PATH_INFO" => "/")
          @client.close_write
          [
            "HTTP/1.1 200 Whatever\r\n" \
            "Content-Length: 2\r\n" \
            "X-Accel-Redirect: /foo\r\n" \
            "Connection: close\r\n\r\n" \
            "ok",

            "HTTP/1.1 200 Whatever\r\n" \
            "X-Accel-Redirect: /foo\r\n" \
            "Content-Length: 2\r\n" \
            "Connection: close\r\n\r\n" \
            "ok"
          ].should include(@client.read)
        end
      end
    end

    describe "with Transfer-Encoding" do
      context "and the response status code allows a body" do
        context "and the request is HEAD" do
          it "disallows Content-Length" do
            setup do |env|
              [200, { "Transfer-Encoding" => "chunked", "Content-Length" => "1" },
                ["2\r\nok\r\n", "0\r\n\r\n"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "HEAD",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should == ""
          end

          it "outputs Transfer-Encoding, ignores the body and allows keep-alive" do
            setup do |env|
              [200, { "Transfer-Encoding" => "chunked" }, ["2\r\nok\r\n", "0\r\n\r\n"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "HEAD",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should ==
              "HTTP/1.1 200 Whatever\r\n" \
              "Transfer-Encoding: chunked\r\n\r\n"
          end
        end

        context "and the request is not HEAD" do
          it "disallows Content-Length" do
            setup do |env|
              [200, { "Transfer-Encoding" => "chunked", "Content-Length" => "1" },
                ["2\r\nok\r\n", "0\r\n\r\n"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "GET",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should == ""
          end

          it "outputs Transfer-Encoding, does not rechunk the body and disallows keep-alive" do
            setup do |env|
              [200, { "Transfer-Encoding" => "chunked" }, ["2\r\nok\r\n", "0\r\n\r\n"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "GET",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should ==
              "HTTP/1.1 200 Whatever\r\n" \
              "Transfer-Encoding: chunked\r\n" \
              "Connection: close\r\n\r\n" \
              "2\r\nok\r\n" \
              "0\r\n\r\n"
          end
        end
      end

      context "and the response status code does not allow a body" do
        context "and the request is HEAD" do
          it "disallows Content-Length" do
            setup do |env|
              [204, { "Transfer-Encoding" => "chunked", "Content-Length" => "1" },
                ["2\r\nok\r\n", "0\r\n\r\n"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "HEAD",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should == ""
          end

          it "outputs Transfer-Encoding, ignores the body and allows keep-alive" do
            setup do |env|
              [204, { "Transfer-Encoding" => "chunked" }, ["2\r\nok\r\n", "0\r\n\r\n"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "HEAD",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should ==
              "HTTP/1.1 204 Whatever\r\n" \
              "Transfer-Encoding: chunked\r\n\r\n"
          end
        end

        context "and the request is not HEAD" do
          it "disallows Content-Length" do
            setup do |env|
              [204, { "Transfer-Encoding" => "chunked", "Content-Length" => "1" },
                ["2\r\nok\r\n", "0\r\n\r\n"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "GET",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should == ""
          end

          it "outputs Transfer-Encoding, ignores the body and allows keep-alive" do
            setup do |env|
              [204, { "Transfer-Encoding" => "chunked" }, ["2\r\nok\r\n", "0\r\n\r\n"]]
            end
            send_binary_request(@client,
              "REQUEST_METHOD" => "GET",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should ==
              "HTTP/1.1 204 Whatever\r\n" \
              "Transfer-Encoding: chunked\r\n\r\n"
          end
        end
      end
    end

    describe "with neither Content-Length nor Transfer-Encoding" do
      context "and the response status code allows a body" do
        context "and the request is HEAD" do
          context "and the body is an Array" do
            before :each do
              setup do |env|
                [200, {}, ["ok"]]
              end
            end

            it "adds Content-Length, ignores the body and allows keep-alive" do
              send_binary_request(@client,
                "REQUEST_METHOD" => "HEAD",
                "PATH_INFO" => "/")
              @client.close_write
              @client.read.should ==
                "HTTP/1.1 200 Whatever\r\n" \
                "Content-Length: 2\r\n\r\n"
            end
          end

          context "and the body is not an Array" do
            before :each do
              setup do |env|
                [200, {}, NonArrayBody.new(["ok"])]
              end
            end

            it "adds Transfer-Encoding, ignores the body and allows keep-alive" do
              send_binary_request(@client,
                "REQUEST_METHOD" => "HEAD",
                "PATH_INFO" => "/")
              @client.close_write
              @client.read.should ==
                "HTTP/1.1 200 Whatever\r\n" \
                "Transfer-Encoding: chunked\r\n\r\n"
            end
          end
        end

        context "and the request is not HEAD" do
          context "and the body is an Array" do
            before :each do
              setup do |env|
                [200, {}, ["ok"]]
              end
            end

            it "adds Content-Length and allows keep-alive" do
              send_binary_request(@client,
                "REQUEST_METHOD" => "GET",
                "PATH_INFO" => "/")
              @client.close_write
              @client.read.should ==
                "HTTP/1.1 200 Whatever\r\n" \
                "Content-Length: 2\r\n\r\n" \
                "ok"
            end
          end

          context "and the body is not an Array" do
            before :each do
              setup do |env|
                [200, {}, NonArrayBody.new(["ok"])]
              end
            end

            it "adds Transfer-Encoding, chunk-encodes the body and allows keep-alive" do
              send_binary_request(@client,
                "REQUEST_METHOD" => "GET",
                "PATH_INFO" => "/")
              @client.close_write
              @client.read.should ==
                "HTTP/1.1 200 Whatever\r\n" \
                "Transfer-Encoding: chunked\r\n\r\n" \
                "2\r\nok\r\n" \
                "0\r\n\r\n"
            end
          end
        end
      end

      context "and the response status code does not allow a body" do
        before :each do
          setup do |env|
            [204, {}, ["ok"]]
          end
        end

        context "and the request is HEAD" do
          it "adds neither Content-Length nor Transfer-Encoding, ignores the body and allows keep-alive" do
            send_binary_request(@client,
              "REQUEST_METHOD" => "HEAD",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should ==
              "HTTP/1.1 204 Whatever\r\n\r\n"
          end
        end

        context "and the request is not HEAD" do
          it "adds neither Content-Length nor Transfer-Encoding, ignores the body and allows keep-alive" do
            send_binary_request(@client,
              "REQUEST_METHOD" => "GET",
              "PATH_INFO" => "/")
            @client.close_write
            @client.read.should ==
              "HTTP/1.1 204 Whatever\r\n\r\n"
          end
        end
      end
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
      @thread_handler.any_instance.should_receive(:process_request) do |headers, connection, full_http_response|
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
      @thread_handler.any_instance.should_receive(:process_request) do |headers, connection, full_http_response|
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
      @thread_handler.any_instance.should_receive(:process_request) do |headers, connection, full_http_response|
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
