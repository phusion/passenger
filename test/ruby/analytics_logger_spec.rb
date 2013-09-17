require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'stringio'
require 'phusion_passenger/analytics_logger'

module PhusionPassenger

describe AnalyticsLogger do
	YESTERDAY = Time.utc(2010, 4, 11, 11, 56, 02)
	TODAY     = Time.utc(2010, 4, 11, 12, 56, 02)
	TOMORROW  = Time.utc(2010, 4, 11, 13, 56, 02)
	
	before :each do
		@username = "logging"
		@password = "1234"
		@dump_file = Utils.passenger_tmpdir + "/log.txt"
		start_agent
		@logger    = AnalyticsLogger.new(@socket_address, @username, @password, "localhost")
		@logger2   = AnalyticsLogger.new(@socket_address, @username, @password, "localhost")
	end
	
	after :each do
		@logger.close
		@logger2.close
		if @agent_pid
			Process.kill('KILL', @agent_pid)
			Process.waitpid(@agent_pid)
		end
	end
	
	def mock_time(time)
		AnalyticsLogger.stub(:current_time).and_return(time)
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
		
		log = @logger.new_transaction("foobar")
		log.should_not be_null
		begin
			log.message("hello")
		ensure
			log.close(true)
		end
		
		File.read(@dump_file).should =~ /hello/
		
		log = @logger.new_transaction("foobar", :processes)
		log.should_not be_null
		begin
			log.message("world")
		ensure
			log.close(true)
		end
		
		File.read(@dump_file).should =~ /world/
	end
	
	specify "#new_transaction reestablishes the connection if disconnected" do
		mock_time(TODAY)
		
		@logger.new_transaction("foobar").close(true)
		connection = @logger.instance_variable_get(:"@connection")
		connection.synchronize do
			connection.channel.close
			connection.channel = nil
		end
		
		log = @logger.new_transaction("foobar")
		begin
			log.message("hello")
		ensure
			log.close(true)
		end
		
		File.read(@dump_file).should =~ /hello/
	end
	
	specify "#new_transaction does not reconnect to the server for a short period of time if connecting failed" do
		@logger.reconnect_timeout = 60
		@logger.max_connect_tries = 1
		
		mock_time(TODAY)
		kill_agent
		@logger.new_transaction("foobar").should be_null
		
		mock_time(TODAY + 30)
		start_agent
		@logger.new_transaction("foobar").should be_null
		
		mock_time(TODAY + 61)
		@logger.new_transaction("foobar").should_not be_null
	end
	
	specify "logging with #continue_transaction works" do
		mock_time(TODAY)
		
		log = @logger.new_transaction("foobar", :processes)
		begin
			log.message("hello")
			log2 = @logger2.continue_transaction(log.txn_id, "foobar", :processes)
			log2.should_not be_null
			log2.txn_id.should == log.txn_id
			begin
				log2.message("world")
			ensure
				log2.close(true)
			end
		ensure
			log.close(true)
		end
		
		File.read(@dump_file).should =~ /#{Regexp.escape log.txn_id} .* hello$/
		File.read(@dump_file).should =~ /#{Regexp.escape log.txn_id} .* world$/
	end
	
	specify "#continue_transaction reestablishes the connection if disconnected" do
		mock_time(TODAY)
		
		log = @logger.new_transaction("foobar")
		log.close(true)
		log2 = @logger2.continue_transaction(log.txn_id, "foobar")
		log2.close(true)
		
		connection = @logger2.instance_variable_get(:"@connection")
		connection.synchronize do
			connection.channel.close
			connection.channel = nil
		end
		
		log2 = @logger2.continue_transaction(log.txn_id, "foobar")
		begin
			log2.message("hello")
		ensure
			log2.close(true)
		end
		
		File.read(@dump_file).should =~ /hello/
	end
	
	specify "#new_transaction and #continue_transaction eventually reestablish the connection to the logging server if the logging server crashed and was restarted" do
		mock_time(TODAY)
		
		log = @logger.new_transaction("foobar")
		@logger2.continue_transaction(log.txn_id, "foobar").close
		kill_agent
		start_agent
		
		log = @logger.new_transaction("foobar")
		log.should be_null
		log2 = @logger2.continue_transaction("1234-abcd", "foobar")
		log2.should be_null
		
		mock_time(TODAY + 60)
		log = @logger.new_transaction("foobar")
		log2 = @logger2.continue_transaction(log.txn_id, "foobar")
		begin
			log2.message("hello")
		ensure
			log2.close(true)
		end
		log.close(true)
		
		File.read(@dump_file).should =~ /hello/
	end
	
	specify "#continue_transaction does not reconnect to the server for a short period of time if connecting failed" do
		@logger.reconnect_timeout = 60
		@logger.max_connect_tries = 1
		@logger2.reconnect_timeout = 60
		@logger2.max_connect_tries = 1
		
		mock_time(TODAY)
		log = @logger.new_transaction("foobar")
		@logger2.continue_transaction(log.txn_id, "foobar")
		kill_agent
		@logger2.continue_transaction(log.txn_id, "foobar").should be_null
		
		mock_time(TODAY + 30)
		start_agent
		@logger2.continue_transaction(log.txn_id, "foobar").should be_null
		
		mock_time(TODAY + 61)
		@logger2.continue_transaction(log.txn_id, "foobar").should_not be_null
	end
	
	specify "AnalyticsLogger only creates null Log objects if no server address is given" do
		logger = AnalyticsLogger.new(nil, nil, nil, nil)
		begin
			logger.new_transaction("foobar").should be_null
		ensure
			logger.close
		end
	end
	
	specify "once a Log object is closed, be becomes null" do
		log = @logger.new_transaction("foobar")
		log.close
		log.should be_null
	end
	
	specify "null Log objects don't do anything" do
		logger = AnalyticsLogger.new(nil, nil, nil, nil)
		begin
			log = logger.new_transaction("foobar")
			log.message("hello")
			log.close(true)
		ensure
			logger.close
		end
		
		File.exist?("#{@log_dir}/1").should be_false
	end
	
	specify "#clear_connection closes the connection" do
		@logger.new_transaction("foobar").close
		@logger.clear_connection
		connection = @logger.instance_variable_get(:"@connection")
		connection.synchronize do
			connection.channel.should be_nil
		end
	end
end

end # module PhusionPassenger
