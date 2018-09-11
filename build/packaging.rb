#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2017 Phusion Holding B.V.
#
#  "Passenger", "Phusion Passenger" and "Union Station" are registered
#  trademarks of Phusion Holding B.V.
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

def recursive_copy_files(files, destination_dir, preprocess = false, variables = {})
  if !STDOUT.tty?
    puts "Copying files..."
  end
  files.each_with_index do |filename, i|
    dir = File.dirname(filename)
    if !File.exist?("#{destination_dir}/#{dir}")
      FileUtils.mkdir_p("#{destination_dir}/#{dir}")
    end
    if !File.directory?(filename)
      if preprocess && filename =~ /\.erb$/
        real_filename = filename.sub(/\.erb$/, '')
        FileUtils.install(filename, "#{destination_dir}/#{real_filename}", :preserve => true)
        Preprocessor.new.start(filename, "#{destination_dir}/#{real_filename}",
          variables)
      else
        FileUtils.install(filename, "#{destination_dir}/#{filename}", :preserve => true)
      end
    end
    if STDOUT.tty?
      printf "\r[%5d/%5d] [%3.0f%%] Copying files...", i + 1, files.size, i * 100.0 / files.size
      STDOUT.flush
    end
  end
  if STDOUT.tty?
    printf "\r[%5d/%5d] [%3.0f%%] Copying files...\n", files.size, files.size, 100
  end
end

task :clobber => 'package:clean'

task 'package:set_official' do
  ENV['OFFICIAL_RELEASE'] = '1'
  # These environment variables interfere with 'brew install'
  # and maybe other stuff, so unset them.
  ENV.delete('CC')
  ENV.delete('CXX')
  ENV.delete('USE_CCACHE')
end

task 'package:gem' => PhusionPassenger::Packaging::PREGENERATED_FILES do
  if ENV['OFFICIAL_RELEASE']
    release_file = "#{PhusionPassenger.resources_dir}/release.txt"
    File.unlink(release_file) rescue nil
  end
  begin
    if release_file
      File.open(release_file, "w").close
    end
    sh("gem build #{PhusionPassenger::PACKAGE_NAME}.gemspec")
  ensure
    if release_file
      File.unlink(release_file) rescue nil
    end
  end
  sh "mkdir -p #{PKG_DIR}"
  sh "mv #{PhusionPassenger::PACKAGE_NAME}-#{PhusionPassenger::VERSION_STRING}.gem #{PKG_DIR}/"
end

task 'package:tarball' => PhusionPassenger::Packaging::PREGENERATED_FILES do
  basename = "#{PhusionPassenger::PACKAGE_NAME}-#{PhusionPassenger::VERSION_STRING}"
  sh "rm -rf #{PKG_DIR}/#{basename}"
  sh "mkdir -p #{PKG_DIR}/#{basename}"
  recursive_copy_files(PhusionPassenger::Packaging.files, "#{PKG_DIR}/#{basename}")
  if ENV['OFFICIAL_RELEASE']
    File.open("#{PKG_DIR}/#{basename}/resources/release.txt", "w").close
  end
  if PlatformInfo.os_name_simple == "macosx"
    sh "cd #{PKG_DIR}/#{basename} && find . -print0 | xargs -0 touch -t '201310270000'"
  else
    sh "cd #{PKG_DIR}/#{basename} && find . -print0 | xargs -0 touch -d '2013-10-27 00:00:00 UTC'"
  end
  sh "cd #{PKG_DIR} && tar -c #{basename} | gzip --no-name --best > #{basename}.tar.gz"
  sh "rm -rf #{PKG_DIR}/#{basename}"
end

desc "Remove gem and tarball"
task 'package:clean' do
  basename = "#{PhusionPassenger::PACKAGE_NAME}-#{PhusionPassenger::VERSION_STRING}"
  sh "rm -f #{PKG_DIR}/#{basename}.{gem,gem.asc,tar.gz,tar.gz.asc}"
end

