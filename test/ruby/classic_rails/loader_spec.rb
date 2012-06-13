# encoding: binary
require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
require 'ruby/shared/loader_spec'

module PhusionPassenger

describe "Classic Rails 2.3 loader" do
	include LoaderSpecHelper

	before :each do
		@stub = ClassicRailsStub.new("rails2.3")
		register_stub(@stub)
	end

	after :each do
		@loader.close if @loader
	end

	def start
		@loader = Loader.new(["ruby", "#{PhusionPassenger.helper_scripts_dir}/classic-rails-loader.rb"], @stub.app_root)
	end

	it_should_behave_like "a loader"

	it "prints an error page if the startup file fails to load" do
		File.write(@stub.environment_rb, %q{
			raise "oh no!"
		})
		result = start.negotiate_startup
		result[:status].should == "Error"
		result[:body].should include("oh no!")
	end

	it "calls the starting_worker_process event after the startup file has been loaded" do
		File.prepend(@stub.environment_rb, %q{
			history_file = "history.txt"
			PhusionPassenger.on_event(:starting_worker_process) do |forked|
				::File.open(history_file, 'a') do |f|
					f.puts "worker_process_started\n"
				end
			end
			::File.open(history_file, 'a') do |f|
				f.puts "end of startup file\n"
			end
		})
		result = start.negotiate_startup
		result[:status].should == "Ready"
		File.read("#{@stub.app_root}/history.txt").should ==
			"end of startup file\n" +
			"worker_process_started\n"
	end

	it "calls the stopping_worker_process event on exit" do
		File.prepend(@stub.environment_rb, %q{
			history_file = "history.txt"
			PhusionPassenger.on_event(:stopping_worker_process) do
				::File.open(history_file, 'a') do |f|
					f.puts "worker_process_stopped\n"
				end
			end
			::File.open(history_file, 'a') do |f|
				f.puts "end of startup file\n"
			end
		})
		result = start.negotiate_startup
		result[:status].should == "Ready"
		@loader.input.close
		eventually do
			File.read("#{@stub.app_root}/history.txt") ==
				"end of startup file\n" +
				"worker_process_stopped\n"
		end
	end
end

end
