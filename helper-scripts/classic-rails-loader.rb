#!/usr/bin/env ruby
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013-2014 Phusion
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

module PhusionPassenger
module App
	def self.options
		return @@options
	end

	def self.format_exception(e)
		result = "#{e} (#{e.class})"
		if !e.backtrace.empty?
			if e.respond_to?(:html?) && e.html?
				require 'erb' if !defined?(ERB)
				result << "\n<pre>  " << ERB::Util.h(e.backtrace.join("\n  ")) << "</pre>"
			else
				result << "\n  " << e.backtrace.join("\n  ")
			end
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
		require "#{options["ruby_libdir"]}/phusion_passenger"
		PhusionPassenger.locate_directories(options["passenger_root"])
		PhusionPassenger.require_passenger_lib 'native_support'
		PhusionPassenger.require_passenger_lib 'ruby_core_enhancements'
		PhusionPassenger.require_passenger_lib 'utils/tmpdir'
		PhusionPassenger.require_passenger_lib 'loader_shared_helpers'
		PhusionPassenger.require_passenger_lib 'request_handler'
		@@options = LoaderSharedHelpers.init(@@options)
		Utils.passenger_tmpdir = options["generation_dir"]
		if defined?(NativeSupport)
			NativeSupport.disable_stdio_buffering
		end
	rescue Exception => e
		LoaderSharedHelpers.about_to_abort(e) if defined?(LoaderSharedHelpers)
		puts "!> Error"
		puts "!> html: true" if e.respond_to?(:html?) && e.html?
		puts "!> "
		puts format_exception(e)
		exit exit_code_for_exception(e)
	end
	
	def self.load_app
		LoaderSharedHelpers.before_loading_app_code_step1('config/environment.rb', options)
		LoaderSharedHelpers.before_loading_app_code_step2(options)
		
		require File.expand_path('config/environment')
		require 'rails/version' if !defined?(Rails::VERSION)
		if Rails::VERSION::MAJOR >= 3
			LoaderSharedHelpers.about_to_abort
			puts "!> Error"
			puts "!> "
			puts "!> This application is a Rails #{Rails::VERSION::MAJOR} " +
				"application, but it was wrongly detected as a Rails " +
				"1 or Rails 2 application. This is probably a bug in " +
				"Phusion Passenger, so please report it."
			exit 1
		end
		if !defined?(Dispatcher)
			require 'dispatcher'
		end
		
		# - No point in preloading the application sources if the garbage collector
		#   isn't copy-on-write friendly.
		# - Rails >= 2.2 already preloads application sources by default, so no need
		#   to do that again.
		if GC.copy_on_write_friendly? && !rails_will_preload_app_code?
			# Rails 2.2+ uses application_controller.rb while old
			# versions use application.rb.
			require_dependency 'application'
			['models','controllers','helpers'].each do |section|
				Dir.glob("app/#{section}}/*.rb").each do |file|
					require_dependency File.expand_path(file)
				end
			end
		end
		
		LoaderSharedHelpers.after_loading_app_code(options)

		if Rails::VERSION::STRING >= '2.3.0'
			PhusionPassenger.require_passenger_lib 'rack/thread_handler_extension'
			RequestHandler::ThreadHandler.send(:include, Rack::ThreadHandlerExtension)
			app = ActionController::Dispatcher.new
		else
			PhusionPassenger.require_passenger_lib 'classic_rails/thread_handler_extension'
			RequestHandler::ThreadHandler.send(:include, ClassicRails::ThreadHandlerExtension)
			app = nil
		end
		handler = RequestHandler.new(STDIN, options.merge("app" => app))

		LoaderSharedHelpers.before_handling_requests(false, options)
		return handler
		
	rescue Exception => e
		LoaderSharedHelpers.about_to_abort(e)
		puts "!> Error"
		puts "!> html: true" if e.respond_to?(:html?) && e.html?
		puts "!> "
		puts format_exception(e)
		exit exit_code_for_exception(e)
	end
	
	def self.rails_will_preload_app_code?
		return ::Rails::Initializer.method_defined?(:load_application_classes)
	end
	
	
	################## Main code ##################
	
	
	handshake_and_read_startup_request
	init_passenger
	handler = load_app
	LoaderSharedHelpers.advertise_readiness
	LoaderSharedHelpers.advertise_sockets(STDOUT, handler)
	puts "!> "
	handler.main_loop
	handler.cleanup
	LoaderSharedHelpers.after_handling_requests
	
end # module App
end # module PhusionPassenger
