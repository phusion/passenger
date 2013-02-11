module PhusionPassenger

shared_examples_for "a Ruby loader" do
	it "prints an error page if the startup file fails to load" do
		File.write(@stub.startup_file, %q{
			raise "oh no!"
		})
		result = start
		result[:status].should == "Error"
		result[:body].should include("oh no!")
	end

	it "calls the starting_worker_process event after the startup file has been loaded" do
		File.prepend(@stub.startup_file, %q{
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
		result = start
		result[:status].should == "Ready"
		File.read("#{@stub.app_root}/history.txt").should ==
			"end of startup file\n" +
			"worker_process_started\n"
	end

	it "calls the stopping_worker_process event on exit" do
		File.prepend(@stub.startup_file, %q{
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
		result = start
		result[:status].should == "Ready"
		@loader.input.close_write
		eventually(3) do
			File.read("#{@stub.app_root}/history.txt") ==
				"end of startup file\n" +
				"worker_process_stopped\n"
		end
	end
end

end # module PhusionPassenger
