require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'phusion_passenger/abstract_server'
require 'phusion_passenger/abstract_server_collection'

module PhusionPassenger

describe AbstractServerCollection do
	before :each do
		@collection = AbstractServerCollection.new
	end
	
	after :each do
		@collection.cleanup
	end
	
	specify "#lookup_or_add adds the server returned by its block" do
		@collection.synchronize do
			@collection.lookup_or_add('foo') do
				AbstractServer.new
			end
			@collection.should have_key('foo')
		end
	end
	
	specify "#lookup_or_add does not execute the block if the key exists" do
		@collection.synchronize do
			@collection.lookup_or_add('foo') do
				AbstractServer.new
			end
			@collection.lookup_or_add('foo') do
				violated
			end
		end
	end
	
	specify "#lookup_or_add returns the found server" do
		@collection.synchronize do
			server = AbstractServer.new
			@collection.lookup_or_add('foo') { server }
			result = @collection.lookup_or_add('foo') { AbstractServer.new }
			result.should == server
		end
	end
	
	specify "#lookup_or_add returns the value of the block if server is not already in the collection" do
		@collection.synchronize do
			server = AbstractServer.new
			result = @collection.lookup_or_add('foo') do
				server
			end
			result.should == server
		end
	end
	
	specify "#delete deletes the server with the given key" do
		@collection.synchronize do
			@collection.lookup_or_add('foo') do
				AbstractServer.new
			end
			@collection.delete('foo')
			@collection.should_not have_key('foo')
		end
	end
	
	specify "#delete stops the server if it's started" do
		@collection.synchronize do
			server = AbstractServer.new
			@collection.lookup_or_add('foo') do
				server.start
				server
			end
			@collection.delete('foo')
			server.should_not be_started
		end
	end
	
	specify "#clear deletes everything" do
		@collection.synchronize do
			@collection.lookup_or_add('foo') do
				AbstractServer.new
			end
			@collection.lookup_or_add('bar') do
				AbstractServer.new
			end
			@collection.clear
			@collection.should_not have_key('foo')
			@collection.should_not have_key('bar')
		end
	end
	
	specify "#cleanup deletes everything" do
		@collection.synchronize do
			@collection.lookup_or_add('foo') do
				AbstractServer.new
			end
			@collection.lookup_or_add('bar') do
				AbstractServer.new
			end
		end
		@collection.cleanup
		@collection.synchronize do
			@collection.should_not have_key('foo')
			@collection.should_not have_key('bar')
		end
	end
	
	specify "#cleanup stops all servers" do
		servers = []
		3.times do
			server = AbstractServer.new
			server.start
			servers << server
		end
		@collection.synchronize do
			@collection.lookup_or_add('foo') { servers[0] }
			@collection.lookup_or_add('bar') { servers[1] }
			@collection.lookup_or_add('baz') { servers[2] }
		end
		@collection.cleanup
		servers.each do |server|
			server.should_not be_started
		end
	end
	
	specify "idle servers are cleaned up periodically" do
		foo = AbstractServer.new
		foo.max_idle_time = 0.05
		bar = AbstractServer.new
		bar.max_idle_time = 2
		
		@collection.synchronize do
			@collection.lookup_or_add('foo') { foo }
			@collection.lookup_or_add('bar') { bar }
		end
		sleep 0.3
		@collection.synchronize do
			@collection.should_not have_key('foo')
			@collection.should have_key('bar')
		end
	end
	
	specify "servers with max_idle_time of 0 are never cleaned up" do
		@collection.synchronize do
			@collection.lookup_or_add('foo') { AbstractServer.new }
		end
		original_cleaning_time = @collection.next_cleaning_time
		@collection.check_idle_servers!
		
		# Wait until the cleaner thread has run.
		while original_cleaning_time == @collection.next_cleaning_time
			sleep 0.01
		end
		
		@collection.synchronize do
			@collection.should have_key('foo')
		end
	end
	
	specify "upon adding a new server to an empty collection, the next cleaning will " <<
	        "be scheduled at that server's next cleaning time" do
		server = AbstractServer.new
		server.max_idle_time = 10
		@collection.synchronize do
			@collection.lookup_or_add('foo') { server }
		end
		@collection.next_cleaning_time.should == server.next_cleaning_time
	end
	
	specify "upon adding a new server to a nonempty collection, and that server's next cleaning " <<
	        "time is not the smallest of all servers' cleaning times, then the next cleaning schedule " <<
	        "will not change" do
		server1 = AbstractServer.new
		server1.max_idle_time = 10
		@collection.synchronize do
			@collection.lookup_or_add('foo') { server1 }
		end
		
		server2 = AbstractServer.new
		server2.max_idle_time = 11
		@collection.synchronize do
			@collection.lookup_or_add('bar') { server2 }
		end
		
		@collection.next_cleaning_time.should == server1.next_cleaning_time
	end
	
	specify "upon deleting server from a nonempty collection, and the deleted server's next cleaning " <<
	        "time IS the smallest of all servers' cleaning times, then the next cleaning schedule " <<
	        "will be changed to the smallest cleaning time of all servers" do
		server1 = AbstractServer.new
		server1.max_idle_time = 10
		@collection.synchronize do
			@collection.lookup_or_add('foo') { server1 }
		end
		
		server2 = AbstractServer.new
		server2.max_idle_time = 11
		@collection.synchronize do
			@collection.lookup_or_add('bar') { server2 }
		end
		
		@collection.synchronize do
			@collection.delete('foo')
		end
		
		@collection.next_cleaning_time.should == server2.next_cleaning_time
	end
	
	specify "upon deleting server from a nonempty collection, and the deleted server's next cleaning " <<
	        "time IS NOT the smallest of all servers' cleaning times, then the next cleaning schedule " <<
	        "will not change" do
		server1 = AbstractServer.new
		server1.max_idle_time = 10
		@collection.synchronize do
			@collection.lookup_or_add('foo') { server1 }
		end
		
		server2 = AbstractServer.new
		server2.max_idle_time = 11
		@collection.synchronize do
			@collection.lookup_or_add('bar') { server2 }
		end
		
		@collection.synchronize do
			@collection.delete('bar')
		end
		
		@collection.next_cleaning_time.should == server1.next_cleaning_time
	end
	
	specify "bug check" do
		block = lambda do
			@collection.synchronize do
				@collection.clear
				@collection.lookup_or_add('foo') do
					s = AbstractServer.new
					s.max_idle_time = 0.05
					s
				end
				@collection.lookup_or_add('bar') { AbstractServer.new }
			end
		end
		block.should_not raise_error
	end
end

end # module PhusionPassenger
