source_root = File.expand_path("../..", File.dirname(__FILE__))
$LOAD_PATH.unshift("#{source_root}/lib")
require 'phusion_passenger'
PhusionPassenger.locate_directories

ENV['PATH'] = "#{PhusionPassenger.bin_dir}:#{ENV['PATH']}"
# This environment variable changes Passenger Standalone's behavior,
# so ensure that it's not set.
ENV.delete('PASSENGER_DEBUG')

describe "Passenger Standalone" do
	def capture_output(command)
		output = `#{command}`.strip
		if $?.exitstatus == 0
			return output
		else
			abort "Command #{command} exited with status #{$?.exitstatus}"
		end
	end

	specify "invoking 'passenger' without an argument is equivalent to 'passenger help'" do
		output = capture_output("passenger")
		output.should == capture_output("passenger help")
	end

	specify "'passenger --help' is equivalent to 'passenger help'" do
		output = capture_output("passenger")
		output.should == capture_output("passenger help")
	end

	specify "'passenger --version' displays the version number" do
		output = capture_output("passenger --version")
		output.should include("version #{PhusionPassenger::VERSION_STRING}\n")
	end

	describe "start command" do
		context "if the runtime is not installed" do
			context "when originally packaged" do
				it "downloads the runtime from the Internet"
				it "builds the runtime downloading fails"
				it "aborts if runtime building fails"
			end

			context "when natively packaged" do
				it "doesn't download the runtime from the Internet"
				it "doesn't build the runtime"
				it "aborts with an error"
			end
		end

		context "if the runtime is installed" do
			it "doesn't download the runtime from the Internet"
			it "doesn't build the runtime"
		end

		it "starts a server which serves the application"
		it "daemonizes if -d is given"
	end

	describe "stop command" do
		# TODO
	end

	describe "status command" do
		# TODO
	end

	describe "help command" do
		it "displays the available commands" do
			capture_output("passenger help").should include("Available commands:")
		end
	end
end
