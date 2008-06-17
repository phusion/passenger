#ifndef _PASSENGER_BACKTRACE_H_
#define _PASSENGER_BACKTRACE_H_

/**
 * Portable C++ backtrace support.
 *
 * C++ doesn't provide a builtin, automatic, portable way of obtaining
 * backtraces. Obtaining a backtrace via a debugger (e.g. gdb) is very
 * expensive. Furthermore, not all machines have a debugger installed.
 * This makes it very hard to debug problems on production servers.
 *
 * This file provides a portable way of specifying and obtaining
 * backtraces.
 *
 * <h2>Basic usage</h2>
 *
 * Backtrace points must be specified manually in the
 * code using TRACE_POINT(). The TracableException class allows one to
 * obtain the backtrace at the moment the exception object was created.
 *
 * For example:
 * @code
 * void foo() {
 *     TRACE_POINT();
 *     do_something();
 *     bar();
 *     do_something_else();
 * }
 *
 * void bar() {
 *     TRACE_POINT();
 *     throw TracableException();
 * }
 * @encode
 *
 * One can obtain the backtrace string, as follows:
 * @code
 * try {
 *     foo();
 * } catch (const TracableException &e) {
 *     cout << "Something bad happened:\n" << e.backtrace();
 * }
 * @endcode
 *
 * This will print something like:
 * @code
 * Something bad happened:
 *     in 'bar' (example.cpp:123)
 *     in 'foo' (example.cpp:117)
 *     in 'example_function' (example.cpp:456)
 * @encode
 *
 * <h2>Making sure the line number is correct</h2>
 * A TRACE_POINT() call will add a backtrace point for the source line on
 * which it is written. However, this causes an undesirable effect in long
 * functions:
 * @code
 * 100   void some_long_function() {
 * 101       TRACE_POINT();
 * 102       do_something();
 * 103       for (...) {
 * 104           do_something();
 * 105       }
 * 106       do_something_else();
 * 107
 * 108       if (!write_file()) {
 * 109           throw TracableException();
 * 110       }
 * 111   }
 * @endcode
 * You will want the thrown exception to show a backtrace line number that's
 * near line 109. But as things are now, the backtrace will show line 101.
 *
 * This can be solved by calling UPDATE_TRACE_POINT() from time to time:
 * @code
 * 100   void some_long_function() {
 * 101       TRACE_POINT();
 * 102       do_something();
 * 103       for (...) {
 * 104           do_something();
 * 105       }
 * 106       do_something_else();
 * 107
 * 108       if (!write_file()) {
 * 109           UPDATE_TRACE_POINT();   // Added!
 * 110           throw TracableException();
 * 111       }
 * 112   }
 * @endcode
 */

#include <exception>
#include <string>

#if defined(NDEBUG)
	namespace Passenger {
	
	using namespace std;
	
	class TracableException: public exception {
	public:
		virtual string backtrace() const throw() {
			return "     (backtrace support disabled during compile time)\n";
		}
		
		virtual const char *what() const throw() {
			return "Passenger::TracableException";
		}
	};
	
	#define TRACE_POINT() do { /* nothing */ } while (false)
	#define UPDATE_TRACE_POINT() do { /* nothing */ } while (false)
	
	} // namespace Passenger
#else
	#include <boost/thread.hpp>
	#include <sstream>
	#include <list>

	namespace Passenger {
	
	using namespace std;
	using namespace boost;
	class TracePoint;
	class TracableException;
	class ThreadRegistration;

	boost::mutex &_getBacktraceMutex();
	list<TracePoint *> *_getCurrentBacktrace();
	extern boost::mutex _threadRegistrationMutex;
	extern list<ThreadRegistration *> _registeredThreads;

	/**
	 * @internal
	 */
	class TracePoint {
	private:
		friend class TracableException;
	
		std::string function;
		std::string source;
		unsigned int line;
		bool detached;
	
		void detach() {
			detached = true;
		}
	public:
		TracePoint(const string &function, const string &source, unsigned int line) {
			this->function = function;
			this->source = source;
			this->line = line;
			detached = false;
			boost::mutex::scoped_lock l(_getBacktraceMutex());
			_getCurrentBacktrace()->push_back(this);
		}
	
		~TracePoint() {
			if (!detached) {
				boost::mutex::scoped_lock l(_getBacktraceMutex());
				_getCurrentBacktrace()->pop_back();
			}
		}
	
		void update(const std::string &source, unsigned int line) {
			this->source = source;
			this->line = line;
		}
	};

	class TracableException: public exception {
	private:
		list<TracePoint> backtraceCopy;
		
		void copyBacktrace() {
			boost::mutex::scoped_lock l(_getBacktraceMutex());
			list<TracePoint *>::const_iterator it;
			list<TracePoint *> *bt = _getCurrentBacktrace();
			for (it = bt->begin(); it != bt->end(); it++) {
				backtraceCopy.push_back(**it);
				(*it)->detach();
			}
		}
	public:
		TracableException() {
			copyBacktrace();
		}
		
		virtual string backtrace() const throw() {
			stringstream result;
			list<TracePoint>::const_iterator it;
			
			for (it = backtraceCopy.begin(); it != backtraceCopy.end(); it++) {
				result << "     in '" << it->function << "' "
					"(" << it->source << ":" << it->line << ")" <<
					std::endl;
			}
			return result.str();
		}
	};
	
	#define TRACE_POINT() Passenger::TracePoint __p(__PRETTY_FUNCTION__, __FILE__, __LINE__)
	#define UPDATE_TRACE_POINT() __p.update(__FILE__, __LINE__)
	
	/**
	 * @internal
	 */
	struct ThreadRegistration {
		string name;
		boost::mutex *backtraceMutex;
		list<TracePoint *> *backtrace;
	};
	
	class RegisterThreadWithBacktrace {
	private:
		ThreadRegistration *registration;
		list<ThreadRegistration *>::iterator it;
	public:
		RegisterThreadWithBacktrace(const string &name) {
			registration = new ThreadRegistration();
			registration->name = name;
			registration->backtraceMutex = &_getBacktraceMutex();
			registration->backtrace = _getCurrentBacktrace();
			
			boost::mutex::scoped_lock l(_threadRegistrationMutex);
			_registeredThreads.push_back(registration);
			it = _registeredThreads.end();
			it--;
		}
		
		~RegisterThreadWithBacktrace() {
			boost::mutex::scoped_lock l(_threadRegistrationMutex);
			_registeredThreads.erase(it);
			delete registration;
		}
	};
	
	} // namespace Passenger
#endif

#endif /* _PASSENGER_BACKTRACE_H_ */
