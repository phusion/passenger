#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2016 Phusion Holding B.V.
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

ORIG_TARBALL_FILES = lambda { PhusionPassenger::Packaging.files }

def recursive_copy_files(files, destination_dir, preprocess = false, variables = {})
  require 'fileutils' if !defined?(FileUtils)
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

def word_wrap(text, max = 72)
  while index = (lines = text.split("\n")).find_index{ |line| line.size > max }
    line = lines[index]
    pos = max
    while pos >= 0 && line[pos..pos] != " "
      pos -= 1
    end
    if pos < 0
      raise "Cannot wrap line: #{line}"
    else
      lines[index] = line[0 .. pos - 1]
      lines.insert(index + 1, line[pos + 1 .. -1])
      text = lines.join("\n")
    end
  end
  return text
end

def is_open_source?
  return !is_enterprise?
end

def is_enterprise?
  return PACKAGE_NAME =~ /enterprise/
end

def enterprise_git_url
  return "TODO"
end

def git_tag_prefix
  if is_open_source?
    return "release"
  else
    return "enterprise"
  end
end

def git_tag
  return "#{git_tag_prefix}-#{VERSION_STRING}"
end

def apt_repo_name
  if is_open_source?
    "passenger"
  else
    "passenger-enterprise"
  end
end

def homebrew_dir
  return "/tmp/homebrew"
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

desc "Build, sign & upload gem & tarball"
task 'package:release' => ['package:set_official', 'package:gem', 'package:tarball', 'package:sign'] do
  PhusionPassenger.require_passenger_lib 'platform_info'
  require 'yaml'
  require 'uri'
  require 'net/http'
  require 'net/https'
  basename   = "#{PhusionPassenger::PACKAGE_NAME}-#{PhusionPassenger::VERSION_STRING}"
  version    = PhusionPassenger::VERSION_STRING
  is_beta    = !!version.split('.')[3]

  if !`git status --porcelain | grep -Ev '^\\?\\? '`.empty?
    STDERR.puts "-------------------"
    abort "*** ERROR: There are uncommitted files. See 'git status'"
  end

  begin
    website_config = YAML.load_file(File.expand_path("~/.passenger_website.yml"))
  rescue Errno::ENOENT
    STDERR.puts "-------------------"
    abort "*** ERROR: Please put the Phusion Passenger website admin " +
      "password in ~/.passenger_website.yml:\n" +
      "admin_password: ..."
  end

  if !PhusionPassenger::PlatformInfo.find_command("hub")
    STDERR.puts "-------------------"
    abort "*** ERROR: Please 'brew install hub' first"
  end

  if is_open_source?
    if boolean_option('HOMEBREW_UPDATE', true) && !is_beta
      puts "Updating Homebrew formula..."
      Rake::Task['package:update_homebrew'].invoke
    else
      puts "HOMEBREW_UPDATE set to false, not updating Homebrew formula."
    end
  end

  sh "git tag -s #{git_tag} -u 0A212A8C -m 'Release #{version}'"

  puts "Proceed with pushing tag to remote Git repo and uploading the gem and signatures? [y/n]"
  if STDIN.readline == "y\n"
    sh "git push origin #{git_tag}"

    if is_open_source?
      sh "s3cmd -P put #{PKG_DIR}/passenger-#{version}.{gem,tar.gz,gem.asc,tar.gz.asc} s3://phusion-passenger/releases/"
      sh "gem push #{PKG_DIR}/passenger-#{version}.gem"

      puts "Updating version number on website..."
      if is_beta
        uri = URI.parse("https://www.phusionpassenger.com/latest_beta_version")
      else
        uri = URI.parse("https://www.phusionpassenger.com/latest_stable_version")
      end
      http = Net::HTTP.new(uri.host, uri.port)
      http.use_ssl = true
      http.verify_mode = OpenSSL::SSL::VERIFY_PEER
      request = Net::HTTP::Post.new(uri.request_uri)
      request.basic_auth("admin", website_config["admin_password"])
      request.set_form_data("version" => version)
      response = http.request(request)
      if response.code != 200 && response.body != "ok"
        abort "*** ERROR: Cannot update version number on www.phusionpassenger.com:\n" +
          "Status: #{response.code}\n\n" +
          response.body
      end

      puts "Initiating building of binaries"
      Rake::Task['package:initiate_binaries_building'].invoke

      if !is_beta
        puts "Initiating building of Debian packages"
        Rake::Task['package:initiate_debian_building'].invoke
        puts "Initiating building of RPM packages"
        Rake::Task['package:initiate_rpm_building'].invoke
      end

      puts "Building OS X binaries..."
      Rake::Task['package:build_osx_binaries'].invoke

      if !is_beta && boolean_option('HOMEBREW_UPDATE', true)
        if boolean_option('HOMEBREW_DRY_RUN', false)
          puts "HOMEBREW_DRY_RUN set, not submitting pull request. Please find the repo in /tmp/homebrew."
        else
          puts "Submitting Homebrew pull request..."
          sh "cd #{homebrew_dir} && hub pull-request -m 'passenger #{version}' -b Homebrew:master"
        end
      end

      puts "--------------"
      puts "All done."
    else
      dir = "/u/apps/passenger_website/shared"
      subdir = string_option('NAME', version)

      sh "scp #{PKG_DIR}/#{basename}.{gem,tar.gz,gem.asc,tar.gz.asc} app@shell.phusion.nl:#{dir}/"
      sh "ssh app@shell.phusion.nl 'mkdir -p \"#{dir}/assets/#{subdir}\" && mv #{dir}/#{basename}.{gem,tar.gz,gem.asc,tar.gz.asc} \"#{dir}/assets/#{subdir}/\"'"
      command = "curl -F file=@#{PKG_DIR}/#{basename}.gem --user admin:'#{website_config['admin_password']}' " +
        "--output /dev/stderr --write-out '%{http_code}' --silent " +
        "https://www.phusionpassenger.com/enterprise_gems/upload"
      puts command
      result = `#{command}`
      if result != "200"
        abort "Gem upload failed. HTTP status code: #{result.inspect}"
      else
        # The response body does not contain a newline,
        # so fix terminal output.
        puts
      end

      puts "Initiating building of binaries"
      Rake::Task['package:initiate_binaries_building'].invoke

      if !is_beta
        puts "Initiating building of Debian packages"
        Rake::Task['package:initiate_debian_building'].invoke
        puts "Initiating building of RPM packages"
        Rake::Task['package:initiate_rpm_building'].invoke
      end

      puts "Building OS X binaries..."
      Rake::Task['package:build_osx_binaries'].invoke

      puts "--------------"
      puts "All done."
    end
  else
    puts "Did not upload anything."
  end
