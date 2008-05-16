# kate: syntax ruby

#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2008  Phusion
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; version 2 of the License.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

$LOAD_PATH.unshift("#{File.dirname(__FILE__)}/lib")
require 'rubygems'
require 'rake/rdoctask'
require 'rake/gempackagetask'
require 'rake/extensions'
require 'rake/cplusplus'
require 'passenger/platform_info'

##### Configuration

# Don't forget to edit Configuration.h too
PACKAGE_VERSION = "1.1.0"
OPTIMIZE = ["yes", "on", "true"].include?(ENV['OPTIMIZE'])

include PlatformInfo
APXS2.nil? and raise "Could not find 'apxs' or 'apxs2'."
APACHE2CTL.nil? and raise "Could not find 'apachectl' or 'apache2ctl'."
HTTPD.nil? and raise "Could not find the Apache web server binary."
APR_FLAGS.nil? and raise "Could not find Apache Portable Runtime (APR)."

CXX = "g++"
THREADING_FLAGS = "-D_REENTRANT"
if OPTIMIZE
	OPTIMIZATION_FLAGS = "-O2 -DNDEBUG"
else
	OPTIMIZATION_FLAGS = "-g -DPASSENGER_DEBUG"
end
CXXFLAGS = "#{THREADING_FLAGS} #{OPTIMIZATION_FLAGS} -Wall -I/usr/local/include #{MULTI_ARCH_FLAGS}"
LDFLAGS = ""


#### Default tasks

desc "Build everything"
task :default => [
	:native_support,
	:apache2,
	'test/Apache2ModuleTests',
	'benchmark/DummyRequestHandler'
]

desc "Remove compiled files"
task :clean

desc "Remove all generated files"
task :clobber


##### Ruby C extension

subdir 'ext/passenger' do
	task :native_support => ["native_support.#{LIBEXT}"]
	
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
	file 'libboost_thread.a' => Dir['*.cpp'] + Dir['pthread/*.cpp'] do
		# Note: NDEBUG *must* be defined! boost::thread uses assert() to check whether
		# the pthread functions return an error. Because of the way Passenger uses
		# processes, sometimes pthread errors will occur. These errors are harmless
		# and should be ignored. Defining NDEBUG guarantees that boost::thread() will
		# not abort if such an error occured.
		flags = "#{OPTIMIZATION_FLAGS} -fPIC -I../.. #{THREADING_FLAGS} -DNDEBUG #{MULTI_ARCH_FLAGS}"
		compile_cxx "*.cpp pthread/*.cpp", flags
		create_static_library "libboost_thread.a", "*.o"
	end
	
	task :clean do
		sh "rm -f libboost_thread.a *.o"
	end
end


##### Apache module

class APACHE2
	CXXFLAGS = "-I.. -fPIC #{OPTIMIZATION_FLAGS} #{APR_FLAGS} #{APXS2_FLAGS} #{CXXFLAGS}"
	OBJECTS = {
		'Configuration.o' => %w(Configuration.cpp Configuration.h),
		'Hooks.o' => %w(Hooks.cpp Hooks.h
				Configuration.h ApplicationPool.h ApplicationPoolServer.h
				SpawnManager.h Exceptions.h Application.h MessageChannel.h
				System.h Utils.h),
		'System.o'  => %w(System.cpp System.h),
		'Utils.o'   => %w(Utils.cpp Utils.h),
		'Logging.o' => %w(Logging.cpp Logging.h)
	}
end

