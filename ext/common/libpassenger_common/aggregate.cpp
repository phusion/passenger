
				#ifndef _GNU_SOURCE
					#define _GNU_SOURCE
				#endif
			#include "common/AccountsDatabase.cpp"
#include "common/AgentBase.cpp"
#include "common/AgentsStarter.cpp"
#include "common/Logging.cpp"
#include "common/LoggingAgent/FilterSupport.cpp"
#include "common/Utils.cpp"
#include "common/Utils/Base64.cpp"
#include "common/Utils/CachedFileStat.cpp"
#include "common/Utils/IOUtils.cpp"
#include "common/Utils/MD5.cpp"
#include "common/Utils/StrIntUtils.cpp"
#include "common/Utils/SystemTime.cpp"