def change_shebang(filename, value)
  contents = File.open(filename, "r") do |f|
    f.read
  end
  contents.gsub!(/\A#\!.+$/, "#!#{value}")
  File.open(filename, "w") do |f|
    f.write(contents)
  end
end

desc "Create a fakeroot, useful for building native packages"
task :fakeroot => [:apache2, :nginx, 'nginx:as_dynamic_module', :doc] do
  require 'rbconfig'
  include RbConfig

  fs_prefix  = ENV['FS_PREFIX']  || "/usr"
  fs_bindir  = ENV['FS_BINDIR']  || "#{fs_prefix}/bin"
  fs_sbindir = ENV['FS_SBINDIR'] || "#{fs_prefix}/sbin"
  fs_datadir = ENV['FS_DATADIR'] || "#{fs_prefix}/share"
  fs_docdir  = ENV['FS_DOCDIR']  || "#{fs_datadir}/doc"
  fs_libdir  = ENV['FS_LIBDIR']  || "#{fs_prefix}/lib"

  # We don't use CONFIG['archdir'] and the like because we want
  # the files to be installed to /usr, and the Ruby interpreter
  # on the packaging machine might be in /usr/local.
  psg_rubylibdir = ENV['RUBYLIBDIR'] || "#{fs_libdir}/ruby/vendor_ruby"
  psg_nodelibdir = "#{fs_datadir}/#{GLOBAL_NAMESPACE_DIRNAME}/node"
  psg_libdir     = "#{fs_libdir}/#{GLOBAL_NAMESPACE_DIRNAME}"
  psg_native_support_dir = ENV["RUBYARCHDIR"] || "#{fs_libdir}/ruby/#{CONFIG['ruby_version']}/#{CONFIG['arch']}"
  psg_support_binaries_dir = "#{fs_libdir}/#{GLOBAL_NAMESPACE_DIRNAME}/support-binaries"
  psg_helper_scripts_dir = "#{fs_datadir}/#{GLOBAL_NAMESPACE_DIRNAME}/helper-scripts"
  psg_resources_dir      = "#{fs_datadir}/#{GLOBAL_NAMESPACE_DIRNAME}"
  psg_include_dir        = "#{fs_datadir}/#{GLOBAL_NAMESPACE_DIRNAME}/include"
  psg_docdir     = "#{fs_docdir}/#{GLOBAL_NAMESPACE_DIRNAME}"
  psg_bindir     = "#{fs_bindir}"
  psg_sbindir    = "#{fs_sbindir}"
  psg_apache2_module_path       = ENV['APACHE2_MODULE_PATH'] || "#{fs_libdir}/apache2/modules/mod_passenger.so"
  psg_ruby_extension_source_dir = "#{fs_datadir}/#{GLOBAL_NAMESPACE_DIRNAME}/ruby_extension_source"
  psg_nginx_module_source_dir   = "#{fs_datadir}/#{GLOBAL_NAMESPACE_DIRNAME}/ngx_http_passenger_module"
  psg_ruby       = ENV['RUBY'] || "#{fs_bindir}/ruby"
  psg_free_ruby  = ENV['FREE_RUBY'] || "/usr/bin/env ruby"

  fakeroot = "#{PKG_DIR}/fakeroot"
  fake_rubylibdir = "#{fakeroot}#{psg_rubylibdir}"
  fake_nodelibdir = "#{fakeroot}#{psg_nodelibdir}"
  fake_libdir     = "#{fakeroot}#{psg_libdir}"
  fake_native_support_dir = "#{fakeroot}#{psg_native_support_dir}"
  fake_support_binaries_dir = "#{fakeroot}#{psg_support_binaries_dir}"
  fake_helper_scripts_dir = "#{fakeroot}#{psg_helper_scripts_dir}"
  fake_resources_dir = "#{fakeroot}#{psg_resources_dir}"
  fake_include_dir   = "#{fakeroot}#{psg_include_dir}"
  fake_docdir     = "#{fakeroot}#{psg_docdir}"
  fake_bindir     = "#{fakeroot}#{psg_bindir}"
  fake_sbindir    = "#{fakeroot}#{psg_sbindir}"
  fake_apache2_module_path       = "#{fakeroot}#{psg_apache2_module_path}"
  fake_ruby_extension_source_dir = "#{fakeroot}#{psg_ruby_extension_source_dir}"
  fake_nginx_module_source_dir   = "#{fakeroot}#{psg_nginx_module_source_dir}"

  packaging_method = ENV['NATIVE_PACKAGING_METHOD'] || ENV['PACKAGING_METHOD'] || "deb"

  sh "rm -rf #{fakeroot}"
  sh "mkdir -p #{fakeroot}"

  # Ruby sources
  sh "mkdir -p #{fake_rubylibdir}"
  sh "cp #{PhusionPassenger.ruby_libdir}/phusion_passenger.rb #{fake_rubylibdir}/"
  sh "cp -R #{PhusionPassenger.ruby_libdir}/phusion_passenger #{fake_rubylibdir}/"

  # Node.js sources
  sh "mkdir -p #{fake_nodelibdir}"
  sh "cp -R #{PhusionPassenger.node_libdir}/* #{fake_nodelibdir}/"

  # C++ support libraries
  sh "mkdir -p #{fake_libdir}"
  sh "cp -R #{COMMON_OUTPUT_DIR} #{fake_libdir}/"
  sh "rm -rf #{fake_libdir}/common/libboost_oxt"
  sh "cp -R #{NGINX_DYNAMIC_OUTPUT_DIR} #{fake_libdir}/"
  sh "rm -rf #{fake_libdir}/nginx_dynamic/libboost_oxt"

  # Ruby extension binaries
  sh "mkdir -p #{fake_native_support_dir}"
  native_support_archdir = PlatformInfo.ruby_extension_binary_compatibility_id
  sh "mkdir -p #{fake_native_support_dir}"
  sh "cp -R buildout/ruby/#{native_support_archdir}/*.#{libext} #{fake_native_support_dir}/"

  # Support binaries
  sh "mkdir -p #{fake_support_binaries_dir}"
  sh "cp -R #{PhusionPassenger.support_binaries_dir}/* #{fake_support_binaries_dir}/"
  sh "rm -rf #{fake_support_binaries_dir}/*.dSYM"
  sh "rm -rf #{fake_support_binaries_dir}/*/*.dSYM"
  sh "rm -rf #{fake_support_binaries_dir}/*.o"

  # Helper scripts
  sh "mkdir -p #{fake_helper_scripts_dir}"
  sh "cp -R #{PhusionPassenger.helper_scripts_dir}/* #{fake_helper_scripts_dir}/"

  # Resources
  sh "mkdir -p #{fake_resources_dir}"
  sh "cp -R resources/* #{fake_resources_dir}/"

  # Headers necessary for building the Nginx module
  sh "mkdir -p #{fake_include_dir}"
  # Infer headers that the Nginx module needs
  headers = []
  Dir["src/nginx_module/**/*.[ch]"].each do |filename|
    File.read(filename).split("\n").grep(%r{#include "cxx_supportlib/(.+)"}) do |match|
      headers << ["src/cxx_supportlib/#{$1}", "cxx_supportlib/#{$1}"]
    end
  end
  # Manually add headers that could not be inferred through
  # the above code
  headers.concat([
    ["src/cxx_supportlib/Exceptions.h", "cxx_supportlib/Exceptions.h"],
    ["src/cxx_supportlib/AppTypeDetector/CBindings.h", "cxx_supportlib/AppTypeDetector/CBindings.h"],
    ["src/cxx_supportlib/WrapperRegistry/CBindings.h", "cxx_supportlib/WrapperRegistry/CBindings.h"],
    ["src/cxx_supportlib/JsonTools/CBindings.h", "cxx_supportlib/JsonTools/CBindings.h"],
    ["src/cxx_supportlib/vendor-modified/modp_b64.h", "cxx_supportlib/vendor-modified/modp_b64.h"],
    ["src/cxx_supportlib/vendor-modified/modp_b64_data.h", "cxx_supportlib/vendor-modified/modp_b64_data.h"]
  ])
  headers.each do |header|
    target = "#{fake_include_dir}/#{header[1]}"
    dir = File.dirname(target)
    if !File.directory?(dir)
      sh "mkdir -p #{dir}"
    end
    sh "cp #{header[0]} #{target}"
  end

  # Nginx module sources
  sh "mkdir -p #{fake_nginx_module_source_dir}"
  sh "cp -R src/nginx_module/* #{fake_nginx_module_source_dir}/"

  # Documentation
  sh "mkdir -p #{fake_docdir}"
  sh "cp -R doc/images #{fake_docdir}/"

  # User binaries
  sh "mkdir -p #{fake_bindir}"
  PhusionPassenger::Packaging::USER_EXECUTABLES.each do |exe|
    sh "cp bin/#{exe} #{fake_bindir}/"
    if PhusionPassenger::Packaging::EXECUTABLES_WITH_FREE_RUBY.include?(exe)
      shebang = psg_free_ruby
    else
      shebang = psg_ruby
    end
    change_shebang("#{fake_bindir}/#{exe}", shebang)
  end

  # Superuser binaries
  sh "mkdir -p #{fake_sbindir}"
  PhusionPassenger::Packaging::SUPER_USER_EXECUTABLES.each do |exe|
    sh "cp bin/#{exe} #{fake_sbindir}/"
    if PhusionPassenger::Packaging::EXECUTABLES_WITH_FREE_RUBY.include?(exe)
      shebang = psg_free_ruby
    else
      shebang = psg_ruby
    end
    change_shebang("#{fake_sbindir}/#{exe}", shebang)
  end

  # Apache 2 module
  sh "mkdir -p #{File.dirname(fake_apache2_module_path)}"
  sh "cp #{APACHE2_TARGET} #{fake_apache2_module_path}"

  # Ruby extension sources
  sh "mkdir -p #{fake_ruby_extension_source_dir}"
  sh "cp -R #{PhusionPassenger.ruby_extension_source_dir}/* #{fake_ruby_extension_source_dir}"

  puts "Creating #{fake_rubylibdir}/phusion_passenger/locations.ini"
  File.open("#{fake_rubylibdir}/phusion_passenger/locations.ini", "w") do |f|
    f.puts "[locations]"
    f.puts "packaging_method=#{packaging_method}"
    f.puts "bin_dir=#{psg_bindir}"
    f.puts "support_binaries_dir=#{psg_support_binaries_dir}"
    f.puts "lib_dir=#{psg_libdir}"
    f.puts "helper_scripts_dir=#{psg_helper_scripts_dir}"
    f.puts "resources_dir=#{psg_resources_dir}"
    f.puts "include_dir=#{psg_include_dir}"
    f.puts "doc_dir=#{psg_docdir}"
    f.puts "ruby_libdir=#{psg_rubylibdir}"
    f.puts "node_libdir=#{psg_nodelibdir}"
    f.puts "apache2_module_path=#{psg_apache2_module_path}"
    f.puts "ruby_extension_source_dir=#{psg_ruby_extension_source_dir}"
    f.puts "nginx_module_source_dir=#{psg_nginx_module_source_dir}"
  end

  # Sanity check the locations.ini file
  options = PhusionPassenger.parse_ini_file("#{fake_rubylibdir}/phusion_passenger/locations.ini")
  PhusionPassenger::REQUIRED_LOCATIONS_INI_FIELDS.each do |field|
    if !options[field.to_s]
      raise "Bug in build/packaging.rb: the generated locations.ini is missing the '#{field}' field"
    end
  end

  sh "find #{fakeroot} -name .DS_Store -print0 | xargs -0 rm -f"
end