subdir 'ext/apache2' do
	apxs_objects = APACHE2::OBJECTS.keys.join(',')

	desc "Build mod_passenger Apache 2 module"
	task :apache2 => ['mod_passenger.so', 'ApplicationPoolServerExecutable', :native_support]
	
	file 'mod_passenger.so' => [
		'../boost/src/libboost_thread.a',
		'mod_passenger.o'
	] + APACHE2::OBJECTS.keys do
		# apxs totally sucks. We couldn't get it working correctly
		# on MacOS X (it had various problems with building universal
		# binaries), so we decided to ditch it and build/install the
		# Apache module ourselves.
		#
		# Oh, and libtool sucks too. Do we even need it anymore in 2008?
		linkflags = "#{LDFLAGS} #{MULTI_ARCH_FLAGS}"
		linkflags << " -lstdc++ -lpthread ../boost/src/libboost_thread.a #{APR_LIBS}"
		create_shared_library 'mod_passenger.so',
			APACHE2::OBJECTS.keys.join(' ') << ' mod_passenger.o',
			linkflags
	end
	
	file 'ApplicationPoolServerExecutable' => [
		'../boost/src/libboost_thread.a',
		'ApplicationPoolServerExecutable.cpp',
		'ApplicationPool.h',
		'StandardApplicationPool.h',
		'MessageChannel.h',
		'SpawnManager.h',
		'System.o',
		'Utils.o',
		'Logging.o'
	] do
		create_executable "ApplicationPoolServerExecutable",
			'ApplicationPoolServerExecutable.cpp System.o Utils.o Logging.o',
			"-I.. #{CXXFLAGS} #{LDFLAGS} -DPASSENGER_DEBUG ../boost/src/libboost_thread.a -lpthread"
	end
	
	file 'mod_passenger.o' => ['mod_passenger.c'] do
		compile_c 'mod_passenger.c', APACHE2::CXXFLAGS
	end
	
	APACHE2::OBJECTS.each_pair do |target, sources|
		file target => sources do
			compile_cxx sources[0], APACHE2::CXXFLAGS
		end
	end
	
	task :clean => :'apache2:clean'
	
	desc "Remove generated files for mod_passenger Apache 2 module"
	task 'apache2:clean' do
		files = [APACHE2::OBJECTS.keys, %w(mod_passenger.o mod_passenger.so
			ApplicationPoolServerExecutable)]
		sh("rm", "-rf", *files.flatten)
	end
end


##### Unit tests

class TEST
	CXXFLAGS = ::CXXFLAGS + " -Isupport -DTESTING_SPAWN_MANAGER -DTESTING_APPLICATION_POOL "
	AP2_FLAGS = "-I../ext/apache2 -I../ext #{APR_FLAGS}"
	
	AP2_OBJECTS = {
		'CxxTestMain.o' => %w(CxxTestMain.cpp),
		'MessageChannelTest.o' => %w(MessageChannelTest.cpp
			../ext/apache2/MessageChannel.h
			../ext/apache2/System.h),
		'SpawnManagerTest.o' => %w(SpawnManagerTest.cpp
			../ext/apache2/SpawnManager.h
			../ext/apache2/Application.h
			../ext/apache2/MessageChannel.h
			../ext/apache2/System.h),
		'ApplicationPoolServerTest.o' => %w(ApplicationPoolServerTest.cpp
			../ext/apache2/ApplicationPoolServer.h
			../ext/apache2/MessageChannel.h
			../ext/apache2/System.h),
		'ApplicationPoolServer_ApplicationPoolTest.o' => %w(ApplicationPoolServer_ApplicationPoolTest.cpp
			ApplicationPoolTest.cpp
			../ext/apache2/ApplicationPoolServer.h
			../ext/apache2/ApplicationPool.h
			../ext/apache2/SpawnManager.h
			../ext/apache2/Application.h
			../ext/apache2/MessageChannel.h
			../ext/apache2/System.h),
		'StandardApplicationPoolTest.o' => %w(StandardApplicationPoolTest.cpp
			ApplicationPoolTest.cpp
			../ext/apache2/ApplicationPool.h
			../ext/apache2/StandardApplicationPool.h
			../ext/apache2/SpawnManager.h
			../ext/apache2/Application.h),
		'UtilsTest.o' => %w(UtilsTest.cpp ../ext/apache2/Utils.h)
	}
