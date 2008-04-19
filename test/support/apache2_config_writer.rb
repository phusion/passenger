require 'erb'
require 'etc'
require 'passenger/platform_info'

class Apache2ConfigWriter
	include PlatformInfo
	
	def initialize
		@stub_dir = File.expand_path(File.dirname(__FILE__) + "/../stub")
		@server_root = "#{@stub_dir}/apache2"
		@passenger_root = File.expand_path(File.dirname(__FILE__) + "/../..")
		@modules_dir = `#{APXS2} -q LIBEXECDIR`.strip
		@modules = `#{HTTPD} -l`.split("\n").grep(/\.c$/).map do |line|
			line.strip
		end
		@mod_passenger = File.expand_path(File.dirname(__FILE__) + "/../../ext/apache2/mod_passenger.so")
		@normal_user = CONFIG['normal_user_1']
		@normal_group = Etc.getgrgid(Etc.getpwnam(@normal_user).gid).name
	end
	
	def write
		template = ERB.new(File.read("#{@stub_dir}/apache2/httpd.conf.erb"))
		File.open("#{@stub_dir}/apache2/httpd.conf", 'w') do |f|
			f.write(template.result(get_binding))
		end
	end
	
private
	def get_binding
		return binding
	end
	
	def has_builtin_module?(name)
		return @modules.include?(name)
	end
	
	def has_module?(name)
		return File.exist?("#{@modules_dir}/#{name}")
	end
	
	def running_as_root?
		return Process.uid == 0
	end
end
