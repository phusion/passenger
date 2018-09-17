#include <TestSupport.h>
#include <Core/ApplicationPool/Options.h>
#include <Core/SpawningKit/UserSwitchingRules.h>
#include <SystemTools/UserDatabase.h>
#include <Utils.h>

using namespace Passenger;
using namespace Passenger::SpawningKit;

namespace tut {
	struct Core_SpawningKit_UserSwitchingRulesTest: public TestBase {
		WrapperRegistry::Registry wrapperRegistry;
		AppPoolOptions options;
		UserSwitchingInfo result;

		Core_SpawningKit_UserSwitchingRulesTest() {
			wrapperRegistry.finalize();
			options.spawnMethod = "direct";
			options.loadShellEnvvars = false;
			options.appRoot = "tmp.wsgi";
			options.appType = "wsgi";
			options.defaultUser = testConfig["default_user"].asCString();
			options.defaultGroup = testConfig["default_group"].asCString();
		}
	};

	#define SETUP_USER_SWITCHING_TEST(code) \
		if (geteuid() != 0) { \
			return; \
		} \
		TempDirCopy stub("stub/wsgi", "tmp.wsgi"); \
		code

	#define RUN_USER_SWITCHING_TEST() \
		result = prepareUserSwitching(options, wrapperRegistry)

	static uid_t uidFor(const string &userName) {
		OsUser osUser;

		if (lookupSystemUserByName(userName, osUser)) {
			return osUser.pwd.pw_uid;
		} else {
			throw RuntimeException("OS user account " + userName + " does not exist");
		}
	}

	static gid_t gidFor(const string &groupName) {
		OsGroup osGroup;

		if (lookupSystemGroupByName(groupName, osGroup)) {
			return osGroup.grp.gr_gid;
		} else {
			throw RuntimeException("OS group account " + groupName + " does not exist");
		}
	}

	DEFINE_TEST_GROUP(Core_SpawningKit_UserSwitchingRulesTest);


