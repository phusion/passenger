define 'apache2' do
  name 'Apache 2'
  website 'http://httpd.apache.org/'
  define_checker do
    PhusionPassenger.require_passenger_lib 'platform_info/apache'
    if check_for_command(PlatformInfo.httpd)
      {
        :found => true,
        "Location of httpd" => PlatformInfo.httpd,
        "Apache version"    => PlatformInfo.httpd_version
      }
    else
      false
    end
  end

  on :ubuntu do
    if `#{PlatformInfo::uname_command} -a`.include? 'precise'
      apt_get_install "apache2-mpm-worker"
    else
      apt_get_install "apache2"
    end
  end
  on :debian do
    apt_get_install "apache2-mpm-worker"
  end
  on :mandriva do
    urpmi "apache"
  end
  on :redhat do
    yum_install "httpd"
  end
  on :gentoo do
    emerge "apache"
  end
end

define 'apache2-dev' do
  name "Apache 2 development headers"
  website "http://httpd.apache.org/"
  define_checker do
    PhusionPassenger.require_passenger_lib 'platform_info/apache'
    if PlatformInfo.apxs2
      {
        :found => true,
        "Location of apxs2" => PlatformInfo.apxs2
      }
    else
      false
    end
  end

  on :ubuntu do
    if `#{PlatformInfo::uname_command} -a`.include? 'precise'
      apt_get_install "apache2-threaded-dev"
    else
      apt_get_install "apache2-dev"
    end
  end
  on :debian do
    if PlatformInfo::os_version >= '9.4'
      apt_get_install "apache2-dev"
    else
      apt_get_install "apache2-threaded-dev"
    end
  end
  on :mandriva do
    urpmi "apache-devel"
  end
  on :redhat do
    yum_install "httpd-devel"
  end
  on :gentoo do
    emerge "apache"
  end
  on :macosx do
    install_osx_command_line_tools
  end
end

define 'apr-dev' do
  name "Apache Portable Runtime (APR) development headers"
  website "http://httpd.apache.org/"
  define_checker do
    PhusionPassenger.require_passenger_lib 'platform_info/apache'
    if PlatformInfo.apr_config
      {
        :found     => true,
        "Location" => PlatformInfo.apr_config,
        "Version"  => `#{PlatformInfo.apr_config} --version`.strip
      }
    else
      false
    end
  end

  on :debian do
    apt_get_install "libapr1-dev"
  end
  on :mandriva do
    urpmi "libapr-devel"
  end
  on :redhat do
    yum_install "apr-devel"
  end
  on :gentoo do
    emerge "apr"
  end
  on :macosx do
    install_osx_command_line_tools
  end
end

define 'apu-dev' do
  name "Apache Portable Runtime Utility (APU) development headers"
  website "http://httpd.apache.org/"
  define_checker do
    PhusionPassenger.require_passenger_lib 'platform_info/apache'
    if PlatformInfo.apu_config
      {
        :found     => true,
        "Location" => PlatformInfo.apu_config,
        "Version"  => `#{PlatformInfo.apu_config} --version`.strip
      }
    else
      false
    end
  end

  on :debian do
    apt_get_install "libaprutil1-dev"
  end
  on :mandriva do
    urpmi "libapr-util-devel"
  end
  on :redhat do
    yum_install "apr-util-devel"
  end
  on :macosx do
    install_osx_command_line_tools
  end
end
