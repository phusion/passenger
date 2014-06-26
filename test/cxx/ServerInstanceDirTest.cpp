#include "TestSupport.h"
#include "ServerInstanceDir.h"

using namespace Passenger;
using namespace std;

namespace tut {
	struct ServerInstanceDirTest {
		string parentDir;
		TempDir tmpDir;
		string nobodyGroup;
		
		ServerInstanceDirTest(): tmpDir("server_instance_dir_test.tmp") {
			parentDir = "server_instance_dir_test.tmp";
			nobodyGroup = getPrimaryGroupName("nobody");
		}
		
		void createGenerationDir(const string &instanceDir, unsigned int number) {
			string command = "mkdir " + instanceDir + "/generation-" + toString(number);
			runShellCommand(command.c_str());
		}
	};
	
	DEFINE_TEST_GROUP(ServerInstanceDirTest);
	
	TEST_METHOD(2) {
		// The (string) constructor creates a ServerInstanceDir object that's
		// associated with the given directory, and creates the directory
		// if it doesn't exist.
		DONT_RUN_IN_VAGRANT();
		ServerInstanceDir dir(parentDir + "/passenger-test.1234");
		ServerInstanceDir dir2(dir.getPath());
		ServerInstanceDir dir3(parentDir + "/foo");
		ensure_equals(dir2.getPath(), dir.getPath());
		ensure_equals(dir3.getPath(), parentDir + "/foo");
		ensure_equals(getFileType(dir3.getPath()), FT_DIRECTORY);
	}
	
	TEST_METHOD(3) {
		// A ServerInstanceDir object removes the server instance directory
		// upon destruction, but only if there are no more generations in it.
		{
			ServerInstanceDir dir(parentDir + "/passenger-test.1234");
		}
		ensure_equals(listDir(parentDir).size(), 0u);
		
		{
			ServerInstanceDir dir(parentDir + "/passenger-test.1234");
			createGenerationDir(dir.getPath(), 1);
		}
		ensure_equals(listDir(parentDir).size(), 1u);
	}
	
	TEST_METHOD(4) {
		// The destructor does not throw any exceptions if the server instance
		// directory doesn't exist anymore.
		ServerInstanceDir dir(parentDir + "/passenger-test.1234");
		removeDirTree(dir.getPath());
	}
	
	TEST_METHOD(5) {
		// The destructor doesn't remove the server instance directory if it
		// wasn't created with the ownership flag or if it's been detached.
		string path, path2;
		makeDirTree(parentDir + "/passenger-test.1234");
		makeDirTree(parentDir + "/passenger-test.5678");
		{
			ServerInstanceDir dir(parentDir + "/passenger-test.1234", false);
			ServerInstanceDir dir2(parentDir + "/passenger-test.5678", false);
			dir2.detach();
			path = dir.getPath();
			path2 = dir2.getPath();
		}
		ensure_equals(getFileType(path), FT_DIRECTORY);
		ensure_equals(getFileType(path2), FT_DIRECTORY);
	}
	
	TEST_METHOD(6) {
		// If there are no existing generations, newGeneration() creates a new
		// generation directory with number 0.
		ServerInstanceDir dir(parentDir + "/passenger-test.1234");
		unsigned int ncontents = listDir(dir.getPath()).size();
		ServerInstanceDir::GenerationPtr generation = dir.newGeneration(true,
			"nobody", nobodyGroup, 0, 0);
		
		ensure_equals(generation->getNumber(), 0u);
		ensure_equals(getFileType(generation->getPath()), FT_DIRECTORY);
		ensure_equals(listDir(dir.getPath()).size(), ncontents + 1);
	}
	
	TEST_METHOD(7) {
		// A Generation object returned by newGeneration() deletes the associated
		// generation directory upon destruction.
		ServerInstanceDir dir(parentDir + "/passenger-test.1234");
		ServerInstanceDir::GenerationPtr generation = dir.newGeneration(true,
			"nobody", nobodyGroup, 0, 0);
		string path = generation->getPath();
		generation.reset();
		ensure_equals(getFileType(path), FT_NONEXISTANT);
	}
	
