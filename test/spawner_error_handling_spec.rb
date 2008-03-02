shared_examples_for "a spawner that correctly handles errors" do
	it "should raise an InitializationError if the spawned app raises a standard exception during startup" do
		begin
			spawn_application('stub/broken-railsapp')
			violated "Spawning the application should have raised an InitializationError."
		rescue InitializationError => e
			e.child_exception.message.should == "This is a dummy exception."
		end
	end
	
	it "should raise an InitializationError if the spawned app raises a custom-defined exception during startup" do
		begin
			spawn_application('stub/broken-railsapp3')
			violated "Spawning the application should have raised an InitializationError."
		rescue InitializationError => e
			e.child_exception.message.should == "This is a custom exception. (MyError)"
		end
	end
	
	it "should raise an InitializationError if the spawned app calls exit() during startup" do
		begin
			spawn_application('stub/broken-railsapp2')
			violated "Spawning the application should have raised an InitializationError."
		rescue InitializationError => e
			e.child_exception.should be_nil
		end
	end
end
