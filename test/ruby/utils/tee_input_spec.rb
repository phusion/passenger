# encoding: binary
require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
PhusionPassenger.require_passenger_lib 'utils/tee_input'
require 'stringio'

module PhusionPassenger

shared_examples "TeeInput#gets" do
	context "if Content-Length is given" do
		before :each do
			init_input("hello\nworld\nlast line!", "CONTENT_LENGTH" => 21)
		end

		it "reads a line, including newline character" do
			@input.gets.should == "hello\n"
			@input.gets.should == "world\n"
		end

		it "reads until EOF if no newline is encountered before EOF" do
			@input.gets
			@input.gets
			@input.gets.should == "last line"
		end

		it "returns nil if EOF is reached" do
			@input.gets
			@input.gets
			@input.gets
			@input.gets.should be_nil
		end
	end

	context "if Transfer-Encoding is chunked" do
		before :each do
			init_input("hello\nworld\nlast line", "HTTP_TRANSFER_ENCODING" => "chunked")
		end

		it "reads a line, including newline character" do
			@input.gets.should == "hello\n"
			@input.gets.should == "world\n"
		end

		it "reads until EOF if no newline is encountered before EOF" do
			@input.gets
			@input.gets
			@input.gets.should == "last line"
		end

		it "returns nil if EOF is reached" do
			@input.gets
			@input.gets
			@input.gets
			@input.gets.should be_nil
		end
	end

	context "if neither Content-Length nor Transfer-Encoding chunked are given" do
		before :each do
			init_input("hello\nworld\nlast line")
		end

		it "returns nil" do
			@input.gets.should be_nil
		end
	end
end

shared_examples "TeeInput#read" do
	describe "with no arguments" do
		context "if Content-Length is given" do
			before :each do
				init_input("hello!", "CONTENT_LENGTH" => 5)
			end

			it "slurps up to Content-Length bytes from the socket" do
				@input.read.should == "hello"
			end

			it "returns the empty string if EOF is reached" do
				@input.read
				@input.read.should == ""
			end
		end

		context "if Transfer-Encoding is chunked" do
			before :each do
				init_input("hello!", "HTTP_TRANSFER_ENCODING" => "chunked")
			end

			it "slurps the entire socket" do
				@input.read.should == "hello!"
			end

			it "returns the empty string if EOF is reached" do
				@input.read
				@input.read.should == ""
			end
		end

		context "if neither Content-Length nor Transfer-Encoding chunked are given" do
			before :each do
				init_input("hello!")
			end

			it "returns the empty string" do
				@input.read.should == ""
			end
		end
	end

	describe "with a length argument" do
		it "raises ArgumentError if len < 0" do
			init_input("")
			lambda { @input.read(-1) }.should raise_error(ArgumentError)
		end

		it "returns the empty string if len == 0" do
			init_input("hello", "HTTP_TRANSFER_ENCODING" => "chunked")
			@input.read(0).should == ""
		end

		context "if Content-Length is given" do
			before :each do
				init_input("hello!", "CONTENT_LENGTH" => 5)
			end

			it "reads exactly len bytes if available" do
				@input.read(2).should == "he"
				@input.read(2).should == "ll"
			end

			it "reads at most Content-Length bytes if Content-Length < len are available" do
				@input.read(2).should == "he"
				@input.read(4).should == "llo"
			end

			it "returns nil if Content-Length is reached" do
				@input.read(5).should == "hello"
				@input.read(1).should be_nil
			end
		end

		context "if Transfer-Encoding is chunked" do
			before :each do
				init_input("hello", "HTTP_TRANSFER_ENCODING" => "chunked")
			end

			it "reads exactly len bytes if available" do
				@input.read(2).should == "he"
				@input.read(2).should == "ll"
			end

			it "reads until EOF if less than len bytes are available" do
				@input.read(2).should == "he"
				@input.read(4).should == "llo"
			end

			it "returns nil if EOF is reached" do
				@input.read(5).should == "hello"
				@input.read(1).should be_nil
			end
		end

		context "if neither Content-Length nor Transfer-Encoding chunked are given" do
			it "returns nil" do
				init_input("hello")
				@input.read(2).should be_nil
			end
		end
	end
end

shared_examples "TeeInput#size" do
	context "if Content-Length is given" do
		it "returns the value in Content-Length" do
			init_input("hello world", "CONTENT_LENGTH" => 10)
			@input.size.should == 10
		end
	end

	context "if Transfer-Encoding is chunked" do
		it "returns the number of bytes that can be read from the socket until EOF" do
			init_input("hello world", "HTTP_TRANSFER_ENCODING" => "chunked")
			@input.size.should == 11
		end
	end

	context "if neither Content-Length nor Transfer-Encoding chunked are given" do
		it "returns 0" do
			init_input("hello world")
			@input.size.should == 0
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
