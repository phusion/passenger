require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
require 'phusion_passenger/railz/application_spawner'

require 'ruby/shared/abstract_server_spec'
require 'ruby/shared/spawners/spawn_server_spec'
require 'ruby/shared/spawners/spawner_spec'
require 'ruby/shared/spawners/rails/spawner_spec'

describe Railz::ApplicationSpawner do
	include SpawnerSpecHelper
	
	after :each do
		@spawner.stop if @spawner && @spawner.started?
	end
	
	describe "conservative spawning" do
		def spawn_some_application(extra_options = {})
			stub = register_stub(RailsStub.new('foobar'))
			yield stub if block_given?
			
			default_options = {
				"app_root"    => stub.app_root,
				"lowest_user" => CONFIG['lowest_user']
			}
			options = default_options.merge(extra_options)
			@spawner ||= Railz::ApplicationSpawner.new(options)
			app = @spawner.spawn_application!(options)
			return register_app(app)
		end
		
		it_should_behave_like "a spawner"
		it_should_behave_like "a Rails spawner"
		
		specify "the starting_worker_process event is called with forked=false" do
			after_start %q{
				history_file = "#{PhusionPassenger::Utils.passenger_tmpdir}/history.txt"
				PhusionPassenger.on_event(:starting_worker_process) do |forked|
					::File.append(history_file, "forked = #{forked}\n")
				end
				::File.append(history_file, "end of environment.rb\n");
			}
			
			spawn_some_application
			spawn_some_application
			
			history_file = "#{PhusionPassenger::Utils.passenger_tmpdir}/history.txt"
			eventually(2, 0.5) do
				contents = File.read(history_file)
				lines = contents.split("\n")
				lines.count("forked = false") == 2
			end
		end
	end
	
	describe "smart spawning" do
		def server
			return spawner
		end
		
		def spawner
			@spawner ||= begin
				stub = register_stub(RailsStub.new('foobar'))
				spawner = Railz::ApplicationSpawner.new("app_root" => stub.app_root)
				spawner.start
				spawner
			end
		end
		
		def spawn_some_application(extra_options = {})
			stub = register_stub(RailsStub.new('foobar'))
			yield stub if block_given?
			
			default_options = {
				"app_root"    => stub.app_root,
				"lowest_user" => CONFIG['lowest_user']
			}
			options = default_options.merge(extra_options)
			@spawner ||= begin
				spawner = Railz::ApplicationSpawner.new(options)
				spawner.start
				spawner
			end
			app = @spawner.spawn_application(options)
			return register_app(app)
		end
		
		it_should_behave_like "an AbstractServer"
		it_should_behave_like "a spawn server"
		it_should_behave_like "a spawner"
		it_should_behave_like "a Rails spawner"
		
		specify "the starting_worker_process event is called with forked=true" do
			after_start %q{
				history_file = "#{PhusionPassenger::Utils.passenger_tmpdir}/history.txt"
				PhusionPassenger.on_event(:starting_worker_process) do |forked|
					::File.append(history_file, "forked = #{forked}\n")
				end
				::File.append(history_file, "end of environment.rb\n");
			}
			
			spawn_some_application
			spawn_some_application
			
			history_file = "#{PhusionPassenger::Utils.passenger_tmpdir}/history.txt"
			eventually(2, 0.5) do
				contents = File.read(history_file)
				contents ==
					"end of environment.rb\n" +
					"forked = true\n" +
					"forked = true\n"
			end
		end
	end
end
