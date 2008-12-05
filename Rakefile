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

verbose true

##### Configuration

# Don't forget to edit Configuration.h too
PACKAGE_VERSION = "2.1.0"
OPTIMIZE = ["yes", "on", "true"].include?(ENV['OPTIMIZE'])

include PlatformInfo
APXS2.nil? and raise "Could not find 'apxs' or 'apxs2'."
APACHE2CTL.nil? and raise "Could not find 'apachectl' or 'apache2ctl'."
HTTPD.nil? and raise "Could not find the Apache web server binary."
APR_FLAGS.nil? and raise "Could not find Apache Portable Runtime (APR)."
APU_FLAGS.nil? and raise "Could not find Apache Portable Runtime Utility (APU)."

CXX = "g++"
# _GLIBCPP__PTHREADS is for fixing Boost compilation on OpenBSD.
THREADING_FLAGS = "-D_REENTRANT -D_GLIBCPP__PTHREADS"
if OPTIMIZE
	OPTIMIZATION_FLAGS = "-O2 -DBOOST_DISABLE_ASSERTS"
else
	OPTIMIZATION_FLAGS = "-g -DPASSENGER_DEBUG -DBOOST_DISABLE_ASSERTS"
end
CXXFLAGS = "#{OPTIMIZATION_FLAGS} #{THREADING_FLAGS} #{MULTI_ARCH_FLAGS} -Wall -I/usr/local/include"
LDFLAGS = "#{MULTI_ARCH_LDFLAGS}"

#### Default tasks

