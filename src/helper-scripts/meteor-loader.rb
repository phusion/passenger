#!/usr/bin/env ruby
# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2017 Phusion Holding B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Phusion Holding B.V.
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
      @@options ||= {}
      return @@options
    end

    def self.try_write_file(path, contents)
      begin
        File.open(path, 'wb') do |f|
          f.write(contents)
        end
      rescue SystemCallError => e
        STDERR.puts "Warning: unable to write to #{path}: #{e}"
      end
    end

    def self.record_journey_step_begin(step, state, work_dir = nil)
      dir = work_dir || ENV['PASSENGER_SPAWN_WORK_DIR']
      step_dir = "#{dir}/response/steps/#{step.downcase}"
      try_write_file("#{step_dir}/state", state)
      try_write_file("#{step_dir}/begin_time", Time.now.to_f)
    end

    def self.record_journey_step_end(step, state, work_dir = nil)
      dir = work_dir || ENV['PASSENGER_SPAWN_WORK_DIR']
      step_dir = "#{dir}/response/steps/#{step.downcase}"
      try_write_file("#{step_dir}/state", state)
      if !File.exist?("#{step_dir}/begin_time") && !File.exist?("#{step_dir}/begin_time_monotonic")
        try_write_file("#{step_dir}/begin_time", Time.now.to_f)
      end
      try_write_file("#{step_dir}/end_time", Time.now.to_f)
    end

    def self.init_logging
      logger = Logger.new(STDOUT)
      logger.level = Logger::WARN
      logger.formatter = proc do |severity, datetime, progname, msg|
        "[ pid=#{Process.pid}, time=#{datetime} ]: #{msg.message}"
      end
    end

    def self.read_startup_arguments
	    work_dir = ENV['PASSENGER_SPAWN_WORK_DIR']
        @@options = File.open("#{work_dir}/args.json", 'rb') do |f|
          Utils::JSON.parse(f.read)
        end
    end

    def self.advertise_port(port)
      work_dir = ENV['PASSENGER_SPAWN_WORK_DIR']
	    path = work_dir + '/response/properties.json'
	    doc = {
		    'sockets': [{
				             'name': 'main',
				             'address': "tcp://127.0.0.1:#{port}",
				             'protocol': 'http',
				             'concurrency': 0,
				             'accept_http_requests': true
			              }]
	    }
	    File.write(path, Utils::JSON.generate(doc))
    end

    def self.advertise_readiness
	    work_dir = ENV['PASSENGER_SPAWN_WORK_DIR']
	    path = work_dir + '/response/finish'
	    File.write(path, '1')
    end

    def self.init_passenger
      STDOUT.sync = true
      STDERR.sync = true

      work_dir = ENV['PASSENGER_SPAWN_WORK_DIR'].to_s
      if work_dir.empty?
        abort "This program may only be invoked from Passenger (error: $PASSENGER_SPAWN_WORK_DIR not set)."
      end

      ruby_libdir = File.read("#{work_dir}/args/ruby_libdir").strip
      passenger_root = File.read("#{work_dir}/args/passenger_root").strip
      require "#{ruby_libdir}/phusion_passenger"
      PhusionPassenger.locate_directories(passenger_root)

      PhusionPassenger.require_passenger_lib 'loader_shared_helpers'
      PhusionPassenger.require_passenger_lib 'preloader_shared_helpers'
      PhusionPassenger.require_passenger_lib 'utils/json'
      require 'socket'

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

    def self.load_app
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

        if options["environment"] == "production"
          logger.warn("Warning: meteor running in simulated production mode (--production). For real production use, bundle your app.")
        end

        if options["meteor_app_settings"]
          PhusionPassenger.require_passenger_lib 'utils/shellwords'
          app_settings_file = Shellwords.escape(options["meteor_app_settings"])
          logger.info("Using application settings from file #{app_settings_file}")
          exec("meteor run -p #{port} --settings #{app_settings_file} #{production}")
        else
          exec("meteor run -p #{port} #{production}")
        end
        exec("meteor run -p #{port} #{production} --settings settings.json")
      end
      $0 = options["process_title"] if options["process_title"]
      $0 = "#{$0} (#{pid})"
      return [pid, port]
    end

    def self.wait_for_exit_message
      begin
        STDIN.readline
      rescue EOFError
      end
    end


    ################## Main code ##################


    init_passenger
    init_logging

    record_journey_step_end('SUBPROCESS_EXEC_WRAPPER', 'STEP_PERFORMED')

    record_journey_step_begin('SUBPROCESS_WRAPPER_PREPARATION', 'STEP_IN_PROGRESS')
    begin
		  read_startup_arguments
    rescue Exception
		  record_journey_step_end('SUBPROCESS_WRAPPER_PREPARATION', 'STEP_ERRORED')
		  raise
	  else
		  record_journey_step_end('SUBPROCESS_WRAPPER_PREPARATION', 'STEP_PERFORMED')
    end

    record_journey_step_begin('SUBPROCESS_APP_LOAD_OR_EXEC', 'STEP_IN_PROGRESS')
    begin
		  pid, port = load_app
	  rescue Exception
		  record_journey_step_end('SUBPROCESS_APP_LOAD_OR_EXEC', 'STEP_ERRORED')
		  raise
	  else
		  record_journey_step_end('SUBPROCESS_APP_LOAD_OR_EXEC', 'STEP_PERFORMED')
    end

    record_journey_step_begin('SUBPROCESS_LISTEN', 'STEP_IN_PROGRESS')
	  begin
      while !ping_port(port)
        sleep 0.01
      end
	  rescue Exception
		  record_journey_step_end('SUBPROCESS_LISTEN', 'STEP_ERRORED')
		  raise
	  else
		  record_journey_step_end('SUBPROCESS_LISTEN', 'STEP_PERFORMED')
    end

    advertise_port(port)
    advertise_readiness
    begin
	    wait_for_exit_message
    ensure
      if pid
        Process.kill('INT', -pid) rescue nil
        Process.waitpid(pid) rescue nil
        Process.kill('INT', -pid) rescue nil
      end
    end

  end # module App
end # module PhusionPassenger
