require 'support/config'
require 'etc'
require 'yaml'

shared_examples_for "a spawner that supports lowering of privileges" do
	before :each do
		@stub = setup_rails_stub('foobar')
		@environment_rb = @stub.environment_rb
		@original_uid = File.stat(@environment_rb).uid
		File.append(@environment_rb, %q{
			require 'yaml'
			info = {
				:username => `whoami`.strip,
				:user_id  => `id -u`.strip.to_i,
				:group_id => `id -g`.strip.to_i,
				:groups   => `groups "#{`whoami`.strip}"`.strip,
				:home     => ENV['HOME']
			}
			File.open("#{RAILS_ROOT}/dump.yml", 'w') do |f|
				YAML::dump(info, f)
			end
		})
	end
	
	after :each do
		@stub.destroy
	end
	
	it "lowers its privileges to the owner of environment.rb" do
		File.chown(uid_for('normal_user_1'), nil, @environment_rb)
		spawn_stub_application do |app|
			read_dumped_info[:username].should == CONFIG['normal_user_1']
		end
	end
	
	it "switches the group to environment.rb's owner's primary group, after lowering privileges" do
		File.chown(uid_for('normal_user_1'), nil, @environment_rb)
		spawn_stub_application do |app|
			expected_gid = Etc.getpwnam(CONFIG['normal_user_1']).gid
			read_dumped_info[:group_id].should == expected_gid
		end
	end
	
	it "switches supplementary groups to environment.rb's owner's default supplementary groups" do
		File.chown(uid_for('normal_user_1'), nil, @environment_rb)
		spawn_stub_application do |app|
			default_groups = `groups "#{CONFIG['normal_user_1']}"`.strip
			read_dumped_info[:groups].should == default_groups
		end
	end
	
	it "lowers its privileges to 'lowest_user' if environment.rb is owned by root" do
		File.chown(ApplicationSpawner::ROOT_UID, nil, @environment_rb)
		spawn_stub_application do |app|
			read_dumped_info[:username].should == CONFIG['lowest_user']
		end
	end
	
	it "lowers its privileges to 'lowest_user' if environment.rb is owned by a nonexistant user" do
		File.chown(CONFIG['nonexistant_uid'], nil, @environment_rb)
		spawn_stub_application do |app|
			read_dumped_info[:username].should == CONFIG['lowest_user']
		end
	end
	
	it "doesn't switch user if environment.rb is owned by a nonexistant user, and 'lowest_user' doesn't exist either" do
		File.chown(CONFIG['nonexistant_uid'], nil, @environment_rb)
		spawn_stub_application("lowest_user" => CONFIG['nonexistant_user']) do |app|
			read_dumped_info[:username].should == my_username
		end
	end
	
	it "doesn't switch user if 'lower_privilege' is set to false" do
		File.chown(uid_for('normal_user_2'), nil, @environment_rb)
		spawn_stub_application("lower_privilege" => false) do |app|
			read_dumped_info[:username].should == my_username
		end
	end
	
	it "sets $HOME to the user's home directory, after privilege lowering" do
		spawn_stub_application("lowest_user" => CONFIG['normal_user_1']) do |app|
			read_dumped_info[:home].should == Etc.getpwnam(CONFIG['normal_user_1']).dir
		end
	end
	
	def read_dumped_info
		return YAML.load_file("#{@stub.app_root}/dump.yml")
	end
	
	def my_username
		return `whoami`.strip
	end
	
	def uid_for(name)
		return Etc.getpwnam(CONFIG[name]).uid
	end
end
