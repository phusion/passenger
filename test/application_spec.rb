require 'support/config'
require 'passenger/application'
include Passenger

describe Application do
	it "should correctly detect Rails version numbers specified in environment.rb" do
		rails_version = Application.detect_framework_version('stub/railsapp')
		rails_version.should =~ /^2\.0\.(\d+)$/
	end
	
	it "should return :vendor if an application uses a vendored Rails" do
		rails_version = Application.detect_framework_version('stub/minimal-railsapp')
		rails_version.should == :vendor
	end
	
	it "should return nil if an application does not specify its Rails version" do
		rails_version = Application.detect_framework_version('stub/railsapp-without-version-spec')
		rails_version.should be_nil
	end
	
	it "should raise VersionNotFound if a nonexistant Rails version is specified" do
		detector = lambda { Application.detect_framework_version('stub/broken-railsapp4') }
		detector.should raise_error(VersionNotFound)
	end
end
