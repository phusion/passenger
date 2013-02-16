define 'openssl-dev' do
	name "OpenSSL development headers"
	website "http://www.openssl.org/"
	define_checker do
		check_for_header('openssl/ssl.h')
	end

	on :debian do
		apt_get_install "libssl-dev"
	end
	on :redhat do
		yum_install "openssl-devel"
	end
end

define 'zlib-dev' do
	name "Zlib development headers"
	website "http://www.zlib.net/"
	define_checker do
		check_for_header('zlib.h')
	end

	on :debian do
		apt_get_install "zlib1g-dev"
	end
	on :mandriva do
		urpmi "zlib1-devel"
	end
	on :redhat do
		yum_install "zlib-devel"
	end
end

define 'pcre-dev' do
	name "PCRE development headers"
	website "http://www.pcre.org/"
	define_checker do
		check_for_header('pcre.h')
	end

	on :debian do
		apt_get_install "libpcre3-dev"
	end
	on :redhat do
		yum_install 'pcre-devel'
	end
end