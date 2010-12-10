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

ASCIIDOC_FLAGS = "-a toc -a numbered -a toclevels=3 -a icons"
DOXYGEN = 'doxygen'

desc "Generate all documentation"
task :doc => [:rdoc]

if PlatformInfo.find_command(DOXYGEN)
	task :doc => :doxygen
end

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
end

task :clobber => [:'doxygen:clobber'] do
	sh "rm -f *.html"
end

desc "Generate Doxygen C++ API documentation if necessary"
task :doxygen => ['doc/cxxapi']
file 'doc/cxxapi' => Dir['ext/apache2/*.{h,c,cpp}'] do
	sh "cd doc && doxygen"
end

desc "Force generation of Doxygen C++ API documentation"
task :'doxygen:force' do
	sh "cd doc && doxygen"
end

desc "Remove generated Doxygen C++ API documentation"
task :'doxygen:clobber' do
	sh "rm -rf doc/cxxapi"
end

Rake::RDocTask.new(:clobber_rdoc => "rdoc:clobber", :rerdoc => "rdoc:force") do |rd|
	rd.main = "README"
	rd.rdoc_dir = "doc/rdoc"
	rd.rdoc_files.include("README", "DEVELOPERS.TXT",
		"lib/phusion_passenger/*.rb",
		"lib/phusion_passenger/*/*.rb",
		"ext/phusion_passenger/*.c")
	rd.template = "./doc/template/horo"
	rd.title = "Passenger Ruby API"
	rd.options << "-S" << "-N" << "-p" << "-H"
end
