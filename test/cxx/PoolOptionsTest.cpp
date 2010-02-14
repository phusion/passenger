#include "TestSupport.h"
#include "PoolOptions.h"

using namespace Passenger;
using namespace std;

namespace tut {
	struct PoolOptionsTest {
	};

	DEFINE_TEST_GROUP(PoolOptionsTest);

	// Test the PoolOptions constructors and toVector().
	TEST_METHOD(1) {
		PoolOptions options;
		options.appRoot     = "/foo";
		options.frameworkSpawnerTimeout = 123;
		options.appSpawnerTimeout       = 456;
		options.maxRequests = 789;
		
		vector<string> args;
		args.push_back("abc");
		args.push_back("def");
		options.toVector(args);
		
		PoolOptions copy(args, 2);
		ensure_equals(options.appRoot, copy.appRoot);
		ensure_equals(options.rights, copy.rights);
		ensure_equals(options.environment, copy.environment);
		ensure_equals(options.spawnMethod, copy.spawnMethod);
		ensure_equals(options.appType, copy.appType);
		ensure_equals(options.frameworkSpawnerTimeout, copy.frameworkSpawnerTimeout);
		ensure_equals(options.appSpawnerTimeout, copy.appSpawnerTimeout);
		ensure_equals(options.maxRequests, copy.maxRequests);
	}
	
	// Test empty environmentVariables serialization and deserialization.
	TEST_METHOD(2) {
		PoolOptions options;
		vector<string> args;
		options.toVector(args);
		
		PoolOptions options2(args);
		ensure_equals(options2.environmentVariables->getItems()->size(), 0u);
	}
	
	// Test single item environmentVariables serialization and deserialization.
	TEST_METHOD(3) {
		PoolOptions options;
		SimpleStringListCreatorPtr list = ptr(new SimpleStringListCreator());
		vector<string> args;
		list->items->push_back("hello");
		list->items->push_back("world !!");
		options.environmentVariables = list;
		options.toVector(args);
		
		PoolOptions options2(args);
		const StringListPtr list2 = options2.environmentVariables->getItems();
		ensure_equals(list2->size(), 2u);
		ensure_equals(list2->at(0), "hello");
		ensure_equals(list2->at(1), "world !!");
	}
	
	// Test multiple items environmentVariables serialization and deserialization.
	TEST_METHOD(4) {
		PoolOptions options;
		SimpleStringListCreatorPtr list = ptr(new SimpleStringListCreator());
		vector<string> args;
		list->items->push_back("hello");
		list->items->push_back("world !!");
		list->items->push_back("PATH");
		list->items->push_back("/usr/local/bin");
		options.environmentVariables = list;
		options.toVector(args);
		
		PoolOptions options2(args);
		const StringListPtr list2 = options2.environmentVariables->getItems();
		ensure_equals(list2->size(), 4u);
		ensure_equals(list2->at(0), "hello");
		ensure_equals(list2->at(1), "world !!");
		ensure_equals(list2->at(2), "PATH");
		ensure_equals(list2->at(3), "/usr/local/bin");
	}
	
	// Calling toVector() with storeEnvVars = false on a PoolOption object that
	// has no environment variables works, and the resulting data can be unserialized.
	TEST_METHOD(5) {
		PoolOptions options;
		vector<string> args;
		options.appRoot = "hello";
		options.toVector(args, false);
		
		PoolOptions options2(args);
		ensure_equals(options2.appRoot, "hello");
		ensure_equals(options2.environmentVariables, StringListCreatorPtr());
	}
	
	// Calling toVector() with storeEnvVars = false on a PoolOption object that
	// has no environment variables works, and the resulting data can be unserialized.
	TEST_METHOD(6) {
		PoolOptions options;
		SimpleStringListCreatorPtr list = ptr(new SimpleStringListCreator());
		vector<string> args;
		list->items->push_back("hello");
		list->items->push_back("world");
		list->items->push_back("foo");
		list->items->push_back("bar");
		options.appRoot = "hello";
		options.environmentVariables = list;
		options.toVector(args, false);
		
		PoolOptions options2(args);
		ensure_equals(options2.appRoot, "hello");
		ensure_equals(options2.environmentVariables, StringListCreatorPtr());
	}
}
