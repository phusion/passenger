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
 * MergeDirConfig.cpp is automatically generated from MergeDirConfig.cpp.erb,
 * using definitions from lib/phusion_passenger/apache2/config_options.rb.
 * Edits to MergeDirConfig.cpp will be lost.
 *
 * To update MergeDirConfig.cpp:
 *   rake apache2
 *
 * To force regeneration of MergeDirConfig.cpp:
 *   rm -f ext/apache2/MergeDirConfig.cpp
 *   rake ext/apache2/MergeDirConfig.cpp
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