end

task 'package:gem' => Packaging::PREGENERATED_FILES do
  require 'phusion_passenger'
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

task 'package:tarball' => Packaging::PREGENERATED_FILES do
  require 'phusion_passenger'
  require 'fileutils'

  basename = "#{PhusionPassenger::PACKAGE_NAME}-#{PhusionPassenger::VERSION_STRING}"
  sh "rm -rf #{PKG_DIR}/#{basename}"
  sh "mkdir -p #{PKG_DIR}/#{basename}"
  recursive_copy_files(ORIG_TARBALL_FILES.call, "#{PKG_DIR}/#{basename}")
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

task 'package:sign' do
  if File.exist?(File.expand_path("~/.gnupg/gpg-agent.conf")) || ENV['GPG_AGENT_INFO']
    puts "It looks like you're using gpg-agent, so skipping automatically password caching."
  else
    begin
      require 'highline'
    rescue LoadError
      abort "Please run `gem install highline` first."
    end
    h = HighLine.new
    password = h.ask("Password for software-signing@phusion.nl GPG key: ") { |q| q.echo = false }
    passphrase_opt = "--passphrase-file .gpg-password"
  end

  begin
    if password
      File.open(".gpg-password", "w", 0600) do |f|
        f.write(password)
      end
    end
    version = PhusionPassenger::VERSION_STRING
    ["passenger-#{version}.gem",
     "passenger-#{version}.tar.gz",
     "passenger-enterprise-server-#{version}.gem",
     "passenger-enterprise-server-#{version}.tar.gz"].each do |name|
      if File.exist?("pkg/#{name}")
        sh "gpg --sign --detach-sign #{passphrase_opt} --local-user software-signing@phusion.nl --armor pkg/#{name}"
      end
    end
  ensure
    File.unlink('.gpg-password') if File.exist?('.gpg-password')
  end
end

