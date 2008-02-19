shared_examples_for "AbstractServer" do
	it "should not crash if it's started and stopped multiple times" do
		3.times do
			# Give the server some time to install the
			# signal handlers. If we don't give it enough
			# time, it will raise an ugly exception when
			# we send it a signal.
			sleep 0.1
			@server.stop
			@server.start
		end
	end
end
