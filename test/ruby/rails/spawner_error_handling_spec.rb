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
			spawn_stub_application(@stub).close
			violated "Spawning the application should have raised an InitializationError."
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
			spawn_stub_application(@stub).close
			violated "Spawning the application should have raised an InitializationError."
		rescue AppInitError => e
			e.child_exception.message.should == "This is a custom exception. (MyError)"
		end
	end
	
	it "raises an AppInitError if the spawned app calls exit() during startup" do
		File.prepend(@stub.environment_rb, "exit\n")
		begin
			spawn_stub_application(@stub).close
			violated "Spawning the application should have raised an InitializationError."
		rescue AppInitError => e
			e.child_exception.class.should == SystemExit
		end
	end
end

shared_examples_for "handling errors in framework initialization" do
	include Utils
	it "raises FrameworkInitError if the framework could not be loaded" do
		lambda { load_nonexistant_framework.close }.should raise_error(FrameworkInitError)
	end
end
