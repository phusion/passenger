$LOAD_PATH << "#{File.dirname(__FILE__)}/../lib"
require 'support/config'
require 'etc'

shared_examples_for "spawner that supports lowering of privileges" do
	before :all do
		@environment_rb = "#{@test_app}/config/environment.rb"
		@original_uid = File.stat(@environment_rb).uid
	end
	
	after :all do
		File.chown(@original_uid, nil, @environment_rb)
	end
	
	it "should lower its privileges to the owner environment.rb" do
		File.chown(uid_for('normal_user_1'), nil, @environment_rb)
		spawn_app do |app|
			user_of_process(app.pid).should == CONFIG['normal_user_1']
		end
	end
	
	it "should switch the group as well after lowering privileges" do
		File.chown(uid_for('normal_user_1'), nil, @environment_rb)
		spawn_app do |app|
			expected_gid = Etc.getpwnam(CONFIG['normal_user_1']).gid
			expected_group = Etc.getgrgid(expected_gid).name
			group_of_process(app.pid).should == expected_group
		end
	end
	
	it "should lower its privileges to _lowest_user_ if environment.rb is owned by root" do
		File.chown(ApplicationSpawner::ROOT_UID, nil, @environment_rb)
		spawn_app do |app|
			user_of_process(app.pid).should == CONFIG['lowest_user']
		end
	end
	
	it "should lower its privileges to _lowest_user_ if environment.rb is owned by a nonexistant user" do
		File.chown(CONFIG['nonexistant_uid'], nil, @environment_rb)
		spawn_app do |app|
			user_of_process(app.pid).should == CONFIG['lowest_user']
		end
	end
	
	it "should not switch user if environment.rb is owned by a nonexistant user, and _lowest_user_ doesn't exist either" do
		File.chown(CONFIG['nonexistant_uid'], nil, @environment_rb)
		spawn_app(:lowest_user => CONFIG['nonexistant_user']) do |app|
			user_of_process(app.pid).should == user_of_process(Process.pid)
		end
	end
	
	def uid_for(name)
		return Etc.getpwnam(CONFIG[name]).uid
	end
	
	def user_of_process(pid)
		return `ps -p #{pid} -o user`.split("\n")[1].to_s.strip
	end
	
	def group_of_process(pid)
		return `ps -p #{pid} -o group`.split("\n")[1].to_s.strip
	end
end
