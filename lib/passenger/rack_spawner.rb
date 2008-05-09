require 'passenger/passenger'
module Passenger

class RackSpawner
	include Utils
	
	def self.spawn_application(*args)
		@@instance ||= RackSpawner.new
		@@instance.spawn_application(*args)
	end
	
	def spawn_application(app_root, lower_privilege = true, lowest_user = "nobody", environment = "production")
		a, b = UNIXSocket.pair
		pid = safe_fork(self.class.to_s) do
			safe_fork(self.class.to_s) do
				NilClass.class_eval do
					def blank?
						true
					end
				end
				
				String.class_eval do
					def blank?
						empty?
					end
				end
				
				$0 = "Rack: #{app_root}"
				a.close
				channel = MessageChannel.new(b)
				ENV['RACK_ENV'] = environment
				Dir.chdir(app_root)
				app = load_rack_app(app_root)
				
				reader, writer = IO.pipe
				begin
					handler = Handler.new(reader, app)
					channel.write(Process.pid, handler.socket_name,
						handler.using_abstract_namespace?)
					channel.send_io(writer)
					writer.close
					channel.close
					handler.main_loop
				ensure
					channel.close rescue nil
					writer.close rescue nil
					handler.cleanup rescue nil
				end
			end
		end
		b.close
		Process.waitpid(pid) rescue nil
		
		channel = MessageChannel.new(a)
		pid, socket_name, using_abstract_namespace = channel.read
		if pid.nil?
			raise IOError, "Connection closed"
		end
		owner_pipe = channel.recv_io
		return Application.new(@app_root, pid, socket_name,
			using_abstract_namespace == "true", owner_pipe)
	end

private
	def load_rack_app(app_root)
		config_file = "#{app_root}/config.ru"
		context = RunnerContext.new
		catch(:done) do
			eval(File.read(config_file), context.get_binding, config_file)
		end
		return context.result
	end

	class RunnerContext
		attr_reader :result
		
		def run(app)
			@result = app
			throw :done
		end
		
		def get_binding
			return binding
		end
	end
	
	class Handler < Passenger::RequestHandler
		def initialize(owner_pipe, app)
			super(owner_pipe)
			@app = app
		end
		
		def process_request(socket)
			channel = MessageChannel.new(socket)
			headers_data = channel.read_scalar(MAX_HEADER_SIZE)
			if headers_data.nil?
				socket.close
				return
			end
			
			env = Hash[*headers_data.split("\0")]
			headers_data = nil
			env["CONTENT_LENGTH"] = env["HTTP_CONTENT_LENGTH"]
			env.update({
				"rack.version" => [0, 1],
				"rack.input"   => socket,
				"rack.errors"  => STDERR,
				"rack.multithread"  => false,
				"rack.multiprocess" => true,
				"rack.run_once"   => false,
				"rack.url_scheme" => ["yes", "on", "1"].include?(env["HTTPS"]) ? "https" : "http"
			})
			
			status, headers, body = @app.call(env)
			begin
				socket.write("Status: #{status}\r\n")
				headers.each do |k, vs|
					vs.each do |v|
						socket.write("#{k}: #{v}\r\n")
					end
				end
				socket.write("\r\n")
				body.each do |s|
					socket.write(s)
				end
			ensure
				body.close if body.respond_to?(:close)
			end
		rescue IOError, SocketError, SystemCallError => e
			print_exception("Passenger RequestHandler", e)
		rescue SecurityError => e
			STDERR.puts("*** Passenger RequestHandler: HTTP header size exceeded maximum.")
			STDERR.flush
			print_exception("Passenger RequestHandler", e)
		ensure
			socket.close rescue nil
		end
	end
end

end # module Passenger
