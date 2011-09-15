#!/usr/bin/env ruby
require 'socket'

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
		require 'phusion_passenger/utils/tmpdir'
		require 'phusion_passenger/preloader_utils'
		require 'phusion_passenger/classic_rails/request_handler'
		PhusionPassenger::Utils.passenger_tmpdir = options["generation_dir"]
	rescue Exception => e
		puts "Error"
		puts
		puts e
		puts e.backtrace
		exit 1
	end
	
	def self.preload_app
		PreloaderUtils.before_loading_app_code_step1('config/environment.rb', options)
		PreloaderUtils.before_loading_app_code_step2(options)
		
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
		if gc_is_copy_on_write_friendly? && !rails_will_preload_app_code?
			# Rails 2.2+ uses application_controller.rb while old
			# versions use application.rb.
			require_dependency 'application'
			['models','controllers','helpers'].each do |section|
				Dir.glob("app/#{section}}/*.rb").each do |file|
					require_dependency File.expand_path(file)
				end
			end
		end
		
		PreloaderUtils.after_loading_app_code(options)
		
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
	
	def self.gc_is_copy_on_write_friendly?
		return GC.respond_to?(:copy_on_write_friendly?) && GC.copy_on_write_friendly?
	end
	
	def self.negotiate_spawn_command
		puts "I have control 1.0"
		abort "Invalid initialization header" if STDIN.readline != "You have control 1.0\n"
		
		while (line = STDIN.readline) != "\n"
			name, value = line.strip.split(/: */, 2)
			options[name] = value
		end
		
		handler = ClassicRails::RequestHandler.new(STDIN, options)
		before_handling_requests(true, options)
		puts "Ready"
		handler.server_sockets.each_pair do |name, info|
			puts "socket: #{name};#{info[1]};#{info[0]};1"
		end
		puts
		return handler
	end
	
	
	################## Main code ##################
	
	
	class Forked < StandardError
	end
	
	handshake_and_read_startup_request
	init_passenger
	preload_app
	
	original_pid = Process.pid
	socket_filename = "#{options['generation_dir']}/backends/preloader.#{Process.pid}"
	client = nil
	server = UNIXServer.new(socket_filename)
	
	begin
		puts "Ready"
		puts "socket: unix:#{socket_filename}"
		puts
		
		while true
			ios = select([server, STDIN])[0]
			if ios.include?(server)
				client = server.accept
				begin
					command = client.readline.strip
					if command == "spawn"
						while (line = client.readline) != "\n"
							# Do nothing.
						end
						pid = fork
						if pid.nil?
							client.puts "OK"
							client.puts Process.pid
							client.flush
							# Hack to minimize the stack size when
							# forking a new worker process.
							raise Forked
						else
							Process.detach(pid)
						end
					else
						STDERR.puts "Unknown command '#{command.inspect}'"
					end
				rescue EOFError
					next
				ensure
					client.close if Process.pid == original_pid
				end
			end
			if ios.include?(STDIN)
				STDIN.read(1)
				break
			end
		end
		
	rescue Forked
		server.close
		STDIN.reopen(client)
		STDOUT.reopen(client)
		STDOUT.sync = true
		client.close
		
		handler = negotiate_spawn_command
		handler.main_loop
		
	ensure
		# In case an exception occurred before 'fork' and 'raise Forked',
		# we don't want to run cleanup code.
		if Process.pid == original_pid
			File.unlink(socket_filename) rescue nil
		end
	end
	
end # module App
end # module PhusionPassenger
