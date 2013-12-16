require File.expand_path(File.dirname(__FILE__) + '/../spec_helper')
PhusionPassenger.require_passenger_lib 'utils/file_system_watcher'

module PhusionPassenger

describe Utils::FileSystemWatcher do
	before :each do
		@tmpdir = "tmp.fs_watcher"
		@tmpdir2 = "tmp.fs_watcher2"
		@tmpdir3 = "tmp.fs_watcher3"
		@term_pipe = IO.pipe
		[@tmpdir, @tmpdir2, @tmpdir3].each do |dir|
			remove_dir_tree(dir)
			Dir.mkdir(dir)
		end
	end
	
	after :each do
		if @thread
			@term_pipe[1].write("x")
			@thread.join
		end
		@watcher.close if @watcher
		@term_pipe[0].close
		@term_pipe[1].close
		[@tmpdir, @tmpdir2, @tmpdir3].each do |dir|
			remove_dir_tree(dir)
		end
	end
	
	def create(*args)
		@watcher = Utils::FileSystemWatcher.new(*args)
		@watcher.poll_interval = 0.1 if @watcher.respond_to?(:poll_interval=)
		return @watcher
	end
	
	describe "#wait_for_change blocks until" do
		def test_block(filenames)
			create(filenames, @term_pipe[0])
			result = nil
			thread = Thread.new do
				result = @watcher.wait_for_change
			end
			yield if block_given?
			eventually do
				!thread.alive?
			end
			return result
		ensure
			if thread
				@term_pipe[1].write("x")
				thread.join
			end
			@watcher.close
		end
		
		specify "a subdirectory has been created in one of the watched directories" do
			result = test_block([@tmpdir, @tmpdir2]) do
				Dir.mkdir("#{@tmpdir}/foo")
			end
			result.should be_true
		end
		
		specify "a subdirectory has been removed in one of the watched directories" do
			Dir.mkdir("#{@tmpdir2}/foo")
			result = test_block([@tmpdir, @tmpdir2]) do
				Dir.rmdir("#{@tmpdir2}/foo")
			end
			result.should be_true
		end
		
		specify "a subdirectory has been renamed in one of the watched directories" do
			Dir.mkdir("#{@tmpdir}/foo")
			result = test_block([@tmpdir, @tmpdir2]) do
				File.rename("#{@tmpdir}/foo", "#{@tmpdir3}/bar")
			end
			result.should be_true
		end
		
		specify "a file has been created in one of the watched directories" do
			result = test_block([@tmpdir, @tmpdir2]) do
				File.touch("#{@tmpdir}/foo")
			end
			result.should be_true
		end
		
		specify "a file has been removed in one of the watched directories" do
			File.touch("#{@tmpdir2}/foo")
			result = test_block([@tmpdir, @tmpdir2]) do
				File.unlink("#{@tmpdir2}/foo")
			end
			result.should be_true
		end
		
		specify "a file has been renamed in one of the watched directories" do
			File.touch("#{@tmpdir}/foo")
			result = test_block([@tmpdir, @tmpdir2]) do
				File.rename("#{@tmpdir}/foo", "#{@tmpdir3}/bar")
			end
			result.should be_true
		end
		
		specify "a watched file has been written to" do
			File.touch("#{@tmpdir}/foo")
			result = test_block(["#{@tmpdir}/foo"]) do
				File.write("#{@tmpdir}/foo", "bar")
			end
		end
		
		specify "a watched file has been truncated" do
			File.write("#{@tmpdir}/foo", "contents")
			result = test_block(["#{@tmpdir}/foo"]) do
				if RUBY_PLATFORM =~ /darwin/
					# OS X kernel bug in kqueue... sigh...
					File.open("#{@tmpdir}/foo", "w") do |f|
						f.write("a")
						f.truncate(0)
					end
				else
					File.open("#{@tmpdir}/foo", "w").close
				end
			end
		end
		
		specify "a watched file has been removed" do
			File.touch("#{@tmpdir}/foo")
			result = test_block(["#{@tmpdir}/foo"]) do
				File.unlink("#{@tmpdir}/foo")
			end
		end
		
		specify "a watched file has been renamed" do
			File.touch("#{@tmpdir}/foo")
			result = test_block(["#{@tmpdir}/foo"]) do
				File.rename("#{@tmpdir}/foo", "#{@tmpdir}/bar")
			end
		end
		
		specify "the termination pipe became readable" do
			result = test_block([@tmpdir]) do
				@term_pipe[1].write("x")
			end
			result.should be_nil
		end
		
		specify "one of the watched files or directories could not be statted while constructing the object" do
			test_block([@tmpdir, "#{@tmpdir}/foo"]).should be_false
			
			when_not_running_as_root do
				Dir.mkdir("#{@tmpdir}/foo")
				File.touch("#{@tmpdir}/foo/file")
				Dir.mkdir("#{@tmpdir}/foo/dir")
				File.chmod(0000, "#{@tmpdir}/foo")
				
				test_block([@tmpdir, "#{@tmpdir}/foo/file"]).should be_false
				test_block([@tmpdir, "#{@tmpdir}/foo/dir"]).should be_false
			end
		end
		
		if Utils::FileSystemWatcher.opens_files?
		when_not_running_as_root do
			specify "one of the watched files or directories could not be opened while constructing the object" do
				File.touch("#{@tmpdir}/file")
				File.chmod(0000, "#{@tmpdir}/file")
				test_block([@tmpdir, "#{@tmpdir}/file"]).should be_false
				
				Dir.mkdir("#{@tmpdir}/dir")
				File.chmod(0000, "#{@tmpdir}/dir")
				test_block([@tmpdir, "#{@tmpdir}/dir"]).should be_false
			end
		end
		end # if
	end
	
	describe "#wait_for_change does not return if" do
		def test_block(filenames)
			create(filenames, @term_pipe[0])
			@thread = Thread.new do
				@watcher.wait_for_change
			end
			yield
			should_never_happen(0.4) do
				!@thread.alive?
			end
		end
		
		specify "nothing happened in one of its watched files or directories" do
			test_block([@tmpdir, @tmpdir2]) do
				File.touch("#{@tmpdir3}/file")
				Dir.mkdir("#{@tmpdir3}/dir")
			end
		end
		
		specify "something happened in a subdirectory that isn't on the watch list" do
			# In other words it does not watch subdirectories recursively.
			Dir.mkdir("#{@tmpdir}/subdir")
			test_block([@tmpdir, @tmpdir2]) do
				File.touch("#{@tmpdir}/subdir/file")
			end
		end
		
		specify "a file in a watched directory is merely modified" do
			File.touch("#{@tmpdir}/hello", 10)
			test_block([@tmpdir, @tmpdir2]) do
				File.touch("#{@tmpdir}/hello", 4567)
				File.write("#{@tmpdir}/hello", "foobar")
			end
		end
	end
	
	specify "#wait_for_change notices events that have occurred after object construction but before #wait_for_change has been called" do
		create([@tmpdir, @tmpdir2], @term_pipe[0])
		@thread = Thread.new do
			@watcher.wait_for_change
		end
		File.touch("#{@tmpdir}/foo", Time.now - 10)
		eventually do
			!@thread.alive?
		end
	end
	
	it "can be closed multiple times" do
		create([@tmpdir])
		@watcher.close
		@watcher.close
	end
end

end # module PhusionPassenger
