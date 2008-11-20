require 'support/config'
require 'support/test_helper'
require 'passenger/abstract_request_handler'

require 'fileutils'

include Passenger

describe AbstractRequestHandler do
	before :each do
		ENV['PHUSION_PASSENGER_TMP'] = "abstract_request_handler_spec.tmp"
		@owner_pipe = IO.pipe
		@request_handler = AbstractRequestHandler.new(@owner_pipe[1])
		def @request_handler.process_request(*args)
			# Do nothing.
		end
	end
	
	after :each do
		@request_handler.cleanup
		@owner_pipe[0].close rescue nil
		ENV.delete('PHUSION_PASSENGER_TMP')
		FileUtils.rm_rf("abstract_request_handler_spec.tmp")
	end
	
	def prepare
		# Do nothing. To be overrided by sub describe blocks.
	end
	
	it "exits if the owner pipe is closed" do
		@request_handler.start_main_loop_thread
		@owner_pipe[0].close
		begin
			Timeout.timeout(5) do
				wait_until do
					!@request_handler.main_loop_running?
				end
			end
		rescue Timeout::Error
			violated
		end
	end
	
	it "ignores new connections that don't send any data" do
		def @request_handler.accept_connection
			return nil
		end
		@request_handler.start_main_loop_thread
		wait_until do
			@request_handler.iterations != 0
		end
		@request_handler.processed_requests.should == 0
	end
	
	it "creates a socket file in the Phusion Passenger temp folder" do
		Dir["abstract_request_handler_spec.tmp/*"].should_not be_empty
	end
	
	it "accepts pings" do
		@request_handler.start_main_loop_thread
		client = UNIXSocket.new(@request_handler.socket_name)
		begin
			channel = MessageChannel.new(client)
			channel.write_scalar("REQUEST_METHOD\0ping\0")
			client.read.should == "pong"
		ensure
			client.close
		end
	end
	
	def wait_until
		while !yield
			sleep 0.01
		end
	end
end