task 'package:update_homebrew' do
  require 'digest/sha2'
  version = VERSION_STRING
  sha256 = File.open("#{PKG_DIR}/passenger-#{version}.tar.gz", "rb") do |f|
    Digest::SHA256.hexdigest(f.read)
  end
  if !File.exist?(homebrew_dir)
    sh "git clone git@github.com:phusion/homebrew-core.git #{homebrew_dir}"
    sh "cd #{homebrew_dir} && git remote add Homebrew https://github.com/Homebrew/homebrew-core.git"
  end
  sh "cd #{homebrew_dir} && git fetch Homebrew"
  sh "cd #{homebrew_dir} && git reset --hard Homebrew/master"
  formula = File.read("/tmp/homebrew/Formula/passenger.rb")
  formula.gsub!(/passenger-.+?\.tar\.gz/, "passenger-#{version}.tar.gz") ||
    abort("Unable to substitute Homebrew formula tarball filename")
  formula.gsub!(/^  sha256 .*/, "  sha256 \"#{sha256}\"") ||
    abort("Unable to substitute Homebrew formula SHA-256")
  necessary_dirs = ORIG_TARBALL_FILES.call.map{ |filename| filename.split("/").first }.uniq
  necessary_dirs -= Packaging::HOMEBREW_EXCLUDE
  necessary_dirs += ["buildout"]
  necessary_dirs_str = word_wrap(necessary_dirs.inspect).split("\n").join("\n      ")
  formula.sub!(/necessary_files = .*?\]/m, "necessary_files = Dir#{necessary_dirs_str}") ||
    abort("Unable to substitute file whitelist")
  File.open("/tmp/homebrew/Formula/passenger.rb", "w") do |f|
    f.write(formula)
  end
  sh "cd #{homebrew_dir} && git commit -a -m 'passenger #{version}'"
  sh "cd #{homebrew_dir} && git push -f"
  if boolean_option('HOMEBREW_TEST', true)
    sh "cp /tmp/homebrew/Formula/passenger.rb /usr/local/Homebrew/Library/Taps/homebrew/homebrew-core/Formula/passenger.rb"
    if `brew info passenger` !~ /^Not installed$/
      sh "brew uninstall passenger"
    end
    sh "cp #{PKG_DIR}/passenger-#{version}.tar.gz `brew --cache`/"
    sh "brew install passenger"
    Rake::Task['test:integration:native_packaging'].invoke
  end
end

task 'package:initiate_binaries_building' do
  require 'yaml'
  require 'uri'
  require 'net/http'
  require 'net/https'
  begin
    website_config = YAML.load_file(File.expand_path("~/.passenger_website.yml"))
  rescue Errno::ENOENT
    STDERR.puts "-------------------"
    abort "*** ERROR: Please put the Phusion Passenger website admin " +
      "password in ~/.passenger_website.yml:\n" +
      "admin_password: ..."
  end
  if is_open_source?
    type = "open%20source"
    jenkins_token = website_config["jenkins_token"]
    if !jenkins_token
      abort "*** ERROR: Please put the Passenger open source Jenkins " +
        "authentication token in ~/.passenger_website.yml, under " +
        "the 'jenkins_token' key."
    end
  else
    type = "Enterprise"
    jenkins_token = website_config["jenkins_enterprise_token"]
    if !jenkins_token
      abort "*** ERROR: Please put the Passenger Enterprise Jenkins " +
        "authentication token in ~/.passenger_website.yml, under " +
        "the 'jenkins_enterprise_token' key."
    end
  end

  uri = URI.parse("https://oss-jenkins.phusion.nl/buildByToken/buildWithParameters?" +
    "job=Passenger%20#{type}%20binaries%20(release)&ref=#{git_tag}&testing=false")
  http = Net::HTTP.new(uri.host, uri.port)
  http.use_ssl = true
  http.verify_mode = OpenSSL::SSL::VERIFY_PEER
  request = Net::HTTP::Post.new(uri.request_uri)
  request.set_form_data("token" => jenkins_token)
  response = http.request(request)
  if response.code != '201'
    abort "*** ERROR: Cannot initiate building of binaries:\n" +
      "Status: #{response.code}\n\n" +
      response.body
  end
  puts "Initiated building of binaries."
end

task 'package:initiate_debian_building' do
  require 'yaml'
  require 'uri'
  require 'net/http'
  require 'net/https'
  begin
    website_config = YAML.load_file(File.expand_path("~/.passenger_website.yml"))
  rescue Errno::ENOENT
    STDERR.puts "-------------------"
    abort "*** ERROR: Please put the Phusion Passenger website admin " +
      "password in ~/.passenger_website.yml:\n" +
      "admin_password: ..."
  end
  if is_open_source?
    type = "open%20source"
    jenkins_token = website_config["jenkins_token"]
    if !jenkins_token
      abort "*** ERROR: Please put the Passenger open source Jenkins " +
        "authentication token in ~/.passenger_website.yml, under " +
        "the 'jenkins_token' key."
    end
  else
    type = "Enterprise"
    jenkins_token = website_config["jenkins_enterprise_token"]
    if !jenkins_token
      abort "*** ERROR: Please put the Passenger Enterprise Jenkins " +
        "authentication token in ~/.passenger_website.yml, under " +
        "the 'jenkins_enterprise_token' key."
    end
  end

  uri = URI.parse("https://oss-jenkins.phusion.nl/buildByToken/buildWithParameters?" +
    "job=Passenger%20#{type}%20Debian%20packages%20(release)&ref=#{git_tag}&repo=#{apt_repo_name}")
  http = Net::HTTP.new(uri.host, uri.port)
  http.use_ssl = true
  http.verify_mode = OpenSSL::SSL::VERIFY_PEER
  request = Net::HTTP::Post.new(uri.request_uri)
  request.set_form_data("token" => jenkins_token)
  response = http.request(request)
  if response.code != '201'
    abort "*** ERROR: Cannot initiate building of Debian packages:\n" +
      "Status: #{response.code}\n\n" +
      response.body
  end
  puts "Initiated building of Debian packages."
