#!/usr/bin/env ruby
# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013-2018 Phusion Holding B.V.
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

GC.copy_on_write_friendly = true if GC.respond_to?(:copy_on_write_friendly=)

module PhusionPassenger
  module App
    def self.options
      @@options
    end

    def self.app
      @@app
    end

    def self.init_passenger
      STDOUT.sync = true
      STDERR.sync = true

      work_dir = ENV['PASSENGER_SPAWN_WORK_DIR'].to_s
      if work_dir.empty?
        abort "This program may only be invoked from Passenger (error: $PASSENGER_SPAWN_WORK_DIR not set)."
      end

      record_journey_step_end('SUBPROCESS_EXEC_WRAPPER', 'STEP_PERFORMED')
      record_journey_step_begin('SUBPROCESS_WRAPPER_PREPARATION', 'STEP_IN_PROGRESS')

      ruby_libdir = File.read("#{work_dir}/args/ruby_libdir").strip
      passenger_root = File.read("#{work_dir}/args/passenger_root").strip
      require "#{ruby_libdir}/phusion_passenger"
      PhusionPassenger.locate_directories(passenger_root)

      PhusionPassenger.require_passenger_lib 'loader_shared_helpers'
      PhusionPassenger.require_passenger_lib 'preloader_shared_helpers'
      PhusionPassenger.require_passenger_lib 'utils/json'
      require 'socket'
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

    def self.preload_app
      LoaderSharedHelpers.before_loading_app_code_step1('config.ru', options)
      LoaderSharedHelpers.run_load_path_setup_code(options)
      LoaderSharedHelpers.before_loading_app_code_step2(options)
      LoaderSharedHelpers.activate_gem 'rack'

      app_root = options["app_root"]
      rackup_file = LoaderSharedHelpers.maybe_make_path_relative_to_app_root(
        app_root, options["startup_file"] || "#{app_root}/config.ru")
      rackup_code = ::File.open(rackup_file, 'rb') do |f|
        f.read
      end
      @@app = eval("Rack::Builder.new {( #{rackup_code}\n )}.to_app",
        TOPLEVEL_BINDING, rackup_file)

      LoaderSharedHelpers.after_loading_app_code(options)
    rescue Exception => e
      LoaderSharedHelpers.record_and_print_exception(e)
      LoaderSharedHelpers.about_to_abort(options, e)
      exit LoaderSharedHelpers.exit_code_for_exception(e)
    end

    def self.create_server(options)
      if defined?(NativeSupport)
        unix_path_max = NativeSupport::UNIX_PATH_MAX
      else
        unix_path_max = options.fetch('UNIX_PATH_MAX', 100).to_i
      end
      if options['socket_dir']
        socket_dir = options['socket_dir']
        socket_prefix = "preloader"
      else
        socket_dir = Dir.tmpdir
        socket_prefix = "PsgPreloader"
      end

      socket_filename = nil
      server = nil
      Utils.retry_at_most(128, Errno::EADDRINUSE) do
        socket_filename = "#{socket_dir}/#{socket_prefix}.#{rand(0xFFFFFFFF).to_s(36)}"
        socket_filename = socket_filename.slice(0, unix_path_max - 10)
        server = UNIXServer.new(socket_filename)
      end
      server.close_on_exec!
      File.chmod(0600, socket_filename)

      [server, socket_filename]
    end

    def self.reinitialize_std_channels(work_dir)
      if File.exist?("#{work_dir}/stdin")
        STDIN.reopen("#{work_dir}/stdin", 'r')
      end
      if File.exist?("#{work_dir}/stdout_and_err")
        STDOUT.reopen("#{work_dir}/stdout_and_err", 'w')
        STDERR.reopen(STDOUT)
      end
      STDOUT.sync = STDERR.sync = true
    end

    def self.negotiate_spawn_command
      begin
        work_dir = ENV['PASSENGER_SPAWN_WORK_DIR']
        @@options = File.open("#{work_dir}/args.json", 'rb') do |f|
          Utils::JSON.parse(f.read)
        end

        reinitialize_std_channels(work_dir)

        LoaderSharedHelpers.run_block_and_record_step_progress('SUBPROCESS_PREPARE_AFTER_FORKING_FROM_PRELOADER') do
          LoaderSharedHelpers.before_handling_requests(true, options)
        end

        handler = nil
        LoaderSharedHelpers.run_block_and_record_step_progress('SUBPROCESS_LISTEN') do
          handler = RequestHandler.new(STDIN, options.merge("app" => app))
        end

        LoaderSharedHelpers.dump_all_information(options)
        LoaderSharedHelpers.advertise_sockets(options, handler)
        LoaderSharedHelpers.advertise_readiness(options)
      rescue Exception => e
        LoaderSharedHelpers.record_and_print_exception(e)
        LoaderSharedHelpers.about_to_abort(options, e)
        exit LoaderSharedHelpers.exit_code_for_exception(e)
      end

      handler
    end


    ################## Main code ##################


    init_passenger
    @@options = PreloaderSharedHelpers.init(self)
    LoaderSharedHelpers.record_journey_step_end('SUBPROCESS_WRAPPER_PREPARATION',
      'STEP_PERFORMED')

    LoaderSharedHelpers.run_block_and_record_step_progress('SUBPROCESS_APP_LOAD_OR_EXEC') do
      preload_app
    end

    server = nil
    LoaderSharedHelpers.run_block_and_record_step_progress('SUBPROCESS_LISTEN') do
      begin
        server = create_server(options)
        PreloaderSharedHelpers.advertise_sockets(options, server)
        LoaderSharedHelpers.dump_all_information(options)
      rescue Exception => e
        LoaderSharedHelpers.record_and_print_exception(e)
        LoaderSharedHelpers.about_to_abort(options, e)
        exit LoaderSharedHelpers.exit_code_for_exception(e)
      end
    end

    LoaderSharedHelpers.advertise_readiness(options)

    subprocess_work_dir = PreloaderSharedHelpers.run_main_loop(server, options)
    if subprocess_work_dir
      # Inside forked subprocess
      ENV['PASSENGER_SPAWN_WORK_DIR'] = subprocess_work_dir
      handler = negotiate_spawn_command
      handler.main_loop
      handler.cleanup
      LoaderSharedHelpers.after_handling_requests
    end

  end # module App
end # module PhusionPassenger
