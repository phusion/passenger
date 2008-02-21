# kate: syntax ruby
$LOAD_PATH.unshift("#{File.dirname(__FILE__)}/lib")
require 'rubygems'
require 'rake/rdoctask'
require 'rake/gempackagetask'
require 'rake/extensions'

desc "Build everything"
task :default => [
	'ext/mod_rails/native_support.so',
	:apache2,
	'test/Apache2ModuleTests',
	'benchmark/DummyRequestHandler'
]

desc "Remove generated files"
task :clean


##### Configuration

CXX = "g++"
CXXFLAGS = "-Wall -g -I/usr/local/include"
LDFLAGS = ""
APR_FLAGS = `pkg-config --cflags apr-1 apr-util-1`.strip
APR_LIBS = `pkg-config --libs apr-1 apr-util-1`.strip

require 'rake/cplusplus'
if RUBY_PLATFORM =~ /darwin/
	# MacOS X
	OSX_ARCHS = "-arch ppc7400 -arch ppc64 -arch i386 -arch x86_64"
	CXXFLAGS << " " << OSX_ARCHS
end


##### Ruby C extension

subdir 'ext/mod_rails' do
	file 'Makefile' => 'extconf.rb' do
		sh "ruby extconf.rb"
	end
	
	file 'native_support.so' => ['Makefile', 'native_support.c'] do
		sh "make"
	end
	
	task :clean do
		sh "make clean" if File.exist?('Makefile')
		sh "rm -f Makefile"
	end
end


##### boost::thread static library

subdir 'ext/boost/src' do
	file 'libboost_thread.a' => Dir['*.cpp'] do
		# Note: NDEBUG *must* be defined! boost::thread use assert() to check whether
		# the pthread functions return an error. Because of the way Passenger uses
		# processes, sometimes pthread errors will occur. These errors are harmless
		# and should be ignored. Defining NDEBUG guarantees that boost::thread() will
		# not abort if such an error occured.
		flags = "-O2 -fPIC -DNDEBUG"
		if defined?(OSX_ARCHS)
			flags << " " << OSX_ARCHS
		end
		compile_cxx "*.cpp", flags
		create_static_library "libboost_thread.a", "*.o"
	end
	
	task :clean do
		sh "rm -f libboost_thread.a *.o"
	end
end


##### Apache module

class APACHE2
	XS = 'apxs2'
	CTL = 'apache2ctl'
	APXS_FLAGS = `#{XS} -q CFLAGS`.strip << " -I" << `#{XS} -q INCLUDEDIR`.strip
	CXXFLAGS = CXXFLAGS + " -fPIC -g -DPASSENGER_DEBUG #{APR_FLAGS} #{APXS_FLAGS} -I.."
	OBJECTS = {
		'Configuration.o' => %w(Configuration.cpp),
		'Hooks.o' => %w(Hooks.cpp Hooks.h
				Configuration.h ApplicationPool.h ApplicationPoolClientServer.h
				SpawnManager.h Exceptions.h Application.h MessageChannel.h
				Utils.h),
		'Utils.o' => %w(Utils.cpp Utils.h)
	}
end

subdir 'ext/apache2' do
	apxs_objects = APACHE2::OBJECTS.keys.join(',')

	desc "Build mod_passenger Apache 2 module"
	task :apache2 => 'mod_rails.so'
	
	file 'mod_rails.so' => ['../boost/src/libboost_thread.a', 'mod_rails.o'] + APACHE2::OBJECTS.keys do
		# apxs totally sucks. We couldn't get it working correctly
		# on MacOS X (it had various problems with building universal
		# binaries), so we decided to ditch it and build/install the
		# Apache module ourselves.
		#
		# Oh, and libtool sucks too. Do we even need it anymore in 2008?
		linkflags = LDFLAGS.dup
		if defined?(OSX_ARCHS)
			linkflags << " " << OSX_ARCHS
		end
		linkflags << " -lstdc++ -lpthread ../boost/src/libboost_thread.a #{APR_LIBS}"
		create_shared_library 'mod_rails.so',
			APACHE2::OBJECTS.keys.join(' ') << ' mod_rails.o',
			linkflags
	end
	
	desc "Install mod_passenger Apache 2 module"
	task 'apache2:install' => 'mod_rails.o' do
		install_dir = `#{APACHE2::XS} -q LIBEXECDIR`.strip
		sh "cp", "mod_rails.so", install_dir
	end
	
	desc "Install mod_passenger Apache 2 module and restart Apache"
	task 'apache2:install_restart' do
		sh "#{APACHE2::CTL} stop" do end
		unless `pidof apache2`.strip.empty?
			sh "killall apache2" do end
		end
		Dir.chdir("../..") do
			Rake::Task['apache2:install'].invoke
		end
		sh "#{APACHE2::CTL} start"
	end
	
	file 'mod_rails.o' => ['mod_rails.c'] do
		compile_c 'mod_rails.c', APACHE2::CXXFLAGS
	end
	
	APACHE2::OBJECTS.each_pair do |target, sources|
		file target => sources do
			compile_cxx sources[0], APACHE2::CXXFLAGS
		end
	end
	
	task :clean => 'apache2:clean'.to_sym
	
	desc "Remove generated files for mod_passenger Apache 2 module"
	task 'apache2:clean' do
		files = [APACHE2::OBJECTS.keys, %w(mod_rails.lo mod_rails.slo mod_rails.la .libs)]
		sh("rm", "-rf", *files.flatten)
	end
