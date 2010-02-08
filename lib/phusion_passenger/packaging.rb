module PhusionPassenger

module Packaging
	ASCII_DOCS = [
		'doc/Users guide Apache.html',
		'doc/Users guide Nginx.html',
		'doc/Security of user switching support.html',
		'doc/Architectural overview.html'
	]
	
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
		'ext/common/*.{cpp,c,h,hpp}',
		'ext/common/ApplicationPool/*.h',
		'ext/apache2/*.{cpp,h,c,TXT}',
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

end