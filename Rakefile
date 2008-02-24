# kate: syntax ruby
$LOAD_PATH.unshift("#{File.dirname(__FILE__)}/lib")
require 'rubygems'
require 'rake/rdoctask'
require 'rake/gempackagetask'
require 'rake/extensions'
require 'rake/cplusplus'
require 'mod_rails/platform_info'

##### Configuration

include PlatformInfo
APACHE2CTL.nil? and raise "Could not find 'apachectl' or 'apache2ctl'."
APXS2.nil? and raise "Could not find Apache Portable Runtime (APR)."

CXX = "g++"
CXXFLAGS = "-Wall -g -I/usr/local/include " << MULTI_ARCH_FLAGS
LDFLAGS = ""


#### Default tasks

desc "Build everything"
task :default => [
	"ext/mod_rails/native_support.#{LIBEXT}",
	:apache2,
	'test/Apache2ModuleTests',
	'benchmark/DummyRequestHandler'
]

desc "Remove compiled files"
task :clean

desc "Remove all generated files"
task :clobber


##### Ruby C extension

subdir 'ext/mod_rails' do
	file 'Makefile' => 'extconf.rb' do
		sh "ruby extconf.rb"
	end
	
	file "native_support.#{LIBEXT}" => ['Makefile', 'native_support.c'] do
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
		flags = "-O2 -fPIC -DNDEBUG #{MULTI_ARCH_FLAGS}"
		compile_cxx "*.cpp", flags
		create_static_library "libboost_thread.a", "*.o"
	end
	
	task :clean do
		sh "rm -f libboost_thread.a *.o"
	end
end


##### Apache module

class APACHE2
	CXXFLAGS = CXXFLAGS + " -fPIC -g -DPASSENGER_DEBUG #{APR1_FLAGS} #{APXS2_FLAGS} -I.."
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
	task :apache2 => ['mod_passenger.so', "../mod_rails/native_support.#{LIBEXT}"]
	
	file 'mod_passenger.so' => ['../boost/src/libboost_thread.a', 'mod_passenger.o'] + APACHE2::OBJECTS.keys do
		# apxs totally sucks. We couldn't get it working correctly
		# on MacOS X (it had various problems with building universal
		# binaries), so we decided to ditch it and build/install the
		# Apache module ourselves.
		#
		# Oh, and libtool sucks too. Do we even need it anymore in 2008?
		linkflags = "#{LDFLAGS} #{MULTI_ARCH_FLAGS}"
		linkflags << " -lstdc++ -lpthread ../boost/src/libboost_thread.a #{APR1_LIBS}"
		create_shared_library 'mod_passenger.so',
			APACHE2::OBJECTS.keys.join(' ') << ' mod_passenger.o',
			linkflags
	end
	
	desc "Install mod_passenger Apache 2 module"
	task 'apache2:install' => :apache2 do
		install_dir = `#{APXS2} -q LIBEXECDIR`.strip
		sh "cp", "mod_passenger.so", install_dir
	end
	
	desc "Install mod_passenger Apache 2 module and restart Apache"
	task 'apache2:install_restart' do
		sh "#{APACHE2CTL} stop" do end
		unless `pidof apache2`.strip.empty?
			sh "killall apache2" do end
		end
		Dir.chdir("../..") do
			Rake::Task['apache2:install'].invoke
		end
		sh "#{APACHE2CTL} start"
	end
	
	file 'mod_passenger.o' => ['mod_passenger.c'] do
		compile_c 'mod_passenger.c', APACHE2::CXXFLAGS
	end
	
	APACHE2::OBJECTS.each_pair do |target, sources|
		file target => sources do
			compile_cxx sources[0], APACHE2::CXXFLAGS
		end
	end
	
	task :clean => 'apache2:clean'.to_sym
	
	desc "Remove generated files for mod_passenger Apache 2 module"
	task 'apache2:clean' do
		files = [APACHE2::OBJECTS.keys, %w(mod_passenger.o mod_passenger.so)]
		sh("rm", "-rf", *files.flatten)
	end
end


##### Unit tests

class TEST
	CXXFLAGS = ::CXXFLAGS + " -Isupport -DTESTING_SPAWN_MANAGER "
	AP2_FLAGS = "-I../ext/apache2 -I../ext #{APR1_FLAGS}"
	
	AP2_OBJECTS = {
		'CxxTestMain.o' => %w(CxxTestMain.cpp),
		'MessageChannelTest.o' => %w(MessageChannelTest.cpp ../ext/apache2/MessageChannel.h),
		'SpawnManagerTest.o' => %w(SpawnManagerTest.cpp
			../ext/apache2/SpawnManager.h
			../ext/apache2/Application.h),
		'ApplicationPoolClientServerTest.o' => %w(ApplicationPoolClientServerTest.cpp
			ApplicationPoolTestTemplate.cpp
			../ext/apache2/ApplicationPoolClientServer.h
			../ext/apache2/ApplicationPool.h
			../ext/apache2/SpawnManager.h
			../ext/apache2/Application.h),
		'StandardApplicationPoolTest.o' => %w(StandardApplicationPoolTest.cpp
			ApplicationPoolTestTemplate.cpp
			../ext/apache2/ApplicationPool.h
			../ext/apache2/SpawnManager.h
			../ext/apache2/Application.h),
		'UtilsTest.o' => %w(UtilsTest.cpp ../ext/apache2/Utils.h)
	}
