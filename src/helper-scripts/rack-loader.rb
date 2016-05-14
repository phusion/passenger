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

      record_journey_step_performed(:step => 'SUBPROCESS_EXEC_WRAPPER', :begin_time => Time.now)
      step_info = record_journey_step_in_progress('SUBPROCESS_WRAPPER_PREPARATION')

      ruby_libdir = File.read("#{work_dir}/args/ruby_libdir").strip
      passenger_root = File.read("#{work_dir}/args/passenger_root").strip
      require "#{ruby_libdir}/phusion_passenger"
      PhusionPassenger.locate_directories(passenger_root)

      PhusionPassenger.require_passenger_lib 'loader_shared_helpers'

      step_info
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

    def self.record_journey_step_in_progress(step)
      dir = ENV['PASSENGER_SPAWN_WORK_DIR']
      path = "#{dir}/response/steps/#{step.downcase}/state"
      try_write_file(path, 'STEP_IN_PROGRESS')
      { :step => step, :begin_time => Time.now }
    end

    def self.record_journey_step_complete(info, state)
      dir = ENV['PASSENGER_SPAWN_WORK_DIR']

      path = "#{dir}/response/steps/#{info[:step].downcase}/state"
      try_write_file(path, state)

      path = "#{dir}/response/steps/#{info[:step].downcase}/duration"
      duration = Time.now - info[:begin_time]
      try_write_file(path, duration.to_s)
    end

    def self.record_journey_step_performed(info)
      record_journey_step_complete(info, 'STEP_PERFORMED')
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


    step_info = init_passenger
    @@options = LoaderSharedHelpers.init(self, step_info)
    LoaderSharedHelpers.record_journey_step_performed(step_info)

    LoaderSharedHelpers.run_block_and_record_step_progress('SUBPROCESS_APP_LOAD_OR_EXEC') do
      load_app
    end

    handler = nil
    LoaderSharedHelpers.run_block_and_record_step_progress('SUBPROCESS_LISTEN') do
      begin
        LoaderSharedHelpers.before_handling_requests(false, options)
        handler = RequestHandler.new(STDIN, options.merge('app' => app))
        LoaderSharedHelpers.advertise_sockets(options, handler)
        LoaderSharedHelpers.advertise_readiness(options)
      rescue Exception => e
        LoaderSharedHelpers.record_and_print_exception(e)
        LoaderSharedHelpers.about_to_abort(options, e)
        exit LoaderSharedHelpers.exit_code_for_exception(e)
      end
    end

    handler.main_loop
    handler.cleanup
    LoaderSharedHelpers.after_handling_requests

  end # module App
end # module PhusionPassenger
