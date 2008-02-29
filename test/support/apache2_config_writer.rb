require 'erb'
require 'mod_rails/platform_info'

class Apache2ConfigWriter
	include PlatformInfo
	
	def initialize
		@stub_dir = File.expand_path(File.dirname(__FILE__) + "/../stub")
		@server_root = "#{@stub_dir}/apache2"
		@spawn_server = File.expand_path(File.dirname(__FILE__) + "/../../bin/passenger-spawn-server")
		@modules_dir = `#{APXS2} -q LIBEXECDIR`.strip
		@modules = `#{HTTPD} -l`.split("\n").grep(/\.c$/).map do |line|
			line.strip
		end
		@mod_passenger = File.expand_path(File.dirname(__FILE__) + "/../../ext/apache2/mod_passenger.so")
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
	
	def has_module?(name)
		return @modules.include?(name)
	end
end
