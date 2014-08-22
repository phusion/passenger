#include <TestSupport.h>
#include <Utils.h>
#include <Utils/StrIntUtils.h>
#include <Utils/MemZeroGuard.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <string.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct UtilsTest {
		vector<string> output;
		string cwd;
		string oldPath;
		TempDir tempDir;

		UtilsTest(): tempDir("tmp.dir") {
			char buffer[PATH_MAX];
			cwd = getcwd(buffer, sizeof(buffer));
			oldPath = getenv("PATH");
			unsetenv("PASSENGER_TEMP_DIR");
		}

		~UtilsTest() {
			setenv("PATH", oldPath.c_str(), 1);
			unsetenv("PASSENGER_TEMP_DIR");
			chdir(cwd.c_str());
		}

		void testMakeDirTreeMode(const char *name, const char *mode, mode_t expected) {
			TempDir td("tmp.dir2");
			struct stat buf;
			mode_t allModes = S_IRWXU | S_ISUID | S_IRWXG | S_ISGID | S_IRWXO;

			makeDirTree("tmp.dir2/foo", mode);
			stat("tmp.dir2/foo", &buf);
			ensure_equals(name, buf.st_mode & allModes, expected);
		}
	};

	DEFINE_TEST_GROUP_WITH_LIMIT(UtilsTest, 100);

	/***** Test split() *****/

	TEST_METHOD(1) {
		split("", ':', output);
		ensure_equals(output.size(), 0u);

		splitIncludeSep("", ':', output);
		ensure_equals(output.size(), 0u);
	}

	TEST_METHOD(2) {
		split("hello world", ':', output);
		ensure_equals(output.size(), 1u);
		ensure_equals(output[0], "hello world");

		splitIncludeSep("hello world", ':', output);
		ensure_equals(output.size(), 1u);
		ensure_equals(output[0], "hello world");
	}

	TEST_METHOD(3) {
		split("hello world:foo bar", ':', output);
		ensure_equals(output.size(), 2u);
		ensure_equals(output[0], "hello world");
		ensure_equals(output[1], "foo bar");

		splitIncludeSep("hello world:foo bar", ':', output);
		ensure_equals(output.size(), 2u);
		ensure_equals(output[0], "hello world:");
		ensure_equals(output[1], "foo bar");
	}

	TEST_METHOD(4) {
		split("hello world:", ':', output);
		ensure_equals("(1)", output.size(), 2u);
		ensure_equals("(2)", output[0], "hello world");
		ensure_equals("(3)", output[1], "");

		splitIncludeSep("hello world:", ':', output);
		ensure_equals("(4)", output.size(), 1u);
		ensure_equals("(5)", output[0], "hello world:");
	}

	TEST_METHOD(5) {
		split(":hello world", ':', output);
		ensure_equals(output.size(), 2u);
		ensure_equals(output[0], "");
		ensure_equals(output[1], "hello world");

		splitIncludeSep(":hello world", ':', output);
		ensure_equals(output.size(), 2u);
		ensure_equals(output[0], ":");
		ensure_equals(output[1], "hello world");
	}

	TEST_METHOD(6) {
		split("abc:def::ghi", ':', output);
		ensure_equals(output.size(), 4u);
		ensure_equals(output[0], "abc");
		ensure_equals(output[1], "def");
		ensure_equals(output[2], "");
		ensure_equals(output[3], "ghi");

		splitIncludeSep("abc:def::ghi", ':', output);
		ensure_equals(output.size(), 4u);
		ensure_equals(output[0], "abc:");
		ensure_equals(output[1], "def:");
		ensure_equals(output[2], ":");
		ensure_equals(output[3], "ghi");
	}

	TEST_METHOD(7) {
		split("abc:::def", ':', output);
		ensure_equals(output.size(), 4u);
		ensure_equals(output[0], "abc");
		ensure_equals(output[1], "");
		ensure_equals(output[2], "");
		ensure_equals(output[3], "def");

		splitIncludeSep("abc:::def", ':', output);
		ensure_equals(output.size(), 4u);
		ensure_equals(output[0], "abc:");
		ensure_equals(output[1], ":");
		ensure_equals(output[2], ":");
		ensure_equals(output[3], "def");
	}


	/***** Test getSystemTempDir() *****/

	TEST_METHOD(11) {
		// It returns "/tmp" if the PASSENGER_TEMP_DIR environment is NULL.
		ensure_equals(string(getSystemTempDir()), "/tmp");
	}

	TEST_METHOD(12) {
		// It returns "/tmp" if the PASSENGER_TEMP_DIR environment is an empty string.
		setenv("PASSENGER_TEMP_DIR", "", 1);
		ensure_equals(string(getSystemTempDir()), "/tmp");
	}

	TEST_METHOD(13) {
		// It returns the value of the PASSENGER_TEMP_DIR environment if it is not NULL and not empty.
		setenv("PASSENGER_TEMP_DIR", "/foo", 1);
		ensure_equals(string(getSystemTempDir()), "/foo");
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
		DONT_RUN_IN_VAGRANT();
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
		ensure_equals(escapeForXml("hello\nworld"), "hello&#10;world");
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
		ensure_equals("Test 11", extractDirName(""), ".");
		ensure_equals("Test 12", extractDirName(".///"), ".");
		ensure_equals("Test 13", extractDirName("foo//bar"), "foo");
	}

	TEST_METHOD(27) {
		ensure_equals("Test 1", extractDirNameStatic("/usr/lib"), "/usr");
		ensure_equals("Test 2", extractDirNameStatic("/usr/lib/"), "/usr");
		ensure_equals("Test 3", extractDirNameStatic("/usr/"), "/");
		ensure_equals("Test 4", extractDirNameStatic("usr"), ".");
		ensure_equals("Test 5", extractDirNameStatic("/"), "/");
		ensure_equals("Test 6", extractDirNameStatic("///"), "/");
		ensure_equals("Test 7", extractDirNameStatic("."), ".");
		ensure_equals("Test 8", extractDirNameStatic(".."), ".");
		ensure_equals("Test 9", extractDirNameStatic("./foo"), ".");
		ensure_equals("Test 10", extractDirNameStatic("../foo"), "..");
		ensure_equals("Test 11", extractDirNameStatic(""), ".");
		ensure_equals("Test 12", extractDirNameStatic(".///"), ".");
		ensure_equals("Test 13", extractDirNameStatic("foo//bar"), "foo");
	}

	/***** Test resolveSymlink() *****/

	TEST_METHOD(28) {
		TempDir d("tmp.symlinks");
		runShellCommand("touch tmp.symlinks/foo.txt");
		runShellCommand("ln -s /usr/bin tmp.symlinks/absolute_symlink");
		runShellCommand("ln -s foo.txt tmp.symlinks/file");
		runShellCommand("ln -s file tmp.symlinks/file2");
		runShellCommand("ln -s file2 tmp.symlinks/file3");
		ensure_equals(resolveSymlink("tmp.symlinks/file"), "tmp.symlinks/foo.txt");
		ensure_equals(resolveSymlink("tmp.symlinks/file2"), "tmp.symlinks/file");
		ensure_equals(resolveSymlink("tmp.symlinks/file3"), "tmp.symlinks/file2");
		ensure_equals(resolveSymlink("tmp.symlinks/absolute_symlink"), "/usr/bin");
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

	/***** Test MemZeroGuard *****/

	TEST_METHOD(31) {
		char buf[12] = "hello world";
		{
			MemZeroGuard g(buf, 2);
		}
		ensure(memcmp(buf, "\0\0llo world", sizeof(buf)) == 0);
	}

	TEST_METHOD(32) {
		string str("hello ");
		{
			MemZeroGuard g(str);
			str.append("world");
		}
		ensure(memcmp(str.c_str(), "\0\0\0\0\0\0\0\0\0\0\0", 11) == 0);
	}

	TEST_METHOD(33) {
		string str("hello ");
		{
			MemZeroGuard g(str);
			g.zeroNow();
			ensure(memcmp(str.c_str(), "\0\0\0\0\0\0", 6) == 0);
			str.append("world");
			ensure(memcmp(str.c_str(), "\0\0\0\0\0\0world", 11) == 0);
		}
		ensure(memcmp(str.c_str(), "\0\0\0\0\0\0\0\0\0\0\0", 11) == 0);
	}

	/***** Test parseModeString() *****/

	static bool modeStringCannotBeParsed(const StaticString &modeString) {
		try {
			parseModeString(modeString);
			return false;
		} catch (const InvalidModeStringException &) {
			return true;
		}
	}

	TEST_METHOD(36) {
		ensure_equals(parseModeString(""), (mode_t) 0);
		ensure_equals(parseModeString("u="), (mode_t) 0);
		ensure_equals(parseModeString("u=,u="), (mode_t) 0);
		ensure_equals(parseModeString("u=,g="), (mode_t) 0);
		ensure_equals(parseModeString("u=,g=,o="), (mode_t) 0);
		ensure_equals(parseModeString("u=,g=,o=,u=,g="), (mode_t) 0);
		ensure_equals(parseModeString("o="), (mode_t) 0);
	}

	TEST_METHOD(37) {
		ensure_equals("(1)", parseModeString("u=rwx"), (mode_t) S_IRWXU);
		ensure_equals("(2)", parseModeString("g=rwx"), (mode_t) S_IRWXG);
		ensure_equals("(3)", parseModeString("o=rwx"), (mode_t) S_IRWXO);
		ensure_equals("(4)", parseModeString("u=r,g=,o=rx"),
			(mode_t) (S_IRUSR | S_IROTH | S_IXOTH));
		ensure_equals("(5)", parseModeString("o=r,g=wx"),
			(mode_t) (S_IROTH | S_IWGRP | S_IXGRP));
		ensure_equals("(6)", parseModeString("u=r,g=w,o=x,u=x"),
			(mode_t) (S_IRUSR | S_IXUSR | S_IWGRP | S_IXOTH));
		ensure_equals("(7)", parseModeString("u=rs,g=ws"),
			(mode_t) (S_IRUSR | S_ISUID | S_IWGRP | S_ISGID));
		ensure_equals("(7)", parseModeString("u=rwx,g=rwx,+t"),
			(mode_t) (S_IRWXU | S_IRWXG | S_ISVTX));
	}

	TEST_METHOD(38) {
		ensure(modeStringCannotBeParsed("0"));
		ensure(modeStringCannotBeParsed("0600"));
		ensure(modeStringCannotBeParsed("600"));
		ensure(modeStringCannotBeParsed("x=rs"));
		ensure(modeStringCannotBeParsed("u=rs,g=rs,x=rs"));
		ensure(modeStringCannotBeParsed("x=rs"));
		ensure(modeStringCannotBeParsed("rwxrwxrwx"));
	}

	/***** Test makeDirTree() *****/

	TEST_METHOD(40) {
		// Creating a single subdirectory works.
		makeDirTree("tmp.dir/foo");
		ensure_equals(getFileType("tmp.dir/foo"), FT_DIRECTORY);
	}

	TEST_METHOD(41) {
		// Creating multiple subdirectories works.
		makeDirTree("tmp.dir/foo/bar");
		ensure_equals(getFileType("tmp.dir/foo"), FT_DIRECTORY);
		ensure_equals(getFileType("tmp.dir/foo/bar"), FT_DIRECTORY);
	}

	TEST_METHOD(42) {
		// It applies the permissions to all created directories.
		struct stat buf, buf2;

		stat("tmp.dir", &buf);
		makeDirTree("tmp.dir/foo/bar", "u=rwxs,g=,o=rx");
		stat("tmp.dir", &buf2);
		ensure_equals(buf.st_mode, buf2.st_mode);

		stat("tmp.dir/foo", &buf);
		stat("tmp.dir/foo/bar", &buf2);
		ensure_equals(buf.st_mode, buf2.st_mode);
		ensure_equals((mode_t) (buf.st_mode & 0xFFF),
			(mode_t) (S_IRUSR | S_IWUSR | S_IXUSR | S_ISUID |
			S_IROTH | S_IXOTH));
	}

	TEST_METHOD(43) {
		// It correctly parses the permission string.
		testMakeDirTreeMode("empty 1", "", (mode_t) 0);
		testMakeDirTreeMode("empty 2", "u=", (mode_t) 0);
		testMakeDirTreeMode("empty 3", "g=", (mode_t) 0);
		testMakeDirTreeMode("empty 4", "o=", (mode_t) 0);
		testMakeDirTreeMode("empty 5", "u=,g=", (mode_t) 0);
		testMakeDirTreeMode("empty 6", "g=,o=", (mode_t) 0);

		testMakeDirTreeMode("(1)", "u=rwxs,g=rwxs,o=rwx",
			S_IRWXU | S_ISUID | S_IRWXG | S_ISGID | S_IRWXO);
		testMakeDirTreeMode("(2)", "u=s,g=rx,o=w",
			S_ISUID | S_IRGRP | S_IXGRP | S_IWOTH);
		testMakeDirTreeMode("(3)", "u=rwxs,g=,o=rwx",
			S_IRWXU | S_ISUID | S_IRWXO);
	}

	TEST_METHOD(44) {
		// It doesn't do anything if the directory already exists.
		struct stat buf, buf2;
		stat("tmp.dir", &buf);
		makeDirTree("tmp.dir");
		stat("tmp.dir", &buf2);
		ensure_equals(buf.st_mode, buf2.st_mode);
	}

	/***** Test stringToULL(), stringToUint *****/

	TEST_METHOD(47) {
		ensure_equals(stringToULL(""), 0ull);
		ensure_equals(stringToULL("bla"), 0ull);
		ensure_equals(stringToULL("0"), 0ull);
		ensure_equals(stringToULL("000"), 0ull);
		ensure_equals(stringToULL("1"), 1ull);
		ensure_equals(stringToULL("9"), 9ull);
		ensure_equals(stringToULL("010"), 10ull);
		ensure_equals(stringToULL("928"), 928ull);
		ensure_equals(stringToULL("2937104"), 2937104ull);
		ensure_equals(stringToULL("18446744073709551615"), 18446744073709551615ull);
		ensure_equals(stringToULL("    5abcdef1234"), 5ull);

		ensure_equals(stringToUint(""), 0u);
		ensure_equals(stringToUint("bla"), 0u);
		ensure_equals(stringToUint("0"), 0u);
		ensure_equals(stringToUint("000"), 0u);
		ensure_equals(stringToUint("1"), 1u);
		ensure_equals(stringToUint("9"), 9u);
		ensure_equals(stringToUint("010"), 10u);
		ensure_equals(stringToUint("928"), 928u);
		ensure_equals(stringToUint("2937104"), 2937104u);
		ensure_equals(stringToUint("    5abcdef1234"), 5u);
	}

	/***** Test integerToHex() and integerToHexatri() *****/

	TEST_METHOD(48) {
		char buf[sizeof(int) * 2 + 1];

		ensure_equals("(1)", integerToHex<int>(0x0, buf), 1u);
		ensure("(1)", strcmp(buf, "0") == 0);

		ensure_equals("(2)", integerToHex<int>(0x1, buf), 1u);
		ensure("(2)", strcmp(buf, "1") == 0);

		ensure_equals("(3)", integerToHex<int>(0x9, buf), 1u);
		ensure("(3)", strcmp(buf, "9") == 0);

		ensure_equals("(4)", integerToHex<int>(0xe, buf), 1u);
		ensure("(4)", strcmp(buf, "e") == 0);

		ensure_equals("(5)", integerToHex<unsigned int>(0xdeadbeef, buf), 8u);
		ensure("(5)", strcmp(buf, "deadbeef") == 0);

		ensure_equals("(6)", integerToHex<int>(0x1234f, buf), 5u);
		ensure("(6)", strcmp(buf, "1234f") == 0);


		ensure_equals("(7)", integerToHexatri<int>(0x0, buf), 1u);
		ensure("(7)", strcmp(buf, "0") == 0);

		ensure_equals("(8)", integerToHexatri<int>(0x1, buf), 1u);
		ensure("(8)", strcmp(buf, "1") == 0);

		ensure_equals("(9)", integerToHexatri<int>(0x9, buf), 1u);
		ensure("(9)", strcmp(buf, "9") == 0);

		ensure_equals("(10)", integerToHexatri<int>(0xe, buf), 1u);
		ensure("(10)", strcmp(buf, "e") == 0);

		ensure_equals("(11)", integerToHexatri<int>(35, buf), 1u);
		ensure("(11)", strcmp(buf, "z") == 0);

		ensure_equals(integerToHexatri<unsigned int>(0xdeadbeef, buf), 7u);
		ensure(strcmp(buf, "1ps9wxb") == 0);

		ensure_equals(integerToHexatri<int>(0x1234f, buf), 4u);
		ensure(strcmp(buf, "1ljj") == 0);
	}

	/***** Test hexToULL(), hexToUint() and hexatriToULL() *****/

	TEST_METHOD(49) {
		ensure_equals(hexToULL(""), 0ull);
		ensure_equals(hexToULL("   "), 0ull);
		ensure_equals(hexToULL("1"), 1ull);
		ensure_equals(hexToULL("9"), 9ull);
		ensure_equals(hexToULL("a"), 10ull);
		ensure_equals(hexToULL("B"), 11ull);
		ensure_equals(hexToULL("1234"), 4660ull);
		ensure_equals(hexToULL("1a6b"), 6763ull);
		ensure_equals(hexToULL("1A6B"), 6763ull);
		ensure_equals(hexToULL("1a6B"), 6763ull);
		ensure_equals(hexToULL("deadbeef"), 3735928559ull);
		ensure_equals(hexToULL("dEaDbEeF"), 3735928559ull);
		ensure_equals(hexToULL("09a2s89"), 2466ull);
		ensure_equals(hexToULL(" 9a2s89"), 0ull);

		ensure_equals(hexToUint(""), 0u);
		ensure_equals(hexToUint("   "), 0u);
		ensure_equals(hexToUint("1"), 1u);
		ensure_equals(hexToUint("9"), 9u);
		ensure_equals(hexToUint("a"), 10u);
		ensure_equals(hexToUint("B"), 11u);
		ensure_equals(hexToUint("1234"), 4660u);
		ensure_equals(hexToUint("1a6b"), 6763u);
		ensure_equals(hexToUint("1A6B"), 6763u);
		ensure_equals(hexToUint("1a6B"), 6763u);
		ensure_equals(hexToUint("deadbeef"), 3735928559u);
		ensure_equals(hexToUint("dEaDbEeF"), 3735928559u);
		ensure_equals(hexToUint("09a2s89"), 2466u);
		ensure_equals(hexToUint(" 9a2s89"), 0u);

		ensure_equals(hexatriToULL(""), 0ull);
		ensure_equals(hexatriToULL("   "), 0ull);
		ensure_equals(hexatriToULL("1"), 1ull);
		ensure_equals(hexatriToULL("9"), 9ull);
		ensure_equals(hexatriToULL("a"), 10ull);
		ensure_equals(hexatriToULL("B"), 11ull);
		ensure_equals(hexatriToULL("1234"), 49360ull);
		ensure_equals(hexatriToULL("1a6z"), 59867ull);
		ensure_equals(hexatriToULL("1A6Z"), 59867ull);
		ensure_equals(hexatriToULL("1a6Z"), 59867ull);
		ensure_equals(hexatriToULL("deadroof"), 1049836874415ull);
		ensure_equals(hexatriToULL("dEaDrOoF"), 1049836874415ull);
		ensure_equals(hexatriToULL("09a2s89"), 561121641ull);
		ensure_equals(hexatriToULL(" 9a2s89"), 0ull);
	}

	/***** Test stringToLL(), stringToInt() *****/

	TEST_METHOD(50) {
		ensure_equals(stringToLL(""), 0ll);
		ensure_equals(stringToLL("bla"), 0ll);
		ensure_equals(stringToLL("0"), 0ll);
		ensure_equals(stringToLL("000"), 0ll);
		ensure_equals(stringToLL("1"), 1ll);
		ensure_equals(stringToLL("9"), 9ll);
		ensure_equals(stringToLL("010"), 10ll);
		ensure_equals(stringToLL("928"), 928ll);
		ensure_equals(stringToLL("2937104"), 2937104ll);
		ensure_equals(stringToLL("9223372036854775807"), 9223372036854775807ll);
		ensure_equals(stringToLL("    5abcdef1234"), 5ll);

		ensure_equals(stringToLL("-0"), 0ll);
		ensure_equals(stringToLL("-1"), -1ll);
		ensure_equals(stringToLL("-010"), -10ll);
		ensure_equals(stringToLL("-9876"), -9876ll);
		ensure_equals(stringToLL("-9223372036854775807"), -9223372036854775807ll);
		ensure_equals(stringToLL("    -5abcdef1234"), -5ll);

		ensure_equals(stringToInt(""), 0);
		ensure_equals(stringToInt("bla"), 0);
		ensure_equals(stringToInt("0"), 0);
		ensure_equals(stringToInt("000"), 0);
		ensure_equals(stringToInt("1"), 1);
		ensure_equals(stringToInt("9"), 9);
		ensure_equals(stringToInt("010"), 10);
		ensure_equals(stringToInt("928"), 928);
		ensure_equals(stringToInt("2937104"), 2937104);
		ensure_equals(stringToInt("    5abcdef1234"), 5);

		ensure_equals(stringToInt("-0"), 0ll);
		ensure_equals(stringToInt("-1"), -1ll);
		ensure_equals(stringToInt("-010"), -10ll);
		ensure_equals(stringToInt("-9876"), -9876);
		ensure_equals(stringToInt("    -5abcdef1234"), -5);
	}

	/***** Test cEscapeString() *****/

	TEST_METHOD(51) {
		ensure_equals(cEscapeString(""), "");
		ensure_equals(cEscapeString("abcdXYZ123!?"), "abcdXYZ123!?");
		ensure_equals(cEscapeString("foo\n"), "foo\\n");
		ensure_equals(cEscapeString("foo\r\nbar\e"), "foo\\r\\nbar\\e");
		ensure_equals(cEscapeString(StaticString("\0\x1\x2\x3\x4\x5\x6\x7\x8\x9", 10)),
			"\\x00\\x01\\x02\\x03\\x04\\x05\\x06\\x07\\x08\\t");
		ensure_equals(cEscapeString("\xFF\xFE\t\xD0"), "\\xFF\\xFE\\t\\xD0");
	}

	/***** Test escapeHTML() *****/

	TEST_METHOD(52) {
		const char weird[] = "Weird \x01\x00 characters?";
		ensure_equals(escapeHTML(""), "");
		ensure_equals(escapeHTML("hello\n\r\t WORLD!"), "hello\n\r\t WORLD!");
		ensure_equals(escapeHTML("<b>bold</b>"), "&lt;b&gt;bold&lt;/b&gt;");
		ensure_equals(escapeHTML(StaticString(weird, sizeof(weird) - 1)),
			"Weird &#1;&#0; characters?");
		ensure_equals(escapeHTML("UTF-8: ☃ ☀; ☁ ☂\x01"), "UTF-8: ☃ ☀; ☁ ☂&#1;");
	}

	/***** Test absolutizePath() *****/

	TEST_METHOD(53) {
		ensure_equals(absolutizePath(""), cwd);
		ensure_equals(absolutizePath("."), cwd);
		ensure_equals(absolutizePath("foo"), cwd + "/foo");
		ensure_equals(absolutizePath("foo/bar"), cwd + "/foo/bar");
		ensure_equals(absolutizePath("foo//bar"), cwd + "/foo/bar");
		ensure_equals(absolutizePath("foo/bar///baz"), cwd + "/foo/bar/baz");
		ensure_equals(absolutizePath("foo/./bar"), cwd + "/foo/bar");
		ensure_equals(absolutizePath("foo/bar/../baz"), cwd + "/foo/baz");
		ensure_equals(absolutizePath("foo/bar/../.."), cwd);
		ensure_equals(absolutizePath("foo/.././bar"), cwd + "/bar");
		ensure_equals(absolutizePath("foo/../bar/./baz"), cwd + "/bar/baz");
		ensure_equals(absolutizePath("foo/"), cwd + "/foo");
		ensure_equals(absolutizePath("foo//"), cwd + "/foo");

		ensure_equals(absolutizePath("/"), "/");
		ensure_equals(absolutizePath("////"), "/");
		ensure_equals(absolutizePath("/."), "/");
		ensure_equals(absolutizePath("/foo"), "/foo");
		ensure_equals(absolutizePath("/foo/bar"), "/foo/bar");
		ensure_equals(absolutizePath("/foo//bar"), "/foo/bar");
		ensure_equals(absolutizePath("/foo/bar///baz"), "/foo/bar/baz");
		ensure_equals(absolutizePath("/foo/./bar"), "/foo/bar");
		ensure_equals(absolutizePath("/foo/bar/../baz"), "/foo/baz");
		ensure_equals(absolutizePath("/foo/bar/../.."), "/");
		ensure_equals(absolutizePath("/foo/.././bar"), "/bar");
		ensure_equals(absolutizePath("/foo/../bar/./baz"), "/bar/baz");
		ensure_equals(absolutizePath("/foo/"), "/foo");
		ensure_equals(absolutizePath("/foo//"), "/foo");
		ensure_equals(absolutizePath("//foo/bar"), "/foo/bar");
		ensure_equals(absolutizePath("///foo//bar"), "/foo/bar");
		ensure_equals(absolutizePath("/../.."), "/");
		ensure_equals(absolutizePath("/../.././foo"), "/foo");

		chdir("/usr/lib");
		ensure_equals(absolutizePath(".."), "/usr");
		ensure_equals(absolutizePath("."), "/usr/lib");
		ensure_equals(absolutizePath("../.."), "/");
		ensure_equals(absolutizePath("../../foo"), "/foo");
		ensure_equals(absolutizePath("../.././foo/bar"), "/foo/bar");

		ensure_equals(absolutizePath("..", "/usr/local/bin"), "/usr/local");
		ensure_equals(absolutizePath(".", "/usr/local/bin"), "/usr/local/bin");
		ensure_equals(absolutizePath("../..", "/usr/local/bin"), "/usr");
		ensure_equals(absolutizePath("../../foo", "/usr/local/bin"), "/usr/foo");
		ensure_equals(absolutizePath("../.././foo/bar", "/usr/local/bin"), "/usr/foo/bar");
	}

	/***** Test constantTimeCompare() *****/

	TEST_METHOD(54) {
		ensure("(1)", constantTimeCompare("", ""));
		ensure("(2)", constantTimeCompare("a", "a"));
		ensure("(3)", constantTimeCompare("aa", "aa"));
		ensure("(4)", constantTimeCompare("abc", "abc"));

		ensure("(5)", !constantTimeCompare("", "a"));
		ensure("(6)", !constantTimeCompare("", "abcd"));
		ensure("(7)", !constantTimeCompare("ab", "cd"));
		ensure("(8)", !constantTimeCompare("ab", "abc"));
		ensure("(9)", !constantTimeCompare("ab", "abcd"));

		ensure("(10)", !constantTimeCompare("a", ""));
		ensure("(11)", !constantTimeCompare("abcd", ""));
		ensure("(12)", !constantTimeCompare("cd", "ab"));
		ensure("(13)", !constantTimeCompare("abc", "ab"));
		ensure("(14)", !constantTimeCompare("abcd", "ab"));
	}

	/***** Test integerToOtherBase() *****/

	TEST_METHOD(56) {
		char buf[20], smallbuf[4];

		ensure_equals("(1)", integerToOtherBase<int, 10>(0, buf, sizeof(buf)), 1u);
		ensure_equals("(1.1)", buf[0], '0');
		ensure_equals("(1.2)", buf[1], '\0');

		ensure_equals("(2)", integerToOtherBase<int, 10>(1234, buf, sizeof(buf)), 4u);
		ensure_equals("(2.1)", buf[0], '1');
		ensure_equals("(2.2)", buf[1], '2');
		ensure_equals("(2.3)", buf[2], '3');
		ensure_equals("(2.4)", buf[3], '4');
		ensure_equals("(2.5)", buf[4], '\0');

		ensure_equals("(3)", integerToOtherBase<int, 10>(123, smallbuf, sizeof(smallbuf)), 3u);
		ensure_equals("(3.1)", smallbuf[0], '1');
		ensure_equals("(3.2)", smallbuf[1], '2');
		ensure_equals("(3.3)", smallbuf[2], '3');
		ensure_equals("(3.4)", smallbuf[3], '\0');

		try {
			integerToOtherBase<int, 10>(1234, smallbuf, sizeof(smallbuf));
			fail("Exception expected");
		} catch (const std::length_error &) {
			// Pass.
		}
	}
}
