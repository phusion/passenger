require 'stringio'

shared_examples_for "handling errors in application initialization" do
	before :each do
		@stub = setup_rails_stub('foobar')
		@stub.use_vendor_rails('minimal')
	end
	
	after :each do
		teardown_rails_stub
	end

	it "raises an AppInitError if the spawned app raises a standard exception during startup" do
		File.prepend(@stub.environment_rb, "raise 'This is a dummy exception.'\n")
		begin
			spawn_stub_application(@stub, "print_exceptions" => false).close
			violated "Spawning the application should have raised an AppInitError."
		rescue AppInitError => e
			e.child_exception.message.should == "This is a dummy exception."
		end
	end
	
	it "raises an AppInitError if the spawned app raises a custom-defined exception during startup" do
		File.prepend(@stub.environment_rb, %{
			class MyError < StandardError
			end
			
			raise MyError, "This is a custom exception."
		})
		begin
			spawn_stub_application(@stub, "print_exceptions" => false).close
			violated "Spawning the application should have raised an AppInitError."
		rescue AppInitError => e
			e.child_exception.message.should == "This is a custom exception. (MyError)"
		end
	end
	
	it "raises an AppInitError if the spawned app calls exit() during startup" do
		File.prepend(@stub.environment_rb, "exit\n")
		begin
			spawn_stub_application(@stub, "print_exceptions" => false).close
			violated "Spawning the application should have raised an AppInitError."
		rescue AppInitError => e
			e.child_exception.class.should == SystemExit
		end
	end
	
	it "prints the exception to STDERR if the spawned app raised an error" do
		old_stderr = STDERR
		file = File.new('output.tmp', 'w+')
		begin
			Object.send(:remove_const, "STDERR") rescue nil
			Object.const_set("STDERR", file)
			
			File.prepend(@stub.environment_rb, "raise 'This is a dummy exception.'\n")
			block = lambda do
				spawn_stub_application(@stub).close
			end
			block.should raise_error(AppInitError)
			
			file.rewind
			data = file.read
			data.should =~ /spawn_stub_application/
			data.should =~ /spawner_error_handling_spec\.rb/
		ensure
			Object.send(:remove_const, "STDERR") rescue nil
			Object.const_set("STDERR", old_stderr)
			file.close rescue nil
			File.unlink('output.tmp') rescue nil
		end
	end
end

shared_examples_for "handling errors in framework initialization" do
	include Utils
	
	it "raises FrameworkInitError if the framework could not be loaded" do
		block = lambda do
			load_nonexistant_framework(:print_framework_loading_exceptions => false).close
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
			data.should =~ /spawner_error_handling_spec\.rb/
		ensure
			Object.send(:remove_const, "STDERR") rescue nil
			Object.const_set("STDERR", old_stderr)
			file.close rescue nil
			File.unlink('output.tmp') rescue nil
		end
	end
end
