require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')

PhusionPassenger.require_passenger_lib 'admin_tools'
PhusionPassenger.require_passenger_lib 'admin_tools/instance_registry'

module PhusionPassenger

  describe AdminTools::InstanceRegistry do

    specify "List of instances does not contain duplicates" do
      as_is = AdminTools::InstanceRegistry.new(["/tmp","/tmp"]).list.map{|i| i.path}
      puts as_is
      deduped = as_is.uniq
      as_is.should == deduped
    end

  end

end # module PhusionPassenger
