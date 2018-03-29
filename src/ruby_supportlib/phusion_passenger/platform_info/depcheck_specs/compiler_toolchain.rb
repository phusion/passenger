define 'cc' do
  name "C compiler"
  website "http://gcc.gnu.org/"
  define_checker do
    PhusionPassenger.require_passenger_lib 'platform_info/compiler'
    PlatformInfo.cc_block_support_ok? && check_for_command(PlatformInfo.cc, false)
  end

  on :debian do
    apt_get_install "build-essential"
  end
  on :mandriva do
    urpmi "gcc"
  end
  on :redhat do
    yum_install "gcc"
  end
  on :gentoo do
    emerge "gcc"
  end
  on :macosx do
    install_osx_command_line_tools
    append_install_instructions("   You must use an Apple compiler, with BLOCKS support.") unless PlatformInfo.cc_block_support_ok?
  end
end

define 'c++' do
  name "C++ compiler"
  website "http://gcc.gnu.org/"
  define_checker do
    PhusionPassenger.require_passenger_lib 'platform_info/compiler'
    PlatformInfo.cxx_block_support_ok? && check_for_command(PlatformInfo.cxx, false)
  end

  on :debian do
    apt_get_install "build-essential"
  end
  on :mandriva do
    urpmi "gcc-c++"
  end
  on :redhat do
    yum_install "gcc-c++"
  end
  on :gentoo do
    emerge "gcc"
  end
  on :macosx do
    install_osx_command_line_tools
    append_install_instructions("   You must use an Apple compiler, with BLOCKS support.") unless PlatformInfo.cxx_block_support_ok?
  end
end

define 'make' do
  name "The 'make' tool"
  define_checker do
    PhusionPassenger.require_passenger_lib 'platform_info/compiler'
    check_for_command(PlatformInfo.make)
  end

  on :debian do
    apt_get_install "build-essential"
  end
  on :mandriva do
    urpmi "make"
  end
  on :redhat do
    yum_install "make"
  end
  on :macosx do
    install_osx_command_line_tools
  end
  on :other_platforms do
    website "http://www.gnu.org/software/make/"
  end
end

define 'gmake' do
  name "GNU make"
  define_checker do
    PhusionPassenger.require_passenger_lib 'platform_info/compiler'
    check_for_command(PlatformInfo.gnu_make)
  end

  on :debian do
    apt_get_install "build-essential"
  end
  on :mandriva do
    urpmi "make"
  end
  on :redhat do
    yum_install "make"
  end
  on :macosx do
    install_osx_command_line_tools
  end
  on :other_platforms do
    website "http://www.gnu.org/software/make/"
  end
end
