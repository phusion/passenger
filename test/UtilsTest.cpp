#include "tut.h"
#include "Utils.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct UtilsTest {
		vector<string> output;
		string oldPath;
		
		UtilsTest() {
			oldPath = getenv("PATH");
			unsetenv("TMP");
			unsetenv("PHUSION_PASSENGER_TMP");
		}
		
		~UtilsTest() {
			setenv("PATH", oldPath.c_str(), 1);
			unsetenv("TMP");
			unsetenv("PHUSION_PASSENGER_TMP");
		}
	};
	
	static vector<string>
	listDir(const char *path) {
		vector<string> result;
		DIR *d = opendir(path);
		struct dirent *ent;
		
		while ((ent = readdir(d)) != NULL) {
			if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) {
				continue;
			}
			result.push_back(ent->d_name);
		}
		return result;
	}

	DEFINE_TEST_GROUP(UtilsTest);

	/***** Test split() *****/

	TEST_METHOD(1) {
		split("", ':', output);
		ensure_equals(output.size(), 1u);
		ensure_equals(output[0], "");
	}
	
	TEST_METHOD(2) {
		split("hello world", ':', output);
		ensure_equals(output.size(), 1u);
		ensure_equals(output[0], "hello world");
	}
	
	TEST_METHOD(3) {
		split("hello world:foo bar", ':', output);
		ensure_equals(output.size(), 2u);
		ensure_equals(output[0], "hello world");
		ensure_equals(output[1], "foo bar");
	}
	
	TEST_METHOD(4) {
		split("hello world:", ':', output);
		ensure_equals(output.size(), 2u);
		ensure_equals(output[0], "hello world");
		ensure_equals(output[1], "");
	}
	
	TEST_METHOD(5) {
		split(":hello world", ':', output);
		ensure_equals(output.size(), 2u);
		ensure_equals(output[0], "");
		ensure_equals(output[1], "hello world");
	}
	
	TEST_METHOD(6) {
		split("abc:def::ghi", ':', output);
		ensure_equals(output.size(), 4u);
		ensure_equals(output[0], "abc");
		ensure_equals(output[1], "def");
		ensure_equals(output[2], "");
		ensure_equals(output[3], "ghi");
	}
	
	TEST_METHOD(7) {
		split("abc:::def", ':', output);
		ensure_equals(output.size(), 4u);
		ensure_equals(output[0], "abc");
		ensure_equals(output[1], "");
		ensure_equals(output[2], "");
		ensure_equals(output[3], "def");
	}
	
	
	/**** Test findSpawnServer() ****/
	
	TEST_METHOD(8) {
		// If $PATH is empty, it should not find anything.
		setenv("PATH", "", 1);
		ensure_equals(findSpawnServer(), "");
	}
	
	TEST_METHOD(9) {
		// It should ignore relative paths.
		setenv("PATH", "../bin", 1);
		ensure_equals(findSpawnServer(), "");
	}
	
	TEST_METHOD(10) {
		// It should find in $PATH.
		char cwd[PATH_MAX];
		string binpath(getcwd(cwd, sizeof(cwd)));
		binpath.append("/../bin");
		setenv("PATH", binpath.c_str(), 1);
		ensure("Spawn server is found.", !findSpawnServer().empty());
	}
	
	
	/***** Test getTempDir() *****/
	
	TEST_METHOD(11) {
		// It returns "/tmp" if the TMP environment is NULL.
		ensure_equals(string(getTempDir()), "/tmp");
	}
	
	TEST_METHOD(12) {
		// It returns "/tmp" if the TMP environment is an empty string.
		setenv("TMP", "", 1);
		ensure_equals(string(getTempDir()), "/tmp");
	}
	
	TEST_METHOD(13) {
		// It returns the value of the TMP environment if it is not NULL and not empty.
		setenv("TMP", "/foo", 1);
		ensure_equals(string(getTempDir()), "/foo");
	}
	
	
	/***** Test getPassengerTempDir() *****/
	
	TEST_METHOD(15) {
		// It returns "(tempdir)/passenger.(pid)"
		char dir[128];
		
		snprintf(dir, sizeof(dir), "/tmp/passenger.%d", getpid());
		ensure_equals(getPassengerTempDir(), dir);
	}
	
	TEST_METHOD(16) {
		// It caches the result into the PHUSION_PASSENGER_TMP environment variable.
		char dir[128];
		
		snprintf(dir, sizeof(dir), "/tmp/passenger.%d", getpid());
		getPassengerTempDir();
		ensure_equals(getenv("PHUSION_PASSENGER_TMP"), string(dir));
	}
	
	TEST_METHOD(17) {
		// It returns the value of the PHUSION_PASSENGER_TMP environment variable if it's not NULL and not an empty string.
		setenv("PHUSION_PASSENGER_TMP", "/foo", 1);
		ensure_equals(getPassengerTempDir(), "/foo");
	}
	
	TEST_METHOD(18) {
		// It does not use query the PHUSION_PASSENGER_TMP environment variable if bypassCache is true.
		char dir[128];
		
		setenv("PHUSION_PASSENGER_TMP", "/foo", 1);
		snprintf(dir, sizeof(dir), "/tmp/passenger.%d", getpid());
		ensure_equals(getPassengerTempDir(true), dir);
	}
	
	
	/***** Test TempFile *****/
	
	TEST_METHOD(20) {
		// It creates a temp file inside getPassengerTempDir().
		setenv("PHUSION_PASSENGER_TMP", "utils_test.tmp", 1);
		mkdir("utils_test.tmp", S_IRWXU);
		TempFile t("temp", false);
		unsigned int size = listDir("utils_test.tmp").size();
		removeDirTree("utils_test.tmp");
		ensure_equals(size, 1u);
	}
	
	TEST_METHOD(21) {
		// It deletes the temp file upon destruction.
		setenv("PHUSION_PASSENGER_TMP", "utils_test.tmp", 1);
		mkdir("utils_test.tmp", S_IRWXU);
		{
			TempFile t("temp", false);
		}
		bool dirEmpty = listDir("utils_test.tmp").empty();
		removeDirTree("utils_test.tmp");
		ensure(dirEmpty);
	}
	
	TEST_METHOD(22) {
		// The temp file's filename is constructed using the given identifier.
		setenv("PHUSION_PASSENGER_TMP", "utils_test.tmp", 1);
		mkdir("utils_test.tmp", S_IRWXU);
		TempFile t("foobar", false);
		vector<string> files(listDir("utils_test.tmp"));
		removeDirTree("utils_test.tmp");
		
		ensure(files[0].find("foobar") != string::npos);
	}
	
	TEST_METHOD(23) {
		// It immediately unlinks the temp file if 'anonymous' is true.
		// It creates a temp file inside getPassengerTempDir().
		setenv("PHUSION_PASSENGER_TMP", "utils_test.tmp", 1);
		mkdir("utils_test.tmp", S_IRWXU);
		TempFile t;
		unsigned int size = listDir("utils_test.tmp").size();
		removeDirTree("utils_test.tmp");
		ensure_equals(size, 0u);
	}
	
	/***** Test escapeForXml() *****/
	
	TEST_METHOD(25) {
		ensure_equals(escapeForXml(""), "");
		ensure_equals(escapeForXml("hello world"), "hello world");
		ensure_equals(escapeForXml("./hello_world/foo.txt"), "./hello_world/foo.txt");
		ensure_equals(escapeForXml("hello<world"), "hello&#60;world");
		ensure_equals(escapeForXml("hello\xFFworld"), "hello&#255;world");
		ensure_equals(escapeForXml("hello\xFF\xCCworld"), "hello&#255;&#204;world");
		ensure_equals(escapeForXml("hello\xFFworld\xCC"), "hello&#255;world&#204;");
	}
}
