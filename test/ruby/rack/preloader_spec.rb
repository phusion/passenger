require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
require 'ruby/shared/loader_sharedspec'
require 'ruby/shared/ruby_loader_sharedspec'

module PhusionPassenger

describe "Rack preloader" do
	include LoaderSpecHelper

	before :each do
		@stub = register_stub(RackStub.new("rack"))
	end

	def start(options = {})
		@preloader = Preloader.new(["ruby", "#{PhusionPassenger.helper_scripts_dir}/rack-preloader.rb"], @stub.app_root)
		result = @preloader.start(options)
		if result[:status] == "Ready"
			@loader = @preloader.spawn(options)
			return @loader.start(options)
		else
			return result
		end
	end

	it_should_behave_like "a loader"
	it_should_behave_like "a Ruby loader"

	it "calls the starting_worker_process event with forked=true" do
		File.prepend(@stub.startup_file, %q{
			history_file = "history.txt"
			PhusionPassenger.on_event(:starting_worker_process) do |forked|
				::File.open(history_file, 'a') do |f|
					f.puts "worker_process_started: forked=#{forked}\n"
				end
			end
			::File.open(history_file, 'a') do |f|
				f.puts "end of startup file\n"
			end
		})
		result = start
		result[:status].should == "Ready"
		File.read("#{@stub.app_root}/history.txt").should ==
			"end of startup file\n" +
			"worker_process_started: forked=true\n"
	end
end

end # module PhusionPassenger
