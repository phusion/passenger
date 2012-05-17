#ifndef _PASSENGER_PRIORITY_QUEUE_H_
#define _PASSENGER_PRIORITY_QUEUE_H_

#include "fib.h"

namespace Passenger {

template<typename T>
class PriorityQueue {
private:
	struct fibheap heap;
	
public:
	typedef struct fibheap_el * Handle;
	
	PriorityQueue() {
		fh_initheap(&heap);
		heap.fh_keys = 1;
	}
	
	~PriorityQueue() {
		fh_destroyheap(&heap);
	}
	
	Handle push(T *item, int priority) {
		return fh_insertkey(&heap, priority, item);
	}
	
	T *pop() {
		return (T *) fh_extractmin(&heap);
	}
	
	T *top() const {
		return (T *) fh_min(const_cast<struct fibheap *>(&heap));
	}
	
	void decrease(Handle handle, int priority) {
		fh_replacekeydata(&heap, handle, priority, handle->fhe_data);
	}
	
	void erase(Handle handle) {
		fh_delete(&heap, handle);
	}
	
	void clear() {
		fh_destroyheap(&heap);
		fh_initheap(&heap);
		heap.fh_keys = 1;
	}
};

} // namespace Passenger

#endif /* _PASSENGER_PRIORITY_QUEUE_H_ */
