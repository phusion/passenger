#  Phusion Passenger - http://www.modrails.com/
#  Copyright (C) 2010  Phusion
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; version 2 of the License.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License along
#  with this program; if not, write to the Free Software Foundation, Inc.,
#  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.


# Defines tasks for compiling a static library containing code shared between
# all Phusion Passenger components.
#
# namespace: a 'clean' task will be defined in the given namespace.
# output_dir: directory in which compilation objects should be placed.
# extra_compiler_flags: extra flags to pass to the compiler when compiling
#   the library source files.
# Returns: filename of the static library to be generated. There's a file
#   target defined for this filename.
def define_common_library_task(namespace, output_dir, extra_compiler_flags = nil)
	components = {
		'Logging.o' => %w(
			Logging.cpp
			Logging.h),
		'Utils/CachedFileStat.o' => %w(
			Utils/CachedFileStat.cpp
			Utils/CachedFileStat.h
			Utils/CachedFileStat.hpp),
		'Utils/Base64.o' => %w(
			Utils/Base64.cpp
			Utils/Base64.h),
		'Utils/MD5.o' => %w(
			Utils/MD5.cpp
			Utils/MD5.h),
		'Utils/SystemTime.o' => %w(
			Utils/SystemTime.cpp
			Utils/SystemTime.h),
		'Utils/StrIntUtils.o' => %w(
			Utils/StrIntUtils.cpp
			Utils/StrIntUtils.h),
		'Utils/IOUtils.o' => %w(
			Utils/IOUtils.cpp
			Utils/IOUtils.h),
		'Utils.o' => %w(
			Utils.cpp
			Utils.h
			Utils/Base64.h
			Utils/StrIntUtils.h
			ResourceLocator.h),
		'AccountsDatabase.o' => %w(
			AccountsDatabase.cpp
			AccountsDatabase.h
			RandomGenerator.h
			Constants.h
			Utils.h),
		'AgentsStarter.o' => %w(
			AgentsStarter.cpp
			AgentsStarter.h
			AgentsStarter.hpp
			ResourceLocator.h
			MessageClient.h
			MessageChannel.h
			ServerInstanceDir.h
			Utils/VariantMap.h),
		'AgentBase.o' => %w(
			AgentBase.cpp
			AgentBase.h
			Utils/VariantMap.h),
		#'BCrypt.o' => %w(
		#	BCrypt.cpp
		#	BCrypt.h
		#	Blowfish.h
		#	Blowfish.c)
	}
	
	static_library = "#{output_dir}.a"
	
	# Define compilation targets for the object files in libpassenger_common.
	flags =  "-Iext -Iext/common #{LIBEV_CFLAGS} #{extra_compiler_flags} "
	flags << "#{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"
	common_object_files = []
	components.each_pair do |object_name, dependencies|
		source_file = dependencies[0]
		object_file = "#{output_dir}/#{object_name}"
		common_object_files << object_file
		dependencies = dependencies.map do |dep|
			"ext/common/#{dep}"
		end
		
		file object_file => dependencies do
			sh "mkdir -p #{output_dir}" if !File.directory?(output_dir)
			sh "mkdir -p #{output_dir}/Utils" if !File.directory?("#{output_dir}/Utils")
			compile_cxx("ext/common/#{source_file}", "#{flags} -o #{object_file}")
		end
	end
	
	file(static_library => common_object_files) do
		create_static_library(static_library, "#{output_dir}/*.o #{output_dir}/Utils/*.o")
	end
	
	task "#{namespace}:clean" do
		sh "rm -rf #{static_library} #{output_dir}"
	end
	
	return static_library
end

# Defines tasks for compiling a static library containing Boost and OXT.
def define_libboost_oxt_task(namespace, output_dir, extra_compiler_flags = nil)
	output_file = "#{output_dir}.a"
	flags = "-Iext #{extra_compiler_flags} #{PlatformInfo.portability_cflags} #{EXTRA_CXXFLAGS}"
	
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
	
	file(output_file => boost_object_files + oxt_object_files) do
		sh "mkdir -p #{output_dir}/boost #{output_dir}/oxt"
		create_static_library(output_file,
			"#{output_dir}/boost/*.o " <<
			"#{output_dir}/oxt/*.o")
	end
	
	task "#{namespace}:clean" do
		sh "rm -rf #{output_file} #{output_dir}"
	end
	
	return output_file
end


########## libev ##########

if USE_VENDORED_LIBEV
	LIBEV_CFLAGS = "-Iext/libev"
	LIBEV_LIBS = "ext/libev/.libs/libev.a"
	
	task :libev => "ext/libev/.libs/libev.a"
	
	file "ext/libev/Makefile" => ["ext/libev/configure", "ext/libev/config.h.in", "ext/libev/Makefile.am"] do
		sh "cd ext/libev && sh ./configure --disable-shared --enable-static"
	end
	
	libev_sources = Dir["ext/libev/{*.c,*.h}"]
	file "ext/libev/.libs/libev.a" => ["ext/libev/Makefile"] + libev_sources do
		sh "rm -f ext/libev/libev.la"
		sh "cd ext/libev && make libev.la"
	end
	
	task :clean do
		if File.exist?("ext/libev/Makefile")
			sh "cd ext/libev && make maintainer-clean"
		end
	end
else
	LIBEV_CFLAGS = string_option('LIBEV_CFLAGS', '')
	LIBEV_LIBS   = string_option('LIBEV_LIBS', '')
	task :libev  # do nothing
end


##############################


LIBBOOST_OXT = define_libboost_oxt_task("common", "ext/common/libboost_oxt")
LIBCOMMON    = define_common_library_task("common", "ext/common/libpassenger_common")
