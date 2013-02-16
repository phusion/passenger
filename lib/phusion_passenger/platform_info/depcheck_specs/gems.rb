define 'fastthread' do
	name 'fastthread'
	define_checker do
		check_for_ruby_library('fastthread')
	end
	gem_install 'fastthread'
end

define 'rack' do
	name 'rack'
	define_checker do
		check_for_ruby_library('rack')
	end
	gem_install 'rack'
end

define 'daemon_controller >= 1.1.0' do
	name 'daemon_controller >= 1.1.0'
	define_checker do
		if check_for_ruby_library('daemon_controller')
			gem_command = PlatformInfo.gem_command || "gem"
			begin
				require 'daemon_controller/version'
				{
					:found => DaemonController::VERSION_STRING >= '1.1.0',
					"Installed version" => DaemonController::VERSION_STRING
				}
			rescue LoadError
				{
					:found => false,
					"Installed version" => "way too old"
				}
			end
		else
			false
		end
	end
	gem_install 'daemon_controller'
end