end

subdir 'test' do
	desc "Run all unit tests (but not integration tests)"
	task :test => [:'test:apache2', :'test:ruby', :'test:integration']
	
	desc "Run unit tests for the Apache 2 module"
	task 'test:apache2' => [
		'Apache2ModuleTests',
		'../ext/apache2/ApplicationPoolServerExecutable',
		:native_support
	] do
		sh "./Apache2ModuleTests"
	end
	
	desc "Run unit tests for the Apache 2 module in Valgrind"
	task 'test:valgrind' => [
		'Apache2ModuleTests',
		'../ext/apache2/ApplicationPoolServerExecutable',
		:native_support
	] do
		sh "valgrind #{ENV['ARGS']} ./Apache2ModuleTests"
	end
	
	desc "Run unit tests for the Ruby libraries"
	task 'test:ruby' => [:native_support] do
		sh "spec -c -f s ruby/*.rb ruby/*/*.rb"
	end
	
	task 'test:rcov' => [:native_support] do
		rspec = PlatformInfo.find_command('spec')
		sh "rcov", "--exclude",
			"lib\/spec,\/spec$,_spec\.rb$,support\/,platform_info,integration_tests",
			rspec, "--", "-c", "-f", "s",
			*Dir["ruby/*.rb", "ruby/*/*.rb", "integration_tests.rb"]
	end
	
	desc "Run integration tests"
	task 'test:integration' => [:apache2, :native_support] do
		sh "spec -c -f s integration_tests.rb"
	end

	file 'Apache2ModuleTests' => TEST::AP2_OBJECTS.keys +
	  ['../ext/boost/src/libboost_thread.a',
	   '../ext/apache2/System.o',
	   '../ext/apache2/Utils.o',
	   '../ext/apache2/Logging.o'] do
		objects = TEST::AP2_OBJECTS.keys.join(' ') <<
			" ../ext/apache2/System.o" <<
			" ../ext/apache2/Utils.o" <<
			" ../ext/apache2/Logging.o"
		create_executable "Apache2ModuleTests", objects,
			"#{LDFLAGS} #{APR_LIBS} #{MULTI_ARCH_FLAGS} " <<
			"../ext/boost/src/libboost_thread.a -lpthread"
	end
	
	TEST::AP2_OBJECTS.each_pair do |target, sources|
		file target => sources do
			compile_cxx sources[0], "#{TEST::AP2_FLAGS} #{TEST::CXXFLAGS}"
		end
	end
	
	desc "Run the restart integration test infinitely, and abort if/when it fails"
	task 'test:restart' => [:apache2, :native_support] do
		color_code_start = "\e[33m\e[44m\e[1m"
		color_code_end = "\e[0m"
		i = 1
		while true do
			puts "#{color_code_start}Test run #{i} (press Ctrl-C multiple times to abort)#{color_code_end}"
			sh "spec -c -f s integration_tests.rb -e 'mod_passenger running in Apache 2 : MyCook(tm) beta running on root URI should support restarting via restart.txt'"
			i += 1
		end
	end
	
	task :clean do
		sh "rm -f Apache2ModuleTests *.o"
	end
end


##### Benchmarks

subdir 'benchmark' do
	file 'DummyRequestHandler' => ['DummyRequestHandler.cpp',
	  '../ext/apache2/MessageChannel.h',
	  '../ext/apache2/System.o',
	  '../ext/boost/src/libboost_thread.a'] do
		create_executable "DummyRequestHandler", "DummyRequestHandler.cpp",
			"-I../ext -I../ext/apache2 #{CXXFLAGS} #{LDFLAGS} " <<
			"../ext/apache2/System.o " <<
			"../ext/boost/src/libboost_thread.a -lpthread"
	end
	
	task :clean do
		sh "rm -f DummyRequestHandler"
	end
