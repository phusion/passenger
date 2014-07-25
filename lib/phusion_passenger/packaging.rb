#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2013 Phusion
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
		'ext/common/Constants.h',
		'doc/Packaging.html'
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
		'.gitignore',
		'.travis.yml',
		'.editorconfig',
		'configure',
		'Rakefile',
		'Vagrantfile',
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
		'build/*.rb',
		'lib/*.rb',
		'lib/**/*.rb',
		'lib/**/*.py',
		'node_lib/**/*.js',
		'bin/*',
		'doc/**/*',
		'man/*',
		'debian.template/**/*',
		'packaging/**/*',
		'helper-scripts/**/*',
		'ext/common/**/*.{cpp,c,h,hpp,md,erb}',
		'ext/apache2/*.{cpp,h,hpp,c,erb}',
		'ext/nginx/*.{c,cpp,h,erb}',
		'ext/nginx/config',
		'ext/boost/**/*',
		'ext/libev/{LICENSE,Changes,README,Makefile.am,Makefile.in}',
		'ext/libev/{*.m4,autogen.sh,config.guess,config.h.in,config.sub}',
		'ext/libev/{configure,configure.ac,depcomp,install-sh,ltmain.sh,missing,mkinstalldirs}',
		'ext/libev/{*.h,*.c}',
		'ext/libeio/{LICENSE,Changes,README,Makefile.am,Makefile.in}',
		'ext/libeio/{*.m4,autogen.sh,config.guess,config.h.in,config.sub}',
		'ext/libeio/{configure,configure.ac,install-sh,ltmain.sh,missing,mkinstalldirs}',
		'ext/libeio/{*.h,*.c}',
		'ext/oxt/*.hpp',
		'ext/oxt/*.cpp',
		'ext/oxt/*.txt',
		'ext/oxt/detail/*.hpp',
		'ext/ruby/*.{c,rb}',
		'dev/**/*',
		'resources/**/*',
		'test/.rspec',
		'test/*.example',
		'test/*.travis',
		'test/*.rpm-automation',
		'test/*.vagrant',
		'test/*.supp',
		'test/support/*.{c,cpp,h,rb}',
		'test/tut/*',
		'test/cxx/**/*.{cpp,h}',
		'test/oxt/*.{cpp,hpp}',
		'test/ruby/**/*',
		'test/node/**/*',
		'test/integration_tests/**/*',
		'test/stub/**/*',
		'test/stub/**/.*'
	]
	
	EXCLUDE_GLOB = [
		'**/.DS_Store',
		'packaging/*/.git',
		'test/stub/rails_apps/3.0/empty/help/**/*',
		'test/stub/*.dSYM'
	]

	# Files that should be excluded from the Debian tarball.
	DEBIAN_EXCLUDE_GLOB = [
		"debian.template/**/*",
		"packaging/**/*",
	]

	# Files and directories that should be excluded from the Homebrew installation.
	HOMEBREW_EXCLUDE = [
		".gitignore", ".gitmodules", ".travis.yml", "package.json", "Vagrantfile",
		"npm-shrinkwrap.json", "debian.template", "packaging", "dev", "test"
	]

	def self.files
		result = Dir[*GLOB] - Dir[*EXCLUDE_GLOB]
		result.reject! { |path| path =~ %r{/\.\.?$} }
		return result
	end

	def self.debian_orig_tarball_files
		return files - Dir[*DEBIAN_EXCLUDE_GLOB]
	end
end

end # module PhusionPassenger
