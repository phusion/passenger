#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010 Phusion
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
		'doc/Users guide Apache.html',
		'doc/Users guide Nginx.html',
		'doc/Security of user switching support.html',
		'doc/Architectural overview.html'
	]
	
	# A list of globs which match all files that should be packaged
	# in the Phusion Passenger gem or tarball.
	GLOB = [
		'Rakefile',
		'README',
		'DEVELOPERS.TXT',
		'PACKAGING.TXT',
		'LICENSE',
		'INSTALL',
		'NEWS',
		'lib/*.rb',
		'lib/**/*.rb',
		'lib/**/*.py',
		'lib/phusion_passenger/templates/*',
		'lib/phusion_passenger/templates/apache2/*',
		'lib/phusion_passenger/templates/nginx/*',
		'lib/phusion_passenger/templates/lite/*',
		'lib/phusion_passenger/templates/lite_default_root/*',
		'bin/*',
		'doc/**/*',
		'man/*',
		'debian/*',
		'helper-scripts/*',
		'ext/common/*.{cpp,c,h,hpp}',
		'ext/common/ApplicationPool/*.h',
		'ext/apache2/*.{cpp,h,hpp,c,TXT}',
		'ext/nginx/*.{c,cpp,h}',
		'ext/nginx/config',
		'ext/boost/**/*',
		'ext/google/**/*',
		'ext/oxt/*.hpp',
		'ext/oxt/*.cpp',
		'ext/oxt/detail/*.hpp',
		'ext/phusion_passenger/*.{c,rb}',
		'misc/**/*',
		'test/*.example',
		'test/support/*.{cpp,h,rb}',
		'test/tut/*',
		'test/cxx/*.{cpp,h}',
		'test/oxt/*.{cpp,hpp}',
		'test/ruby/**/*',
		'test/integration_tests/**/*',
		'test/stub/**/*'
	
	# If you're running 'rake package' for the first time, then ASCII_DOCS
	# files don't exist yet, and so won't be matched by the glob.
	# So we add these filenames manually.
	] + ASCII_DOCS
end

end # module PhusionPassenger