end

subdir 'test' do
	desc "Run all unit tests (but not integration tests)"
	task :test => ['test:apache2'.to_sym, 'test:ruby'.to_sym]
	
	desc "Run unit tests for the Apache 2 module"
	task 'test:apache2' => 'Apache2ModuleTests' do
		sh "./Apache2ModuleTests"
	end
	
	desc "Run unit tests for the Apache 2 module in Valgrind"
	task 'test:valgrind' => 'Apache2ModuleTests' do
		sh "valgrind #{ENV['ARGS']} ./Apache2ModuleTests"
	end
	
	desc "Run unit tests for the Ruby libraries"
	task 'test:ruby' => ["../ext/mod_rails/native_support.#{LIBEXT}"] do
		sh "spec -f s *_spec.rb"
	end

	file 'Apache2ModuleTests' => TEST::AP2_OBJECTS.keys +
	  ['../ext/boost/src/libboost_thread.a',
	   "../ext/mod_rails/native_support.#{LIBEXT}",
	   '../ext/apache2/Utils.o'] do
		objects = TEST::AP2_OBJECTS.keys.join(' ') << " ../ext/apache2/Utils.o"
		create_executable "Apache2ModuleTests", objects,
			"#{LDFLAGS} #{APR1_LIBS} ../ext/boost/src/libboost_thread.a -lpthread"
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


##### Documentation

Rake::RDocTask.new do |rd|
	rd.rdoc_dir = "doc/rdoc"
	rd.rdoc_files.include("lib/mod_rails/*.rb", "lib/rake/extensions.rb", "ext/mod_rails/*.c")
	rd.template = "jamis"
	rd.title = "Passenger Ruby API"
	rd.options << "-S"
	rd.options << "-N"
end

desc "Generate Doxygen C++ API documentation if necessary"
task :doxygen => ['doc/cxxapi']
file 'doc/cxxapi' => Dir['ext/apache2/*.{h,c,cpp}'] do
	Dir.chdir('doc') do
		sh "doxygen"
	end
end

desc "Force generation of Doxygen C++ API documentation"
task 'doxygen:force' do
	Dir.chdir('doc') do
		sh "doxygen"
	end
end

desc "Remove generated Doxygen C++ API documentation"
task 'doxygen:clobber' do
	Dir.chdir('doc') do
		sh "rm -rf cxxapi"
	end
end
task :clobber => 'doxygen:clobber'


##### Gem

spec = Gem::Specification.new do |s|
	s.platform = Gem::Platform::RUBY
	s.homepage = "http://passenger.phusion.nl/"
	s.summary = "Apache module for Ruby on Rails support."
	s.name = "passenger"
	s.version = "0.9.0"
	s.requirements << "fastthread" << "Apache 2 with development headers"
	s.require_path = "lib"
	s.add_dependency 'rake', '>= 0.8.1'
	s.add_dependency 'fastthread', '>= 1.0.1'
	s.add_dependency 'rspec', '>= 1.1.2'
	s.add_dependency 'rails', '>= 1.2.0'
	s.extensions << 'ext/mod_rails/extconf.rb'
	s.files = FileList[
		'Rakefile',
		'lib/**/*.rb',
		'bin/*',
		'doc/*',
		'doc/cxxapi/*',
		'doc/rdoc/*',
		'doc/rdoc/*/*',
		'doc/rdoc/*/*/*',
		'doc/rdoc/*/*/*/*',
		'doc/rdoc/*/*/*/*/*',
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
	s.executables = ['passenger-spawn-server', 'passenger-install-apache2-module']
	s.has_rdoc = true
	s.test_file = 'test/support/run_rspec_tests.rb'
	s.description = "Passenger is an Apache module for Ruby on Rails support."
end

Rake::GemPackageTask.new(spec) do |pkg|
	pkg.need_tar_gz = true
end

Rake::Task['package'].prerequisites.push('rdoc', 'doxygen')
Rake::Task['package:gem'].prerequisites.push('rdoc', 'doxygen')
Rake::Task['package:force'].prerequisites.push('rdoc', 'doxygen')


##### Misc

desc "Run 'sloccount' to see how much code Passenger has"
task :sloccount do
	ENV['LC_ALL'] = 'C'
	begin
		# sloccount doesn't recognize the scripts in
		# bin/ as Ruby, so we make symlinks with proper
		# extensions.
		tmpdir = ".sloccount"
		system "rm -rf #{tmpdir}"
		mkdir tmpdir
		Dir['bin/*'].each do |file|
			safe_ln file, "#{tmpdir}/#{File.basename(file)}.rb"
		end
		sh "sloccount", *Dir[
			"#{tmpdir}/*",
			"lib/mod_rails/*",
			"lib/rake/{cplusplus,extensions}.rb",
			"ext/apache2",
			"ext/mod_rails/*.c",
			"test/*.{cpp,rb}",
			"test/support/*.rb",
			"test/stub/*.rb",
			"benchmark/*.{cpp,rb}"
		]
	ensure
		system "rm -rf #{tmpdir}"
	end
end

