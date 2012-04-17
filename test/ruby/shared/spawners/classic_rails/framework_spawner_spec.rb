require File.expand_path(File.dirname(__FILE__) + '/../../../spec_helper')

module PhusionPassenger

shared_examples_for "a ClassicRails::FrameworkSpawner" do
	it "raises FrameworkInitError if the framework could not be loaded" do
		block = lambda do
			load_nonexistant_framework("print_framework_loading_exceptions" => false).close
		end
		block.should raise_error(FrameworkInitError)
	end
	
	it "prints the exception to STDERR if the framework could not be loaded" do
		old_stderr = STDERR
		file = File.new('output.tmp', 'w+')
		begin
			Object.send(:remove_const, "STDERR") rescue nil
			Object.const_set("STDERR", file)
			
			block = lambda do
				load_nonexistant_framework.close
			end
			block.should raise_error(FrameworkInitError)
			
			file.rewind
			data = file.read
			data.should =~ /load_nonexistant_framework/
			data.should =~ /framework_spawner_spec\.rb/
		ensure
			Object.send(:remove_const, "STDERR") rescue nil
			Object.const_set("STDERR", old_stderr)
			file.close rescue nil
			File.unlink('output.tmp') rescue nil
		end
	end
end

end # module PhusionPassenger
