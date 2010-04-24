require File.expand_path(File.dirname(__FILE__) + '/../../../spec_helper')

module PhusionPassenger

shared_examples_for "a Rails app that lacks RAILS_GEM_VERSION" do
	it "loads a random Rails version if the app doesn't specify RAILS_GEM_VERSION" do
		after_start %q{
			File.write("rails_version.txt", Rails::VERSION::STRING)
		}
		app = spawn_some_application do |stub|
			File.write(stub.environment_rb) do |content|
				content.sub(/^RAILS_GEM_VERSION = .*$/, '')
			end
		end
		File.read("#{app.app_root}/rails_version.txt").should =~ /^(\d+.)+\d+$/
	end
end

end # module PhusionPassenger
