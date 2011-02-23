#ifndef _PASSENGER_APPLICATION_POOL2_COMMON_H_
#define _PASSENGER_APPLICATION_POOL2_COMMON_H_

#include <boost/thread.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <oxt/tracable_exception.hpp>
#include <Utils/StringMap.h>

namespace Passenger {
namespace ApplicationPool2 {

using namespace std;
using namespace boost;
using namespace oxt;

class Pool;
class SuperGroup;
class Group;
class Process;
class Session;

typedef shared_ptr<Pool> PoolPtr;
typedef shared_ptr<SuperGroup> SuperGroupPtr;
typedef shared_ptr<Group> GroupPtr;
typedef shared_ptr<Process> ProcessPtr;
typedef shared_ptr<Session> SessionPtr;
typedef shared_ptr<tracable_exception> ExceptionPtr;
typedef StringMap<SuperGroupPtr> SuperGroupMap;
typedef function<void (const SessionPtr &session, const ExceptionPtr &e)> GetCallback;
typedef function<void ()> Callback;

struct Ticket {
	boost::mutex syncher;
	condition_variable cond;
	SessionPtr session;
	ExceptionPtr exception;
};

ExceptionPtr copyException(const tracable_exception &e);
void rethrowException(const ExceptionPtr &e);

} // namespace ApplicationPool2
} // namespace Passenger

#endif /* _PASSENGER_APPLICATION_POOL2_COMMON_H_ */
