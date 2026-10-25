#include <TestSupport.h>
#include <InstanceDirectory.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Passenger;
using namespace std;

namespace tut {
	struct InstanceDirectoryTest: public TestBase {
		string registryPath;
		TempDir registryDir;

		InstanceDirectoryTest()
			: registryPath("/tmp/instancedirectorytest." + toString(getpid())),
			  registryDir(registryPath)
			{ }
	};

	DEFINE_TEST_GROUP(InstanceDirectoryTest);

	TEST_METHOD(1) {
		set_test_name("It creates a world-readable instance directory with the expected contents");

		InstanceDirectory::CreationOptions options;
		InstanceDirectory instanceDir(options, registryPath);
		struct stat buf;

		ensure_equals("(1)", stat(instanceDir.getPath().c_str(), &buf), 0);
		ensure("(2)", S_ISDIR(buf.st_mode));
		ensure_equals("(3)", buf.st_mode & 07777, (mode_t) 0755);
		ensure_equals("(4)", getFileType(instanceDir.getPath() + "/web_server_info"), FT_DIRECTORY);
		ensure_equals("(5)", getFileType(instanceDir.getPath() + "/agents.s"), FT_DIRECTORY);
		ensure_equals("(6)", getFileType(instanceDir.getPath() + "/apps.s"), FT_DIRECTORY);
		ensure_equals("(7)", getFileType(instanceDir.getPath() + "/lock"), FT_REGULAR);
	}
}
