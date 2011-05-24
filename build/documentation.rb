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

ASCIIDOC_FLAGS = "-b html5 -a toc -a theme=flask -a numbered -a toclevels=3 -a icons"

desc "Generate all documentation"
task :doc => Packaging::ASCII_DOCS

Packaging::ASCII_DOCS.each do |target|
	source = target.sub(/\.html$/, '.txt')
	file target => [source] + Dir["doc/users_guide_snippets/**/*"] do
		if PlatformInfo.asciidoc
			if target =~ /apache/i
				type = "-a apache"
			elsif target =~ /nginx/i
				type = "-a nginx"
			else
				type = nil
			end
	  		sh "#{PlatformInfo.asciidoc} #{ASCIIDOC_FLAGS} #{type} '#{source}'"
		else
			sh "echo 'asciidoc required to build docs' > '#{target}'"
		end
	end
	
	task :clean do
		sh "rm -f '#{target}'"
	end
end
