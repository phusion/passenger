require 'support/config'
require 'support/test_helper'
require 'passenger/abstract_request_handler'

include Passenger

describe AbstractRequestHandler do
	before :each do
		@owner_pipe = IO.pipe
		@request_handler = AbstractRequestHandler.new(@owner_pipe[1])
		def @request_handler.process_request(*args)
			# Do nothing.
		end
	end
	
	after :each do
		@request_handler.cleanup
		@owner_pipe[0].close rescue nil
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
	
	it "exits after processing 'max_requests' requests" do
		def @request_handler.accept_connection
			return "foo"
		end
		def @request_handler.parse_request(client)
			return "bar", "baz"
		end
		
		@request_handler.max_requests = 10
		@request_handler.start_main_loop_thread
		begin
			Timeout.timeout(5) do
				wait_until do
					!@request_handler.main_loop_running?
				end
			end
			@request_handler.processed_requests.should == 10
		rescue Timeout::Error
			violated
		end
	end
	
	def wait_until
		while !yield
			sleep 0.01
		end
	end
end
