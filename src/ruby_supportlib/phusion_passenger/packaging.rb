#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2018 Phusion Holding B.V.
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
    # Files that must be generated before packaging.
    PREGENERATED_FILES = [
      'src/cxx_supportlib/Constants.h'
    ]

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
      'passenger.gemspec',
      'build/**/*',
      'bin/*',
      'doc/**/*',
      'images/*',
      'man/*',
      # Only inlcude the top-level scripts, required by e.g. the Homebrew packaging.
      'dev/*',
      'src/**/*',
      'resources/**/*',
      'resources/templates/error_renderer/.editorconfig'
    ]

    # Files that should be excluded from the gem or tarball. Overrides GLOB.
    #
    # This is not merely an exclusion list on top of GLOB! All files that you
    # do not want to include in the package must be explicitly specified here!
    # Otherwise source_packaging_test.rb will complain.
    EXCLUDE_GLOB = [
      '**/.DS_Store',
      '**/*.gch',
      '**/.editorconfig',
      '.externalToolBuilders/**/*',
      '.github/**/*',
      '.settings/**/*',
      '.vscode/**/*',
      '.cproject',
      '.gitattributes',
      '.gitignore',
      '.gitmodules',
      '.project',
      'CODE_OF_CONDUCT.md',
      'Gemfile',
      'Gemfile.lock',
      'Jenkinsfile',
      'Passenger.sublime-project',
      'Vagrantfile',
      'yarn.lock',
      'build/support/vendor/*/.*',
      'build/support/vendor/*/spec/**/*',
      'dev/*/**/*',
      'packaging/**/*',
      'src/ruby_supportlib/phusion_passenger/vendor/*/.*',
      'src/ruby_supportlib/phusion_passenger/vendor/*/hacking/**/*',
      'src/ruby_supportlib/phusion_passenger/vendor/*/spec/**/*',
      'src/cxx_supportlib/vendor-copy/*/.*',
      'test/**/*'
    ]

    def self.files
      result = Dir[*GLOB] - Dir[*EXCLUDE_GLOB]
      result.reject! { |path| path =~ %r{/\.\.?$} }
      result
    end
  end

end # module PhusionPassenger
