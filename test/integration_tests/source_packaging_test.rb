#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2013-2014 Phusion
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

SOURCE_ROOT = File.expand_path("../..", File.dirname(__FILE__))
Dir.chdir(SOURCE_ROOT)
$LOAD_PATH.unshift("#{SOURCE_ROOT}/lib")
require 'phusion_passenger'
PhusionPassenger.locate_directories
PhusionPassenger.require_passenger_lib 'packaging'
require 'rubygems'
require 'tmpdir'
require 'fileutils'
require 'yaml'

PACKAGE_NAME = PhusionPassenger::PACKAGE_NAME
VERSION      = PhusionPassenger::VERSION_STRING
if RUBY_PLATFORM =~ /linux/
	TAR = "tar --warning=none"
else
	TAR = "tar"
end

def sh(*command)
	if !system(*command)
		abort "Command failed: #{command.join(' ')}"
	end
end

shared_examples_for "a proper package" do
	it "includes all files in the git repository" do
		git_files = `git ls-files`.split("\n")
		git_files.reject! { |filename| filename =~ /^Passenger.xcodeproj\// }
		git_files.delete(".gitmodules")
		git_files.delete("packaging/rpm")
		git_files.delete("ext/libeio/eio.3")
		git_files.delete("ext/libeio/eio.pod")

		package_files = {}
		Dir.chdir(@pkg_contents_dir) do
			`find .`.split("\n").each do |filename|
				filename.sub!(/^.\//, '')
				if !File.directory?(filename)
					package_files[filename] = true
				end
			end
		end

		error = false
		git_files.each do |filename|
			if !package_files[filename]
				error = true
				puts "File \"#{filename}\" is not in the package"
			end
		end
		raise "Some files are not in the package" if error
	end

	it "includes documentation HTML files" do
		File.exist?("#{@pkg_contents_dir}/doc/Users guide Apache.html").should be_true
		File.exist?("#{@pkg_contents_dir}/doc/Users guide Nginx.html").should be_true
		File.exist?("#{@pkg_contents_dir}/doc/Users guide Standalone.html").should be_true
	end
end

shared_examples_for "a user-generated package" do
	it "isn't marked official" do
		File.exist?("#{@pkg_contents_dir}/resources/release.txt").should be_false
	end
end

shared_examples_for "an official package" do
	it "is marked official" do
		File.exist?("#{@pkg_contents_dir}/resources/release.txt").should be_true
	end
end

describe "A user-generated gem" do
	before :all do
		ENV['PKG_DIR'] = @temp_dir = Dir.mktmpdir
		basename = "#{PACKAGE_NAME}-#{VERSION}"
		@pkg_contents_dir = "#{@temp_dir}/#{basename}"
		Dir.chdir(SOURCE_ROOT) do
			sh "rake", *PhusionPassenger::Packaging::PREGENERATED_FILES
			sh "gem build #{PACKAGE_NAME}.gemspec"
			sh "mv #{PACKAGE_NAME}-#{VERSION}.gem #{@temp_dir}/"
		end
		Dir.chdir(@temp_dir) do
			sh "#{TAR} -xf #{basename}.gem"
			sh "mkdir #{basename}"
			sh "gunzip metadata.gz"
			Dir.chdir(basename) do
				sh "#{TAR} -xzf ../data.tar.gz"
			end
		end
	end

	after :all do
		ENV.delete('PKG_DIR')
		FileUtils.remove_entry_secure(@temp_dir)
	end

	it_behaves_like "a proper package"
	it_behaves_like "a user-generated package"

	it "doesn't invoke the binaries downloader upon gem installation" do
		spec = YAML.load_file("#{@temp_dir}/metadata")
		spec.extensions.should be_empty
	end
end

describe "A user-generated tarball" do
	before :all do
		ENV['PKG_DIR'] = @temp_dir = Dir.mktmpdir
		basename = "#{PACKAGE_NAME}-#{VERSION}"
		@pkg_contents_dir = "#{@temp_dir}/#{basename}"
		sh "rake package:tarball"
		Dir.chdir(@temp_dir) do
			sh "#{TAR} -xzf #{basename}.tar.gz"
		end
	end

	after :all do
		ENV.delete('PKG_DIR')
		FileUtils.remove_entry_secure(@temp_dir)
	end

	it_behaves_like "a proper package"
	it_behaves_like "a user-generated package"
end

describe "An officially-generated gem" do
	before :all do
		ENV['PKG_DIR'] = @temp_dir = Dir.mktmpdir
		basename = "#{PACKAGE_NAME}-#{VERSION}"
		@pkg_contents_dir = "#{@temp_dir}/#{basename}"
		Dir.chdir(SOURCE_ROOT) do
			sh "rake package:set_official package:gem SKIP_SIGNING=1"
		end
		Dir.chdir(@temp_dir) do
			sh "#{TAR} -xf #{basename}.gem"
			sh "mkdir #{basename}"
			sh "gunzip metadata.gz"
			Dir.chdir(basename) do
				sh "#{TAR} -xzf ../data.tar.gz"
			end
		end
	end

	after :all do
		ENV.delete('PKG_DIR')
		FileUtils.remove_entry_secure(@temp_dir)
	end

	it_behaves_like "a proper package"
	it_behaves_like "an official package"

	it "invokes the binaries downloader upon gem installation" do
		spec = YAML.load_file("#{@temp_dir}/metadata")
		spec.extensions.should_not be_empty
	end
end

describe "An officially-generated tarball" do
	before :all do
		ENV['PKG_DIR'] = @temp_dir = Dir.mktmpdir
		basename = "#{PACKAGE_NAME}-#{VERSION}"
		@pkg_contents_dir = "#{@temp_dir}/#{basename}"
		sh "rake package:set_official package:tarball"
		Dir.chdir(@temp_dir) do
			sh "#{TAR} -xzf #{basename}.tar.gz"
		end
	end

	after :all do
		ENV.delete('PKG_DIR')
		FileUtils.remove_entry_secure(@temp_dir)
	end

	it_behaves_like "a proper package"
	it_behaves_like "an official package"
end
