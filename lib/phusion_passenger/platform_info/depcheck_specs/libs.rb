define 'openssl-dev' do
	name "OpenSSL development headers"
	website "http://www.openssl.org/"
	define_checker do
		source_file = "#{PlatformInfo.tmpexedir}/passenger-openssl-check.c"
		object_file = "#{PlatformInfo.tmpexedir}/passenger-openssl-check.o"
		begin
			File.open(source_file, 'w') do |f|
				f.write("#include <openssl/ssl.h>")
			end
			Dir.chdir(File.dirname(source_file)) do
				if system("(gcc #{ENV['CFLAGS']} -c '#{source_file}') >/dev/null 2>/dev/null")
					result.found
				else
					result.not_found
				end
			end
		ensure
			File.unlink(source_file) rescue nil
			File.unlink(object_file) rescue nil
		end
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
		source_file = "#{PlatformInfo.tmpexedir}/zlib-check.c"
		object_file = "#{PlatformInfo.tmpexedir}/zlib-check.o"
		begin
			File.open(source_file, 'w') do |f|
				f.write("#include <zlib.h>")
			end
			Dir.chdir(File.dirname(source_file)) do
				if system("(g++ -c zlib-check.c) >/dev/null 2>/dev/null")
					result.found
				else
					result.not_found
				end
			end
		ensure
			File.unlink(source_file) rescue nil
			File.unlink(object_file) rescue nil
		end
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
		source_file = "#{PlatformInfo.tmpexedir}/pcre-check.c"
		object_file = "#{PlatformInfo.tmpexedir}/pcre-check.o"
		begin
			File.open(source_file, 'w') do |f|
				f.write("#include <pcre.h>")
			end
			Dir.chdir(File.dirname(source_file)) do
				if system("(g++ -c pcre-check.c) >/dev/null 2>/dev/null")
					result.found
				else
					result.not_found
				end
			end
		ensure
			File.unlink(source_file) rescue nil
			File.unlink(object_file) rescue nil
		end
	end

	on :debian do
		apt_get_install "libpcre3-dev"
	end
	on :redhat do
		yum_install 'pcre-devel'
	end
end