desc "Build everything"
task :default => [
	:native_support,
	:apache2,
	'test/oxt/oxt_test_main',
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
		sh "#{RUBY} extconf.rb"
	end
	
	file "native_support.#{LIBEXT}" => ['Makefile', 'native_support.c'] do
		sh "make"
	end
	
	task :clean do
		sh "make clean" if File.exist?('Makefile')
		sh "rm -f Makefile"
	end
end


##### boost::thread and OXT static library

file 'ext/libboost_oxt.a' =>
	Dir['ext/boost/src/*.cpp'] +
	Dir['ext/boost/src/pthread/*.cpp'] +
	Dir['ext/oxt/*.cpp'] +
	Dir['ext/oxt/*.hpp'] +
	Dir['ext/oxt/detail/*.hpp'] do
	Dir.chdir('ext/boost/src') do
		puts "### In ext/boost/src:"
		flags = "-I../.. -fPIC #{OPTIMIZATION_FLAGS} #{THREADING_FLAGS} #{MULTI_ARCH_FLAGS}"
		compile_cxx "*.cpp", flags
		# NOTE: 'compile_cxx "pthread/*.cpp", flags' doesn't work on some systems,
		# so we do this instead.
		Dir['pthread/*.cpp'].each do |file|
			compile_cxx file, flags
		end
	end
	Dir.chdir('ext/oxt') do
		puts "### In ext/oxt:"
		compile_cxx "*.cpp", "-I.. -fPIC #{CXXFLAGS}"
	end
	create_static_library "ext/libboost_oxt.a", "ext/boost/src/*.o ext/oxt/*.o"
end

task :clean do
	sh "rm -f ext/libboost_oxt.a ext/boost/src/*.o ext/oxt/*.o"
end


##### Apache module

class APACHE2
	CXXFLAGS = "-I.. -fPIC #{OPTIMIZATION_FLAGS} #{APR_FLAGS} #{APU_FLAGS} #{APXS2_FLAGS} #{CXXFLAGS}"
	OBJECTS = {
		'Configuration.o' => %w(Configuration.cpp Configuration.h),
		'Bucket.o' => %w(Bucket.cpp Bucket.h),
		'Hooks.o' => %w(Hooks.cpp Hooks.h
				Configuration.h ApplicationPool.h ApplicationPoolServer.h
				SpawnManager.h Exceptions.h Application.h MessageChannel.h
				PoolOptions.h Utils.h DirectoryMapper.h),
		'Utils.o'   => %w(Utils.cpp Utils.h),
		'Logging.o' => %w(Logging.cpp Logging.h)
	}
end

subdir 'ext/apache2' do
	apxs_objects = APACHE2::OBJECTS.keys.join(',')

	desc "Build mod_passenger Apache 2 module"
	task :apache2 => ['mod_passenger.so', 'ApplicationPoolServerExecutable', :native_support]
	
	file 'mod_passenger.so' => [
		'../libboost_oxt.a',
		'mod_passenger.o'
	] + APACHE2::OBJECTS.keys do
		# apxs totally sucks. We couldn't get it working correctly
		# on MacOS X (it had various problems with building universal
		# binaries), so we decided to ditch it and build/install the
		# Apache module ourselves.
		#
		# Oh, and libtool sucks too. Do we even need it anymore in 2008?
		linkflags = "#{LDFLAGS} #{MULTI_ARCH_FLAGS}"
		linkflags << " -lstdc++ -lpthread " <<
			"../libboost_oxt.a " <<
			APR_LIBS
		create_shared_library 'mod_passenger.so',
			APACHE2::OBJECTS.keys.join(' ') << ' mod_passenger.o',
			linkflags
	end
	
	file 'ApplicationPoolServerExecutable' => [
		'../libboost_oxt.a',
		'ApplicationPoolServerExecutable.cpp',
		'ApplicationPool.h',
		'StandardApplicationPool.h',
		'MessageChannel.h',
		'SpawnManager.h',
		'PoolOptions.h',
		'Utils.o',
		'Logging.o'
	] do
		create_executable "ApplicationPoolServerExecutable",
			'ApplicationPoolServerExecutable.cpp Utils.o Logging.o',
			"-I.. #{CXXFLAGS} #{LDFLAGS} " <<
			"../libboost_oxt.a " <<
			"-lpthread"
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
	CXXFLAGS = "#{::CXXFLAGS} -DTESTING_SPAWN_MANAGER -DTESTING_APPLICATION_POOL "

	AP2_FLAGS = "-I../ext/apache2 -I../ext -Isupport #{APR_FLAGS} #{APU_FLAGS}"
	AP2_OBJECTS = {
		'CxxTestMain.o' => %w(CxxTestMain.cpp),
		'MessageChannelTest.o' => %w(MessageChannelTest.cpp
			../ext/apache2/MessageChannel.h),
		'SpawnManagerTest.o' => %w(SpawnManagerTest.cpp
			../ext/apache2/SpawnManager.h
			../ext/apache2/PoolOptions.h
			../ext/apache2/Application.h
			../ext/apache2/MessageChannel.h),
		'ApplicationPoolServerTest.o' => %w(ApplicationPoolServerTest.cpp
			../ext/apache2/ApplicationPoolServer.h
			../ext/apache2/PoolOptions.h
			../ext/apache2/MessageChannel.h),
		'ApplicationPoolServer_ApplicationPoolTest.o' => %w(ApplicationPoolServer_ApplicationPoolTest.cpp
			ApplicationPoolTest.cpp
			../ext/apache2/ApplicationPoolServer.h
			../ext/apache2/ApplicationPool.h
			../ext/apache2/SpawnManager.h
			../ext/apache2/PoolOptions.h
			../ext/apache2/Application.h
			../ext/apache2/MessageChannel.h),
		'StandardApplicationPoolTest.o' => %w(StandardApplicationPoolTest.cpp
			ApplicationPoolTest.cpp
			../ext/apache2/ApplicationPool.h
			../ext/apache2/StandardApplicationPool.h
			../ext/apache2/SpawnManager.h
			../ext/apache2/PoolOptions.h
			../ext/apache2/Application.h),
		'PoolOptionsTest.o' => %w(PoolOptionsTest.cpp ../ext/apache2/PoolOptions.h),
		'UtilsTest.o' => %w(UtilsTest.cpp ../ext/apache2/Utils.h)
	}
	
	OXT_FLAGS = "-I../../ext -I../support"
	OXT_OBJECTS = {
		'oxt_test_main.o' => %w(oxt_test_main.cpp),
		'backtrace_test.o' => %w(backtrace_test.cpp),
		'syscall_interruption_test.o' => %w(syscall_interruption_test.cpp)
	}
end

subdir 'test' do
	desc "Run all unit tests (but not integration tests)"
	task :test => [:'test:oxt', :'test:apache2', :'test:ruby', :'test:integration']
	
	desc "Run unit tests for the OXT library"
	task 'test:oxt' => 'oxt/oxt_test_main' do
		sh "./oxt/oxt_test_main"
	end
	
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
	
	file 'oxt/oxt_test_main' => TEST::OXT_OBJECTS.keys.map{ |x| "oxt/#{x}" } +
	  ['../ext/libboost_oxt.a'] do
		Dir.chdir('oxt') do
			objects = TEST::OXT_OBJECTS.keys.join(' ')
			create_executable "oxt_test_main", objects,
				"#{LDFLAGS} #{MULTI_ARCH_FLAGS} " <<
				"../../ext/libboost_oxt.a " <<
				"-lpthread"
		end
	end
	
	TEST::OXT_OBJECTS.each_pair do |target, sources|
		file "oxt/#{target}" => sources.map{ |x| "oxt/#{x}" } do
			Dir.chdir('oxt') do
				puts "### In test/oxt:"
				compile_cxx sources[0], "#{TEST::OXT_FLAGS} #{TEST::CXXFLAGS}"
			end
		end
	end

	file 'Apache2ModuleTests' => TEST::AP2_OBJECTS.keys +
	  ['../ext/libboost_oxt.a',
	   '../ext/apache2/Utils.o',
	   '../ext/apache2/Logging.o'] do
		objects = TEST::AP2_OBJECTS.keys.join(' ') <<
			" ../ext/apache2/Utils.o" <<
			" ../ext/apache2/Logging.o"
		create_executable "Apache2ModuleTests", objects,
			"#{LDFLAGS} #{APR_LIBS} #{MULTI_ARCH_FLAGS} " <<
			"../ext/libboost_oxt.a " <<
			"-lpthread"
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
		sh "rm -f oxt/oxt_test_main oxt/*.o Apache2ModuleTests *.o"
	end
end


##### Benchmarks

subdir 'benchmark' do
	file 'DummyRequestHandler' => ['DummyRequestHandler.cpp',
	  '../ext/apache2/MessageChannel.h',
	  '../ext/libboost_oxt.a'] do
		create_executable "DummyRequestHandler", "DummyRequestHandler.cpp",
			"-I../ext -I../ext/apache2 #{CXXFLAGS} #{LDFLAGS} " <<
			"../ext/libboost_oxt.a " <<
			"-lpthread"
	end
	
	file 'ApplicationPool' => ['ApplicationPool.cpp',
	  '../ext/apache2/StandardApplicationPool.h',
	  '../ext/apache2/ApplicationPoolServerExecutable',
	  '../ext/apache2/Logging.o',
	  '../ext/apache2/Utils.o',
	  '../ext/libboost_oxt.a',
	  :native_support] do
		create_executable "ApplicationPool", "ApplicationPool.cpp",
			"-I../ext -I../ext/apache2 #{CXXFLAGS} #{LDFLAGS} " <<
			"../ext/apache2/Logging.o " <<
			"../ext/apache2/Utils.o " <<
			"../ext/libboost_oxt.a " <<
			"-lpthread"
	end
	
	task :clean do
		sh "rm -f DummyRequestHandler ApplicationPool"
	end
end


##### Documentation

subdir 'doc' do
  ASCIIDOC = 'asciidoc'
	ASCIIDOC_FLAGS = "-a toc -a numbered -a toclevels=3 -a icons"
	ASCII_DOCS = ['Security of user switching support', 'Users guide',
		'Architectural overview']

	DOXYGEN = 'doxygen'
	
	desc "Generate all documentation"
	task :doc => [:rdoc]
	
	if PlatformInfo.find_command(DOXYGEN)
		task :doc => :doxygen
	end

	task :doc => ASCII_DOCS.map{ |x| "#{x}.html" }

	ASCII_DOCS.each do |name|
		file "#{name}.html" => ["#{name}.txt"] do
			if PlatformInfo.find_command(ASCIIDOC)
		  	sh "#{ASCIIDOC} #{ASCIIDOC_FLAGS} '#{name}.txt'"
			else
				sh "echo 'asciidoc required to build docs' > '#{name}.html'"
			end
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

Rake::RDocTask.new(:clobber_rdoc => "rdoc:clobber", :rerdoc => "rdoc:force") do |rd|
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
	s.require_path = ["lib", "ext"]
	s.add_dependency 'rake', '>= 0.8.1'
	s.add_dependency 'fastthread', '>= 1.0.1'
	s.add_dependency 'rack', '>= 0.1.0'
	s.extensions << 'ext/passenger/extconf.rb'
	s.files = FileList[
		'Rakefile',
		'README',
		'DEVELOPERS.TXT',
		'LICENSE',
		'INSTALL',
		'NEWS',
		'lib/**/*.rb',
		'lib/**/*.py',
		'lib/passenger/templates/*',
		'bin/*',
		'doc/*',
		
		# If you're running 'rake package' for the first time, then these
		# files don't exist yet, and so won't be matched by the above glob.
		# So we add these filenames manually.
		'doc/Users guide.html',
		'doc/Security of user switching support.html',
		
		'doc/*/*',
		'doc/*/*/*',
		'doc/*/*/*/*',
		'doc/*/*/*/*/*',
		'doc/*/*/*/*/*/*',
		'man/*',
		'debian/*',
		'ext/apache2/*.{cpp,h,c,TXT}',
		'ext/boost/*.{hpp,TXT}',
		'ext/boost/**/*.{hpp,cpp,pl,inl,ipp}',
		'ext/oxt/*.hpp',
		'ext/oxt/*.cpp',
		'ext/oxt/detail/*.hpp',
		'ext/passenger/*.{c,rb}',
		'benchmark/*.{cpp,rb}',
		'misc/*',
		'test/*.{rb,cpp,example}',
		'test/support/*',
		'test/oxt/*.cpp',
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
		'passenger-make-enterprisey',
		'passenger-status',
		'passenger-stress-test'
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
	libexecdir = "#{fakeroot}/usr/lib/passenger"
	
	sh "rm -rf #{fakeroot}"
	sh "mkdir -p #{fakeroot}"
	
	sh "mkdir -p #{libdir}"
	sh "cp -R lib/passenger #{libdir}/"

	sh "mkdir -p #{fakeroot}/etc"
	sh "echo -n '#{PACKAGE_VERSION}' > #{fakeroot}/etc/passenger_version.txt"
	
	sh "mkdir -p #{extdir}/passenger"
	sh "cp -R ext/passenger/*.#{LIBEXT} #{extdir}/passenger/"
	
	sh "mkdir -p #{bindir}"
	sh "cp bin/* #{bindir}/"
	
	sh "mkdir -p #{libexecdir}"
	sh "cp ext/apache2/mod_passenger.so #{libexecdir}/"
	sh "mv #{fakeroot}/usr/bin/passenger-spawn-server #{libexecdir}/"
	sh "cp ext/apache2/ApplicationPoolServerExecutable #{libexecdir}/"
	
	sh "mkdir -p #{docdir}"
	sh "cp -R doc/* #{docdir}/"
	sh "rm", "-rf", *Dir["#{docdir}/{definitions.h,Doxyfile,template}"]
end

desc "Create a Debian package"
task 'package:debian' => :fakeroot do
	if Process.euid != 0
		STDERR.puts
		STDERR.puts "*** ERROR: the 'package:debian' task must be run as root."
		STDERR.puts
		exit 1
	end

	fakeroot = "pkg/fakeroot"
	arch = `uname -m`.strip
	if arch =~ /^i.86$/
		arch = "i386"
	end
	
	sh "sed -i 's/Version: .*/Version: #{PACKAGE_VERSION}/' debian/control"
	sh "cp -R debian #{fakeroot}/DEBIAN"
	sh "sed -i 's/: any/: #{arch}/' #{fakeroot}/DEBIAN/control"
	sh "chown -R root:root #{fakeroot}"
	sh "dpkg -b #{fakeroot} pkg/passenger_#{PACKAGE_VERSION}-#{arch}.deb"
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
			"ext/oxt",
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

