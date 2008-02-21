def compile_cxx(source, flags = CXXFLAGS)
	sh "#{CXX} #{flags} -c #{source}"
end

def create_static_library(target, sources)
	sh "ar cru #{target} #{sources}"
	sh "ranlib #{target}"
end

def create_executable(target, sources, linkflags = LDFLAGS)
	sh "#{CXX} #{sources} -o #{target} #{linkflags}"
end
