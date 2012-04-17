handle SIGUSR1 noprint pass
handle SIGPIPE noprint pass
set print thread-events off

define rake
	shell rake $arg0
end
