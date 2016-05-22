#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2015 Phusion Holding B.V.
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

module PhusionPassenger

  module Packaging
    # A list of HTML files that are generated with Asciidoc.
    ASCII_DOCS = [
      'doc/Users guide.html',
      'doc/Users guide Apache.html',
      'doc/Users guide Nginx.html',
      'doc/Users guide Standalone.html',
      'doc/Security of user switching support.html',
      'doc/Design and Architecture.html'
    ]

    # Files that must be generated before packaging.
    PREGENERATED_FILES = [
      'src/cxx_supportlib/Constants.h',
      'doc/Packaging.html',
      'doc/CloudLicensingConfiguration.html',
      'doc/ServerOptimizationGuide.html'
    ] + ASCII_DOCS

    USER_EXECUTABLES = [
      'passenger',
      'passenger-install-apache2-module',
      'passenger-install-nginx-module',
      'passenger-config'
    ]

    SUPER_USER_EXECUTABLES = [
      'passenger-status',
      'passenger-memory-stats'
    ]

    # Used during native packaging. Specifies executables for
    # which the shebang should NOT be set to #!/usr/bin/ruby,
    # so that these executables can be run with any Ruby interpreter
    # the user desires.
    EXECUTABLES_WITH_FREE_RUBY = [
      'passenger',
      'passenger-config',
      'passenger-install-apache2-module',
      'passenger-install-nginx-module'
    ]

    # A list of globs which match all files that should be packaged
    # in the Phusion Passenger gem or tarball.
    GLOB = [
      '.editorconfig',
      'configure',
      'Rakefile',
      'README.md',
      'CONTRIBUTORS',
      'CONTRIBUTING.md',
      'LICENSE',
      'CHANGELOG',
      'INSTALL.md',
      'NEWS',
      'package.json',
      'npm-shrinkwrap.json',
      'passenger.gemspec',
      'build/**/*',
      'bin/*',
      'doc/**/*',
      'man/*',
      'dev/**/*',
      'src/**/*',
      'resources/**/*'
    ]

    # Files that should be excluded from the gem or tarball. Overrides GLOB.
    EXCLUDE_GLOB = [
      '**/.DS_Store',
      '.gitignore',
      '.gitattributes',
      '.gitmodules',
      '.github/*',
      '.travis.yml',
      '.settings/*',
      '.externalToolBuilders/*',
      '.cproject',
      '.project',
      'Gemfile',
      'Gemfile.lock',
      'Vagrantfile',
      'Passenger.sublime-project',
      'Passenger.xcodeproj/**/*',
      'build/support/vendor/*/.*',
      'build/support/vendor/*/spec/**/*',
      'src/ruby_supportlib/phusion_passenger/vendor/*/.*',
      'src/ruby_supportlib/phusion_passenger/vendor/*/hacking/**/*',
      'src/ruby_supportlib/phusion_passenger/vendor/*/spec/**/*',
      'src/ruby_supportlib/phusion_passenger/vendor/union_station_hooks_rails/rails_test_apps/**/*',
      'packaging/**/*',
      'test/**/*'
    ]

    # Files and directories that should be excluded from the Homebrew installation.
    HOMEBREW_EXCLUDE = [
      "package.json", "npm-shrinkwrap.json"
    ]

    def self.files
      result = Dir[*GLOB] - Dir[*EXCLUDE_GLOB]
      result.reject! { |path| path =~ %r{/\.\.?$} }
      result
    end
  end

end # module PhusionPassenger
