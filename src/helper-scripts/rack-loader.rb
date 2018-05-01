#!/usr/bin/env ruby
# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013-2017 Phusion Holding B.V.
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

    def self.record_journey_step_begin(step, state)
      dir = ENV['PASSENGER_SPAWN_WORK_DIR']
      step_dir = "#{dir}/response/steps/#{step.downcase}"
      try_write_file("#{step_dir}/state", state)
      try_write_file("#{step_dir}/begin_time", Time.now.to_f)
    end

    def self.record_journey_step_end(step, state)
      dir = ENV['PASSENGER_SPAWN_WORK_DIR']
      step_dir = "#{dir}/response/steps/#{step.downcase}"
      try_write_file("#{step_dir}/state", state)
      if !File.exist?("#{step_dir}/begin_time") && !File.exist?("#{step_dir}/begin_time_monotonic")
        try_write_file("#{step_dir}/begin_time", Time.now.to_f)
      end
      try_write_file("#{step_dir}/end_time", Time.now.to_f)
    end

    def self.load_app
      LoaderSharedHelpers.before_loading_app_code_step1('config.ru', options)
      LoaderSharedHelpers.run_load_path_setup_code(options)
      LoaderSharedHelpers.before_loading_app_code_step2(options)
      LoaderSharedHelpers.activate_gem 'rack'

      app_root = options['app_root']
      rackup_file = LoaderSharedHelpers.maybe_make_path_relative_to_app_root(
        app_root, options['startup_file'] || "#{app_root}/config.ru")
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


    ################## Main code ##################


    init_passenger
    @@options = LoaderSharedHelpers.init(self)
    LoaderSharedHelpers.record_journey_step_end('SUBPROCESS_WRAPPER_PREPARATION',
      'STEP_PERFORMED')

    LoaderSharedHelpers.run_block_and_record_step_progress('SUBPROCESS_APP_LOAD_OR_EXEC') do
      load_app
    end

    handler = nil
    LoaderSharedHelpers.run_block_and_record_step_progress('SUBPROCESS_LISTEN') do
      begin
        LoaderSharedHelpers.before_handling_requests(false, options)
        handler = RequestHandler.new(STDIN, options.merge('app' => app))
        LoaderSharedHelpers.advertise_sockets(options, handler)
      rescue Exception => e
        LoaderSharedHelpers.record_and_print_exception(e)
        LoaderSharedHelpers.about_to_abort(options, e)
        exit LoaderSharedHelpers.exit_code_for_exception(e)
      end
    end

    LoaderSharedHelpers.advertise_readiness(options)

    handler.main_loop
    handler.cleanup
    LoaderSharedHelpers.after_handling_requests

  end # module App
end # module PhusionPassenger
