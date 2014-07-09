require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'stringio'
PhusionPassenger.require_passenger_lib 'union_station/core'

module PhusionPassenger

describe UnionStation::Core do
	YESTERDAY = Time.utc(2010, 4, 11, 11, 56, 02)
	TODAY     = Time.utc(2010, 4, 11, 12, 56, 02)
	TOMORROW  = Time.utc(2010, 4, 11, 13, 56, 02)
	
	before :each do
		@username  = "logging"
		@password  = "1234"
		@dump_file = Utils.passenger_tmpdir + "/transaction.txt"
		start_agent
		@core      = UnionStation::Core.new(@socket_address, @username, @password, "localhost")
		@core2     = UnionStation::Core.new(@socket_address, @username, @password, "localhost")
	end
	
	after :each do
		@core.close
		@core2.close
		if @agent_pid
			Process.kill('KILL', @agent_pid)
			Process.waitpid(@agent_pid)
		end
	end
	
	def mock_time(time)
		UnionStation::Core.stub(:current_time).and_return(time)
	end
	
	def start_agent
		@agent_pid, @socket_filename, @socket_address = spawn_logging_agent(@dump_file, @password)
	end
	
	def kill_agent
		if @agent_pid
			Process.kill('KILL', @agent_pid)
			Process.waitpid(@agent_pid)
			File.unlink(@socket_filename)
			@agent_pid = nil
		end
	end
	
	specify "logging with #new_transaction works" do
		mock_time(TODAY)
		
		transaction = @core.new_transaction("foobar")
		transaction.should_not be_null
		begin
			transaction.message("hello")
		ensure
			transaction.close(true)
		end
		
		File.read(@dump_file).should =~ /hello/
		
		transaction = @core.new_transaction("foobar", :processes)
		transaction.should_not be_null
		begin
			transaction.message("world")
		ensure
			transaction.close(true)
		end
		
		File.read(@dump_file).should =~ /world/
	end
	
	specify "#new_transaction reestablishes the connection if disconnected" do
		mock_time(TODAY)
		
		@core.new_transaction("foobar").close(true)
		connection = @core.instance_variable_get(:"@connection")
		connection.synchronize do
			connection.channel.close
			connection.channel = nil
		end
		
		transaction = @core.new_transaction("foobar")
		begin
			transaction.message("hello")
		ensure
			transaction.close(true)
		end
		
		File.read(@dump_file).should =~ /hello/
	end
	
	specify "#new_transaction does not reconnect to the server for a short period of time if connecting failed" do
		@core.reconnect_timeout = 60
		@core.max_connect_tries = 1
		
		mock_time(TODAY)
		kill_agent
		@core.new_transaction("foobar").should be_null
		
		mock_time(TODAY + 30)
		start_agent
		@core.new_transaction("foobar").should be_null
		
		mock_time(TODAY + 61)
		@core.new_transaction("foobar").should_not be_null
	end
	
	specify "logging with #continue_transaction works" do
		mock_time(TODAY)
		
		transaction = @core.new_transaction("foobar", :processes)
		begin
			transaction.message("hello")
			transaction2 = @core2.continue_transaction(transaction.txn_id, "foobar", :processes)
			transaction2.should_not be_null
			transaction2.txn_id.should == transaction.txn_id
			begin
				transaction2.message("world")
			ensure
				transaction2.close(true)
			end
		ensure
			transaction.close(true)
		end
		
		File.read(@dump_file).should =~ /#{Regexp.escape transaction.txn_id} .* hello$/
		File.read(@dump_file).should =~ /#{Regexp.escape transaction.txn_id} .* world$/
	end
	
	specify "#continue_transaction reestablishes the connection if disconnected" do
		mock_time(TODAY)
		
		transaction = @core.new_transaction("foobar")
		transaction.close(true)
		transaction2 = @core2.continue_transaction(transaction.txn_id, "foobar")
		transaction2.close(true)
		
		connection = @core2.instance_variable_get(:"@connection")
		connection.synchronize do
			connection.channel.close
			connection.channel = nil
		end
		
		transaction2 = @core2.continue_transaction(transaction.txn_id, "foobar")
		begin
			transaction2.message("hello")
		ensure
			transaction2.close(true)
		end
		
		File.read(@dump_file).should =~ /hello/
	end
	
	specify "#new_transaction and #continue_transaction eventually reestablish the connection to the logging server if the logging server crashed and was restarted" do
		mock_time(TODAY)
		
		transaction = @core.new_transaction("foobar")
		@core2.continue_transaction(transaction.txn_id, "foobar").close
		kill_agent
		start_agent
		
		transaction = @core.new_transaction("foobar")
		transaction.should be_null
		transaction2 = @core2.continue_transaction("1234-abcd", "foobar")
		transaction2.should be_null
		
		mock_time(TODAY + 60)
		transaction = @core.new_transaction("foobar")
		transaction2 = @core2.continue_transaction(transaction.txn_id, "foobar")
		begin
			transaction2.message("hello")
		ensure
			transaction2.close(true)
		end
		transaction.close(true)
		
		File.read(@dump_file).should =~ /hello/
	end
	
	specify "#continue_transaction does not reconnect to the server for a short period of time if connecting failed" do
		@core.reconnect_timeout = 60
		@core.max_connect_tries = 1
		@core2.reconnect_timeout = 60
		@core2.max_connect_tries = 1
		
		mock_time(TODAY)
		transaction = @core.new_transaction("foobar")
		@core2.continue_transaction(transaction.txn_id, "foobar")
		kill_agent
		@core2.continue_transaction(transaction.txn_id, "foobar").should be_null
		
		mock_time(TODAY + 30)
		start_agent
		@core2.continue_transaction(transaction.txn_id, "foobar").should be_null
		
		mock_time(TODAY + 61)
		@core2.continue_transaction(transaction.txn_id, "foobar").should_not be_null
	end
	
	it "only creates null Transaction objects if no server address is given" do
		core = UnionStation::Core.new(nil, nil, nil, nil)
		begin
			core.new_transaction("foobar").should be_null
		ensure
			core.close
		end
	end
	
	specify "#clear_connection closes the connection" do
		@core.new_transaction("foobar").close
		@core.clear_connection
		connection = @core.instance_variable_get(:"@connection")
		connection.synchronize do
			connection.channel.should be_nil
		end
	end

	describe "transaction objects" do
		it "becomes null once it is closed" do
			transaction = @core.new_transaction("foobar")
			transaction.close
			transaction.should be_null
		end

		it "does nothing if it's null" do
			logger = UnionStation::Core.new(nil, nil, nil, nil)
			begin
				transaction = logger.new_transaction("foobar")
				transaction.message("hello")
				transaction.close(true)
			ensure
				logger.close
			end
			
			File.exist?("#{@log_dir}/1").should be_false
		end

		describe "#begin_measure" do
			it "sends a BEGIN message" do
				transaction = @core.new_transaction("foobar")
				begin
					transaction.should_receive(:message).with(/^BEGIN: hello \(.+?,.+?,.+?\) $/)
					transaction.begin_measure("hello")
				ensure
					transaction.close
				end
			end

			it "adds extra information as base64" do
				transaction = @core.new_transaction("foobar")
				begin
					transaction.should_receive(:message).with(/^BEGIN: hello \(.+?,.+?,.+?\) YWJjZA==$/)
					transaction.begin_measure("hello", "abcd")
				ensure
					transaction.close
				end
			end
		end

		describe "#end_measure" do
			it "sends an END message if error_countered=false" do
				transaction = @core.new_transaction("foobar")
				begin
					transaction.should_receive(:message).with(/^END: hello \(.+?,.+?,.+?\)$/)
					transaction.end_measure("hello")
				ensure
					transaction.close
				end
			end

			it "sends a FAIL message if error_countered=true" do
				transaction = @core.new_transaction("foobar")
				begin
					transaction.should_receive(:message).with(/^FAIL: hello \(.+?,.+?,.+?\)$/)
					transaction.end_measure("hello", true)
				ensure
					transaction.close
				end
			end
		end
	end
end

end # module PhusionPassenger
