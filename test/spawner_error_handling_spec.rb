shared_examples_for "handling errors in application initialization" do
	it "should raise an AppInitError if the spawned app raises a standard exception during startup" do
		begin
			spawn_application('stub/broken-railsapp')
			violated "Spawning the application should have raised an InitializationError."
		rescue AppInitError => e
			e.child_exception.message.should == "This is a dummy exception."
		end
	end
	
	it "should raise an AppInitError if the spawned app raises a custom-defined exception during startup" do
		begin
			spawn_application('stub/broken-railsapp3')
			violated "Spawning the application should have raised an InitializationError."
		rescue AppInitError => e
			e.child_exception.message.should == "This is a custom exception. (MyError)"
		end
	end
	
	it "should raise an AppInitError if the spawned app calls exit() during startup" do
		begin
			spawn_application('stub/broken-railsapp2')
			violated "Spawning the application should have raised an InitializationError."
		rescue AppInitError => e
			e.child_exception.should be_nil
		end
	end
end

shared_examples_for "handling errors in framework initialization" do
	include Utils
	it "should raise FrameworkInitError if the framework could not be loaded" do
		lambda { load_nonexistant_framework }.should raise_error(FrameworkInitError)
	end
end
