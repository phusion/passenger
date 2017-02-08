/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2014-2017 Phusion Holding B.V.
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
#ifndef _PASSENGER_SERVER_KIT_HOOKS_H_
#define _PASSENGER_SERVER_KIT_HOOKS_H_

namespace Passenger {
namespace ServerKit {


class HooksImpl;

struct Hooks {
	HooksImpl *impl;
	void *userData;
};

class HooksImpl {
public:
	virtual ~HooksImpl() { }

	virtual bool hook_isConnected(Hooks *hooks, void *source) {
		return true;
	}

	virtual void hook_ref(Hooks *hooks, void *source, const char *file, unsigned int line) { }
	virtual void hook_unref(Hooks *hooks, void *source, const char *file, unsigned int line) { }
};

struct RefGuard {
	Hooks *hooks;
	void *source;
	const char *file;
	unsigned int line;

	RefGuard(Hooks *_hooks, void *_source, const char *_file, unsigned int _line)
		: hooks(_hooks),
		  source(_source),
		  file(_file),
		  line(_line)
	{
		if (_hooks != NULL && _hooks->impl != NULL) {
			_hooks->impl->hook_ref(_hooks, _source, _file, _line);
		}
	}

	~RefGuard() {
		if (hooks != NULL && hooks->impl != NULL) {
			hooks->impl->hook_unref(hooks, source, file, line);
		}
	}
};


} // namespace ServerKit
} // namespace Passenger

#endif /* _PASSENGER_SERVER_KIT_HOOKS_H_ */
