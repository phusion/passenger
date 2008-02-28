require 'support/config'
require 'mod_rails/application'
include ModRails

describe Application do
	it "should correctly detect Rails version numbers specified in environment.rb" do
		rails_version = Application.detect_framework_version('stub/railsapp')
		rails_version.should =~ /^2\.0\.(\d+)$/
	end
	
	it "should correctly detect vendor Rails" do
		rails_version = Application.detect_framework_version('stub/minimal-railsapp')
		rails_version.should be_nil
	end
end