end

task 'package:initiate_rpm_building' do
  require 'yaml'
  require 'uri'
  require 'net/http'
  require 'net/https'
  begin
    website_config = YAML.load_file(File.expand_path("~/.passenger_website.yml"))
  rescue Errno::ENOENT
    STDERR.puts "-------------------"
    abort "*** ERROR: Please put the Phusion Passenger website admin " +
      "password in ~/.passenger_website.yml:\n" +
      "admin_password: ..."
  end
  if is_open_source?
    type = "open%20source"
    jenkins_token = website_config["jenkins_token"]
    if !jenkins_token
      abort "*** ERROR: Please put the Passenger open source Jenkins " +
        "authentication token in ~/.passenger_website.yml, under " +
        "the 'jenkins_token' key."
    end
  else
    type = "Enterprise"
    jenkins_token = website_config["jenkins_enterprise_token"]
    if !jenkins_token
      abort "*** ERROR: Please put the Passenger Enterprise Jenkins " +
        "authentication token in ~/.passenger_website.yml, under " +
        "the 'jenkins_enterprise_token' key."
    end
  end

  uri = URI.parse("https://oss-jenkins.phusion.nl/buildByToken/buildWithParameters?" +
    "job=Passenger%20#{type}%20RPM%20packages%20(release)&ref=#{git_tag}&repo=#{apt_repo_name}")
  http = Net::HTTP.new(uri.host, uri.port)
  http.use_ssl = true
  http.verify_mode = OpenSSL::SSL::VERIFY_PEER
  request = Net::HTTP::Post.new(uri.request_uri)
  request.set_form_data("token" => jenkins_token)
  response = http.request(request)
  if response.code != '201'
    abort "*** ERROR: Cannot initiate building of RPM packages:\n" +
      "Status: #{response.code}\n\n" +
      response.body
  end
  puts "Initiated building of RPM packages."
end

task 'package:build_osx_binaries' do
  sh "env ENTERPRISE=#{!!is_enterprise?} TESTING=false " \
    "PASSENGER_ROOT=#{Shellwords.shellescape Dir.pwd} " \
    "./packaging/binaries/integration/publish/macos.sh"
end

desc "Remove gem, tarball and signatures"
task 'package:clean' do
  require 'phusion_passenger'
  basename = "#{PhusionPassenger::PACKAGE_NAME}-#{PhusionPassenger::VERSION_STRING}"
  sh "rm -f pkg/#{basename}.{gem,gem.asc,tar.gz,tar.gz.asc}"
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
  require 'fileutils'
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

  fakeroot = "pkg/fakeroot"
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
  sh "cp -R buildout/ruby/#{native_support_archdir}/*.#{LIBEXT} #{fake_native_support_dir}/"

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
  Dir["src/nginx_module/*.[ch]"].each do |filename|
    File.read(filename).split("\n").grep(%r{#include "cxx_supportlib/(.+)"}) do |match|
      headers << ["src/cxx_supportlib/#{$1}", "cxx_supportlib/#{$1}"]
    end
  end
  # Manually add headers that could not be inferred through
  # the above code
  headers.concat([
    ["src/cxx_supportlib/Exceptions.h", "cxx_supportlib/Exceptions.h"],
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
  sh "cp src/nginx_module/* #{fake_nginx_module_source_dir}/"

  # Documentation
  sh "mkdir -p #{fake_docdir}"
  sh "cp doc/*.html #{fake_docdir}/"
  sh "cp -R doc/images #{fake_docdir}/"

  # User binaries
  sh "mkdir -p #{fake_bindir}"
  Packaging::USER_EXECUTABLES.each do |exe|
    sh "cp bin/#{exe} #{fake_bindir}/"
    if Packaging::EXECUTABLES_WITH_FREE_RUBY.include?(exe)
      shebang = psg_free_ruby
    else
      shebang = psg_ruby
    end
    change_shebang("#{fake_bindir}/#{exe}", shebang)
  end

  # Superuser binaries
  sh "mkdir -p #{fake_sbindir}"
  Packaging::SUPER_USER_EXECUTABLES.each do |exe|
    sh "cp bin/#{exe} #{fake_sbindir}/"
    if Packaging::EXECUTABLES_WITH_FREE_RUBY.include?(exe)
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
