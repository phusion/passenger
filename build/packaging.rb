#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2010  Phusion
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

spec = Gem::Specification.new do |s|
	s.platform = Gem::Platform::RUBY
	s.homepage = "http://www.modrails.com/"
	s.summary = "Easy and robust Ruby web application deployment"
	s.name = "passenger"
	s.version = VERSION_STRING
	s.rubyforge_project = "passenger"
	s.author = "Phusion - http://www.phusion.nl/"
	s.email = "info@phusion.nl"
	s.require_paths = ["lib"]
	s.add_dependency 'rake', '>= 0.8.1'
	s.add_dependency 'fastthread', '>= 1.0.1'
	s.add_dependency 'daemon_controller', '>= 0.2.5'
	s.add_dependency 'file-tail'
	s.add_dependency 'rack'
	s.files = FileList[*Packaging::GLOB] - FileList[*Packaging::EXCLUDE_GLOB]
	s.executables = Packaging::USER_EXECUTABLES + Packaging::SUPER_USER_EXECUTABLES
	s.has_rdoc = true
	s.extra_rdoc_files = ['README']
	s.rdoc_options <<
		"-S" << "-N" << "-p" << "-H" <<
		'--main' << 'README' <<
		'--title' << 'Passenger Ruby API'
	s.description = "Easy and robust Ruby web application deployment."
end

Rake::GemPackageTask.new(spec) do |pkg|
	pkg.need_tar_gz = true
end

task 'package:filelist' do
	# The package:filelist task is used by Phusion Passenger Lite
	# to obtain a list of files that it must copy to a temporary
	# directory in order to compile Nginx and to compile Phusion
	# Passenger. The original Phusion Passenger sources might not
	# be writiable, e.g. when installed via a gem as root, hence
	# the reason for copying.
	#
	# The Asciidoc HTML files are in the packaging list, but
	# they're auto-generated and aren't included in the source
	# repository, so here we ensure that they exist. This is
	# generally only for people who are developing Phusion
	# Passenger itself; users who installed Phusion Passenger
	# via a gem, tarball or native package already have the
	# HTML files.
	#
	# The reason why we don't just specify Packaging::ASCII_DOCS
	# as a task dependency is because we only want to generate
	# the HTML files if they don't already exist; we don't want
	# to regenerate if they exist but their source .txt files
	# are modified. When the user installs Phusion Passenger
	# via a gem/tarball/package, all file timestamps are set
	# to the current clock time, which could lead Rake into
	# thinking that the source .txt files are modified. Since
	# the user probably has no write permission to the original
	# Phusion Passenger sources we want to avoid trying to
	# regenerate the HTML files.
	Packaging::ASCII_DOCS.each do |ascii_doc|
		Rake::Task[ascii_doc].invoke if !File.exist?(ascii_doc)
	end
	puts spec.files
end

Rake::Task['package'].prerequisites.unshift(:doc)
Rake::Task['package:gem'].prerequisites.unshift(:doc)
Rake::Task['package:force'].prerequisites.unshift(:doc)
task :clobber => :'package:clean'

