# encoding: utf-8
#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013 Phusion
#
#  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
#
#  Permission is hereby granted, free of charge, to any person obtaining a copy
#  of this software and associated documentation files (the "Software"), to deal
#  in the Software without restriction, including without limitation the rights
#  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#  copies of the Software, and to permit persons to whom the Software is
#  furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice shall be included in
#  all copies or substantial portions of the Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
#  THE SOFTWARE.

PhusionPassenger.require_passenger_lib 'constants'
require 'build/preprocessor'

# If you change the default distribution list, don't forget to update the configuration
# file in passenger_apt_automation too: https://github.com/phusion/passenger_apt_automation
ALL_DISTRIBUTIONS  = string_option("DEBIAN_DISTROS", "raring precise lucid").split(/[ ,]/)
DEBIAN_NAME        = "passenger"
DEBIAN_EPOCH       = 1
DEBIAN_ARCHS       = string_option("DEBIAN_ARCHS", "i386 amd64").split(/[ ,]/)
DEBIAN_ORIG_TARBALL_FILES = lambda { PhusionPassenger::Packaging.debian_orig_tarball_files }

def create_debian_package_dir(distribution, output_dir = PKG_DIR)
	require 'time'

	variables = {
		:distribution => distribution
	}

	root = "#{output_dir}/#{distribution}"
	orig_tarball = File.expand_path("#{PKG_DIR}/#{DEBIAN_NAME}_#{PACKAGE_VERSION}.orig.tar.gz")

	sh "rm -rf #{root}"
	sh "mkdir -p #{root}"
	sh "cd #{root} && tar xzf #{orig_tarball}"
	sh "bash -c 'shopt -s dotglob && mv #{root}/#{DEBIAN_NAME}_#{PACKAGE_VERSION}/* #{root}'"
	sh "rmdir #{root}/#{DEBIAN_NAME}_#{PACKAGE_VERSION}"
	recursive_copy_files(Dir["debian.template/**/*"], root,
		true, variables)
	sh "mv #{root}/debian.template #{root}/debian"
	changelog = File.read("#{root}/debian/changelog")
	changelog =
		"#{DEBIAN_NAME} (#{DEBIAN_EPOCH}:#{PACKAGE_VERSION}-1~#{distribution}1) #{distribution}; urgency=low\n" +
		"\n" +
		"  * Package built.\n" +
		"\n" +
		" -- #{MAINTAINER_NAME} <#{MAINTAINER_EMAIL}>  #{Time.now.rfc2822}\n\n" +
		changelog
	File.open("#{root}/debian/changelog", "w") do |f|
		f.write(changelog)
	end
end

task 'debian:orig_tarball' => Packaging::PREGENERATED_FILES do
	if File.exist?("#{PKG_DIR}/#{DEBIAN_NAME}_#{PACKAGE_VERSION}.orig.tar.gz")
		puts "WARNING: Debian orig tarball #{PKG_DIR}/#{DEBIAN_NAME}_#{PACKAGE_VERSION}.orig.tar.gz already exists. " +
			"It will not be regenerated. If you are sure that the orig tarball is outdated, please delete it " +
			"and rerun this task."
	else
		sh "mkdir -p #{PKG_DIR}"
		nginx_version = PhusionPassenger::PREFERRED_NGINX_VERSION
		local_nginx_tarball = File.expand_path("#{PKG_DIR}/nginx-#{nginx_version}.tar.gz")
		if File.exist?(local_nginx_tarball)
			puts "#{local_nginx_tarball} already exists"
		else
			sh "curl -L --fail -o #{local_nginx_tarball} http://nginx.org/download/nginx-#{nginx_version}.tar.gz"
		end
		sh "rm -rf #{PKG_DIR}/#{DEBIAN_NAME}_#{PACKAGE_VERSION}"
		sh "mkdir -p #{PKG_DIR}/#{DEBIAN_NAME}_#{PACKAGE_VERSION}"
		recursive_copy_files(DEBIAN_ORIG_TARBALL_FILES.call, "#{PKG_DIR}/#{DEBIAN_NAME}_#{PACKAGE_VERSION}")
		sh "cd #{PKG_DIR}/#{DEBIAN_NAME}_#{PACKAGE_VERSION} && tar xzf #{local_nginx_tarball}"
		sh "cd #{PKG_DIR} && find #{DEBIAN_NAME}_#{PACKAGE_VERSION} -print0 | xargs -0 touch -d '2013-10-27 00:00:00 UTC'"
		sh "cd #{PKG_DIR} && tar -c #{DEBIAN_NAME}_#{PACKAGE_VERSION} | gzip --no-name --best > #{DEBIAN_NAME}_#{PACKAGE_VERSION}.orig.tar.gz"
	end
end

