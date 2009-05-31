#include "tut.h"
#include "Utils.h"
#include "support/Support.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

using namespace Passenger;
using namespace std;
using namespace Test;

namespace tut {
	struct UtilsTest {
		vector<string> output;
		string oldPath;
		string oldPassengerTempDir;
		
		UtilsTest() {
			oldPath = getenv("PATH");
			oldPassengerTempDir = getPassengerTempDir();
			setPassengerTempDir("");
			unsetenv("TMPDIR");
		}
		
		~UtilsTest() {
			setenv("PATH", oldPath.c_str(), 1);
			unsetenv("TMPDIR");
			setPassengerTempDir(oldPassengerTempDir);
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
	
	
	/***** Test getSystemTempDir() *****/
	
	TEST_METHOD(11) {
		// It returns "/tmp" if the TMPDIR environment is NULL.
		ensure_equals(string(getSystemTempDir()), "/tmp");
	}
	
	TEST_METHOD(12) {
		// It returns "/tmp" if the TMPDIR environment is an empty string.
		setenv("TMPDIR", "", 1);
		ensure_equals(string(getSystemTempDir()), "/tmp");
	}
	
	TEST_METHOD(13) {
		// It returns the value of the TMPDIR environment if it is not NULL and not empty.
		setenv("TMPDIR", "/foo", 1);
		ensure_equals(string(getSystemTempDir()), "/foo");
	}
	
	
	/***** Test getPassengerTempDir() *****/
	
	TEST_METHOD(15) {
		// It returns "(tempdir)/passenger.(pid)"
		char dir[128];
		
		snprintf(dir, sizeof(dir), "/tmp/passenger.%lu", (unsigned long) getpid());
		ensure_equals(getPassengerTempDir(), dir);
	}
	
	TEST_METHOD(16) {
		// It returns the cached value if it's not the empty string.
		setPassengerTempDir("/foo");
		ensure_equals(getPassengerTempDir(), "/foo");
		
		setPassengerTempDir("/bar");
		ensure_equals(getPassengerTempDir(), "/bar");
		
		char dir[128];
		snprintf(dir, sizeof(dir), "/tmp/passenger.%lu", (unsigned long) getpid());
		setPassengerTempDir("");
		ensure_equals(getPassengerTempDir(), dir);
	}
	
	TEST_METHOD(17) {
		// It does not use query the cached value if bypassCache is true.
		char dir[128];
		
		setPassengerTempDir("/foo");
		snprintf(dir, sizeof(dir), "/tmp/passenger.%lu", (unsigned long) getpid());
		ensure_equals(getPassengerTempDir(true), dir);
	}
	
	TEST_METHOD(18) {
		// It uses the systemTempDir argument if it's not the empty string.
		char dir[128];
		
		snprintf(dir, sizeof(dir), "/foo/passenger.%lu", (unsigned long) getpid());
		ensure_equals(getPassengerTempDir(false, "/foo"), dir);
	}
	
	
	/***** Test BufferedUpload *****/
	
	TEST_METHOD(20) {
		// The resulting file handle is readable and writable.
		TempDir td("utils_test.tmp");
		BufferedUpload t("utils_test.tmp");
		char line[30];
		
		fprintf(t.handle, "hello world!");
		fflush(t.handle);
		fseek(t.handle, 0, SEEK_SET);
		memset(line, 0, sizeof(line));
		fgets(line, sizeof(line), t.handle);
		ensure_equals(string(line), "hello world!");
	}
	
	TEST_METHOD(21) {
		// It immediately unlinks the temp file.
		TempDir td("utils_test.tmp");
		BufferedUpload t("utils_test.tmp");
		ensure_equals(listDir("utils_test.tmp").size(), 0u);
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
	
	/***** Test extractDirName() *****/
	
	TEST_METHOD(26) {
		ensure_equals("Test 1", extractDirName("/usr/lib"), "/usr");
		ensure_equals("Test 2", extractDirName("/usr/lib/"), "/usr");
		ensure_equals("Test 3", extractDirName("/usr/"), "/");
		ensure_equals("Test 4", extractDirName("usr"), ".");
		ensure_equals("Test 5", extractDirName("/"), "/");
		ensure_equals("Test 6", extractDirName("///"), "/");
		ensure_equals("Test 7", extractDirName("."), ".");
		ensure_equals("Test 8", extractDirName(".."), ".");
		ensure_equals("Test 9", extractDirName("./foo"), ".");
		ensure_equals("Test 10", extractDirName("../foo"), "..");
	}
	
	/***** Test resolveSymlink() *****/
	
	TEST_METHOD(27) {
		TempDir d("tmp.symlinks");
		system("touch tmp.symlinks/foo.txt");
		system("ln -s /usr/bin tmp.symlinks/absolute_symlink");
		system("ln -s foo.txt tmp.symlinks/file");
		system("ln -s file tmp.symlinks/file2");
		system("ln -s file2 tmp.symlinks/file3");
		ensure_equals(resolveSymlink("tmp.symlinks/file"), "tmp.symlinks/foo.txt");
		ensure_equals(resolveSymlink("tmp.symlinks/file2"), "tmp.symlinks/file");
		ensure_equals(resolveSymlink("tmp.symlinks/file3"), "tmp.symlinks/file2");
		ensure_equals(resolveSymlink("tmp.symlinks/absolute_symlink"), "/usr/bin");
	}
	
	/***** Test generateSecureToken() *****/
	
	TEST_METHOD(28) {
		char buf[10], buf2[10];
		generateSecureToken(buf, sizeof(buf));
		generateSecureToken(buf2, sizeof(buf2));
		ensure(memcmp(buf, buf2, sizeof(buf)) != 0);
	}
	
	/***** Test toHex() *****/
	
	TEST_METHOD(29) {
		ensure_equals(toHex(""), "");
		ensure_equals(toHex(StaticString("\x00", 1)), "00");
		ensure_equals(toHex(StaticString("\x00\x01", 2)), "0001");
		ensure_equals(toHex(StaticString("\x00\x01\x02", 3)), "000102");
		ensure_equals(toHex(StaticString("\x00\x01\xF0\xAF\xFF\x98", 6)), "0001f0afff98");
		ensure_equals(toHex("hello world!"), "68656c6c6f20776f726c6421");
	}
	
	/***** Test fillInMiddle() *****/
	
	TEST_METHOD(30) {
		ensure_equals(fillInMiddle(20, "server.", "123456", ".socket"), "server.123456.socket");
		ensure_equals(fillInMiddle(25, "server.", "123456", ".socket"), "server.123456.socket");
		ensure_equals(fillInMiddle(19, "server.", "123456", ".socket"), "server.12345.socket");
		ensure_equals(fillInMiddle(16, "server.", "123456", ".socket"), "server.12.socket");
		
		ensure_equals(fillInMiddle(10, "", "1234", ""), "1234");
		ensure_equals(fillInMiddle(4, "", "1234", ""), "1234");
		ensure_equals(fillInMiddle(2, "", "1234", ""), "12");
		
		ensure_equals(fillInMiddle(20, "", "1234", ".socket"), "1234.socket");
		ensure_equals(fillInMiddle(11, "", "1234", ".socket"), "1234.socket");
		ensure_equals(fillInMiddle(9, "", "1234", ".socket"), "12.socket");
		
		try {
			fillInMiddle(14, "server.", "123456", ".socket");
			fail();
		} catch (const ArgumentException &) { }
		
		try {
			fillInMiddle(10, "server.", "123456", ".socket");
			fail();
		} catch (const ArgumentException &) { }
		
		try {
			fillInMiddle(10, "server.", "", ".socket");
			fail();
		} catch (const ArgumentException &) { }
	}
}
