#include <TestSupport.h>
#include <FileTools/FileManip.h>
#include <FileTools/PathSecurityCheck.h>
#include <unistd.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct FileTools_PathSecurityCheckTest: public TestBase {
		string tmpPath;
		TempDir tmpDir;
		vector<string> errors, checkErrors;

		FileTools_PathSecurityCheckTest()
			: tmpPath("/tmp/pathsecuritychecktest." + toString(getpid())),
			  tmpDir(tmpPath)
			{ }
	};

	DEFINE_TEST_GROUP(FileTools_PathSecurityCheckTest);

	#define ONLY_RUN_AS_ROOT() \
		do { \
			if (geteuid() != 0) { \
				return; \
			} \
		} while (false)

	TEST_METHOD(1) {
		set_test_name("It succeeds if no directory in the path is writable by a non-root user");
		ONLY_RUN_AS_ROOT();

		makeDirTree(tmpPath + "/a", "u=rwx,g=rx,o=rx");
		makeDirTree(tmpPath + "/a/b", "u=rwx,g=rx,o=rx");
		makeDirTree(tmpPath + "/a/b/c", "u=rwx,g=rx,o=rx");

		ensure("(1)", isPathProbablySecureForRootUse(tmpPath + "/a/b/c",
			errors, checkErrors));
		ensure_equals("(2)", errors.size(), 0u);
		ensure_equals("(3)", checkErrors.size(), 0u);
	}

	TEST_METHOD(2) {
		set_test_name("It fails if parts of the path is owned by a non-root user");
		ONLY_RUN_AS_ROOT();

		makeDirTree(tmpPath + "/a", "u=rwx,g=rx,o=rx");
		makeDirTree(tmpPath + "/a/b", "u=rwx,g=rx,o=rx");
		makeDirTree(tmpPath + "/a/b/c", "u=rwx,g=rx,o=rx");
		chown((tmpPath + "/a").c_str(), 1, 0);

		ensure("(1)", !isPathProbablySecureForRootUse(tmpPath + "/a/b/c",
			errors, checkErrors));
		ensure_equals("(2)", errors.size(), 1u);
		ensure_equals("(3)", checkErrors.size(), 0u);
		ensure("(4)", containsSubstring(errors[0],
			tmpPath + "/a is not secure: it can be modified by user"));
	}

	TEST_METHOD(3) {
		set_test_name("It fails if parts of the path is group-writable");
		ONLY_RUN_AS_ROOT();

		makeDirTree(tmpPath + "/a", "u=rwx,g=rx,o=rx");
		makeDirTree(tmpPath + "/a/b", "u=rwx,g=rwx,o=rx");
		makeDirTree(tmpPath + "/a/b/c", "u=rwx,g=rx,o=rx");

		ensure("(1)", !isPathProbablySecureForRootUse(tmpPath + "/a/b/c",
			errors, checkErrors));
		ensure_equals("(2)", errors.size(), 1u);
		ensure_equals("(3)", checkErrors.size(), 0u);
		ensure("(4)", containsSubstring(errors[0],
			tmpPath + "/a/b is not secure: it can be modified by group"));
	}

	TEST_METHOD(4) {
		set_test_name("It fails if parts of the path is world-writable");
		ONLY_RUN_AS_ROOT();

		makeDirTree(tmpPath + "/a", "u=rwx,g=rx,o=rx");
		makeDirTree(tmpPath + "/a/b", "u=rwx,g=rx,o=rwx");
		makeDirTree(tmpPath + "/a/b/c", "u=rwx,g=rx,o=rx");

		ensure("(1)", !isPathProbablySecureForRootUse(tmpPath + "/a/b/c",
			errors, checkErrors));
		ensure_equals("(2)", errors.size(), 1u);
		ensure_equals("(3)", checkErrors.size(), 0u);
		ensure("(4)", containsSubstring(errors[0],
			tmpPath + "/a/b is not secure: it can be modified by anybody"));
	}

	TEST_METHOD(5) {
		set_test_name("It does not fail if parts of the path is group- or world-writable as long as it's sticky");
		ONLY_RUN_AS_ROOT();

		makeDirTree(tmpPath + "/a", "u=rwx,g=rx,o=rx");
		makeDirTree(tmpPath + "/a/b", "u=rwx,g=rwx,o=rwx,+t");
		makeDirTree(tmpPath + "/a/b/c", "u=rwx,g=rx,o=rx");

		ensure("(1)", isPathProbablySecureForRootUse(tmpPath + "/a/b/c",
			errors, checkErrors));
		ensure_equals("(2)", errors.size(), 0u);
		ensure_equals("(3)", checkErrors.size(), 0u);
	}
}
