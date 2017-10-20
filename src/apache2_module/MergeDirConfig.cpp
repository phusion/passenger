/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2017 Phusion Holding B.V.
 *
 *  "Passenger", "Phusion Passenger" and "Union Station" are registered
 *  trademarks of Phusion Holding B.V.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *  THE SOFTWARE.
 */

/*
 * MergeDirConfig.cpp is automatically generated from MergeDirConfig.cpp.cxxcodebuilder,
 * using definitions from src/ruby_supportlib/phusion_passenger/apache2/config_options.rb.
 * Edits to MergeDirConfig.cpp will be lost.
 *
 * To update MergeDirConfig.cpp:
 *   rake apache2
 *
 * To force regeneration of MergeDirConfig.cpp:
 *   rm -f src/apache2_module/MergeDirConfig.cpp
 *   rake src/apache2_module/MergeDirConfig.cpp
 */

config->ruby = mergeStrValue(add->ruby, base->ruby,
	StaticString());
config->python = mergeStrValue(add->python, base->python,
	DEFAULT_PYTHON);
config->nodejs = mergeStrValue(add->nodejs, base->nodejs,
	DEFAULT_NODEJS);
config->meteorAppSettings = mergeStrValue(add->meteorAppSettings, base->meteorAppSettings);
config->baseURIs = mergeStrSetValue(add->baseURIs, base->baseURIs);
config->appEnv = mergeStrValue(add->appEnv, base->appEnv,
	"production");
config->minInstances = mergeIntValue(add->minInstances, base->minInstances,
	1);
config->maxInstancesPerApp = mergeIntValue(add->maxInstancesPerApp, base->maxInstancesPerApp);
config->user = mergeStrValue(add->user, base->user);
config->group = mergeStrValue(add->group, base->group);
config->errorOverride = mergeBoolValue(add->errorOverride, base->errorOverride);
config->maxRequests = mergeIntValue(add->maxRequests, base->maxRequests,
	0);
config->startTimeout = mergeIntValue(add->startTimeout, base->startTimeout,
	DEFAULT_START_TIMEOUT / 1000);
config->highPerformance = mergeBoolValue(add->highPerformance, base->highPerformance);
config->enabled = mergeBoolValue(add->enabled, base->enabled,
	true);
config->maxRequestQueueSize = mergeIntValue(add->maxRequestQueueSize, base->maxRequestQueueSize,
	DEFAULT_MAX_REQUEST_QUEUE_SIZE);
config->maxPreloaderIdleTime = mergeIntValue(add->maxPreloaderIdleTime, base->maxPreloaderIdleTime,
	DEFAULT_MAX_PRELOADER_IDLE_TIME);
config->loadShellEnvvars = mergeBoolValue(add->loadShellEnvvars, base->loadShellEnvvars,
	true);
config->bufferUpload = mergeBoolValue(add->bufferUpload, base->bufferUpload,
	true);
config->appType = mergeStrValue(add->appType, base->appType);
config->startupFile = mergeStrValue(add->startupFile, base->startupFile);
config->stickySessions = mergeBoolValue(add->stickySessions, base->stickySessions);
config->stickySessionsCookieName = mergeBoolValue(add->stickySessionsCookieName, base->stickySessionsCookieName,
	DEFAULT_STICKY_SESSIONS_COOKIE_NAME);
config->spawnMethod = mergeStrValue(add->spawnMethod, base->spawnMethod);
config->showVersionInHeader = mergeBoolValue(add->showVersionInHeader, base->showVersionInHeader,
	true);
config->friendlyErrorPages = mergeBoolValue(add->friendlyErrorPages, base->friendlyErrorPages);
config->restartDir = mergeStrValue(add->restartDir, base->restartDir,
	"tmp");
config->appGroupName = mergeStrValue(add->appGroupName, base->appGroupName);
config->forceMaxConcurrentRequestsPerProcess = mergeIntValue(add->forceMaxConcurrentRequestsPerProcess, base->forceMaxConcurrentRequestsPerProcess,
	-1);
config->lveMinUid = mergeIntValue(add->lveMinUid, base->lveMinUid,
	DEFAULT_LVE_MIN_UID);
config->appRoot = mergeStrValue(add->appRoot, base->appRoot);
config->bufferResponse = mergeBoolValue(add->bufferResponse, base->bufferResponse);
config->resolveSymlinksInDocumentRoot = mergeBoolValue(add->resolveSymlinksInDocumentRoot, base->resolveSymlinksInDocumentRoot);
config->allowEncodedSlashes = mergeBoolValue(add->allowEncodedSlashes, base->allowEncodedSlashes);