desc "Build Debian source and binary package(s) for local testing"
task 'debian:dev' do
	sh "rm -f #{PKG_DIR}/#{DEBIAN_NAME}_#{PACKAGE_VERSION}.orig.tar.gz"
	Rake::Task["debian:clean"].invoke
	Rake::Task["debian:orig_tarball"].invoke
	if string_option('DISTRO').nil?
		distributions = [File.read("/etc/lsb-release").scan(/^DISTRIB_CODENAME=(.+)/).first.first]
	else
		distributions = ALL_DISTRIBUTIONS
	end
	distributions.each do |distribution|
		create_debian_package_dir(distribution)
		sh "cd #{PKG_DIR}/#{distribution} && dpkg-checkbuilddeps"
	end
	distributions.each do |distribution|
		sh "cd #{PKG_DIR}/#{distribution} && debuild -F -us -uc"
	end
end

desc "(Re)install the Debian binary packages built for local testing"
task 'debian:dev:reinstall' do
	package_names = ["passenger", "passenger-dev",
		"passenger-doc", "libapache2-mod-passenger"]
	package_names.each do |name|
		if Dir["#{PKG_DIR}/#{name}_*.deb"].size > 1
			abort "Please ensure that #{PKG_DIR} only has 1 version of the Phusion Passenger packages."
		end
	end
	package_names.each do |name|
		if !system "sudo apt-get remove -y #{name}"
			if !$? || $?.exitstatus != 100
				abort
			end
		end
	end
	package_names.each do |name|
		filename = Dir["#{PKG_DIR}/#{name}_*.deb"].first
		sh "sudo gdebi -n #{filename}"
	end
end

desc "Build official Debian source packages"
task 'debian:source_packages' => 'debian:orig_tarball' do
	if boolean_option('USE_CCACHE', false)
		# The resulting Debian rules file must not set USE_CCACHE.
		abort "USE_CCACHE must be returned off when running the debian:source_packages task."
	end

	pkg_dir = "#{PKG_DIR}/official"
	if File.exist?(pkg_dir)
		abort "#{pkg_dir} must not already exist when running the debian:source_packages task."
	end
	sh "mkdir #{pkg_dir}"
	sh "cd #{pkg_dir} && ln -s ../#{DEBIAN_NAME}_#{PACKAGE_VERSION}.orig.tar.gz ."

	ALL_DISTRIBUTIONS.each do |distribution|
		create_debian_package_dir(distribution, pkg_dir)
	end
	ALL_DISTRIBUTIONS.each do |distribution|
		sh "cd #{pkg_dir}/#{distribution} && debuild -S -us -uc"
	end
end

def pbuilder_base_name(distribution, arch)
	if arch == "amd64"
		return distribution
	else
		return "#{distribution}-#{arch}"
	end
end

def create_debian_binary_package_task(distribution, arch)
	task "debian:binary_package:#{distribution}_#{arch}" => 'debian:binary_packages:check' do
		require 'shellwords'
		base_name = "#{DEBIAN_NAME}_#{PACKAGE_VERSION}-1~#{distribution}1"
		logfile = "#{PKG_DIR}/official/passenger_#{distribution}_#{arch}.log"
		command = "cd #{PKG_DIR}/official && " +
			"pbuilder-dist #{distribution} #{arch} build #{base_name}.dsc " +
			"2>&1 | awk '{ print strftime(\"%Y-%m-%d %H:%M:%S -- \"), $0; fflush(); }'" +
			" | tee #{logfile}; test ${PIPESTATUS[0]} -eq 0"
		sh "bash -c #{Shellwords.escape(command)}"
		sh "echo Done >> #{logfile}"
	end
end

DEBIAN_BINARY_PACKAGE_TASKS = []
ALL_DISTRIBUTIONS.each do |distribution|
	DEBIAN_ARCHS.each do |arch|
		task = create_debian_binary_package_task(distribution, arch)
		DEBIAN_BINARY_PACKAGE_TASKS << task
	end
end

task 'debian:binary_packages:check' do
	pkg_dir = "#{PKG_DIR}/official"
	if !File.exist?(pkg_dir)
		abort "Please run rake debian:source_packages first."
	end

	pbuilder_dir = File.expand_path("~/pbuilder")
	ALL_DISTRIBUTIONS.each do |distribution|
		DEBIAN_ARCHS.each do |arch|
			pbase_name = pbuilder_base_name(distribution, arch) + "-base.tgz"
			if !File.exist?("#{pbuilder_dir}/#{pbase_name}")
				abort "Missing pbuilder environment for #{distribution}-#{arch}. " +
					"Please run this first: pbuilder-dist #{distribution} #{arch} create"
			end
		end
	end
end

desc "Build official Debian binary packages"
task 'debian:binary_packages' => DEBIAN_BINARY_PACKAGE_TASKS

desc "Clean Debian packaging products, except for orig tarball"
task 'debian:clean' do
	files = Dir["#{PKG_DIR}/*.{changes,build,deb,dsc,upload}"]
	sh "rm -f #{files.join(' ')}"
	sh "rm -rf #{PKG_DIR}/official"
	ALL_DISTRIBUTIONS.each do |distribution|
		sh "rm -rf #{PKG_DIR}/#{distribution}"
	end
	sh "rm -rf #{PKG_DIR}/*.debian.tar.gz"
end
