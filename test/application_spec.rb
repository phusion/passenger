require 'support/config'
require 'passenger/application'
include Passenger

describe Application do
	it "should correctly detect Rails version numbers specified in environment.rb" do
		rails_version = Application.detect_framework_version('stub/railsapp')
		rails_version.should =~ /^2\.0\.(\d+)$/
	end
	
	it "should correctly detect vendor Rails" do
		rails_version = Application.detect_framework_version('stub/minimal-railsapp')
		rails_version.should be_nil
	end
	
	it "should raise VersionNotFound if a nonexistant Rails version is specified" do
		detector = lambda { Application.detect_framework_version('stub/broken-railsapp4') }
		detector.should raise_error(VersionNotFound)
	end
end
