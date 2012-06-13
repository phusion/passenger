require 'socket'
require 'phusion_passenger/utils'
require 'phusion_passenger/message_channel'

module PhusionPassenger

class Loader
	attr_reader :pid, :input, :output, :sockets

	def initialize(command, app_root)
		@app_root = app_root
		a, input = UNIXSocket.pair
		c, output = UNIXSocket.pair
		@pid = pid = fork do
			STDIN.reopen(a)
			STDOUT.reopen(c)
			input.close
			output.close
			Dir.chdir(app_root)
			exec(*command)
		end
		a.close
		c.close
		@input = input
		@output = output
		@sockets = {}
	end

	def close
		@input.close if !@input.closed?
		@output.close if !@output.closed?
		Process.kill('TERM', @pid)
		begin
			Process.waitpid(@pid)
		rescue Errno::ESCHR, Errno::ECHILD
		end
	end

	def negotiate_startup(options = {})
		@input.puts "You have control 1.0"
		init_message = read_response
		if init_message != "I have control 1.0\n"
			raise "Unknown response initialization message: #{init_message.inspect}"
		end
		@input.puts "passenger_root: #{PhusionPassenger.root}"
		@input.puts "ruby_libdir: #{PhusionPassenger.ruby_libdir}"
		@input.puts "generation_dir: #{Utils.passenger_tmpdir}"
		options.each_pair do |key, value|
			@input.puts "#{key}: #{value}"
		end
		@input.puts

		status = read_response

		headers = {}
		line = read_response
		while line != "\n"
			key, value = line.strip.split(/ *: */, 2)
			if key == "socket"
				name, address, protocol, concurrency = value.split(';')
				@sockets[name] = { :address => address, :protocol => protocol, :concurrency => concurrency }
			else
				headers[key] = value
			end
			line = read_response
		end

		if status == "Error\n"
			body = @output.read
		end

		return { :status => status.strip, :headers => headers, :body => body }
	end

	def connect_and_send_request(options)
		socket = Utils.connect_to_server(sockets["main"][:address])
		channel = MessageChannel.new(socket)
		data = ""
		options.each_pair do |key, value|
			data << "#{key}\0#{value}\0"
		end
		channel.write_scalar(data)
		return socket
	end

private
	def read_response
		while true
			line = @output.readline
			if line.start_with?("!> ")
				line.sub!(/^\!> /, '')
				return line
			end
		end
	end
end

shared_examples_for "a loader" do
	it "works" do
		result = start.negotiate_startup
		result[:status].should == "Ready"
		headers, body = perform_request(
			"REQUEST_METHOD" => "GET",
			"PATH_INFO" => "/hello",
			# For Rails 2
			"REQUEST_URI" => "/hello"
		)
		headers["Status"].should == "200"
		body.should == "hello world"
	end
end

end # module PhusionPassenger
