#!/usr/bin/env ruby
module PhusionPassenger
module App
	def self.options
		return @@options
	end
	
	def self.handshake_and_read_startup_request
		STDOUT.sync = true
		STDERR.sync = true
		puts "I have control 1.0"
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
		require 'phusion_passenger/loader_shared_helpers'
		require 'phusion_passenger/classic_rails/request_handler'
		Utils.passenger_tmpdir = options["generation_dir"]
		NativeSupport.disable_stdio_buffering
	rescue Exception => e
		puts "Error"
		puts
		puts e
		puts e.backtrace
		exit 1
	end
	
	def self.load_app
		LoaderSharedHelpers.before_loading_app_code_step1('config/environment.rb', options)
		LoaderSharedHelpers.before_loading_app_code_step2(options)
		
		require File.expand_path('config/environment')
		require 'rails/version' if !defined?(Rails::VERSION)
		if Rails::VERSION::MAJOR >= 3
			puts "Error"
			puts
			puts "This application is a Rails #{Rails::VERSION::MAJOR} " +
				"application, but it was wrongly detected as a Rails " +
				"1 or Rails 2 application. This is probably a bug in " +
				"Phusion Passenger, so please report it."
			exit
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
		
	rescue Exception => e
		puts "Error"
		puts
		puts e
		puts e.backtrace
		exit 1
	end
	
	def self.rails_will_preload_app_code?
		return ::Rails::Initializer.method_defined?(:load_application_classes)
	end
	
	
	################## Main code ##################
	
	
	handshake_and_read_startup_request
	init_passenger
	load_app
	handler = ClassicRails::RequestHandler.new(STDIN, options)
	LoaderSharedHelpers.before_handling_requests(false, options)
	puts "Ready"
	LoaderSharedHelpers.advertise_sockets(STDOUT, handler)
	puts
	handler.main_loop
	LoaderSharedHelpers.after_handling_requests
	
end # module App
end # module PhusionPassenger
