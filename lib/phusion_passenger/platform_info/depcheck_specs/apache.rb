define 'apache' do
	name 'Apache 2'
	website 'http://httpd.apache.org/'
	define_checker do
		require 'phusion_passenger/platform_info/apache'
		check_for_command(PlatformInfo.httpd)
	end

	on :debian do
		apt_get_install "apache2-mpm-worker"
	end
	on :mandriva do
		urpmi "apache"
	end
	on :redhat do
		yum_install "httpd"
	end
	on :gentoo do
		emerge "apache"
	end
end

define 'apache-dev' do
	name "Apache 2 development headers"
	website = "http://httpd.apache.org/"
	define_checker do
		require 'phusion_passenger/platform_info/apache'
		{
			:found => PlatformInfo.apxs2 && PlatformInfo.httpd,
			"Location of apxs2" => PlatformInfo.apxs2 || "not found",
			"Location of httpd" => PlatformInfo.httpd || "not found"
			"Apache version"    => PlatformInfo.httpd_version || "-"
		}
	end

	on :debian do
		apt_get_install "apache2-worker-dev"
	end
	on :mandriva do
		urpmi "apache-devel"
	end
	on :redhat do
		yum_install "httpd-devel"
	end
	on :gentoo do
		emerge "apache"
	end
end

define 'apr-dev' do
	name "Apache Portable Runtime (APR) development headers"
	website "http://httpd.apache.org/"
	define_checker do
		check_for_command(PlatformInfo.apr_config)
	end

	on :debian do
		apt_get_install "libapr1-dev"
	end
	on :mandriva do
		urpmi "libapr-devel"
	end
	on :redhat do
		yum_install "apr-devel"
	end
	on :gentoo do
		emerge "apr"
	end
end

define 'apu-dev' do
	name "Apache Portable Runtime Utility (APU) development headers"
	website "http://httpd.apache.org/"
	define_checker do
		require 'phusion_passenger/platform_info/apache'
		check_for_command(PlatformInfo.apu_config)
	end

	on :debian do
		apt_get_install "libaprutil1-dev"
	end
	on :mandriva do
		urpmi "libapr-util-devel"
	end
	on :redhat do
		yum_install "apr-util-devel"
	end
	on :macosx do
		xcode_install "Command line tools"
	end
end