desc "Create a fakeroot, useful for building native packages"
task :fakeroot => [:apache2, :nginx] + Packaging::ASCII_DOCS do
	require 'rbconfig'
	require 'fileutils'
	include Config
	fakeroot = "pkg/fakeroot"
	
	# We don't use CONFIG['archdir'] and the like because we want
	# the files to be installed to /usr, and the Ruby interpreter
	# on the packaging machine might be in /usr/local.
	fake_libdir = "#{fakeroot}/usr/lib/ruby/#{CONFIG['ruby_version']}"
	fake_native_support_dir = "#{fakeroot}/usr/lib/ruby/#{CONFIG['ruby_version']}/#{CONFIG['arch']}"
	fake_agents_dir = "#{fakeroot}#{NATIVELY_PACKAGED_AGENTS_DIR}"
	fake_helper_scripts_dir = "#{fakeroot}#{NATIVELY_PACKAGED_HELPER_SCRIPTS_DIR}"
	fake_docdir = "#{fakeroot}#{NATIVELY_PACKAGED_DOCDIR}"
	fake_bindir = "#{fakeroot}/usr/bin"
	fake_sbindir = "#{fakeroot}/usr/sbin"
	fake_source_root = "#{fakeroot}#{NATIVELY_PACKAGED_SOURCE_ROOT}"
	fake_apache2_module = "#{fakeroot}#{NATIVELY_PACKAGED_APACHE2_MODULE}"
	fake_apache2_module_dir = File.dirname(fake_apache2_module)
	fake_certificates_dir = "#{fakeroot}/usr/share/phusion-passenger/certificates"
	
	sh "rm -rf #{fakeroot}"
	sh "mkdir -p #{fakeroot}"
	
	sh "mkdir -p #{fake_libdir}"
	sh "cp #{LIBDIR}/phusion_passenger.rb #{fake_libdir}/"
	sh "cp -R #{LIBDIR}/phusion_passenger #{fake_libdir}/"
	
	sh "mkdir -p #{fake_native_support_dir}"
	native_support_archdir = PlatformInfo.ruby_extension_binary_compatibility_ids.join("-")
	sh "mkdir -p #{fake_native_support_dir}"
	sh "cp -R ext/ruby/#{native_support_archdir}/*.#{LIBEXT} #{fake_native_support_dir}/"
	
	sh "mkdir -p #{fake_agents_dir}"
	sh "cp -R #{AGENTS_DIR}/* #{fake_agents_dir}/"
	sh "rm -rf #{fake_agents_dir}/*.dSYM"
	sh "rm -rf #{fake_agents_dir}/*/*.dSYM"
	
	sh "mkdir -p #{fake_helper_scripts_dir}"
	sh "cp -R #{HELPER_SCRIPTS_DIR}/* #{fake_helper_scripts_dir}/"
	
	sh "mkdir -p #{fake_docdir}"
	Packaging::ASCII_DOCS.each do |docfile|
		sh "cp", docfile, "#{fake_docdir}/"
	end
	sh "cp -R doc/images #{fake_docdir}/"
	
	sh "mkdir -p #{fake_bindir}"
	Packaging::USER_EXECUTABLES.each do |exe|
		sh "cp bin/#{exe} #{fake_bindir}/"
	end
	
	sh "mkdir -p #{fake_sbindir}"
	Packaging::SUPER_USER_EXECUTABLES.each do |exe|
		sh "cp bin/#{exe} #{fake_sbindir}/"
	end
	
	sh "mkdir -p #{fake_apache2_module_dir}"
	sh "cp #{APACHE2_MODULE} #{fake_apache2_module_dir}/"
	
	sh "mkdir -p #{fake_certificates_dir}"
	sh "cp resources/*.crt #{fake_certificates_dir}/"
	
	sh "mkdir -p #{fake_source_root}"
	spec.files.each do |filename|
		next if File.directory?(filename)
		dirname = File.dirname(filename)
		if !File.directory?("#{fake_source_root}/#{dirname}")
			sh "mkdir -p '#{fake_source_root}/#{dirname}'"
		end
		puts "cp '#{filename}' '#{fake_source_root}/#{dirname}/'"
		FileUtils.cp(filename, "#{fake_source_root}/#{dirname}/")
	end
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
	raw_arch = `uname -m`.strip
	arch = case raw_arch
	when /^i.86$/
		"i386"
	when /^x86_64/
		"amd64"
	else
		raw_arch
	end
	
	sh "sed -i 's/Version: .*/Version: #{VERSION_STRING}/' debian/control"
	sh "cp -R debian #{fakeroot}/DEBIAN"
	sh "sed -i 's/: any/: #{arch}/' #{fakeroot}/DEBIAN/control"
	sh "chown -R root:root #{fakeroot}"
	sh "dpkg -b #{fakeroot} pkg/passenger_#{VERSION_STRING}-#{arch}.deb"
end