require File.expand_path(File.dirname(__FILE__) + '/spec_helper')
require 'tmpdir'
require 'fileutils'
require 'stringio'
require 'etc'
require 'phusion_passenger/message_channel'
require 'phusion_passenger/platform_info/ruby'
require 'phusion_passenger/utils'

require 'ruby/shared/utils/pseudo_io_spec'

module PhusionPassenger

describe Utils do
	include Utils
	
	specify "#close_all_io_objects_for_fds closes all IO objects that are associated with the given file descriptors" do
		filename = "#{Dir.tmpdir}/passenger_test.#{Process.pid}.txt"
		begin
			pid = safe_fork('utils_spec') do
				a, b = IO.pipe
				close_all_io_objects_for_fds([0, 1, 2])
				File.open(filename, "w") do |f|
					f.write("#{a.closed?}, #{b.closed?}")
				end
			end
			Process.waitpid(pid) rescue nil
			File.read(filename).should == "true, true"
		ensure
			File.unlink(filename) rescue nil
		end
	end
	
	describe "#report_app_init_status" do
		it "reports normal errors, which #unmarshal_and_raise_errors raises" do
			a, b = IO.pipe
			begin
				pid = safe_fork('utils_spec') do
					a.close
					report_app_init_status(MessageChannel.new(b)) do
						raise RuntimeError, "hello world"
					end
				end
				b.close
				lambda { unmarshal_and_raise_errors(MessageChannel.new(a)) }.should raise_error(/hello world/)
			ensure
				a.close rescue nil
				b.close rescue nil
			end
		end
		
		it "reports SystemExit errors, which #unmarshal_and_raise_errors raises" do
			a, b = IO.pipe
			begin
				pid = safe_fork('utils_spec') do
					a.close
					report_app_init_status(MessageChannel.new(b)) do
						exit
					end
				end
				b.close
				lambda { unmarshal_and_raise_errors(MessageChannel.new(a)) }.should raise_error(/exited during startup/)
			ensure
				a.close rescue nil
				b.close rescue nil
			end
		end
		
		it "returns whether the block succeeded" do
			channel = MessageChannel.new(StringIO.new)
			success = report_app_init_status(channel) do
				false
			end
			success.should be_true
			
			success = report_app_init_status(channel) do
				raise StandardError, "hi"
			end
			success.should be_false
		end
		
		it "reports all data written to STDERR and $stderr" do
			a, b = IO.pipe
			begin
				pid = safe_fork('utils_spec') do
					a.close
					report_app_init_status(MessageChannel.new(b), nil) do
						STDERR.puts "Something went wrong!"
						$stderr.puts "Something went wrong again!"
						exit
					end
				end
				b.close
				
				begin
					unmarshal_and_raise_errors(MessageChannel.new(a))
					violated "No exception raised"
				rescue AppInitError => e
					e.stderr.should =~ /Something went wrong!/
					e.stderr.should =~ /Something went wrong again!/
				end
			ensure
				a.close rescue nil
				b.close rescue nil
			end
		end
		
		it "reports all data written to STDERR and $stderr even if it was reopened" do
			a, b = IO.pipe
			begin
				pid = safe_fork('utils_spec') do
					a.close
					report_app_init_status(MessageChannel.new(b), nil) do
						STDERR.puts "Something went wrong!"
						STDERR.reopen("output.tmp", "w")
						STDERR.puts "Something went wrong again!"
						STDERR.flush
						$stderr.puts "Something went wrong yet again!"
						$stderr.flush
						exit
					end
				end
				b.close
				
				begin
					unmarshal_and_raise_errors(MessageChannel.new(a))
					violated "No exception raised"
				rescue AppInitError => e
					e.stderr.should =~ /Something went wrong!/
					e.stderr.should =~ /Something went wrong again!/
					e.stderr.should =~ /Something went wrong yet again!/
				end
				
				file_contents = File.read("output.tmp")
				file_contents.should =~ /Something went wrong again!/
				file_contents.should =~ /Something went wrong yet again!/
			ensure
				a.close rescue nil
				b.close rescue nil
				File.unlink("output.tmp") rescue nil
			end
		end
		
		describe "if the block failed" do
			before :each do
				@raise_error = true
			end
			
			it_should_behave_like "a pseudo stderr created by #report_app_init_status"
		end
		
		describe "if the block succeeded" do
			it_should_behave_like "a pseudo stderr created by #report_app_init_status"
		end
	end
	
	specify "#safe_fork with double_fork == false reseeds the pseudo-random number generator" do
		a, b = IO.pipe
		begin
			pid = safe_fork do
				b.puts(rand)
			end
			Process.waitpid(pid) rescue nil
			pid = safe_fork do
				b.puts(rand)
			end
			Process.waitpid(pid) rescue nil
			
			first_num = a.readline
			second_num = a.readline
			first_num.should_not == second_num
		ensure
			a.close rescue nil
			b.close rescue nil
		end
	end
	
	specify "#safe_fork with double_fork == true reseeds the pseudo-random number generator" do
		a, b = IO.pipe
		begin
			# Seed the pseudo-random number generator here
			# so that it doesn't happen in the child processes.
			srand
			
			safe_fork(self.class, true) do
				b.puts(rand)
			end
			safe_fork(self.class, true) do
				b.puts(rand)
			end
			
			first_num = a.readline
			second_num = a.readline
			first_num.should_not == second_num
		ensure
			a.close rescue nil
			b.close rescue nil
		end
	end
	
	describe "#unmarshal_and_raise_errors" do
		before :each do
			@a, @b = IO.pipe
			@report_channel = MessageChannel.new(@a)
			report_app_init_status(MessageChannel.new(@b)) do
				raise StandardError, "Something went wrong!"
			end
		end
		
		after :each do
			@a.close rescue nil
			@b.close rescue nil
		end
		
		it "prints the exception information to the 'print_exception' argument using #puts, if 'print_exception' responds to that" do
			buffer = StringIO.new
			lambda { unmarshal_and_raise_errors(@report_channel, buffer) }.should raise_error(AppInitError)
			buffer.string.should =~ /Something went wrong!/
			buffer.string.should =~ /utils\.rb/
			buffer.string.should =~ /utils_spec\.rb/
		end
		
		it "appends the exception information to the file pointed to by 'print_exception', if 'print_exception' responds to #to_str" do
			begin
				lambda { unmarshal_and_raise_errors(@report_channel, "exception.txt") }.should raise_error(AppInitError)
				data = File.read('exception.txt')
				data.should =~ /Something went wrong!/
				data.should =~ /utils\.rb/
				data.should =~ /utils_spec\.rb/
			ensure
				File.unlink('exception.txt') rescue nil
			end
		end
	end
	
	specify "#to_boolean works" do
		to_boolean(nil).should be_false
		to_boolean(false).should be_false
		to_boolean(true).should be_true
		to_boolean(1).should be_true
		to_boolean(0).should be_true
		to_boolean("").should be_true
		to_boolean("true").should be_true
		to_boolean("false").should be_false
		to_boolean("bla bla").should be_true
	end
	
	specify "#split_by_null_into_hash works" do
		split_by_null_into_hash("").should == {}
		split_by_null_into_hash("foo\0bar\0").should == { "foo" => "bar" }
		split_by_null_into_hash("foo\0\0bar\0baz\0").should == { "foo" => "", "bar" => "baz" }
		split_by_null_into_hash("foo\0bar\0baz\0\0").should == { "foo" => "bar", "baz" => "" }
		split_by_null_into_hash("\0\0").should == { "" => "" }
	end
	
	describe "#passenger_tmpdir" do
		before :each do
			@old_passenger_tmpdir = Utils.passenger_tmpdir
			Utils.passenger_tmpdir = nil
		end
		
		after :each do
			Utils.passenger_tmpdir = @old_passenger_tmpdir
		end
		
		it "returns a directory under /tmp if Utils.passenger_tmpdir is nil" do
			File.dirname(passenger_tmpdir(false)).should == "/tmp"
		end
		
		it "returns a directory under /tmp if Utils.passenger_tmpdir is an empty string" do
			Utils.passenger_tmpdir = ''
			File.dirname(passenger_tmpdir(false)).should == "/tmp"
		end
		
		it "returns Utils.passenger_tmpdir if it's set" do
			Utils.passenger_tmpdir = '/foo'
			passenger_tmpdir(false).should == '/foo'
		end
		
		it "creates the directory if it doesn't exist, if the 'create' argument is true" do
			Utils.passenger_tmpdir = 'utils_spec.tmp'
			passenger_tmpdir
			begin
				File.directory?('utils_spec.tmp').should be_true
			ensure
				FileUtils.chmod_R(0777, 'utils_spec.tmp')
				FileUtils.rm_rf('utils_spec.tmp')
			end
		end
	end
	
	when_user_switching_possible do
		describe "#lower_privilege" do
			before :each do
				@options = {
					"default_user"  => CONFIG["default_user"],
					"default_group" => CONFIG["default_group"]
				}
				@startup_file = "tmp.startup_file"
				@startup_file_target = "tmp.startup_file_target"
				File.symlink(@startup_file_target, @startup_file)
				File.touch(@startup_file_target)
			end
			
			after :each do
				File.unlink(@startup_file) rescue nil
				File.unlink(@startup_file_target) rescue nil
			end
			
			def run(options = {})
				script = %q{
					require 'phusion_passenger/utils'
					include PhusionPassenger::Utils
					options = Marshal.load(ARGV[0].unpack('m').first)
					startup_file = ARGV[1]
					begin
						lower_privilege(startup_file, options)
						puts "success"
						puts Process.uid
						puts Process.gid
						puts `groups`
						puts ENV["HOME"]
						puts ENV["USER"]
					rescue => e
						puts "error"
						puts e
					end
				}.strip
				data = Marshal.dump(@options.merge(options))
				output = run_script(script, [data].pack('m'), @startup_file)
				lines = output.split("\n")
				status = lines.shift
				if status == "success"
					@uid, @gid, @groups, @env_home, @env_user = lines
					@uid = @uid.to_i
					@gid = @gid.to_i
					@username = Etc.getpwuid(@uid).name
					@groupname = Etc.getgrgid(@gid).name
				else
					@error = lines[0]
				end
			end
			
			def primary_group_for(username)
				gid = Etc.getpwnam(username).gid
				return Etc.getgrgid(gid).name
			end

			def uid_for(username)
				return Etc.getpwnam(username).uid
			end
			
			def gid_for(group_name)
				return Etc.getgrnam(group_name).gid
			end
			
			def group_name_for_gid(gid)
				return Etc.getgrgid(gid).name
			end
			
			describe "if 'user' is given" do
				describe "and 'user' is 'root'" do
					before :each do
						@options["user"] = "root"
					end
					
					it "changes the user to the value of 'default_user'" do
						run
						@username.should == CONFIG["default_user"]
					end
					
					specify "if 'group' is given, it changes group to the given group name" do
						run("group" => CONFIG["normal_group_1"])
						@groupname.should == CONFIG["normal_group_1"]
					end
					
					specify "if 'group' is set to the root group, it changes group to default_group" do
						run("group" => group_name_for_gid(0))
						@groupname.should == CONFIG["default_group"]
					end
					
					describe "and 'group' is set to '!STARTUP_FILE!'" do
						before :each do
							@options["group"] = "!STARTUP_FILE!"
						end
						
						it "changes the group to the startup file's group" do
							File.lchown(-1,
								gid_for(CONFIG["normal_group_1"]),
								@startup_file)
							run
							@groupname.should == CONFIG["normal_group_1"]
						end
						
						specify "if the startup file is a symlink, then it uses the symlink's group, not the target's group" do
							File.lchown(-1,
								gid_for(CONFIG["normal_group_2"]),
								@startup_file)
							File.chown(-1,
								gid_for(CONFIG["normal_group_1"]),
								@startup_file_target)
							run
							@groupname.should == CONFIG["normal_group_2"]
						end
					end
					
					specify "if 'group' is not given, it changes the group to default_user's primary group" do
						run
						@groupname.should == primary_group_for(CONFIG["default_user"])
					end
				end
				
				describe "and 'user' is not 'root'" do
					before :each do
						@options["user"] = CONFIG["normal_user_1"]
					end
					
					it "changes the user to the given username" do
						run
						@username.should == CONFIG["normal_user_1"]
					end
					
					specify "if 'group' is given, it changes group to the given group name" do
						run("group" => CONFIG["normal_group_1"])
						@groupname.should == CONFIG["normal_group_1"]
					end
					
					specify "if 'group' is set to the root group, it changes group to default_group" do
						run("group" => group_name_for_gid(0))
						@groupname.should == CONFIG["default_group"]
					end
					
					describe "and 'group' is set to '!STARTUP_FILE!'" do
						before :each do
							@options["group"] = "!STARTUP_FILE!"
						end
						
						it "changes the group to the startup file's group" do
							File.lchown(-1,
								gid_for(CONFIG["normal_group_1"]),
								@startup_file)
							run
							@groupname.should == CONFIG["normal_group_1"]
						end
						
						specify "if the startup file is a symlink, then it uses the symlink's group, not the target's group" do
							File.lchown(-1,
								gid_for(CONFIG["normal_group_2"]),
								@startup_file)
							File.chown(-1,
								gid_for(CONFIG["normal_group_1"]),
								@startup_file_target)
							run
							@groupname.should == CONFIG["normal_group_2"]
						end
					end
					
					specify "if 'group' is not given, it changes the group to the user's primary group" do
						run
						@groupname.should == primary_group_for(CONFIG["normal_user_1"])
					end
				end
				
				describe "and the given username does not exist" do
					before :each do
						@options["user"] = CONFIG["nonexistant_user"]
					end
					
					it "changes the user to the value of 'default_user'" do
						run
						@username.should == CONFIG["default_user"]
					end
					
					specify "if 'group' is given, it changes group to the given group name" do
						run("group" => CONFIG["normal_group_1"])
						@groupname.should == CONFIG["normal_group_1"]
					end
					
					specify "if 'group' is set to the root group, it changes group to default_group" do
						run("group" => group_name_for_gid(0))
						@groupname.should == CONFIG["default_group"]
					end
					
					describe "and 'group' is set to '!STARTUP_FILE!'" do
						before :each do
							@options["group"] = "!STARTUP_FILE!"
						end
						
						it "changes the group to the startup file's group" do
							File.lchown(-1,
								gid_for(CONFIG["normal_group_1"]),
								@startup_file)
							run
							@groupname.should == CONFIG["normal_group_1"]
						end
						
						specify "if the startup file is a symlink, then it uses the symlink's group, not the target's group" do
							File.lchown(-1,
								gid_for(CONFIG["normal_group_2"]),
								@startup_file)
							File.chown(-1,
								gid_for(CONFIG["normal_group_1"]),
								@startup_file_target)
							run
							@groupname.should == CONFIG["normal_group_2"]
						end
					end
					
					specify "if 'group' is not given, it changes the group to default_user's primary group" do
						run
						@groupname.should == primary_group_for(CONFIG["default_user"])
					end
				end
			end
			describe "if 'user' is not given" do
				describe "and the startup file's owner exists" do
					before :each do
						File.lchown(uid_for(CONFIG["normal_user_1"]),
							-1,
							@startup_file)
					end
					
					it "changes the user to the owner of the startup file" do
						run
						@username.should == CONFIG["normal_user_1"]
					end
					
					specify "if the startup file is a symlink, then it uses the symlink's owner, not the target's owner" do
						File.lchown(uid_for(CONFIG["normal_user_2"]),
							-1,
							@startup_file)
						File.chown(uid_for(CONFIG["normal_user_1"]),
							-1,
							@startup_file_target)
						run
						@username.should == CONFIG["normal_user_2"]
					end
					
					specify "if 'group' is given, it changes group to the given group name" do
						run("group" => CONFIG["normal_group_1"])
						@groupname.should == CONFIG["normal_group_1"]
					end
					
					specify "if 'group' is set to the root group, it changes group to default_group" do
						run("group" => group_name_for_gid(0))
						@groupname.should == CONFIG["default_group"]
					end
					
					describe "and 'group' is set to '!STARTUP_FILE!'" do
						before :each do
							@options["group"] = "!STARTUP_FILE!"
						end
						
						it "changes the group to the startup file's group" do
							File.lchown(-1,
								gid_for(CONFIG["normal_group_1"]),
								@startup_file)
							run
							@groupname.should == CONFIG["normal_group_1"]
						end
						
						specify "if the startup file is a symlink, then it uses the symlink's group, not the target's group" do
							File.lchown(-1,
								gid_for(CONFIG["normal_group_2"]),
								@startup_file)
							File.chown(-1,
								gid_for(CONFIG["normal_group_1"]),
								@startup_file_target)
							run
							@groupname.should == CONFIG["normal_group_2"]
						end
					end
					
					specify "if 'group' is not given, it changes the group to the startup file's owner's primary group" do
						run
						@groupname.should == primary_group_for(CONFIG["normal_user_1"])
					end
				end
				
				describe "and the startup file's owner doesn't exist" do
					before :each do
						File.lchown(CONFIG["nonexistant_uid"],
							-1,
							@startup_file)
					end
					
					it "changes the user to the value of 'default_user'" do
						run
						@username.should == CONFIG["default_user"]
					end
					
					specify "if 'group' is given, it changes group to the given group name" do
						run("group" => CONFIG["normal_group_1"])
						@groupname.should == CONFIG["normal_group_1"]
					end
					
					specify "if 'group' is set to the root group, it changes group to default_group" do
						run("group" => group_name_for_gid(0))
						@groupname.should == CONFIG["default_group"]
					end
					
					describe "and 'group' is set to '!STARTUP_FILE!'" do
						before :each do
							@options["group"] = "!STARTUP_FILE!"
						end
						
						describe "and the startup file's group doesn't exist" do
							before :each do
								File.lchown(-1,
									CONFIG["nonexistant_gid"],
									@startup_file)
							end
							
							it "changes the group to the value given by 'default_group'" do
								run
								@groupname.should == CONFIG["default_group"]
							end
						end
						
						describe "and the startup file's group exists" do
							before :each do
								File.lchown(-1,
									gid_for(CONFIG["normal_group_1"]),
									@startup_file)
							end
							
							it "changes the group to the startup file's group" do
								run
								@groupname.should == CONFIG["normal_group_1"]
							end
							
							specify "if the startup file is a symlink, then it uses the symlink's group, not the target's group" do
								File.lchown(-1,
									gid_for(CONFIG["normal_group_2"]),
									@startup_file)
								File.chown(-1,
									gid_for(CONFIG["normal_group_1"]),
									@startup_file_target)
								run
								@groupname.should == CONFIG["normal_group_2"]
							end
						end
					end
					
					specify "if 'group' is not given, it changes the group to default_user's primary group" do
						run
						@groupname.should == primary_group_for(CONFIG["default_user"])
					end
				end
			end
			
			it "raises an error if it tries to lower to 'default_user', but that user doesn't exist" do
				run("user" => "root", "default_user" => CONFIG["nonexistant_user"])
				@error.should =~ /Cannot determine a user to lower privilege to/
			end
			
			it "raises an error if it tries to lower to 'default_group', but that group doesn't exist" do
				run("user" => CONFIG["normal_user_1"],
					"group" => group_name_for_gid(0),
					"default_group" => CONFIG["nonexistant_group"])
				@error.should =~ /Cannot determine a group to lower privilege to/
			end
			
			it "changes supplementary groups to the owner's default supplementary groups" do
				run("user" => CONFIG["normal_user_1"])
				default_groups = `groups "#{CONFIG['normal_user_1']}"`.strip
				default_groups.gsub!(/.*: */, '')
				@groups.should == default_groups
			end
			
			it "sets $HOME to the user's home directory" do
				run("user" => CONFIG["normal_user_1"])
				@env_home.should == Etc.getpwnam(CONFIG["normal_user_1"]).dir
			end
			
			it "sets $USER to the user's name" do
				run("user" => CONFIG["normal_user_1"])
				@env_user.should == CONFIG["normal_user_1"]
			end
		end
	end
	
	describe "#check_directory_tree_permissions" do
		before :each do
			@root = PhusionPassenger::Utils.passenger_tmpdir
		end
		
		def primary_group_for(username)
			gid = Etc.getpwnam(username).gid
			return Etc.getgrgid(gid).name
		end
		
		it "raises an error for the top-most parent directory that has wrong permissions" do
			FileUtils.mkdir_p("#{@root}/a/b/c/d")
			
			when_running_as_root do
				user = CONFIG['normal_user_1']
				group = primary_group_for(user)
				system("chown -R #{user} #{@root}/a")
				system("chgrp -R #{group} #{@root}/a")
				
				output = run_script(%q{
					require 'phusion_passenger/utils'
					include PhusionPassenger::Utils
					@root = ARGV[0]
					lower_privilege(nil,
						"user" => ARGV[1],
						"group" => ARGV[2])
					
					File.chmod(0600, "#{@root}/a/b/c/d")
					File.chmod(0600, "#{@root}/a/b/c")
					File.chmod(0600, "#{@root}/a")
					p check_directory_tree_permissions("#{@root}/a/b/c/d")
					File.chmod(0700, "#{@root}/a")
					p check_directory_tree_permissions("#{@root}/a/b/c/d")
					File.chmod(0700, "#{@root}/a/b/c")
					p check_directory_tree_permissions("#{@root}/a/b/c/d")
					File.chmod(0700, "#{@root}/a/b/c/d")
					p check_directory_tree_permissions("#{@root}/a/b/c/d")
				}, @root, user, group)
				lines = output.split("\n")
				lines[0].should == ["#{@root}/a", true].inspect
				lines[1].should == ["#{@root}/a/b/c", true].inspect
				lines[2].should == ["#{@root}/a/b/c/d", false].inspect
				lines[3].should == "nil"
			end
			when_not_running_as_root do
				File.chmod(0000, "#{@root}/a/b/c/d")
				File.chmod(0600, "#{@root}/a/b/c")
				File.chmod(0600, "#{@root}/a")
				check_directory_tree_permissions("#{@root}/a/b/c/d").should ==
					["#{@root}/a", true]
				File.chmod(0700, "#{@root}/a")
				check_directory_tree_permissions("#{@root}/a/b/c/d").should ==
					["#{@root}/a/b/c", true]
				File.chmod(0700, "#{@root}/a/b/c")
				check_directory_tree_permissions("#{@root}/a/b/c/d").should ==
					["#{@root}/a/b/c/d", false]
				File.chmod(0700, "#{@root}/a/b/c/d")
				check_directory_tree_permissions("#{@root}/a/b/c/d").should be_nil
			end
		end
	end
	
	######################
end

end # module PhusionPassenger
