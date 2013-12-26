require 'socket'
PhusionPassenger.require_passenger_lib 'utils'
PhusionPassenger.require_passenger_lib 'message_channel'

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
			ENV['RAILS_ENV'] = ENV['RACK_ENV'] = ENV['PASSENGER_ENV'] = 'production'
			exec(*command)
		end
		a.close
		c.close
		@input = input
		@output = output
		@sockets = {}
	end

	def self.new_with_sockets(input, output, app_root)
		result = allocate
		result.instance_variable_set(:@input, input)
		result.instance_variable_set(:@output, output)
		result.instance_variable_set(:@app_root, app_root)
		result.instance_variable_set(:@sockets, {})
		return result
	end

	def close
		@input.close_write
		# Wait at most 100 msec for process to exit.
		select([@output], nil, nil, 0.1)

		@input.close if !@input.closed?
		@output.close if !@output.closed?
		if @pid
			begin
				Process.kill('TERM', @pid)
			rescue Errno::ESRCH
			end
			begin
				Process.waitpid(@pid)
			rescue Errno::ECHILD
			end
		end
	end

	def start(options = {})
		init_message = read_response_line
		if init_message != "I have control 1.0\n"
			raise "Unknown response initialization message: #{init_message.inspect}"
		end
		write_request_line "You have control 1.0"
		write_start_request(options)
		return process_response
	end

	def connect_and_send_request(headers)
		socket = Utils.connect_to_server(sockets["main"][:address])
		channel = MessageChannel.new(socket)
		data = ""
		headers["REQUEST_METHOD"] ||= "GET"
		headers["REQUEST_URI"] ||= headers["PATH_INFO"]
		headers["QUERY_STRING"] ||= ""
		headers["SCRIPT_NAME"] ||= ""
		headers.each_pair do |key, value|
			data << "#{key}\0#{value}\0"
		end
		channel.write_scalar(data)
		return socket
	end

private
	def write_request_line(line = "")
		STDERR.puts "---> #{line}" if DEBUG
		@input.puts line
	end

	def read_response_line
		while true
			line = @output.readline
			STDERR.puts "<--- #{line.strip}" if DEBUG
			if line.start_with?("!> ")
				line.sub!(/^\!> /, '')
				return line
			end
		end
	end

	def write_start_request(options)
		write_request_line "passenger_root: #{PhusionPassenger.source_root}"
		write_request_line "ruby_libdir: #{PhusionPassenger.ruby_libdir}"
		write_request_line "generation_dir: #{Utils.passenger_tmpdir}"
		write_request_line "log_level: 3" if DEBUG
		options.each_pair do |key, value|
			write_request_line "#{key}: #{value}"
		end
		write_request_line
	end

	def process_response
		status = read_response_line

		headers = {}
		line = read_response_line
		while line != "\n"
			key, value = line.strip.split(/ *: */, 2)
			if key == "socket"
				process_socket(value)
			else
				headers[key] = value
			end
			line = read_response_line
		end

		if status == "Error\n"
			body = @output.read
			STDERR.puts "<--- #{body}" if DEBUG
		end

		return { :status => status.strip, :headers => headers, :body => body }
	end

	def process_socket(spec)
		name, address, protocol, concurrency = spec.split(';')
		@sockets[name] = { :address => address, :protocol => protocol, :concurrency => concurrency }
	end
end

class Preloader < Loader
	def spawn(options = {})
		socket = Utils.connect_to_server(sockets["spawn"])
		loader = Loader.new_with_sockets(socket, socket.dup, @app_root)
		begin
			loader.send(:write_request_line, "spawn")
			loader.send(:write_start_request, options)
			
			line = loader.output.readline
			puts "<--- #{line.strip}" if DEBUG
			if line != "OK\n"
				raise "Unexpected spawn response status #{line.inspect}"
			end

			line = loader.output.readline
			puts "<--- #{line.strip}" if DEBUG
			loader.instance_variable_set(:@pid, line.to_i)

			return loader
		rescue
			loader.close
			raise
		end
	end

private
	def process_socket(spec)
		sockets["spawn"] = spec
	end
end

module LoaderSpecHelper
	def self.included(klass)
		klass.before(:each) do
			@stubs = []
		end
		
		klass.after(:each) do
			begin
				@loader.close if @loader
				@preloader.close if @preloader
			ensure
				@stubs.each do |stub|
					stub.destroy
				end
			end
		end
	end
	
	def before_start(code)
		@before_start = code
	end
	
	def after_start(code)
		@after_start = code
	end
	
	def register_stub(stub)
		@stubs << stub
		File.prepend(stub.startup_file, "#{@before_start}\n")
		File.append(stub.startup_file, "\n#{@after_start}")
		return stub
	end
	
	def register_app(app)
		@apps << app
		return app
	end

	def start!(options = {})
		result = start(options)
		if result[:status] != "Ready"
			raise "Loader failed to start; error page:\n#{result[:body]}"
		end
	end

	def perform_request(headers)
		socket = @loader.connect_and_send_request(headers)
		headers = {}
		line = socket.readline
		while line != "\r\n"
			key, value = line.strip.split(/ *: */, 2)
			headers[key] = value
			line = socket.readline
		end
		body = socket.read
		socket.close
		return [headers, body]
	end
end

shared_examples_for "a loader" do
	it "works" do
		result = start
		result[:status].should == "Ready"
		headers, body = perform_request(
			"REQUEST_METHOD" => "GET",
			"PATH_INFO" => "/",
			# For Rails 2
			"REQUEST_URI" => "/"
		)
		headers["Status"].should == "200"
		body.should == "front page"
	end
end

end # module PhusionPassenger
