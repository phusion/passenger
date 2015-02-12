#!/usr/bin/env ruby
# A simple pure-Ruby HTTP server, meant as a helper tool in benchmarks.
# It supports HTTP keep-alive and it supports forwarding the request to
# another server.

require 'thread'
require 'socket'
require 'optparse'

class TestServer
  REQUEST =
    "GET / HTTP/1.1\r\n" <<
    "Connection: Keep-Alive\r\n" <<
    "Host: 127.0.0.1:3001\r\n" <<
    "User-Agent: ApacheBench/2.3\r\n" <<
    "Accept: */*\r\n\r\n"

  RESPONSE =
    "HTTP/1.1 200 OK\r\n" <<
    "Status: 200 OK\r\n" <<
    "Content-Type: text/plain\r\n" <<
    "Content-Length: 3\r\n" <<
    "Connection: keep-alive\r\n" <<
    "\r\n" <<
    "ok\n"

  def initialize(options = {})
    @options = options
    @options[:transport] ||= :tcp
    @options[:protocol] ||= :http
    @options[:port] ||= 3000
    @options[:file] ||= './socket'
    @options[:threads] ||= 2
    @options[:processes] ||= 2
    @forward = @options[:forward]
    @forward_transport = @options[:forward_transport]
    @forward_file = @options[:forward_file]
    @forward_port = @options[:forward_port]
    @forward_keep_alive = @options[:forward_keep_alive]
  end

  def run
    case @options[:transport]
    when :tcp
      @server = TCPServer.new('127.0.0.1', @options[:port])
      @server.setsockopt(Socket::IPPROTO_TCP, Socket::TCP_NODELAY, 1)
      puts "Listening on http://127.0.0.1:#{@options[:port]}/"
    when :unix
      File.unlink(@options[:file]) rescue nil
      @server = UNIXServer.new(@options[:file])
      puts "Listening on Unix domain socket: #{@options[:file]}"
    else
      abort "Unknown transport #{@options[:transport]}"
    end
    @server.listen(100)

    case @options[:protocol]
    when :http
      puts "Using HTTP protocol"
      @protocol = :http
    when :session
      puts "Using session protocol"
      @protocol = :session
    else
      abort "Unknown protocol #{@options[:protocol]}"
    end

    if @forward
      case @forward_transport
      when :tcp
        puts "Forwarding to http://127.0.0.1:#{@forward_port}/"
      when :unix
        puts "Forwarding to Unix domain socket: #{@forward_file}"
      end
    end

    puts "Using #{@options[:processes]} processes"
    puts "Using #{@options[:threads]} threads per process"
    fork_children
    threads = []
    @options[:threads].times { threads << start_thread }
    begin
      threads.each { |t| t.join }
    rescue Interrupt
    end
  end

private
  def fork_children
    if @options[:processes] == 1
      return
    end

    children = []
    @options[:processes].times do
      pid = fork
      if pid
        # Parent
        puts "Spawned child process: #{pid}"
        children << pid
      else
        return
      end
    end
    if !children.empty?
      # Parent
      begin
        sleep 999999
      rescue Interrupt
        exit
      ensure
        children.each do |pid|
          puts "Reaping child process: #{pid}"
          Process.kill('INT', pid)
        end
        children.each do |pid|
          Process.waitpid(pid)
        end
      end
    end
  end

  def start_thread
    Thread.new do
      Thread.current.abort_on_exception = true
      if @forward && @forward_keep_alive
        forward_connection = connect_to_forwarding_target
      end
      while true
        handle_next_client(forward_connection)
      end
    end
  end

  def handle_next_client(forward_connection)
    client = @server.accept
    begin
      buffer = "".force_encoding("binary")
      while true
        begin
          read_header(client, buffer)

          if @forward
            forward(forward_connection)
          end

          # Write response
          client.write(RESPONSE)
        rescue EOFError, Errno::ECONNRESET
          break
        end
      end
    ensure
      client.close
    end
  end

  def read_header(client, buffer)
    if @protocol == :http
      while client.readline != "\r\n"
        # Do nothing.
      end
    else
      temp = client.read(4, buffer)
      raise EOFError if temp.nil?
      size = temp.unpack('N')[0]
      temp = client.read(size, buffer)
      raise EOFError if temp.nil?
    end
  end

  def forward(target_connection)
    if target_connection
      io = target_connection
    else
      io = connect_to_forwarding_target
    end
    begin
      io.write(REQUEST)
      while io.readline != "ok\n"
        # Do nothing
      end
    ensure
      if !target_connection
        io.close
      end
    end
  end

  def connect_to_forwarding_target
    if @forward_transport == :unix
      UNIXSocket.new(@forward_file)
    else
      TCPSocket.new('127.0.0.1', @forward_port)
    end
  end
end

options = {}
parser = OptionParser.new do |opts|
  opts.banner = "Usage: ./ruby.rb [options]"
  opts.separator ""

  opts.separator "Options:"
  opts.on("--port PORT", Integer, "Listen on the given TCP port. Default: 3000") do |val|
    options[:transport] = :tcp
    options[:port] = val
  end
  opts.on("--file PATH", String, "Listen on the given Unix domain socket file") do |val|
    options[:transport] = :unix
    options[:file] = val
  end
  opts.on("--session-protocol", "Accept session protocol instead of HTTP") do
    options[:protocol] = :session
  end
  opts.on("--threads N", Integer, "Number of threads to use. Default: 2") do |val|
    options[:threads] = val
  end
  opts.on("--processes N", Integer, "Number of processes to use. Default: 2") do |val|
    options[:processes] = val
  end
  opts.on("--forward-tcp PORT", Integer, "Forward request to another TCP server") do |val|
    options[:forward] = true
    options[:forward_transport] = :tcp
    options[:forward_port] = val
  end
  opts.on("--forward-file PATH", String, "Forward request to another Unix domain socket server") do |val|
    options[:forward] = true
    options[:forward_transport] = :unix
    options[:forward_file] = val
  end
  opts.on("--forward-keep-alive", "Use keep-alive when forwarding") do
    options[:forward_keep_alive] = true
  end
end
begin
  parser.parse!
rescue OptionParser::ParseError => e
  puts e
  puts
  puts "Please see '--help' for valid options."
  exit 1
end
TestServer.new(options).run
