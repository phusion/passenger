require 'support/config'
require 'support/test_helper'
require 'passenger/abstract_server'
require 'passenger/abstract_server_collection'

include Passenger

describe AbstractServerCollection do
	before :each do
		@collection = AbstractServerCollection.new
	end
	
	after :each do
		@collection.cleanup
	end
	
	specify "#lookup_or_add adds the server returned by its block" do
		@collection.lookup_or_add('foo') do
			AbstractServer.new
		end
		@collection.should have_key('foo')
	end
	
	specify "#lookup_or_add does not execute the block if the key exists" do
		@collection.lookup_or_add('foo') do
			AbstractServer.new
		end
		@collection.lookup_or_add('foo') do
			violated
		end
	end
	
	specify "#delete deletes the server with the given key" do
		@collection.lookup_or_add('foo') do
			AbstractServer.new
		end
		@collection.delete('foo')
		@collection.should_not have_key('foo')
	end
	
	specify "#delete stop the server if it's started" do
		server = AbstractServer.new
		@collection.lookup_or_add('foo') do
			server.start
			server
		end
		@collection.delete('foo')
		server.should_not be_started
	end
	
	specify "#cleanup deletes everything" do
		@collection.lookup_or_add('foo') do
			AbstractServer.new
		end
		@collection.lookup_or_add('bar') do
			AbstractServer.new
		end
		@collection.cleanup
		@collection.should_not have_key('foo')
		@collection.should_not have_key('bar')
	end
	
	specify "#cleanup stops all servers" do
		servers = []
		3.times do
			server = AbstractServer.new
			server.start
			servers << server
		end
		@collection.lookup_or_add('foo') { servers[0] }
		@collection.lookup_or_add('bar') { servers[1] }
		@collection.lookup_or_add('baz') { servers[2] }
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
		
		@collection.min_cleaning_interval = 0.02
		@collection.max_cleaning_interval = 0.02
		@collection.lookup_or_add('foo') { foo }
		@collection.lookup_or_add('bar') { bar }
		sleep 0.15
		@collection.should_not have_key('foo')
		@collection.should have_key('bar')
	end
	
	specify "servers with max_idle_time of 0 are never cleaned up" do
		@collection.min_cleaning_interval = 0.01
		@collection.max_cleaning_interval = 0.01
		@collection.lookup_or_add('foo') { AbstractServer.new }
		sleep 0.15
		@collection.should have_key('foo')
	end
end
