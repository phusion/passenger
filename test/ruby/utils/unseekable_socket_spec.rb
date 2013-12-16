require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
PhusionPassenger.require_passenger_lib 'utils/unseekable_socket'

module PhusionPassenger

describe Utils::UnseekableSocket do
	class MyException < StandardError
	end
	
	class MySocket
		def write(data)
		end
		
		def gets
		end
		
		def sync=(value)
		end
	end
	
	before :each do
		@socket  = MySocket.new
		@wrapper = Utils::UnseekableSocket.wrap(@socket)
	end
	
	def catch_exception
		yield
		return nil
	rescue MyException => e
		return e
	end
	
	it "delegates method calls to the wrapped socket" do
		@socket.should_receive(:write).with("some data")
		@socket.should_receive(:gets)
		@wrapper.write("some data")
		@wrapper.gets
	end
	
	it "annotates exceptions so that we can identify its source" do
		@wrapper.source_of_exception?(MyException.new("foo")).should be_false
		
		@socket.should_receive(:write).at_least(:once).and_raise(MyException.new("an error"))
		@wrapper2 = Utils::UnseekableSocket.wrap(@socket)
		e1 = catch_exception { @wrapper.write("hello") }
		
		@wrapper.source_of_exception?(e1).should be_true
		@wrapper2.source_of_exception?(e1).should be_true
		
		@socket2 = MySocket.new
		@socket2.should_receive(:write).at_least(:once).and_raise(MyException.new("an error"))
		@wrapper.wrap(@socket2)
		@wrapper2.wrap(@socket2)
		e2 = catch_exception { @wrapper.write("hello") }
		
		@wrapper.source_of_exception?(e1).should be_false
		@wrapper2.source_of_exception?(e1).should be_false
		@wrapper.source_of_exception?(e2).should be_true
		@wrapper2.source_of_exception?(e2).should be_true
		
		Utils::UnseekableSocket.new.source_of_exception?(e1).should be_false
		Utils::UnseekableSocket.new.source_of_exception?(e2).should be_false
	end
end

end # module PhusionPassenger
