require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
PhusionPassenger.require_passenger_lib 'utils/hosts_file_parser'
require 'stringio'

module PhusionPassenger

describe Utils::HostsFileParser do
	before :each do
		@io = StringIO.new
	end
	
	def create
		@io.rewind
		@parser = Utils::HostsFileParser.new(@io)
	end
	
	describe "parsing" do
		it "ignores comments" do
			@io.puts("# 127.0.0.1 foo.com")
			create
			@parser.ip_count.should == 0
			@parser.host_count.should == 0
		end
		
		it "ignores comments that come after leading spaces" do
			@io.puts("   # 127.0.0.1 foo.com")
			create
			@parser.ip_count.should == 0
			@parser.host_count.should == 0
		end
		
		it "ignores comments that come after leading tabs" do
			@io.puts("\t# 127.0.0.1 foo.com")
			create
			@parser.ip_count.should == 0
			@parser.host_count.should == 0
		end
		
		it "ignores empty lines" do
			@io.puts("127.0.0.1 foo.com")
			@io.puts
			@io.puts("127.0.0.1 bar.com")
			@io.puts("127.0.0.2 baz.com")
			create
			@parser.ip_count.should == 2
			@parser.host_count.should == 3
		end
		
		it "ignores leading and trailing spaces" do
			@io.puts("  127.0.0.1 foo.com")
			@io.puts
			@io.puts("  127.0.0.1 bar.com  ")
			@io.puts("127.0.0.2 baz.com  ")
			create
			@parser.ip_count.should == 2
			@parser.host_count.should == 3
			@parser.resolve("foo.com").should == "127.0.0.1"
			@parser.resolve("bar.com").should == "127.0.0.1"
			@parser.resolve("baz.com").should == "127.0.0.2"
		end
		
		it "ignores leading and trailing tabs" do
			@io.puts("\t\t127.0.0.1 foo.com")
			@io.puts
			@io.puts("\t127.0.0.1 bar.com\t")
			@io.puts("127.0.0.2 baz.com\t\t")
			create
			@parser.ip_count.should == 2
			@parser.host_count.should == 3
			@parser.resolve("foo.com").should == "127.0.0.1"
			@parser.resolve("bar.com").should == "127.0.0.1"
			@parser.resolve("baz.com").should == "127.0.0.2"
		end
		
		it "correctly handles spaces as seperators" do
			@io.puts("127.0.0.1 foo.com bar.com  baz.com")
			create
			@parser.ip_count.should == 1
			@parser.host_count.should == 3
			@parser.resolve("foo.com").should == "127.0.0.1"
			@parser.resolve("bar.com").should == "127.0.0.1"
			@parser.resolve("baz.com").should == "127.0.0.1"
		end
		
		it "correctly handles tabs as seperators" do
			@io.puts("127.0.0.1\tfoo.com\t\tbar.com baz.com")
			create
			@parser.ip_count.should == 1
			@parser.host_count.should == 3
			@parser.resolve("foo.com").should == "127.0.0.1"
			@parser.resolve("bar.com").should == "127.0.0.1"
			@parser.resolve("baz.com").should == "127.0.0.1"
		end
	end
	
	describe "#resolve" do
		it "returns nil if the host name is not in the file" do
			@io.puts("127.0.0.1 foo.com")
			create
			@parser.resolve("bar.com").should be_nil
		end
		
		it "returns the IP address associated with the host name if it exists" do
			@io.puts("255.255.255.255 foo.com")
			create
			@parser.resolve("foo.com").should == "255.255.255.255"
		end
		
		it "is case-insensitive" do
			@io.puts("255.255.255.255 fOO.com")
			create
			@parser.resolve("foo.COM").should == "255.255.255.255"
		end
		
		it "correctly handles lines that contain multiple host names" do
			@io.puts("255.255.255.255 foo.com bar.com")
			create
			@parser.resolve("foo.com").should == "255.255.255.255"
			@parser.resolve("bar.com").should == "255.255.255.255"
			@parser.resolve("baz.com").should be_nil
		end
		
		it "always returns 127.0.0.1 for localhost" do
			create
			@parser.resolve("localhost").should == "127.0.0.1"
			@parser.resolve("localHOST").should == "127.0.0.1"
		end
	end
	
	describe "#resolves_to_localhost?" do
		before :each do
			@io.puts "127.0.0.1 kotori"
			@io.puts "192.168.0.1 kanako"
			@io.puts "::1 ageha"
			@io.puts "::2 sawako"
			@io.puts "0.0.0.0 mizusawa"
			create
		end
		
		it "returns true if the host name resolves to 127.0.0.1" do
			@parser.resolves_to_localhost?("kotori").should be_true
		end
		
		it "returns true if the host name resolves to ::1" do
			@parser.resolves_to_localhost?("ageha").should be_true
		end
		
		it "returns true if the host name resolves to 0.0.0.0" do
			@parser.resolves_to_localhost?("mizusawa").should be_true
		end
		
		it "returns false if the host name resolves to something else" do
			@parser.resolves_to_localhost?("sawako").should be_false
			@parser.resolves_to_localhost?("kanako").should be_false
		end
		
		it "returns false if the host name does not resolve" do
			@parser.resolves_to_localhost?("foo.com").should be_false
		end
	end
	
	describe "#add_group_data" do
		before :each do
			@standard_entries =
				"127.0.0.1 kotori hazuki\n" +
				"  192.168.0.1 kanako\n\n" +
				"\t::1 ageha sawako\n" +
				"0.0.0.0 mizusawa naru\n"
			@target = StringIO.new
		end
		
		it "adds the group data if it doesn't exist" do
			@io.puts @standard_entries
			create
			
			@parser.add_group_data("some marker",
				"# a comment\n" +
				"127.0.0.1 foo\n")
			@parser.write(@target)
			@target.string.should ==
				@standard_entries +
				"###### BEGIN some marker ######\n" +
				"# a comment\n" +
				"127.0.0.1 foo\n" +
				"###### END some marker ######\n"
		end
		
		it "replaces the existing group data if it does exist" do
			@io.puts "###### BEGIN some marker ######\n" +
				"# another comment\n" +
				"127.0.0.1 bar\n" +
				"###### END some marker ######\n" +
				"\n" +
				@standard_entries +
				"###### BEGIN some other marker ######\n" +
				"# another comment\n" +
				"127.0.0.1 bar\n" +
				"###### END some other marker ######\n"
			create
			
			@parser.add_group_data("some marker", "127.0.0.1 foo\n")
			@parser.write(@target)
			@target.string.should ==
				"###### BEGIN some marker ######\n" +
				"127.0.0.1 foo\n" +
				"###### END some marker ######\n" +
				"\n" +
				@standard_entries +
				"###### BEGIN some other marker ######\n" +
				"# another comment\n" +
				"127.0.0.1 bar\n" +
				"###### END some other marker ######\n"
		end
		
		it "correctly handles the lack of a terminating newline in the group data" do
			@io.puts @standard_entries
			create
			
			@parser.add_group_data("some marker",
				"# a comment\n" +
				"127.0.0.1 foo")
			@parser.write(@target)
			@target.string.should ==
				@standard_entries +
				"###### BEGIN some marker ######\n" +
				"# a comment\n" +
				"127.0.0.1 foo\n" +
				"###### END some marker ######\n"
		end
		
		it "ensures that the group data starts at a new line" do
			@io.write "127.0.0.1 foo.com"
			create
			@parser.add_group_data("some marker", "127.0.0.1 bar.com")
			@parser.write(@target)
			@target.string.should ==
				"127.0.0.1 foo.com\n" +
				"###### BEGIN some marker ######\n" +
				"127.0.0.1 bar.com\n" +
				"###### END some marker ######\n"
		end
	end
	
	describe "#write" do
		it "preserves all comments and leading and trailing whitespaces" do
			@io.puts "127.0.0.1 foo.com  "
			@io.puts "# a comment"
			@io.puts "\t127.0.0.1  bar.com\t"
			create
			original_data = @io.string
			target = StringIO.new
			@parser.write(target)
			target.string.should == original_data
		end
	end
end

end # module PhusionPassenger