end


##### Unit tests

class TEST
	CXXFLAGS = ::CXXFLAGS + " -Isupport -DVALGRIND_FRIENDLY"
	AP2_FLAGS = "-I../ext/apache2 -I../ext #{APR_FLAGS}"
	
	AP2_OBJECTS = {
		'CxxTestMain.o' => %w(CxxTestMain.cpp),
		'MessageChannelTest.o' => %w(MessageChannelTest.cpp ../ext/apache2/MessageChannel.h),
		'SpawnManagerTest.o' => %w(SpawnManagerTest.cpp ../ext/apache2/SpawnManager.h),
		'ApplicationPoolClientServerTest.o' => %w(ApplicationPoolClientServerTest.cpp
			ApplicationPoolTestTemplate.cpp
			../ext/apache2/ApplicationPoolClientServer.h
			../ext/apache2/ApplicationPool.h),
		'StandardApplicationPoolTest.o' => %w(StandardApplicationPoolTest.cpp
			ApplicationPoolTestTemplate.cpp
			../ext/apache2/ApplicationPool.h),
		'UtilsTest.o' => %w(UtilsTest.cpp ../ext/apache2/Utils.h)
	}
end

subdir 'test' do
	file 'Apache2ModuleTests' => TEST::AP2_OBJECTS.keys + ['../ext/boost/src/libboost_thread.a'] do
		objects = TEST::AP2_OBJECTS.keys.join(' ')
		create_executable "Apache2ModuleTests", objects,
			"#{LDFLAGS} #{APR_LIBS} ../ext/boost/src/libboost_thread.a -lpthread"
	end
	
	TEST::AP2_OBJECTS.each_pair do |target, sources|
		file target => sources do
			compile_cxx sources[0], TEST::CXXFLAGS + " " + TEST::AP2_FLAGS
		end
	end
	
	task :clean do
		sh "rm -f Apache2ModuleTests *.o"
	end
end


##### Benchmarks

subdir 'benchmark' do
	file 'DummyRequestHandler' => ['DummyRequestHandler.cpp',
	  '../ext/apache2/MessageChannel.h'] do
		create_executable "DummyRequestHandler", "DummyRequestHandler.cpp",
			"#{CXXFLAGS} -I../ext/apache2 #{LDFLAGS}"
	end
	
	task :clean do
		sh "rm -f DummyRequestHandler"
	end
end


##### RDoc

Rake::RDocTask.new do |rd|
	rd.rdoc_dir = "doc/rdoc"
	rd.rdoc_files.include("lib/mod_rails/*.rb", "lib/rake/extensions.rb")
	rd.template = "jamis"
	rd.title = "Passenger Ruby API"
	rd.options << "-S"
	rd.options << "-N"
end


##### Gem

spec = Gem::Specification.new do |s|
	s.platform = Gem::Platform::RUBY
	s.homepage = "http://passenger.phusion.nl/"
	s.summary = "Apache module for Ruby on Rails support."
	s.name = "passenger"
	s.version = "1.0.0"
	s.requirements << "fastthread" << "Apache 2 with development headers"
	s.require_path = "lib"
	s.add_dependency 'rake', '>= 0.8.1'
	s.add_dependency 'fastthread', '>= 1.0.1'
	s.extensions << 'ext/mod_rails/extconf.rb'
	s.files = FileList[
		'Rakefile',
		'lib/**/*.rb',
		'bin/*',
		'ext/apache2/*.{cpp,h,c}',
		'ext/boost/*.{hpp,TXT}',
		'ext/boost/**/*.{hpp,cpp,pl,inl}',
		'ext/mod_rails/*.{c,rb}',
		'benchmark/*.{cpp,rb}',
		'test/*.{rb,cpp}',
		'test/support/*',
		'test/stub/*',
		'test/stub/*/*',
		'test/stub/*/*/*',
		'test/stub/*/*/*/*',
		'test/stub/*/*/*/*/*'
	] - Dir['test/stub/*/log/*'] \
	  - Dir['test/stub/*/tmp/*/*']
	s.executables = 'passenger-spawn-server'
	s.has_rdoc = true
	s.description = "Passenger is an Apache module for Ruby on Rails support."
end

Rake::GemPackageTask.new(spec) do |pkg|
	pkg.need_tar_gz = true
end


##### Misc

desc "Run 'sloccount' to see how much code Passenger has"
task :sloccount do
	ENV['LC_ALL'] = 'C'
	sh "sloccount", *Dir[
		"bin/*",
		"lib/mod_rails/*",
		"lib/rake/{cplusplus,extensions}.rb",
		"ext/apache2",
		"ext/mod_rails/*.c",
		"test/*.{cpp,rb}",
		"test/stub/*.rb",
		"benchmark/*.{cpp,rb}"
	]
end

