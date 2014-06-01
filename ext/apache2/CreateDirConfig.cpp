/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2010-2013 Phusion
 *
 *  "Phusion Passenger" is a trademark of Hongli Lai & Ninh Bui.
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
 * CreateDirConfig.cpp is automatically generated from CreateDirConfig.cpp.erb,
 * using definitions from lib/phusion_passenger/apache2/config_options.rb.
 * Edits to CreateDirConfig.cpp will be lost.
 *
 * To update CreateDirConfig.cpp:
 *   rake apache2
 *
 * To force regeneration of CreateDirConfig.cpp:
 *   rm -f ext/apache2/CreateDirConfig.cpp
 *   rake ext/apache2/CreateDirConfig.cpp
 */



			config->ruby = NULL;
				config->python = NULL;
				config->nodejs = NULL;
				config->appEnv = NULL;
				config->minInstances = UNSET_INT_VALUE;
				config->maxInstancesPerApp = UNSET_INT_VALUE;
				config->user = NULL;
				config->group = NULL;
				config->errorOverride = DirConfig::UNSET;
				config->maxRequests = UNSET_INT_VALUE;
				config->startTimeout = UNSET_INT_VALUE;
				config->highPerformance = DirConfig::UNSET;
				config->enabled = DirConfig::UNSET;
				config->maxRequestQueueSize = UNSET_INT_VALUE;
				config->loadShellEnvvars = DirConfig::UNSET;
				config->bufferUpload = DirConfig::UNSET;
				config->appType = NULL;
				config->startupFile = NULL;
				config->stickySessions = DirConfig::UNSET;
				config->stickySessionsCookieName = DirConfig::UNSET;
	
