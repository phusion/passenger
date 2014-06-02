#!/usr/bin/env ruby
# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2014 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

require 'socket'
require 'thread'
require 'logger'

module PhusionPassenger
module App
	def self.options
		return @@options
	end
	
	def self.exit_code_for_exception(e)
		if e.is_a?(SystemExit)
			return e.status
		else
			return 1
		end
	end
	
	def self.handshake_and_read_startup_request
		STDOUT.sync = true
		STDERR.sync = true
		puts "!> I have control 1.0"
		abort "Invalid initialization header" if STDIN.readline != "You have control 1.0\n"
		
		@@options = {}
		while (line = STDIN.readline) != "\n"
			name, value = line.strip.split(/: */, 2)
			@@options[name] = value
		end
	end

	def self.init_passenger
		require "#{options["ruby_libdir"]}/phusion_passenger"
		PhusionPassenger.locate_directories(options["passenger_root"])
		PhusionPassenger.require_passenger_lib 'message_channel'
		PhusionPassenger.require_passenger_lib 'utils/tmpio'
	end

	def self.ping_port(port)
		socket_domain = Socket::Constants::AF_INET
		sockaddr = Socket.pack_sockaddr_in(port, '127.0.0.1')
		begin
			socket = Socket.new(socket_domain, Socket::Constants::SOCK_STREAM, 0)
			begin
				socket.connect_nonblock(sockaddr)
			rescue Errno::ENOENT, Errno::EINPROGRESS, Errno::EAGAIN, Errno::EWOULDBLOCK
				if select(nil, [socket], nil, 0.1)
					begin
						socket.connect_nonblock(sockaddr)
					rescue Errno::EISCONN
					end
				else
					raise Errno::ECONNREFUSED
				end
			end
			return true
		rescue Errno::ECONNREFUSED, Errno::ENOENT
			return false
		ensure
			socket.close if socket
		end
	end
	
	def self.create_control_server
		dir = Utils.mktmpdir('meteor')
		filename = "#{dir}/control"
		server = UNIXServer.new(filename)
		return [server, dir, filename]
	end

	def self.load_app(control_server)
		port = nil
		tries = 0
		while port.nil? && tries < 200
			port = 1024 + rand(9999)
			if ping_port(port) || ping_port(port + 1) || ping_port(port + 2)
				port = nil
				tries += 1
			end
		end
		if port.nil?
			abort "Cannot find a suitable port to start Meteor on"
		end

		production = options["environment"] == "production" ? "--production" : ""
		pid = fork do
			# Meteor is quite !@#$% here: if we kill its start script
			# with *any* signal, it'll leave a ton of garbage processes
			# around. Apparently it expects the user to press Ctrl-C in a
			# terminal which happens to send a signal to all processes
			# in the session. We emulate that behavior here by giving
			# Meteor its own process group, and sending signals to the
			# entire process group.
			Process.setpgrp
			control_server.close
			exec("meteor run -p #{port} #{production}")
		end
		$0 = options["process_title"] if options["process_title"]
		$0 = "#{$0} (#{pid})"
		return [pid, port]
	end

	class ExitFlag
		def initialize
			@mutex = Mutex.new
			@cond  = ConditionVariable.new
			@exit  = false
		end

		def set
			@mutex.synchronize do
				@exit = true
				@cond.broadcast
			end
		end

		def wait
			@mutex.synchronize do
				while !@exit
					@cond.wait(@mutex)
				end
			end
		end
	end

	# When the HelperAgent is shutting down, it first sends a message (A) to application
	# processes through the control socket that this is happening. The HelperAgent then
	# waits until all HTTP connections are closed, before sending another message
	# to application processes that they should shut down (B).
	# Because Meteor opens long-running connections (e.g. for WebSocket), we have to shut
	# down the Meteor app when A arrives, otherwise the HelperAgent will never send B.
	def self.wait_for_exit_message(control_server)
		exit_flag = ExitFlag.new
		start_control_server_thread(control_server, exit_flag)
		start_stdin_waiter_thread(exit_flag)
		exit_flag.wait
	end

	def self.start_control_server_thread(control_server, exit_flag)
		Thread.new do
			Thread.current.abort_on_exception = true
			while true
				process_next_control_client(control_server, exit_flag)
			end
		end
	end

	def self.process_next_control_client(control_server, exit_flag)
		logger = Logger.new(STDERR)
		begin
			client = control_server.accept
			channel = MessageChannel.new(client)
			while message = channel.read
				process_next_control_message(message, logger, exit_flag)
			end
		rescue Exception => e
			logger.error("#{e} (#{e.class})\n  " + e.backtrace.join("\n  "))
		ensure
			begin
				client.close if client
			rescue SystemCallError, IOError, SocketError
			end
		end
	end

	def self.process_next_control_message(message, logger, exit_flag)
		if message[0] == "abort_long_running_connections"
			logger.debug("Aborting long-running connections")
			exit_flag.set
		else
			logger.error("Invalid control message: #{message.inspect}")
		end
	end

	def self.start_stdin_waiter_thread(exit_flag)
		Thread.new do
			Thread.current.abort_on_exception = true
			begin
				STDIN.readline
			rescue EOFError
			ensure
				exit_flag.set
			end
		end
	end

	
	
	################## Main code ##################
	
	
	handshake_and_read_startup_request
	init_passenger
	begin
		control_server, control_dir, control_filename = create_control_server
		pid, port = load_app(control_server)
		while !ping_port(port)
			sleep 0.01
		end
		puts "!> Ready"
		puts "!> socket: main;tcp://127.0.0.1:#{port};http_session;0"
		puts "!> socket: control;unix:#{control_filename};control;0"
		puts "!> "
		wait_for_exit_message(control_server)
	ensure
		if pid
			Process.kill('INT', -pid) rescue nil
			Process.waitpid(pid) rescue nil
			Process.kill('INT', -pid) rescue nil
		end
		if control_server
			control_server.close
			begin
				File.unlink(control_filename)
			rescue SystemCallError
			end
			require 'fileutils'
			begin
				FileUtils.remove_entry_secure(control_dir)
			rescue SystemCallError
			end
		end
	end
	
end # module App
end # module PhusionPassenger