	TEST_METHOD(8) {
		// getNewestGeneration() returns the newest generation.
		ServerInstanceDir dir(parentDir + "/passenger-test.1234");
		ServerInstanceDir::GenerationPtr generation0 = dir.newGeneration(true, "nobody", nobodyGroup, 0, 0);
		ServerInstanceDir::GenerationPtr generation1 = dir.newGeneration(true, "nobody", nobodyGroup, 0, 0);
		ServerInstanceDir::GenerationPtr generation2 = dir.newGeneration(true, "nobody", nobodyGroup, 0, 0);
		ServerInstanceDir::GenerationPtr generation3 = dir.newGeneration(true, "nobody", nobodyGroup, 0, 0);
		
		generation2.reset();
		ensure_equals(dir.getNewestGeneration()->getNumber(), 3u);
		generation3.reset();
		ensure_equals(dir.getNewestGeneration()->getNumber(), 1u);
	}
	
	TEST_METHOD(9) {
		// getNewestGeneration returns null if there are no generations.
		ServerInstanceDir dir(parentDir + "/passenger-test.1234");
		ensure(dir.getNewestGeneration() == NULL);
	}
	
	TEST_METHOD(10) {
		// A Generation object returned by getNewestGeneration() doesn't delete
		// the associated generation directory upon destruction.
		ServerInstanceDir dir(parentDir + "/passenger-test.1234");
		ServerInstanceDir::GenerationPtr generation = dir.newGeneration(true, "nobody", nobodyGroup, 0, 0);
		ServerInstanceDir::GenerationPtr newestGeneration = dir.getNewestGeneration();
		newestGeneration.reset();
		ensure_equals(getFileType(generation->getPath()), FT_DIRECTORY);
	}
	
	TEST_METHOD(11) {
		// getGeneration() returns the given generation.
		ServerInstanceDir dir(parentDir + "/passenger-test.1234");
		ServerInstanceDir::GenerationPtr generation0 = dir.newGeneration(true, "nobody", nobodyGroup, 0, 0);
		ServerInstanceDir::GenerationPtr generation1 = dir.newGeneration(true, "nobody", nobodyGroup, 0, 0);
		ServerInstanceDir::GenerationPtr generation2 = dir.newGeneration(true, "nobody", nobodyGroup, 0, 0);
		ServerInstanceDir::GenerationPtr generation3 = dir.newGeneration(true, "nobody", nobodyGroup, 0, 0);
		
		ensure_equals(dir.getGeneration(0)->getNumber(), 0u);
		ensure_equals(dir.getGeneration(3)->getNumber(), 3u);
	}
	
	TEST_METHOD(12) {
		// A Generation object returned by getGeneration() doesn't delete the
		// associated generation directory upon destruction.
		ServerInstanceDir dir(parentDir + "/passenger-test.1234");
		ServerInstanceDir::GenerationPtr generation0 = dir.newGeneration(true, "nobody", nobodyGroup, 0, 0);
		ServerInstanceDir::GenerationPtr generation1 = dir.newGeneration(true, "nobody", nobodyGroup, 0, 0);
		
		dir.getGeneration(0).reset();
		dir.getGeneration(1).reset();
		ensure_equals(getFileType(generation0->getPath()), FT_DIRECTORY);
		ensure_equals(getFileType(generation1->getPath()), FT_DIRECTORY);
	}
	
	TEST_METHOD(13) {
		// A detached Generation doesn't delete the associated generation
		// directory upon destruction.
		ServerInstanceDir dir(parentDir + "/passenger-test.1234");
		ServerInstanceDir::GenerationPtr generation = dir.newGeneration(true, "nobody", nobodyGroup, 0, 0);
		string path = generation->getPath();
		generation->detach();
		generation.reset();
		ensure_equals(getFileType(path), FT_DIRECTORY);
	}
	
	TEST_METHOD(14) {
		// It's possible to have two ServerInstanceDir objects constructed
		// with the same (pid_t, string) constructor arguments.
		DONT_RUN_IN_VAGRANT();
		ServerInstanceDir dir(parentDir + "/passenger-test.1234");
		ServerInstanceDir dir2(parentDir + "/passenger-test.1234");
	}
}
