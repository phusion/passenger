# encoding: utf-8
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

desc "Run 'sloccount' to see how much code Passenger has"
task :sloccount do
	ENV['LC_ALL'] = 'C'
	begin
		# sloccount doesn't recognize the scripts in
		# bin/ as Ruby, so we make symlinks with proper
		# extensions.
		tmpdir = ".sloccount"
		system "rm -rf #{tmpdir}"
		mkdir tmpdir
		Dir['bin/*'].each do |file|
			safe_ln file, "#{tmpdir}/#{File.basename(file)}.rb"
		end
		sh "sloccount", *Dir[
			"#{tmpdir}/*",
			"lib/phusion_passenger",
			"ext/apache2",
			"ext/nginx",
			"ext/common",
			"ext/oxt",
			"ext/phusion_passenger/*.c",
			"test/**/*.{cpp,rb,h}"
		]
	ensure
		system "rm -rf #{tmpdir}"
	end
end

def extract_latest_news_contents_and_items
	# The text is in the following format:
	#
	#   Release x.x.x
	#   -------------
	#
	#    * Text.
	#    * More text.
	#    * A header.
	#      With yet more text.
	#
	#   Release y.y.y
	#   -------------
	#   .....
	contents = File.read("CHANGELOG")

	# We're only interested in the latest release, so extract the text for that.
	contents =~ /\A(Release.*?)^(Release|Older releases)/m
	contents = $1
	contents.sub!(/\A.*?\n-+\n+/m, '')
	contents.sub!(/\n+\Z/, '')

	# Now split the text into individual items.
	items = contents.split(/^ \* /)
	items.shift while items.first == ""

	return [contents, items]
end

desc "Convert the Changelog items for the latest release to HTML"
task :changelog_as_html do
	require 'cgi'
	contents, items = extract_latest_news_contents_and_items

	puts "<ul>"
	items.each do |item|
		def format_paragraph(text)
			# Get rid of newlines: convert them into spaces.
			text.gsub!("\n", ' ')
			while text.index('  ')
				text.gsub!('  ', ' ')
			end

			# Auto-link to issue tracker.
			text.gsub!(/(bug #|issue #|GH-)(\d+)/i) do
				url = "https://github.com/phusion/passenger/issues/#{$2}"
				%Q(<{a href="#{url}"}>#{$1}#{$2}<{/a}>)
			end

			text.strip!
			text = CGI.escapeHTML(text)
			text.gsub!(%r(&lt;\{(.*?)\}&gt;(.*?)&lt;\{/(.*?)\}&gt;)) do
				"<#{CGI.unescapeHTML $1}>#{$2}</#{CGI.unescapeHTML $3}>"
			end
			text
		end

		puts "<li>" + format_paragraph(item.strip) + "</li>"
	end
	puts "</ul>"
end

desc "Convert the Changelog items for the latest release to Markdown"
task :changelog_as_markdown do
	contents, items = extract_latest_news_contents_and_items

	# Auto-link to issue tracker.
	contents.gsub!(/(bug #|issue #|GH-)(\d+)/i) do
		url = "https://github.com/phusion/passenger/issues/#{$2}"
		%Q([#{$1}#{$2}](#{url}))
	end

	puts contents
end

desc "Update CONTRIBUTORS file"
task :contributors do
	entries = `git log --format='%aN' | sort -u`.split("\n")
	entries.delete "Hongli Lai"
	entries.delete "Hongli Lai (Phusion"
	entries.delete "Ninh Bui"
	entries.push "Ninh Bui (Phusion)"
	entries.delete "Phusion Dev"
	entries.delete "Tinco Andringa"
	entries.push "Tinco Andringa (Phusion)"
	entries.delete "Goffert van Gool"
	entries.push "Goffert van Gool (Phusion)"
	entries.delete "Gokulnath"
	entries.push "Gokulnath Manakkattil"
	entries.push "Sean Wilkinson"
	entries.push "Yichun Zhang"
	File.open("CONTRIBUTORS", "w") do |f|
		f.puts(entries.sort{ |a, b| a.downcase <=> b.downcase }.join("\n"))
	end
	puts "Updated CONTRIBUTORS"
end

# Compile the WebHelper binary, used by Homebrew packaging.
task :webhelper => :nginx do
	require 'tmpdir'
	require 'logger'
	PhusionPassenger.require_passenger_lib 'utils/download'
	Dir.mktmpdir do |path|
		Utils::Download.download("http://nginx.org/download/nginx-#{PREFERRED_NGINX_VERSION}.tar.gz",
			"#{path}/nginx.tar.gz")
		sh "cd '#{path}' && tar xzf nginx.tar.gz"
		sh "cd '#{path}/nginx-#{PREFERRED_NGINX_VERSION}' && " +
			"./configure --prefix=/tmp " +
			"#{STANDALONE_NGINX_CONFIGURE_OPTIONS} " +
			"--add-module='#{Dir.pwd}/ext/nginx' && " +
			"make"
		sh "cp '#{path}/nginx-#{PREFERRED_NGINX_VERSION}/objs/nginx' '#{OUTPUT_DIR}PassengerWebHelper'"
	end
end

dependencies = [
	COMMON_LIBRARY.link_objects,
	LIBBOOST_OXT,
	LIBEV_TARGET,
	LIBEIO_TARGET
].flatten.compact
task :compile_app => dependencies do
	source = ENV['SOURCE'] || ENV['FILE'] || ENV['F']
	if !source
		STDERR.puts "Please specify the source filename with SOURCE=(...)"
		exit 1
	end
	if source =~ /\.h/
		File.open('_source.cpp', 'w') do |f|
			f.puts "#include \"#{source}\""
		end
		source = '_source.cpp'
	end
	object = source.sub(/\.cpp$/, '.o')
	exe = source.sub(/\.cpp$/, '')
	begin
		compile_cxx(source,
			"-DSTANDALONE -o #{object} " <<
			"-Iext -Iext/common #{LIBEV_CFLAGS} #{LIBEIO_CFLAGS} " <<
			"#{EXTRA_CXXFLAGS}")
		create_executable(exe, object,
			"-DSTANDALONE " <<
			"-Iext -Iext/common #{LIBEV_CFLAGS} #{LIBEIO_CFLAGS} " <<
			"#{EXTRA_CXXFLAGS} " <<
			"#{COMMON_LIBRARY.link_objects_as_string} " <<
			"#{LIBBOOST_OXT} " <<
			"#{LIBEV_LIBS} " <<
			"#{LIBEIO_LIBS} " <<
			"#{PlatformInfo.portability_cxx_ldflags} " <<
			"#{EXTRA_CXX_LDFLAGS}")
	ensure
		File.unlink('_source.cpp') rescue nil
	end
end
