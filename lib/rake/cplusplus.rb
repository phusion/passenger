def compile_c(source, flags = CXXFLAGS)
	sh "#{CXX} #{flags} -c #{source}"
end

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

def create_shared_library(target, sources, flags = LDFLAGS)
	if RUBY_PLATFORM =~ /darwin/
		shlib_flag = "-flat_namespace -bundle -undefined dynamic_lookup"
	else
		shlib_flag = "-shared"
	end
	sh "#{CXX} #{shlib_flag} #{sources} -fPIC -o #{target} #{flags}"
end
