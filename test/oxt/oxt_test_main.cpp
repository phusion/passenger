#include "../tut/tut.h"
#include "../tut/tut_reporter.h"
#include <oxt/initialize.hpp>
#include <oxt/system_calls.hpp>
#include <signal.h>
#include <cstdlib>

namespace tut {
	test_runner_singleton runner;
}

int main() {
	tut::reporter reporter;
	tut::runner.get().set_callback(&reporter);
	signal(SIGPIPE, SIG_IGN);
	setenv("RAILS_ENV", "production", 1);
	setenv("TESTING_PASSENGER", "1", 1);
	oxt::initialize();
	oxt::setup_syscall_interruption_support();
	try {
		tut::runner.get().run_tests();
	} catch (const std::exception &ex) {
		std::cerr << "Exception raised: " << ex.what() << std::endl;
		return 1;
	}
	return 0;
}
