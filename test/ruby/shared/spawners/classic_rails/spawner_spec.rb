require File.expand_path(File.dirname(__FILE__) + '/../../../spec_helper')

module PhusionPassenger

shared_examples_for "a Rails spawner" do
	it "sets RAILS_ENV" do
		after_start %q{
			File.write("rails_env.txt", RAILS_ENV)
		}
		app = spawn_some_application("environment" => "staging")
		File.read("#{app.app_root}/rails_env.txt").should == "staging"
	end
end

end # module PhusionPassenger
