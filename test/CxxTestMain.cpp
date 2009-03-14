#include "tut.h"
#include "tut_reporter.h"
#include <apr_general.h>
#include <signal.h>
#include <cstdlib>

namespace tut {
	test_runner_singleton runner;
}

int main() {
	apr_initialize();
	tut::reporter reporter;
	tut::runner.get().set_callback(&reporter);
	signal(SIGPIPE, SIG_IGN);
	setenv("RAILS_ENV", "production", 1);
	setenv("TESTING_PASSENGER", "1", 1);
	try {
		tut::runner.get().run_tests();
		if (reporter.all_ok()) {
			return 0;
		} else {
			return 1;
		}
	} catch (const std::exception &ex) {
		std::cerr << "Exception raised: " << ex.what() << std::endl;
		return 2;
	}
}
