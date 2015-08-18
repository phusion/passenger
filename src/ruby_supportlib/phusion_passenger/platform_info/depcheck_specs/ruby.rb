define 'ruby-dev' do
  name "Ruby development headers"
  website "http://www.ruby-lang.org/"
  define_checker do
    require 'rbconfig'
    begin
      require 'mkmf'
      rb_config = PlatformInfo.rb_config
      header_dir = rb_config['rubyhdrdir'] || rb_config['archdir']
      filename = "#{header_dir}/ruby.h"
      if File.exist?(filename)
        { :found => true, "Location" => filename }
      else
        false
      end
    rescue LoadError, SystemExit
      # On RedHat/Fedora/CentOS, if ruby-devel is not installed then
      # mkmf.rb will print an error and call 'exit'. So here we
      # catch SystemExit as well.
      false
    rescue NotImplementedError
      # JRuby raises this.
      false
    end
  end

  if ruby_command =~ %r(^/usr/bin/ruby) || ruby_command =~ %r(^/System/Library/Frameworks/Ruby.framework)
    # Only tell user to install the headers with the system's package manager
    # if Ruby itself was installed with the package manager.
    on :debian do
      apt_get_install "ruby-dev"
    end
    on :mandriva do
      urpmi "ruby-devel"
    end
    on :redhat do
      yum_install "ruby-devel"
    end
    on :macosx do
      install_osx_command_line_tools
    end
  end
  on :other_platforms do
    install_instructions "Please (re)install Ruby by downloading it from <b>#{website}</b>"
  end
end

define 'ruby-openssl' do
  name "OpenSSL support for Ruby"
  if RUBY_PLATFORM =~ /java/
    website "http://jruby.org/openssl"
    install_instructions "Please install OpenSSL support for JRuby: #{website}"
  else
    website "http://www.ruby-lang.org/"
    install_instructions "Please (re)install Ruby with OpenSSL support."
  end
  define_checker do
    begin
      require 'openssl'
      { :found => true }
    rescue LoadError
      false
    end
  end

  if ruby_command =~ %r(^/usr/bin/ruby)
    # Only tell user to install ruby-openssl with the system's package manager
    # if Ruby itself was installed with the package manager.
    on :debian do
      apt_get_install "libopenssl-ruby"
    end
  end
end

define 'rubygems' do
  name "RubyGems"
  website "http://rubyforge.org/frs/?group_id=126"
  define_checker do
    begin
      require 'rubygems'
      { :found => true }
    rescue LoadError
      false
    end
  end

  install_instructions "Please download it from <b>#{website}</b>. " +
    "Extract the tarball, and run <b>ruby setup.rb</b>"
  if ruby_command =~ %r(^/usr/bin/ruby)
    # Only tell user to install RubyGems with the system's package manager
    # if Ruby itself was installed with the package manager.
    #
    # Older versions of Debian have totally messed up RubyGems by patching it to install binaries
    # to /var/lib/gems/bin instead of /usr/bin or even /usr/local/bin. That
    # wouldn't be so much of a problem were it not for the fact that
    # /var/lib/gems/bin is not in $PATH by default, so on a regular basis people
    # ask various Ruby/Rails support forums why they get a 'foo: command not found'
    # after typing 'gem install foo'.
    #
    # Luckily newer Debian versions fixed this problem.
    on :debian do
      apt_get_install "rubygems"
    end
  end
end

# The 'rake' spec looks for a Rake instance that's installed for the same
# Ruby interpreter as the one that's currently running.
# For example if you're running this 'rake.rb' file with Ruby 1.8, then
# this checker will not find Ruby 1.9's Rake or JRuby's Rake.
define 'rake' do
  name "Rake (associated with #{ruby_command})"
  website "http://rake.rubyforge.org/"
  define_checker do
    PhusionPassenger.require_passenger_lib 'platform_info/ruby'
    if result = PlatformInfo.rake_command
      { :found => true,
        "Location" => result }
    else
      false
    end
  end

  if ruby_command =~ %r(^/usr/bin/ruby)
    # Only tell user to install Rake with the system's package manager
    # if Ruby itself was installed with the package manager.
    on :debian do
      apt_get_install "rake"
    end
    on :mandriva do
      urpmi "rake"
    end
    on :redhat do
      yum_install "rubygem-rake", :epel => true
    end
  end
  on :other_platforms do
    gem_install "rake"
  end
end