end


##### Documentation

subdir 'doc' do
	ASCIIDOC = "asciidoc -a toc -a numbered -a toclevels=3 -a icons"
	ASCII_DOCS = ['Security of user switching support', 'Users guide',
		'Architectural overview']

	desc "Generate all documentation"
	task :doc => [:rdoc, :doxygen] + ASCII_DOCS.map{ |x| "#{x}.html" }
	
	ASCII_DOCS.each do |name|
		file "#{name}.html" => ["#{name}.txt"] do
			sh "#{ASCIIDOC} '#{name}.txt'"
		end
	end
	
	task :clobber => [:'doxygen:clobber'] do
		sh "rm -f *.html"
	end
	
	desc "Generate Doxygen C++ API documentation if necessary"
	task :doxygen => ['cxxapi']
	file 'cxxapi' => Dir['../ext/apache2/*.{h,c,cpp}'] do
		sh "doxygen"
	end

	desc "Force generation of Doxygen C++ API documentation"
	task :'doxygen:force' do
		sh "doxygen"
	end

	desc "Remove generated Doxygen C++ API documentation"
	task :'doxygen:clobber' do
		sh "rm -rf cxxapi"
	end
end

Rake::RDocTask.new do |rd|
	rd.main = "README"
	rd.rdoc_dir = "doc/rdoc"
	rd.rdoc_files.include("README", "DEVELOPERS.TXT",
		"lib/passenger/*.rb", "lib/rake/extensions.rb", "ext/passenger/*.c")
	rd.template = "./doc/template/horo"
	rd.title = "Passenger Ruby API"
	rd.options << "-S" << "-N" << "-p" << "-H"
end


##### Gem

spec = Gem::Specification.new do |s|
	s.platform = Gem::Platform::RUBY
	s.homepage = "http://www.modrails.com/"
	s.summary = "Apache module for Ruby on Rails support."
	s.name = "passenger"
	s.version = PACKAGE_VERSION
	s.rubyforge_project = "passenger"
	s.author = "Phusion - http://www.phusion.nl/"
	s.email = "info@phusion.nl"
	s.requirements << "fastthread" << "Apache 2 with development headers"
	s.require_path = "lib"
	s.add_dependency 'rake', '>= 0.8.1'
	s.add_dependency 'fastthread', '>= 1.0.1'
	s.extensions << 'ext/passenger/extconf.rb'
	s.files = FileList[
		'Rakefile',
		'README',
		'DEVELOPERS.TXT',
		'LICENSE',
		'INSTALL',
		'lib/**/*.rb',
		'lib/passenger/templates/*',
		'bin/*',
		'doc/*',
		'doc/*/*',
		'doc/*/*/*',
		'doc/*/*/*/*',
		'doc/*/*/*/*/*',
		'doc/*/*/*/*/*/*',
		'debian/*',
		'ext/apache2/*.{cpp,h,c,TXT}',
		'ext/boost/*.{hpp,TXT}',
		'ext/boost/**/*.{hpp,cpp,pl,inl}',
		'ext/passenger/*.{c,rb}',
		'benchmark/*.{cpp,rb}',
		'misc/*',
		'test/*.{rb,cpp,example}',
		'test/support/*',
		'test/ruby/*',
		'test/ruby/*/*',
		'test/stub/*',
		'test/stub/*/*',
		'test/stub/*/*/*',
		'test/stub/*/*/*/*',
		'test/stub/*/*/*/*/*',
		'test/stub/*/*/*/*/*/*',
		'test/stub/*/*/*/*/*/*/*'
	] - Dir['test/stub/*/log/*'] \
	  - Dir['test/stub/*/tmp/*/*'] \
	  - Dir['test/stub/apache2/*.{pid,lock,log}']
	s.executables = [
		'passenger-spawn-server',
		'passenger-install-apache2-module',
		'passenger-config',
		'passenger-memory-stats',
		'passenger-make-enterprisey'
	]
	s.has_rdoc = true
	s.extra_rdoc_files = ['README']
	s.rdoc_options <<
		"-S" << "-N" << "-p" << "-H" <<
		'--main' << 'README' <<
		'--template' << './doc/template/horo' <<
		'--title' << 'Passenger Ruby API'
	s.test_file = 'test/support/run_rspec_tests.rb'
	s.description = "Passenger is an Apache module for Ruby on Rails support."
