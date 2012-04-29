handle SIGUSR1 noprint pass
handle SIGPIPE noprint pass
set env DYLD_INSERT_LIBRARIES /usr/lib/libgmalloc.dylib
set env MallocGuardEdges YES
set env MallocScribble YES
set env MallocPreScribble YES

define rake
	shell rake $arg0
end

set print thread-events off