	// If 'user' is set
		// and 'user' is 'root'
			TEST_METHOD(1) {
				set_test_name("If user is set and user is root,"
					" then it changes the user to the value of 'defaultUser'");
				// It changes the user to the value of 'defaultUser'.
				SETUP_USER_SWITCHING_TEST(
					options.user = "root";
				);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemUsernameByUid(result.uid), testConfig["default_user"]);
			}

			TEST_METHOD(2) {
				set_test_name("If user is set and user is root,"
					" and 'group' is given,"
					" then it changes group to the given group name");
				SETUP_USER_SWITCHING_TEST(
					options.user = "root";
					options.group = testConfig["normal_group_1"].asCString();
				);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["normal_group_1"].asString());
			}

			TEST_METHOD(3) {
				set_test_name("If user is set and user is root,"
					" and 'group' is set to the root group,"
					" then it changes group to defaultGroup");
				string rootGroup = lookupSystemGroupnameByGid(0);
				SETUP_USER_SWITCHING_TEST(
					options.user = "root";
					options.group = rootGroup;
				);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["default_group"].asString());
			}

			// and 'group' is set to '!STARTUP_FILE!'"
				TEST_METHOD(4) {
					set_test_name("If user is set, user is root,"
						" and 'group' is set to '!STARTUP_FILE!',"
						" then it changes the group to the startup file's group");
					SETUP_USER_SWITCHING_TEST(
						options.user = "root";
						options.group = "!STARTUP_FILE!";
					);
					lchown("tmp.wsgi/passenger_wsgi.py",
						(uid_t) -1,
						gidFor(testConfig["normal_group_1"].asString()));
					RUN_USER_SWITCHING_TEST();
					ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["normal_group_1"].asString());
				}

				TEST_METHOD(5) {
					set_test_name("If user is set, user is root,"
						" and 'group' is set to '!STARTUP_FILE!',"
						" and the startup file is a symlink,"
						" then it uses the symlink's group, not the target's group");
					SETUP_USER_SWITCHING_TEST(
						options.user = "root";
						options.group = "!STARTUP_FILE!";
					);
					lchown("tmp.wsgi/passenger_wsgi.py",
						(uid_t) -1,
						gidFor(testConfig["normal_group_2"].asString()));
					chown("tmp.wsgi/passenger_wsgi.py.real",
						(uid_t) -1,
						gidFor(testConfig["normal_group_1"].asString()));
					RUN_USER_SWITCHING_TEST();
					ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["normal_group_2"].asString());
				}

			TEST_METHOD(6) {
				set_test_name("If user is set and user is root,"
					" and 'group' is not given,"
					" then it changes the group to defaultUser's primary group");
				SETUP_USER_SWITCHING_TEST(
					options.user = "root";
				);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid),
					getPrimaryGroupName(testConfig["default_user"].asString()));
			}

		// and 'user' is not 'root'
			TEST_METHOD(10) {
				set_test_name("If user is set and user is not root,"
					" then it changes the user to the given username");
				SETUP_USER_SWITCHING_TEST(
					options.user = testConfig["normal_user_1"].asCString();
				);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemUsernameByUid(result.uid), testConfig["normal_user_1"].asString());
			}

			TEST_METHOD(11) {
				set_test_name("If user is set and user is not root,"
					" and 'group' is given,"
					" then it changes group to the given group name");
				SETUP_USER_SWITCHING_TEST(
					options.user = testConfig["normal_user_1"].asCString();
					options.group = testConfig["normal_group_1"].asCString();
				);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["normal_group_1"].asString());
			}

			TEST_METHOD(12) {
				set_test_name("If user is set and user is not root,"
					" and 'group' is given,"
					" then it changes the user to the given username");
				SETUP_USER_SWITCHING_TEST(
					options.user = testConfig["normal_user_1"].asCString();
					options.group = testConfig["normal_group_1"].asCString();
				);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemUsernameByUid(result.uid), testConfig["normal_user_1"].asString());
			}

			TEST_METHOD(13) {
				set_test_name("If user is set and user is not root,"
					" and 'group' is set to the root group,"
					" then it changes group to defaultGroup");
				string rootGroup = lookupSystemGroupnameByGid(0);
				SETUP_USER_SWITCHING_TEST(
					options.user = testConfig["normal_user_1"].asCString();
					options.group = rootGroup;
				);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["default_group"].asString());
			}

			TEST_METHOD(14) {
				set_test_name("If user is set and user is not root,"
					" and 'group' is set to the root group,"
					" then it changes the user to the given username");
				string rootGroup = lookupSystemGroupnameByGid(0);
				SETUP_USER_SWITCHING_TEST(
					options.user = testConfig["normal_user_1"].asCString();
					options.group = rootGroup;
				);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemUsernameByUid(result.uid), testConfig["normal_user_1"].asString());
			}

			// and 'group' is set to '!STARTUP_FILE!'
				TEST_METHOD(15) {
					set_test_name("If user is set and user is not root,"
						" and 'group' is set to '!STARTUP_FILE!',"
						" then it changes the group to the startup file's group");
					SETUP_USER_SWITCHING_TEST(
						options.user = testConfig["normal_user_1"].asCString();
						options.group = "!STARTUP_FILE!";
					);
					lchown("tmp.wsgi/passenger_wsgi.py",
						(uid_t) -1,
						gidFor(testConfig["normal_group_1"].asString()));
					RUN_USER_SWITCHING_TEST();
					ensure_equals(lookupSystemGroupnameByGid(result.gid),
						testConfig["normal_group_1"].asString());
				}

				TEST_METHOD(16) {
					set_test_name("If user is set and user is not root,"
						" and 'group' is set to '!STARTUP_FILE!',"
						" and the startup file is a symlink,"
						" then it uses the symlink's group, not the target's group");
					SETUP_USER_SWITCHING_TEST(
						options.user = testConfig["normal_user_1"].asCString();
						options.group = "!STARTUP_FILE!";
					);
					lchown("tmp.wsgi/passenger_wsgi.py",
						(uid_t) -1,
						gidFor(testConfig["normal_group_2"].asString()));
					chown("tmp.wsgi/passenger_wsgi.py.real",
						(uid_t) -1,
						gidFor(testConfig["normal_group_1"].asString()));
					RUN_USER_SWITCHING_TEST();
					ensure_equals(lookupSystemGroupnameByGid(result.gid),
						testConfig["normal_group_2"].asString());
				}

			TEST_METHOD(17) {
				set_test_name("If user is set and user is not root,"
					" and 'group' is not given,"
					" then it changes the group to the user's primary group");
				SETUP_USER_SWITCHING_TEST(
					options.user = testConfig["normal_user_1"].asCString();
				);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid),
					getPrimaryGroupName(testConfig["normal_user_1"].asString()));
			}

		// and the given username does not exist
			TEST_METHOD(20) {
				set_test_name("If user is set and the given username does not exist,"
					" then it changes the user to the value of defaultUser");

				// It changes the user to the value of defaultUser.
				SETUP_USER_SWITCHING_TEST(
					options.user = testConfig["nonexistant_user"].asCString();
				);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemUsernameByUid(result.uid), testConfig["default_user"].asString());
			}

			TEST_METHOD(21) {
				set_test_name("If user is set and the given username does not exist,"
					" and 'group' is given,"
					" then it changes group to the given group name");

				// If 'group' is given, it changes group to the given group name.
				SETUP_USER_SWITCHING_TEST(
					options.user = testConfig["nonexistant_user"].asCString();
					options.group = testConfig["normal_group_1"].asCString();
				);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["normal_group_1"].asString());
			}

			TEST_METHOD(22) {
				set_test_name("If user is set and the given username does not exist,"
					" and 'group' is set to the root group,"
					" then it changes group to defaultGroup");

				string rootGroup = lookupSystemGroupnameByGid(0);
				SETUP_USER_SWITCHING_TEST(
					options.user = testConfig["nonexistant_user"].asCString();
					options.group = rootGroup;
				);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["default_group"].asString());
			}

			// and 'group' is set to '!STARTUP_FILE!'
				TEST_METHOD(23) {
					set_test_name("If user is set and the given username does not exist,"
						" and 'group' is set to '!STARTUP_FILE!',"
						" then it changes the group to the startup file's group");

					SETUP_USER_SWITCHING_TEST(
						options.user = testConfig["nonexistant_user"].asCString();
						options.group = "!STARTUP_FILE!";
					);
					lchown("tmp.wsgi/passenger_wsgi.py",
						(uid_t) -1,
						gidFor(testConfig["normal_group_1"].asString()));
					RUN_USER_SWITCHING_TEST();
					ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["normal_group_1"].asString());
				}

				TEST_METHOD(24) {
					set_test_name("If user is set and the given username does not exist,"
						" and 'group' is set to '!STARTUP_FILE!',"
						" and the startup file is a symlink,"
						" then it uses the symlink's group, not the target's group");

					SETUP_USER_SWITCHING_TEST(
						options.user = testConfig["nonexistant_user"].asCString();
						options.group = "!STARTUP_FILE!";
					);
					lchown("tmp.wsgi/passenger_wsgi.py",
						(uid_t) -1,
						gidFor(testConfig["normal_group_2"].asString()));
					chown("tmp.wsgi/passenger_wsgi.py.real",
						(uid_t) -1,
						gidFor(testConfig["normal_group_1"].asString()));
					RUN_USER_SWITCHING_TEST();
					ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["normal_group_2"].asString());
				}

			TEST_METHOD(25) {
				set_test_name("If user is set and the given username does not exist,"
						" and 'group' is not given,"
						" then it changes the group to defaultUser's primary group");

				SETUP_USER_SWITCHING_TEST(
					options.user = testConfig["nonexistant_user"].asCString();
				);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid),
					getPrimaryGroupName(testConfig["default_user"].asString()));
			}

	// If 'user' is not set
		// and the startup file's owner exists
			TEST_METHOD(30) {
				set_test_name("If user is not set,"
					" and the startup file's owner exists,"
					" it changes the user to the owner of the startup file");

				SETUP_USER_SWITCHING_TEST(
					(void) 0;
				);
				lchown("tmp.wsgi/passenger_wsgi.py",
					uidFor(testConfig["normal_user_1"].asString()),
					(gid_t) -1);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemUsernameByUid(result.uid), testConfig["normal_user_1"].asString());
			}

			TEST_METHOD(31) {
				set_test_name("If user is not set,"
					" and the startup file's owner exists,"
					" and the startup file is a symlink,"
					" it uses the symlink's owner, not the target's owner");

				SETUP_USER_SWITCHING_TEST(
					(void) 0;
				);
				lchown("tmp.wsgi/passenger_wsgi.py",
					uidFor(testConfig["normal_user_2"].asString()),
					(gid_t) -1);
				chown("tmp.wsgi/passenger_wsgi.py.real",
					uidFor(testConfig["normal_user_1"].asString()),
					(gid_t) -1);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemUsernameByUid(result.uid), testConfig["normal_user_2"].asString());
			}

			TEST_METHOD(32) {
				set_test_name("If user is not set,"
					" and the startup file's owner exists,"
					" and 'group' is given,"
					" then it changes group to the given group name");
				SETUP_USER_SWITCHING_TEST(
					options.group = testConfig["normal_group_1"].asCString();
				);
				lchown("tmp.wsgi/passenger_wsgi.py",
					uidFor(testConfig["normal_user_1"].asString()),
					(gid_t) -1);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["normal_group_1"].asString());
			}

			TEST_METHOD(33) {
				set_test_name("If user is not set,"
					" and the startup file's owner exists,"
					" and 'group' is set to the root group,"
					" then it changes group to defaultGroup");
				string rootGroup = lookupSystemGroupnameByGid(0);
				SETUP_USER_SWITCHING_TEST(
					options.group = rootGroup;
				);
				lchown("tmp.wsgi/passenger_wsgi.py",
					uidFor(testConfig["normal_user_1"].asString()),
					(gid_t) -1);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["default_group"].asString());
			}

			// and 'group' is set to '!STARTUP_FILE!'
				TEST_METHOD(34) {
					set_test_name("If user is not set,"
						" and the startup file's owner exists,"
						" and 'group' is set to '!STARTUP_FILE!',"
						" then it changes the group to the startup file's group");
					SETUP_USER_SWITCHING_TEST(
						options.group = "!STARTUP_FILE!";
					);
					lchown("tmp.wsgi/passenger_wsgi.py",
						(uid_t) -1,
						gidFor(testConfig["normal_group_1"].asString()));
					RUN_USER_SWITCHING_TEST();
					ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["normal_group_1"].asString());
				}

				TEST_METHOD(35) {
					set_test_name("If user is not set,"
						" and the startup file's owner exists,"
						" and 'group' is set to '!STARTUP_FILE!',"
						" and the startup file is a symlink,"
						" then it uses the symlink's group, not the target's group");

					SETUP_USER_SWITCHING_TEST(
						options.group = "!STARTUP_FILE!";
					);
					lchown("tmp.wsgi/passenger_wsgi.py",
						(uid_t) -1,
						gidFor(testConfig["normal_group_2"].asString()));
					chown("tmp.wsgi/passenger_wsgi.py.real",
						(uid_t) -1,
						gidFor(testConfig["normal_group_1"].asString()));
					RUN_USER_SWITCHING_TEST();
					ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["normal_group_2"].asString());
				}

			TEST_METHOD(36) {
				set_test_name("If user is not set,"
					" and the startup file's owner exists,"
					" and 'group' is not given,"
					" then it changes the group to the startup file's owner's primary group");
				SETUP_USER_SWITCHING_TEST(
					(void) 0;
				);
				lchown("tmp.wsgi/passenger_wsgi.py",
					uidFor(testConfig["normal_user_1"].asString()),
					(gid_t) -1);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid),
					getPrimaryGroupName(testConfig["normal_user_1"].asString()));
			}

		// and the startup file's owner doesn't exist
			TEST_METHOD(40) {
				set_test_name("If user is not set,"
					" and the startup file's owner doesn't exist,"
					" then it changes the user to the value of defaultUser");
				SETUP_USER_SWITCHING_TEST(
					(void) 0;
				);
				lchown("tmp.wsgi/passenger_wsgi.py",
					(uid_t) testConfig["nonexistant_uid"].asInt64(),
					(gid_t) -1);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemUsernameByUid(result.uid), testConfig["default_user"].asString());
			}

			TEST_METHOD(41) {
				set_test_name("If user is not set,"
					" and the startup file's owner doesn't exist,"
					" and 'group' is given,"
					" then it changes group to the given group name");
				SETUP_USER_SWITCHING_TEST(
					options.group = testConfig["normal_group_1"].asCString();
				);
				lchown("tmp.wsgi/passenger_wsgi.py",
					(uid_t) testConfig["nonexistant_uid"].asInt64(),
					(gid_t) -1);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["normal_group_1"].asString());
			}

			TEST_METHOD(42) {
				set_test_name("If user is not set,"
					" and the startup file's owner doesn't exist,"
					" and 'group' is set to the root group,"
					" then it changes group to defaultGroup");
				string rootGroup = lookupSystemGroupnameByGid(0);
				SETUP_USER_SWITCHING_TEST(
					options.group = rootGroup;
				);
				lchown("tmp.wsgi/passenger_wsgi.py",
					(uid_t) testConfig["nonexistant_uid"].asInt64(),
					(gid_t) -1);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["default_group"].asString());
			}

			// and 'group' is set to '!STARTUP_FILE!'
				// and the startup file's group doesn't exist
					TEST_METHOD(43) {
						set_test_name("If user is not set,"
							" and the startup file's owner doesn't exist,"
							" and 'group' is set to '!STARTUP_FILE!',"
							" and the startup file's group doesn't exist,"
							" then it changes the group to the value given by defaultGroup");
						SETUP_USER_SWITCHING_TEST(
							options.group = "!STARTUP_FILE!";
						);
						lchown("tmp.wsgi/passenger_wsgi.py",
							(uid_t) testConfig["nonexistant_uid"].asInt64(),
							(gid_t) testConfig["nonexistant_gid"].asInt64());
						RUN_USER_SWITCHING_TEST();
						ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["default_group"].asString());
					}

				// and the startup file's group exists
					TEST_METHOD(44) {
						set_test_name("If user is not set,"
							" and the startup file's owner doesn't exist,"
							" and 'group' is set to '!STARTUP_FILE!',"
							" and the startup file's group exists,"
							" then it changes the group to the startup file's group");
						SETUP_USER_SWITCHING_TEST(
							options.group = "!STARTUP_FILE!";
						);
						lchown("tmp.wsgi/passenger_wsgi.py",
							(uid_t) testConfig["nonexistant_uid"].asInt64(),
							gidFor(testConfig["normal_group_1"].asString()));
						RUN_USER_SWITCHING_TEST();
						ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["normal_group_1"].asString());
					}

					TEST_METHOD(45) {
						set_test_name("If user is not set,"
							" and the startup file's owner doesn't exist,"
							" and 'group' is set to '!STARTUP_FILE!',"
							" and the startup file's group exists,"
							" and the startup file is a symlink,"
							" then it uses the symlink's group, not the target's group");

						SETUP_USER_SWITCHING_TEST(
							options.group = "!STARTUP_FILE!";
						);
						lchown("tmp.wsgi/passenger_wsgi.py",
							(uid_t) testConfig["nonexistant_uid"].asInt64(),
							gidFor(testConfig["normal_group_2"].asString()));
						chown("tmp.wsgi/passenger_wsgi.py.real",
							(uid_t) -1,
							gidFor(testConfig["normal_group_1"].asString()));
						RUN_USER_SWITCHING_TEST();
						ensure_equals(lookupSystemGroupnameByGid(result.gid), testConfig["normal_group_2"].asString());
					}

			TEST_METHOD(46) {
				set_test_name("If user is not set,"
					" and the startup file's owner doesn't exist,"
					" and 'group' is not given,"
					" then it changes the group to defaultUser's primary group");

				SETUP_USER_SWITCHING_TEST(
					(void) 0;
				);
				lchown("tmp.wsgi/passenger_wsgi.py",
					(uid_t) testConfig["nonexistant_uid"].asInt64(),
					(gid_t) -1);
				RUN_USER_SWITCHING_TEST();
				ensure_equals(lookupSystemGroupnameByGid(result.gid),
					getPrimaryGroupName(testConfig["default_user"].asString()));
			}

	TEST_METHOD(50) {
		set_test_name("It raises an error if it tries to lower to 'defaultUser',"
			" but that user doesn't exist");

		SETUP_USER_SWITCHING_TEST(
			options.user = "root";
			options.defaultUser = testConfig["nonexistant_user"].asCString();
		);
		try {
			RUN_USER_SWITCHING_TEST();
			fail("RuntimeException expected");
		} catch (const RuntimeException &e) {
			ensure(containsSubstring(e.what(), "Cannot determine a user to lower privilege to"));
		}
	}

	TEST_METHOD(51) {
		set_test_name("It raises an error if it tries to lower to 'default_group',"
			" but that group doesn't exist");
		string rootGroup = lookupSystemGroupnameByGid(0);
		SETUP_USER_SWITCHING_TEST(
			options.user = testConfig["normal_user_1"].asCString();
			options.group = rootGroup;
			options.defaultGroup = testConfig["nonexistant_group"].asCString();
		);
		try {
			RUN_USER_SWITCHING_TEST();
			fail("RuntimeException expected");
		} catch (const RuntimeException &e) {
			ensure(containsSubstring(e.what(), "Cannot determine a group to lower privilege to"));
		}
	}
}
