/*
 *  Phusion Passenger - https://www.phusionpassenger.com/
 *  Copyright (c) 2012-2018 Phusion Holding B.V.
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
#ifndef _PASSENGER_MESSAGE_PASSING_H_
#define _PASSENGER_MESSAGE_PASSING_H_


#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/thread.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <oxt/macros.hpp>
#include <jsoncpp/json.h>
#include <Exceptions.h>
#include <SystemTools/SystemTime.h>

#include <list>


namespace Passenger {

using namespace std;
using namespace boost;


/**
 * A simple in-process message passing library. Each message has a name, a bunch
 * of named arguments and an arbitrary object. Recipients can wait for a certain
 * message to arrive, possibly with a timeout. The receive function will return
 * as soon as the mailbox contains at least one message with the given name,
 * and removes that message from the mailbox, returning it.
 *
 * This library is designed for convenience and correctness, not speed. Messages
 * are allocated on the heap and are never copied: only their smart pointers are
 * passed around. This way you can pass arbitrary C++ objects.
 *
 * You must not modify Message objects after they've been sent. Likewise,
 * do not modify Message objects returned by peek().
 */
class MessageBox;
struct Message;
typedef boost::shared_ptr<MessageBox> MessageBoxPtr;
typedef boost::shared_ptr<Message> MessagePtr;

inline void _sendToMessageBox(const MessageBoxPtr &messageBox, const MessagePtr &message);


struct Message {
	string name;
	Json::Value args;
	boost::weak_ptr<MessageBox> from;
	void *data;
	void (*freeData)(void *p);

	Message()
		: data(0),
		  freeData(0)
		{ }

	Message(const string &_name)
		: name(_name),
		  data(0),
		  freeData(0)
		{ }

	Message(const MessageBoxPtr &from, const string &_name)
		: name(_name),
		  data(0),
		  freeData(0)
	{
		setFrom(from);
	}

	~Message() {
		if (data != NULL && freeData != NULL) {
			freeData(data);
		}
	}

	void setFrom(const MessageBoxPtr &messageBox) {
		from = boost::weak_ptr<MessageBox>(messageBox);
	}

	void sendReply(const MessagePtr &message) {
		MessageBoxPtr messageBox = from.lock();
		if (messageBox != NULL) {
			_sendToMessageBox(messageBox, message);
		}
	}

	void sendReply(const string &name) {
		sendReply(boost::make_shared<Message>(name));
	}
};


class MessageBox: public boost::enable_shared_from_this<MessageBox> {
	typedef list<MessagePtr> MessageList;
	typedef MessageList::iterator Iterator;
	typedef MessageList::const_iterator ConstIterator;

	mutable boost::mutex syncher;
	boost::condition_variable cond;
	MessageList messages;

	Iterator search(const string &name) {
		Iterator it, end = messages.end();
		for (it = messages.begin(); it != end; it++) {
			const MessagePtr &message = *it;
			if (message->name == name) {
				return it;
			}
		}
		return end;
	}

	ConstIterator search(const string &name) const {
		ConstIterator it, end = messages.end();
		for (it = messages.begin(); it != end; it++) {
			const MessagePtr &message = *it;
			if (message->name == name) {
				return it;
			}
		}
		return end;
	}

	template<typename StringCollection>
	Iterator searchAny(const StringCollection &names) {
		Iterator it, end = messages.end();
		for (it = messages.begin(); it != end; it++) {
			const MessagePtr &message = *it;
			typename StringCollection::const_iterator n_it, n_end = names.end();
			for (n_it = names.begin(); n_it != n_end; n_it++) {
				if (message->name == *n_it) {
					return it;
				}
			}
		}
		return end;
	}

	bool checkTimeout(boost::unique_lock<boost::mutex> &l, unsigned long long *timeout,
		unsigned long long beginTime, posix_time::ptime deadline)
	{
		posix_time::time_duration diff = deadline -
			posix_time::microsec_clock::local_time();
		bool timedOut;
		if (diff.is_negative()) {
			timedOut = true;
		} else {
			timedOut = !cond.timed_wait(l,
				posix_time::milliseconds(diff.total_milliseconds()));
		}
		if (timedOut) {
			substractTimePassed(timeout, beginTime);
		}
		return timedOut;
	}

	void substractTimePassed(unsigned long long *timeout, unsigned long long beginTime) {
		unsigned long long now = SystemTime::getMonotonicUsec();
		unsigned long long diff;
		if (now > beginTime) {
			diff = now - beginTime;
		} else {
			diff = 0;
		}
		if (*timeout > diff) {
			*timeout -= diff;
		} else {
			*timeout = 0;
		}
	}

public:
	void send(const MessagePtr &message) {
		boost::lock_guard<boost::mutex> l(syncher);
		message->setFrom(shared_from_this());
		messages.push_back(message);
		cond.notify_all();
	}

	void send(const string &name) {
		send(boost::make_shared<Message>(name));
	}

	const MessagePtr peek(const string &name) const {
		boost::unique_lock<boost::mutex> l(syncher);
		ConstIterator it = search(name);
		if (it == messages.end()) {
			return MessagePtr();
		} else {
			return *it;
		}
	}

	MessagePtr recv(const string &name, unsigned long long *timeout = NULL) {
		boost::unique_lock<boost::mutex> l(syncher);
		posix_time::ptime deadline;
		unsigned long long beginTime = 0; // Shut up compiler warning.
		if (timeout != NULL) {
			beginTime = SystemTime::getMonotonicUsec();
			deadline = posix_time::microsec_clock::local_time() +
				posix_time::microsec(*timeout);
		}

		Iterator it;
		while ((it = search(name)) == messages.end()) {
			if (timeout != NULL) {
				if (checkTimeout(l, timeout, beginTime, deadline)) {
					return MessagePtr();
				}
			} else {
				cond.wait(l);
			}
		}
		if (timeout != NULL) {
			substractTimePassed(timeout, beginTime);
		}

		MessagePtr result = *it;
		messages.erase(it);
		return result;
	}

	MessagePtr recvTE(const string &name, unsigned long long *timeout = NULL) {
		MessagePtr result = recv(name, timeout);
		if (result != NULL) {
			return result;
		} else {
			throw TimeoutException("Timeout receiving from message box");
		}
	}

	template<typename StringCollection>
	MessagePtr recvAny(const StringCollection &names, unsigned long long *timeout = NULL) {
		boost::unique_lock<boost::mutex> l(syncher);
		posix_time::ptime deadline;
		unsigned long long beginTime = 0; // Shut up compiler warning.
		if (timeout != NULL) {
			beginTime = SystemTime::getMonotonicUsec();
			deadline = posix_time::microsec_clock::local_time() +
				posix_time::microsec(*timeout);
		}

		Iterator it;
		while ((it = searchAny<StringCollection>(names)) == messages.end()) {
			if (timeout != NULL) {
				if (checkTimeout(l, timeout, beginTime, deadline)) {
					return MessagePtr();
				}
			} else {
				cond.wait(l);
			}
		}
		if (timeout != NULL) {
			substractTimePassed(timeout, beginTime);
		}

		MessagePtr result = *it;
		messages.erase(it);
		return result;
	}

	unsigned int size() const {
		boost::lock_guard<boost::mutex> l(syncher);
		return messages.size();
	}
};


inline void
_sendToMessageBox(const MessageBoxPtr &messageBox, const MessagePtr &message) {
	messageBox->send(message);
}


} // namespace Passenger

#endif /* _PASSENGER_MESSAGE_PASSING_H_ */
