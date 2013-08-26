#!/usr/bin/env ruby
# encoding: binary
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013 Phusion
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

GC.copy_on_write_friendly = true if GC.respond_to?(:copy_on_write_friendly=)

module PhusionPassenger
module App
	def self.options
		return @@options
	end
	
	def self.app
		return @@app
	end

	def self.format_exception(e)
		result = "#{e} (#{e.class})"
		if !e.backtrace.empty?
			result << "\n  " << e.backtrace.join("\n  ")
		end
		return result
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
		$LOAD_PATH.unshift(options["ruby_libdir"])
		require 'phusion_passenger'
		PhusionPassenger.locate_directories(options["passenger_root"])
		require 'phusion_passenger/native_support'
		require 'phusion_passenger/ruby_core_enhancements'
		require 'phusion_passenger/utils/tmpdir'
		require 'phusion_passenger/preloader_shared_helpers'
		require 'phusion_passenger/loader_shared_helpers'
		require 'phusion_passenger/request_handler'
		require 'phusion_passenger/rack/thread_handler_extension'
		LoaderSharedHelpers.init
		PreloaderSharedHelpers.init
		@@options = LoaderSharedHelpers.sanitize_spawn_options(@@options)
		Utils.passenger_tmpdir = options["generation_dir"]
		if defined?(NativeSupport)
			NativeSupport.disable_stdio_buffering
		end
		RequestHandler::ThreadHandler.send(:include, Rack::ThreadHandlerExtension)
	rescue Exception => e
		LoaderSharedHelpers.about_to_abort(e) if defined?(LoaderSharedHelpers)
		puts "!> Error"
		puts "!> "
		puts format_exception(e)
		exit exit_code_for_exception(e)
	end
	
	def self.preload_app
		LoaderSharedHelpers.before_loading_app_code_step1('config.ru', options)
		LoaderSharedHelpers.run_load_path_setup_code(options)
		LoaderSharedHelpers.before_loading_app_code_step2(options)
		
		require 'rubygems'
		require 'rack'
		rackup_file = ENV["RACKUP_FILE"] || options["rackup_file"] || "config.ru"
		rackup_code = ::File.open(rackup_file, 'rb') do |f|
			f.read
		end
		@@app = eval("Rack::Builder.new {( #{rackup_code}\n )}.to_app",
			TOPLEVEL_BINDING, rackup_file)
		
		LoaderSharedHelpers.after_loading_app_code(options)
	rescue Exception => e
		LoaderSharedHelpers.about_to_abort(e)
		puts "!> Error"
		puts "!> "
		puts format_exception(e)
		exit exit_code_for_exception(e)
	end
	
	def self.negotiate_spawn_command
		puts "!> I have control 1.0"
		abort "Invalid initialization header" if STDIN.readline != "You have control 1.0\n"
		
		begin
			while (line = STDIN.readline) != "\n"
				name, value = line.strip.split(/: */, 2)
				options[name] = value
			end
			@@options = LoaderSharedHelpers.sanitize_spawn_options(@@options)
			
			LoaderSharedHelpers.before_handling_requests(true, options)
			handler = RequestHandler.new(STDIN, options.merge("app" => app))
		rescue Exception => e
			LoaderSharedHelpers.about_to_abort(e)
			puts "!> Error"
			puts "!> "
			puts format_exception(e)
			exit exit_code_for_exception(e)
		end

		puts "!> Ready"
		LoaderSharedHelpers.advertise_sockets(STDOUT, handler)
		puts "!> "
		return handler
	end
	
	
	################## Main code ##################
	
	
	handshake_and_read_startup_request
	init_passenger
	preload_app
	if PreloaderSharedHelpers.run_main_loop(options) == :forked
		handler = negotiate_spawn_command
		handler.main_loop
		LoaderSharedHelpers.after_handling_requests
	end
	
end # module App
end # module PhusionPassenger
