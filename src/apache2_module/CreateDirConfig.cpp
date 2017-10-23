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
 * CreateDirConfig.cpp is automatically generated from CreateDirConfig.cpp.cxxcodebuilder,
 * using definitions from src/ruby_supportlib/phusion_passenger/apache2/config_options.rb.
 * Edits to CreateDirConfig.cpp will be lost.
 *
 * To update CreateDirConfig.cpp:
 *   rake apache2
 *
 * To force regeneration of CreateDirConfig.cpp:
 *   rm -f src/apache2_module/CreateDirConfig.cpp
 *   rake src/apache2_module/CreateDirConfig.cpp
 */

/*
 * config->ruby: default initialized
 */
/*
 * config->python: default initialized
 */
/*
 * config->nodejs: default initialized
 */
/*
 * config->meteorAppSettings: default initialized
 */
/*
 * config->baseURIs: default initialized
 */
/*
 * config->appEnv: default initialized
 */
config->minInstances = UNSET_INT_VALUE;
config->maxInstancesPerApp = UNSET_INT_VALUE;
/*
 * config->user: default initialized
 */
/*
 * config->group: default initialized
 */
config->errorOverride = Apache2Module::UNSET;
config->maxRequests = UNSET_INT_VALUE;
config->startTimeout = UNSET_INT_VALUE;
config->highPerformance = Apache2Module::UNSET;
config->enabled = Apache2Module::UNSET;
config->maxRequestQueueSize = UNSET_INT_VALUE;
config->maxPreloaderIdleTime = UNSET_INT_VALUE;
config->loadShellEnvvars = Apache2Module::UNSET;
config->bufferUpload = Apache2Module::UNSET;
/*
 * config->appType: default initialized
 */
/*
 * config->startupFile: default initialized
 */
config->stickySessions = Apache2Module::UNSET;
config->stickySessionsCookieName = Apache2Module::UNSET;
/*
 * config->spawnMethod: default initialized
 */
config->showVersionInHeader = Apache2Module::UNSET;
config->friendlyErrorPages = Apache2Module::UNSET;
/*
 * config->restartDir: default initialized
 */
/*
 * config->appGroupName: default initialized
 */
config->forceMaxConcurrentRequestsPerProcess = UNSET_INT_VALUE;
config->lveMinUid = UNSET_INT_VALUE;
/*
 * config->appRoot: default initialized
 */
config->bufferResponse = Apache2Module::UNSET;
config->resolveSymlinksInDocumentRoot = Apache2Module::UNSET;
config->allowEncodedSlashes = Apache2Module::UNSET;
