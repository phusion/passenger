# encoding: binary

module PhusionPassenger

# Provides shared functions for preloader apps.
module PreloaderSharedHelpers
	extend self
	
	def accept_and_process_next_client(server_socket)
		original_pid = Process.pid
		client = server_socket.accept
		client.binmode
		begin
			command = client.readline
		rescue EOFError
			return nil
		end
		if command !~ /\n\Z/
			STDERR.puts "Command must end with a newline"
		elsif command == "spawn\n"
			while (line = client.readline) != "\n"
				# Do nothing.
			end
			pid = fork
			if pid.nil?
				client.puts "OK"
				client.puts Process.pid
				client.flush
				client.sync = true
				return [:forked, client]
			else
				NativeSupport.detach_process(pid)
			end
		else
			STDERR.puts "Unknown command '#{command.inspect}'"
		end
		return nil
	ensure
		client.close if client && Process.pid == original_pid
	end
	
	def run_main_loop(options)
		client = nil
		original_pid = Process.pid
		socket_filename = "#{options['generation_dir']}/backends/preloader.#{Process.pid}"
		server = UNIXServer.new(socket_filename)
		
		puts "Ready"
		puts "socket: unix:#{socket_filename}"
		puts
		
		while true
			ios = select([server, STDIN])[0]
			if ios.include?(server)
				result, client = accept_and_process_next_client(server)
				if result == :forked
					STDIN.reopen(client)
					STDOUT.reopen(client)
					STDOUT.sync = true
					client.close
					return :forked
				end
			end
			if ios.include?(STDIN)
				STDIN.read(1)
				break
			end
		end
		return nil
	ensure
		server.close
		if original_pid == Process.pid
			File.unlink(socket_filename) rescue nil
		end
	end
end

end
