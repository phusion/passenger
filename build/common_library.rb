#  Phusion Passenger - http://www.modrails.com/
#  Copyright (c) 2010, 2011, 2012 Phusion
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

########## Phusion Passenger common library ##########

require 'phusion_passenger/common_library'


########## libboost_oxt ##########

# Defines tasks for compiling a static library containing Boost and OXT.
def define_libboost_oxt_task(namespace, output_dir, extra_compiler_flags = nil)
	output_file = "#{output_dir}.a"
	flags = "-Iext #{extra_compiler_flags} #{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"
	
	if boolean_option('RELEASE')
		sources = Dir['ext/boost/src/pthread/*.cpp'] + Dir['ext/oxt/*.cpp']
		sources.sort!
		
		aggregate_source = "#{output_dir}/aggregate.cpp"
		aggregate_object = "#{output_dir}/aggregate.o"
		object_files     = [aggregate_object]
		
		file(aggregate_object => sources) do
			sh "mkdir -p #{output_dir}" if !File.directory?(output_dir)
			aggregate_content = %Q{
				#ifndef _GNU_SOURCE
					#define _GNU_SOURCE
				#endif
			}
			sources.each do |source_file|
				name = source_file.sub(/^ext\//, '')
				aggregate_content << "#include \"#{name}\"\n"
			end
			File.open(aggregate_source, 'w') do |f|
				f.write(aggregate_content)
			end
			compile_cxx(aggregate_source, "#{flags} -o #{aggregate_object}")
		end
	else
		# Define compilation targets for .cpp files in ext/boost/src/pthread.
		boost_object_files = []
		Dir['ext/boost/src/pthread/*.cpp'].each do |source_file|
			object_name = File.basename(source_file.sub(/\.cpp$/, '.o'))
			boost_output_dir  = "#{output_dir}/boost"
			object_file = "#{boost_output_dir}/#{object_name}"
			boost_object_files << object_file
		
			file object_file => source_file do
				sh "mkdir -p #{boost_output_dir}" if !File.directory?(boost_output_dir)
				compile_cxx(source_file, "#{flags} -o #{object_file}")
			end
		end
		
		# Define compilation targets for .cpp files in ext/oxt.
		oxt_object_files = []
		oxt_dependency_files = Dir["ext/oxt/*.hpp"] + Dir["ext/oxt/detail/*.hpp"]
		Dir['ext/oxt/*.cpp'].each do |source_file|
			object_name = File.basename(source_file.sub(/\.cpp$/, '.o'))
			oxt_output_dir  = "#{output_dir}/oxt"
			object_file = "#{oxt_output_dir}/#{object_name}"
			oxt_object_files << object_file

			file object_file => [source_file, *oxt_dependency_files] do
				sh "mkdir -p #{oxt_output_dir}" if !File.directory?(oxt_output_dir)
				compile_cxx(source_file, "#{flags} -o #{object_file}")
			end
		end
		
		object_files = boost_object_files + oxt_object_files
	end
	
	file(output_file => object_files) do
		sh "mkdir -p #{output_dir}"
		create_static_library(output_file, object_files.join(' '))
	end
	
	task "#{namespace}:clean" do
		sh "rm -rf #{output_file} #{output_dir}"
	end
	
	return output_file
end


########## libev ##########

if USE_VENDORED_LIBEV
	LIBEV_SOURCE_DIR = File.expand_path("../ext/libev", File.dirname(__FILE__)) + "/"
	LIBEV_CFLAGS = "-Iext/libev"
	LIBEV_LIBS = LIBEV_OUTPUT_DIR + ".libs/libev.a"
	
	task :libev => LIBEV_OUTPUT_DIR + ".libs/libev.a"
	
	dependencies = [
		"ext/libev/configure",
		"ext/libev/config.h.in",
		"ext/libev/Makefile.am"
	]
	file LIBEV_OUTPUT_DIR + "Makefile" => dependencies do
		# Disable all warnings: http://pod.tst.eu/http://cvs.schmorp.de/libev/ev.pod#COMPILER_WARNINGS
		cflags = "#{EXTRA_CXXFLAGS} -w"
		sh "mkdir -p #{LIBEV_OUTPUT_DIR}" if !File.directory?(LIBEV_OUTPUT_DIR)
		sh "cd #{LIBEV_OUTPUT_DIR} && sh #{LIBEV_SOURCE_DIR}configure " +
			"--disable-shared --enable-static CFLAGS='#{cflags}' orig_CFLAGS=1"
	end
	
	libev_sources = Dir["ext/libev/{*.c,*.h}"]
	file LIBEV_OUTPUT_DIR + ".libs/libev.a" => [LIBEV_OUTPUT_DIR + "Makefile"] + libev_sources do
		sh "rm -f #{LIBEV_OUTPUT_DIR}/libev.la"
		sh "cd #{LIBEV_OUTPUT_DIR} && make libev.la"
	end

	task 'libev:clean' do
		if File.exist?(LIBEV_OUTPUT_DIR + "Makefile")
			sh "cd #{LIBEV_OUTPUT_DIR} && make maintainer-clean"
		end
	end
	
	task :clean => 'libev:clean'
else
	LIBEV_CFLAGS = string_option('LIBEV_CFLAGS', '-I/usr/include/libev')
	LIBEV_LIBS   = string_option('LIBEV_LIBS', '-lev')
	task :libev  # do nothing
end


########## libeio ##########

if USE_VENDORED_LIBEIO
	LIBEIO_SOURCE_DIR = File.expand_path("../ext/libeio", File.dirname(__FILE__)) + "/"
	LIBEIO_CFLAGS = "-Iext/libeio"
	LIBEIO_LIBS = LIBEIO_OUTPUT_DIR + ".libs/libeio.a"
	
	task :libeio => LIBEIO_OUTPUT_DIR + ".libs/libeio.a"
	
	dependencies = [
		"ext/libeio/configure",
		"ext/libeio/config.h.in",
		"ext/libeio/Makefile.am"
	]
	file LIBEIO_OUTPUT_DIR + "Makefile" => dependencies do
		# Disable all warnings. The author has a clear standpoint on that:
		# http://pod.tst.eu/http://cvs.schmorp.de/libev/ev.pod#COMPILER_WARNINGS
		cflags = "#{EXTRA_CXXFLAGS} -w"
		sh "mkdir -p #{LIBEIO_OUTPUT_DIR}" if !File.directory?(LIBEIO_OUTPUT_DIR)
		sh "cd #{LIBEIO_OUTPUT_DIR} && sh #{LIBEIO_SOURCE_DIR}configure " +
			"--disable-shared --enable-static CFLAGS='#{cflags}'"
	end
	
	libeio_sources = Dir["ext/libeio/{*.c,*.h}"]
	file LIBEIO_OUTPUT_DIR + ".libs/libeio.a" => [LIBEIO_OUTPUT_DIR + "Makefile"] + libeio_sources do
		sh "rm -f #{LIBEIO_OUTPUT_DIR}/libeio.la"
		sh "cd #{LIBEIO_OUTPUT_DIR} && make libeio.la"
	end
	
	task :clean do
		if File.exist?(LIBEIO_OUTPUT_DIR + "Makefile")
			sh "cd #{LIBEIO_OUTPUT_DIR} && make maintainer-clean"
		end
	end
else
	LIBEIO_CFLAGS = string_option('LIBEIO_CFLAGS', '-I/usr/include/libeio')
	LIBEIO_LIBS   = string_option('LIBEIO_LIBS', '-leio')
	task :libeio  # do nothing
end


##############################


LIBBOOST_OXT = define_libboost_oxt_task("common", COMMON_OUTPUT_DIR + "libboost_oxt")
COMMON_LIBRARY.define_tasks
