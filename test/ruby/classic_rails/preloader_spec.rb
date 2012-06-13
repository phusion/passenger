require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
require 'ruby/shared/loader_spec'
require 'ruby/shared/classic_rails/loader_spec'

module PhusionPassenger

describe "Classic Rails 2.3 preloader" do
	include LoaderSpecHelper

	before :each do
		@stub = register_stub(ClassicRailsStub.new("rails2.3"))
	end

	def start
		@preloader = Preloader.new(["ruby", "#{PhusionPassenger.helper_scripts_dir}/classic-rails-preloader.rb"], @stub.app_root)
		result = @preloader.start
		if result[:status] == "Ready"
			@loader = @preloader.spawn
			return @loader.start
		else
			return result
		end
	end

	it_should_behave_like "a loader"
	it_should_behave_like "a classic Rails loader"
end

end # module PhusionPassenger
