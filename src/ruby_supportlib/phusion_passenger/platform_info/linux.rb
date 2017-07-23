# encoding: binary
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

PhusionPassenger.require_passenger_lib 'platform_info'
PhusionPassenger.require_passenger_lib 'platform_info/operating_system'

module PhusionPassenger

  module PlatformInfo
    # An identifier for the current Linux distribution. nil if the operating system is not Linux.
    def self.linux_distro
      tags = linux_distro_tags
      if tags
        return tags.first
      else
        return nil
      end
    end

    # Autodetects the current Linux distribution and return a number of identifier tags.
    # The first tag identifies the distribution while the other tags indicate which
    # distributions it is likely compatible with.
    # Returns nil if the operating system is not Linux.
    def self.linux_distro_tags
      if os_name_simple != "linux"
        return nil
      end
      lsb_release = read_file("/etc/lsb-release")
      if lsb_release =~ /Ubuntu/
        return [:ubuntu, :debian]
      elsif File.exist?("/etc/debian_version")
        return [:debian]
      elsif File.exist?("/etc/redhat-release")
        redhat_release = read_file("/etc/redhat-release")
        if redhat_release =~ /CentOS/
          return [:centos, :redhat]
        elsif redhat_release =~ /Fedora/
          return [:fedora, :redhat]
        elsif redhat_release =~ /Mandriva/
          return [:mandriva, :redhat]
        else
          # On official RHEL distros, the content is in the form of
          # "Red Hat Enterprise Linux Server release 5.1 (Tikanga)"
          return [:rhel, :redhat]
        end
      elsif File.exist?("/etc/system-release")
        system_release = read_file("/etc/system-release")
        if system_release =~ /Amazon Linux/
          return [:amazon, :redhat]
        else
          return [:unknown]
        end
      elsif File.exist?("/etc/suse-release")
        return [:suse]
      elsif File.exist?("/etc/gentoo-release")
        return [:gentoo]
      else
        return [:unknown]
      end
      # TODO: Slackware
    end
    memoize :linux_distro_tags
  end

end # PhusionPassenger