end

Rake::GemPackageTask.new(spec) do |pkg|
	pkg.need_tar_gz = true
end

Rake::Task['package'].prerequisites.unshift(:doc)
Rake::Task['package:gem'].prerequisites.unshift(:doc)
Rake::Task['package:force'].prerequisites.unshift(:doc)
task :clobber => :'package:clean'


##### Misc

desc "Create a fakeroot, useful for building native packages"
task :fakeroot => [:apache2, :native_support, :doc] do
	require 'rbconfig'
	include Config
	fakeroot = "pkg/fakeroot"

	# We don't use CONFIG['archdir'] and the like because we want
	# the files to be installed to /usr, and the Ruby interpreter
	# on the packaging machine might be in /usr/local.
	libdir = "#{fakeroot}/usr/lib/ruby/#{CONFIG['ruby_version']}"
	extdir = "#{libdir}/#{CONFIG['arch']}"
	bindir = "#{fakeroot}/usr/bin"
	docdir = "#{fakeroot}/usr/share/doc/passenger"
	libexecdir = "#{fakeroot}/usr/libexec/passenger"
	
	sh "sudo rm -rf #{fakeroot}"
	sh "mkdir -p #{fakeroot}"
	
	sh "mkdir -p #{libdir}"
	sh "cp -R lib/passenger #{libdir}/"

	sh "mkdir -p #{fakeroot}/etc"
	sh "echo -n '#{PACKAGE_VERSION}' > #{fakeroot}/etc/passenger_version.txt"
	
	sh "mkdir -p #{extdir}/passenger"
	sh "cp -R ext/passenger/*.#{LIBEXT} #{extdir}/passenger/"
	
	sh "mkdir -p #{libexecdir}"
	sh "cp ext/apache2/mod_passenger.so #{libexecdir}/"
	
	sh "mkdir -p #{libexecdir}"
	sh "cp ext/apache2/ApplicationPoolServerExecutable #{libexecdir}/"
	
	sh "mkdir -p #{bindir}"
	sh "cp bin/* #{bindir}/"
	
	sh "mkdir -p #{docdir}"
	sh "cp -R doc/* #{docdir}/"
	sh "rm -f #{docdir}/{definitions.h,Doxyfile}"
end

desc "Create a Debian package"
task 'package:debian' => :fakeroot do
	fakeroot = "pkg/fakeroot"
	arch = `uname -m`.strip
	if arch =~ /^i.86$/
		arch = "i386"
	end
	
	sh "sed -i 's/Version: .*/Version: #{PACKAGE_VERSION}/' debian/control"
	sh "cp -R debian #{fakeroot}/DEBIAN"
	sh "sed -i 's/: any/: #{arch}/' #{fakeroot}/DEBIAN/control"
	sh "sudo chown -R root:root #{fakeroot}"
	sh "sudo dpkg -b #{fakeroot} pkg/passenger_#{PACKAGE_VERSION}-#{arch}.deb"
	sh "sudo chown -R `whoami` #{fakeroot}"
end


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
			"lib/passenger/*",
			"lib/rake/{cplusplus,extensions}.rb",
			"ext/apache2",
			"ext/passenger/*.c",
			"test/*.{cpp,rb}",
			"test/support/*.rb",
			"test/stub/*.rb",
			"benchmark/*.{cpp,rb}"
		]
	ensure
		system "rm -rf #{tmpdir}"
	end
end

