# encoding: binary
require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
PhusionPassenger.require_passenger_lib 'utils/tee_input'
require 'stringio'

module PhusionPassenger

shared_examples "TeeInput#gets" do
	it "reads a line, including newline character" do
		init_input("hello\nworld\nlast line")
		@input.gets.should == "hello\n"
		@input.gets.should == "world\n"
	end

	it "reads until EOF if no newline is encountered before EOF" do
		init_input("hello\nworld\nlast line")
		@input.gets
		@input.gets
		@input.gets.should == "last line"
	end

	it "returns nil if EOF is reached" do
		init_input("hello\nworld\nlast line")
		@input.gets
		@input.gets
		@input.gets
		@input.gets.should be_nil
	end
end

shared_examples "TeeInput#read" do
	context "with no arguments" do
		it "slurps the entire socket" do
			init_input("hello\nworld!")
			@input.read.should == "hello\nworld!"
		end

		it "returns the empty string if EOF is reached" do
			init_input("")
			@input.read.should == ""
		end
	end

	context "with a length argument" do
		it "raises ArgumentError if len < 0" do
			init_input("")
			lambda { @input.read(-1) }.should raise_error(ArgumentError)
		end

		it "returns the empty string if len == 0" do
			init_input("hello")
			@input.read(0).should == ""
		end

		it "reads exactly len bytes if available" do
			init_input("hello")
			@input.read(2).should == "he"
			@input.read(2).should == "ll"
		end

		it "reads until EOF if less than len bytes are available" do
			init_input("hello")
			@input.read(6).should == "hello"
		end

		it "returns nil if EOF is reached" do
			init_input("")
			@input.read(6).should be_nil
		end
	end
end

shared_examples "TeeInput#size" do
	it "returns the number of bytes that can be read from the socket until EOF" do
		init_input("hello world")
		@input.size.should == 11
	end

	context "if Content-Length is given" do
		it "returns the value in Content-Length" do
			init_input("hello world", "CONTENT_LENGTH" => 10)
			@input.size.should == 10
		end
	end
end

describe Utils::TeeInput do
	before :each do
		@sock, @sock2 = UNIXSocket.pair
	end

	after :each do
		[@sock, @sock2].each do |sock|
			sock.close if sock && !sock.closed?
		end
		@input.close if @input
	end

	context "when unbuffered" do
		def init_input(data, env = {})
			@input = Utils::TeeInput.new(@sock2, env)
			@sock.write(data)
			@sock.close
		end

		describe("#gets") { include_examples "TeeInput#gets" }
		describe("#read") { include_examples "TeeInput#read" }
		describe("#size") { include_examples "TeeInput#size" }
	end

	context "when buffered" do
		def init_input(data, env = {})
			@input = Utils::TeeInput.new(@sock2, env)
			@sock.write(data)
			@sock.close
			@input.read
			@input.rewind
		end

		describe("#gets") { include_examples "TeeInput#gets" }
		describe("#read") { include_examples "TeeInput#read" }
		describe("#size") { include_examples "TeeInput#size" }
	end
end

end # module PhusionPassenger
