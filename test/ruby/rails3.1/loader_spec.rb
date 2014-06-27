require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
require 'ruby/shared/loader_sharedspec'
require 'ruby/shared/rails/union_station_extensions_sharedspec'

module PhusionPassenger

describe "Rack loader with Rails 3.1" do
	include LoaderSpecHelper

	before :each do
		@stub = register_stub(RackStub.new("rails3.1"))
	end

	def start(options = {})
		@loader = Loader.new(["ruby", "#{PhusionPassenger.helper_scripts_dir}/rack-loader.rb"], @stub.app_root)
		return @loader.start(options)
	end

	def rails_version
		return "3.1"
	end

	include_examples "Union Station extensions for Rails"
end

end # module PhusionPassenger
