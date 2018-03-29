#  Phusion Passenger - https://www.phusionpassenger.com/
#  Copyright (c) 2010-2017 Phusion Holding B.V.
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

SCHEMA_PRINTER_TARGET = "#{AGENT_OUTPUT_DIR}SchemaPrinter"
SCHEMA_PRINTER_MAIN_OBJECT = "#{AGENT_OUTPUT_DIR}SchemaPrinterMain.o"
SCHEMA_PRINTER_OBJECTS = {
  SCHEMA_PRINTER_MAIN_OBJECT =>
    "src/schema_printer/SchemaPrinterMain.cpp"
}

dependencies = [
  'src/schema_printer/SchemaPrinterMain.cpp.cxxcodebuilder',
  CXX_DEPENDENCY_MAP['src/schema_printer/SchemaPrinterMain.cpp.cxxcodebuilder']
].flatten.compact
file('src/schema_printer/SchemaPrinterMain.cpp' => dependencies) do
  source = 'src/schema_printer/SchemaPrinterMain.cpp'
  template = CxxCodeTemplateRenderer.new("#{source}.cxxcodebuilder")
  template.render_to(source)
end

# Define compilation tasks for object files.
SCHEMA_PRINTER_OBJECTS.each_pair do |object, source|
  define_cxx_object_compilation_task(
    object,
    source,
    lambda { {
      :include_paths => [
        "src/agent",
        *CXX_SUPPORTLIB_INCLUDE_PATHS
      ],
      :flags => [
        agent_cflags,
        libev_cflags,
        libuv_cflags,
        websocketpp_cflags,
        PlatformInfo.curl_flags,
        PlatformInfo.openssl_extra_cflags,
        PlatformInfo.zlib_flags
      ]
    } }
  )
end

# Define compilation task for the SchemaPrinter executable.
schema_printer_libs = COMMON_LIBRARY.
  only(:base, :base64, :union_station_filter, :process_management_ruby, :other).
  exclude('WatchdogLauncher.o')
dependencies = SCHEMA_PRINTER_OBJECTS.keys + [
  LIBBOOST_OXT,
  schema_printer_libs.link_objects,
  LIBEV_TARGET,
  LIBUV_TARGET
].flatten.compact
file(SCHEMA_PRINTER_TARGET => dependencies) do
  sh "mkdir -p #{AGENT_OUTPUT_DIR}" if !File.directory?(AGENT_OUTPUT_DIR)
  create_cxx_executable(SCHEMA_PRINTER_TARGET,
    [
      schema_printer_libs.link_objects_as_string,
      SCHEMA_PRINTER_OBJECTS.keys,
      LIBBOOST_OXT_LINKARG
    ],
    :flags => [
      libev_libs,
      libuv_libs,
      websocketpp_libs,
      PlatformInfo.curl_libs,
      PlatformInfo.zlib_libs,
      PlatformInfo.crypto_libs,
      PlatformInfo.portability_cxx_ldflags,
      agent_ldflags
    ]
  )
end

desc 'Update dev/configkit-schemas/index.json'
task :configkit_schemas_index => SCHEMA_PRINTER_TARGET do
  sh "#{SCHEMA_PRINTER_TARGET} > dev/configkit-schemas/index.json"
end

desc 'Update ConfigKit schema inline comments'
task :configkit_schemas_inline_comments => :configkit_schemas_index do
  sh './dev/configkit-schemas/update_schema_inline_comments.rb'
end
