require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'stringio'
require 'phusion_passenger/analytics_logger'

module PhusionPassenger

describe AnalyticsLogger do
	YESTERDAY = Time.utc(2010, 4, 11, 11, 56, 02)
	TODAY     = Time.utc(2010, 4, 11, 12, 56, 02)
	TOMORROW  = Time.utc(2010, 4, 11, 13, 56, 02)
	FOOBAR_MD5 = Digest::MD5.hexdigest("foobar")
	LOCALHOST_MD5 = Digest::MD5.hexdigest("localhost")
	
	before :each do
		@username = "logging"
		@password = "1234"
		@log_dir  = Utils.passenger_tmpdir
		start_agent
		@logger = AnalyticsLogger.new(@socket_address, @username, @password, "localhost")
		@logger2 = AnalyticsLogger.new(@socket_address, @username, @password, "localhost")
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
		AnalyticsLogger.stub!(:current_time).and_return(time)
	end
	
	def start_agent
		@agent_pid, @socket_filename, @socket_address = spawn_logging_agent(@log_dir, @password)
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
		
		log_file = "#{@log_dir}/1/#{FOOBAR_MD5}/#{LOCALHOST_MD5}/requests/2010/04/11/12/log.txt"
		File.read(log_file).should =~ /hello/
		
		log = @logger.new_transaction("foobar", :processes)
		log.should_not be_null
		begin
			log.message("world")
		ensure
			log.close(true)
		end
		
		log_file = "#{@log_dir}/1/#{FOOBAR_MD5}/#{LOCALHOST_MD5}/processes/2010/04/11/12/log.txt"
		File.read(log_file).should =~ /world/
	end
	
	specify "#new_transaction reestablishes the connection if disconnected" do
		mock_time(TODAY)
		
		@logger.new_transaction("foobar").close(true)
		shared_data = @logger.instance_variable_get(:"@shared_data")
		shared_data.synchronize do
			shared_data.client.close
		end
		
		log = @logger.new_transaction("foobar")
		begin
			log.message("hello")
		ensure
			log.close(true)
		end
		
		log_file = "#{@log_dir}/1/#{FOOBAR_MD5}/#{LOCALHOST_MD5}/requests/2010/04/11/12/log.txt"
		File.read(log_file).should =~ /hello/
	end
	
	specify "#new_transaction reestablishes the connection to the logging server if the logging server crashed and was restarted" do
		mock_time(TODAY)
		
		@logger.new_transaction("foobar").close
		kill_agent
		start_agent
		
		log = @logger.new_transaction("foobar")
		begin
			log.message("hello")
		ensure
			log.close(true)
		end
		
		log_file = "#{@log_dir}/1/#{FOOBAR_MD5}/#{LOCALHOST_MD5}/requests/2010/04/11/12/log.txt"
		File.read(log_file).should =~ /hello/
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
		
		log_file = "#{@log_dir}/1/#{FOOBAR_MD5}/#{LOCALHOST_MD5}/processes/2010/04/11/12/log.txt"
		File.read(log_file).should =~ /#{Regexp.escape log.txn_id} .* hello$/
		File.read(log_file).should =~ /#{Regexp.escape log.txn_id} .* world$/
	end
	
	specify "#continue_transaction reestablishes the connection if disconnected" do
		mock_time(TODAY)
		
		log = @logger.new_transaction("foobar")
		log.close(true)
		log2 = @logger2.continue_transaction(log.txn_id, "foobar")
		log2.close(true)
		
		shared_data = @logger2.instance_variable_get(:"@shared_data")
		shared_data.synchronize do
			shared_data.client.close
		end
		
		log2 = @logger2.continue_transaction(log.txn_id, "foobar")
		begin
			log2.message("hello")
		ensure
			log2.close(true)
		end
		
		log_file = "#{@log_dir}/1/#{FOOBAR_MD5}/#{LOCALHOST_MD5}/requests/2010/04/11/12/log.txt"
		File.read(log_file).should =~ /hello/
	end
	
	specify "#continue_transaction reestablishes the connection to the logging server if the logging server crashed and was restarted" do
		mock_time(TODAY)
		
		log = @logger.new_transaction("foobar")
		@logger2.continue_transaction(log.txn_id, "foobar").close
		kill_agent
		start_agent
		
		log2 = @logger2.continue_transaction(log.txn_id, "foobar")
		begin
			log2.message("hello")
		ensure
			log2.close(true)
		end
		
		log_file = "#{@log_dir}/1/#{FOOBAR_MD5}/#{LOCALHOST_MD5}/requests/2010/04/11/12/log.txt"
		File.read(log_file).should =~ /hello/
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
		shared_data = @logger.instance_variable_get(:"@shared_data")
		shared_data.synchronize do
			shared_data.client.should be_nil
		end
	end
end

end # module PhusionPassenger
