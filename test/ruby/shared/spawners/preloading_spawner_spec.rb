require File.expand_path(File.dirname(__FILE__) + '/../../spec_helper')

module PhusionPassenger

shared_examples_for "a spawner that preloads app code" do
	specify "the starting_worker_process event is called with forked=true" do
		after_start %q{
			history_file = "#{PhusionPassenger::Utils.passenger_tmpdir}/history.txt"
			PhusionPassenger.on_event(:starting_worker_process) do |forked|
				::File.append(history_file, "forked = #{forked}\n")
			end
			::File.append(history_file, "end of environment.rb\n");
		}
		
		spawn_some_application
		spawn_some_application
		
		history_file = "#{PhusionPassenger::Utils.passenger_tmpdir}/history.txt"
		eventually do
			contents = File.read(history_file)
			contents ==
				"end of environment.rb\n" +
				"forked = true\n" +
				"forked = true\n"
		end
	end
end

end # module PhusionPassenger
