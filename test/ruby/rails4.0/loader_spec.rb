require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
require 'ruby/shared/loader_sharedspec'
require 'ruby/shared/rails/union_station_extensions_sharedspec'

if RUBY_VERSION_INT >= 190
module PhusionPassenger

describe "Rack loader with Rails 4.0" do
	include LoaderSpecHelper

	before :each do
		@stub = register_stub(RackStub.new("rails4.0"))
	end

	def start(options = {})
		@loader = Loader.new(["ruby", "#{PhusionPassenger.helper_scripts_dir}/rack-loader.rb"], @stub.app_root)
		return @loader.start(options)
	end

	def rails_version
		return "4.0"
	end

	include_examples "Union Station extensions for Rails"
end

end # module PhusionPassenger
end
