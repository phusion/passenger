/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2016 Phusion Holding B.V.
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

config->ruby =
	(add->ruby == NULL) ?
	base->ruby :
	add->ruby;
config->python =
	(add->python == NULL) ?
	base->python :
	add->python;
config->nodejs =
	(add->nodejs == NULL) ?
	base->nodejs :
	add->nodejs;
config->meteorAppSettings =
	(add->meteorAppSettings == NULL) ?
	base->meteorAppSettings :
	add->meteorAppSettings;
config->appEnv =
	(add->appEnv == NULL) ?
	base->appEnv :
	add->appEnv;
config->minInstances =
	(add->minInstances == UNSET_INT_VALUE) ?
	base->minInstances :
	add->minInstances;
config->maxInstancesPerApp =
	(add->maxInstancesPerApp == UNSET_INT_VALUE) ?
	base->maxInstancesPerApp :
	add->maxInstancesPerApp;
config->user =
	(add->user == NULL) ?
	base->user :
	add->user;
config->group =
	(add->group == NULL) ?
	base->group :
	add->group;
config->errorOverride =
	(add->errorOverride == DirConfig::UNSET) ?
	base->errorOverride :
	add->errorOverride;
config->maxRequests =
	(add->maxRequests == UNSET_INT_VALUE) ?
	base->maxRequests :
	add->maxRequests;
config->startTimeout =
	(add->startTimeout == UNSET_INT_VALUE) ?
	base->startTimeout :
	add->startTimeout;
config->highPerformance =
	(add->highPerformance == DirConfig::UNSET) ?
	base->highPerformance :
	add->highPerformance;
config->enabled =
	(add->enabled == DirConfig::UNSET) ?
	base->enabled :
	add->enabled;
config->maxRequestQueueSize =
	(add->maxRequestQueueSize == UNSET_INT_VALUE) ?
	base->maxRequestQueueSize :
	add->maxRequestQueueSize;
config->maxPreloaderIdleTime =
	(add->maxPreloaderIdleTime == UNSET_INT_VALUE) ?
	base->maxPreloaderIdleTime :
	add->maxPreloaderIdleTime;
config->loadShellEnvvars =
	(add->loadShellEnvvars == DirConfig::UNSET) ?
	base->loadShellEnvvars :
	add->loadShellEnvvars;
config->bufferUpload =
	(add->bufferUpload == DirConfig::UNSET) ?
	base->bufferUpload :
	add->bufferUpload;
config->appType =
	(add->appType == NULL) ?
	base->appType :
	add->appType;
config->startupFile =
	(add->startupFile == NULL) ?
	base->startupFile :
	add->startupFile;
config->stickySessions =
	(add->stickySessions == DirConfig::UNSET) ?
	base->stickySessions :
	add->stickySessions;
config->stickySessionsCookieName =
	(add->stickySessionsCookieName == DirConfig::UNSET) ?
	base->stickySessionsCookieName :
	add->stickySessionsCookieName;
config->spawnMethod =
	(add->spawnMethod == NULL) ?
	base->spawnMethod :
	add->spawnMethod;
config->showVersionInHeader =
	(add->showVersionInHeader == DirConfig::UNSET) ?
	base->showVersionInHeader :
	add->showVersionInHeader;
config->friendlyErrorPages =
	(add->friendlyErrorPages == DirConfig::UNSET) ?
	base->friendlyErrorPages :
	add->friendlyErrorPages;
config->restartDir =
	(add->restartDir == NULL) ?
	base->restartDir :
	add->restartDir;
config->appGroupName =
	(add->appGroupName == NULL) ?
	base->appGroupName :
	add->appGroupName;
config->forceMaxConcurrentRequestsPerProcess =
	(add->forceMaxConcurrentRequestsPerProcess == UNSET_INT_VALUE) ?
	base->forceMaxConcurrentRequestsPerProcess :
	add->forceMaxConcurrentRequestsPerProcess;
config->lveMinUid =
	(add->lveMinUid == UNSET_INT_VALUE) ?
	base->lveMinUid :
	add->lveMinUid;
