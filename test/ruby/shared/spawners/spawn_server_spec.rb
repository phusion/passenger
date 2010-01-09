require File.expand_path(File.dirname(__FILE__) + '/../../spec_helper')
require 'ruby/shared/abstract_server_spec'

shared_examples_for "a spawn server" do
	it "raises an AbstractServer::ServerError if the server was killed" do
		Process.kill('SIGABRT', spawner.server_pid)
		spawning = lambda { spawn_some_application }
		spawning.should raise_error(AbstractServer::ServerError)
	end
	
	it "works correctly after a restart, if something went wrong" do
		before_start %q{
			File.touch("works.txt")
		}
		Process.kill('SIGABRT', spawner.server_pid)
		spawner.stop
		spawner.start
		app = spawn_some_application
		File.exist?("#{app.app_root}/works.txt").should be_true
	end
end