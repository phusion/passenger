/*
 *  Phusion Passenger - http://www.modrails.com/
 *  Copyright (c) 2010 Phusion
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
#ifndef _PASSENGER_GROUP_ALLOCATOR_H_
#define _PASSENGER_GROUP_ALLOCATOR_H_

#include <cstddef>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <vector>

namespace Passenger {

using namespace std;

/**
 * A simple STL-compliant allocator that allocates objects in relatively large
 * memory chunks instead of allocating each object seperately. It is extremely
 * fast: allocating an object can be done in amortized constant time, usually
 * only involving incrementing a few pointers. However it does not support
 * deallocating individual objects: it only supports deallocating all objects
 * at once. It is also not thread-safe.
 *
 * GroupAllocator is ideal for use cases in which you need to quickly allocate
 * lots of small objects, use them, then release all of those objects.
 */
template<typename T, unsigned int maxStorages = 32>
class GroupAllocator {
private:
	struct Storage {
		char *memory;
		unsigned int used;
		unsigned int capacity;
		
		Storage(unsigned int capacity) {
			memory = new char[capacity * sizeof(T)];
			used = 0;
			this->capacity = capacity;
		}
		
		~Storage() {
			delete[] memory;
		}
		
		bool canAllocate(size_t n) const {
			return used + n <= capacity;
		}
	};
	
	Storage *storages[maxStorages];
	unsigned int storagesSize;
	/** Number of objects. */
	unsigned int size;
	
	void reset() {
		for (unsigned int i = 0; i < storagesSize; i++) {
			delete storages[i];
		}
		storagesSize = 0;
		size = 0;
	}
	
	Storage *getLastStorage() const {
		if (storagesSize == 0) {
			return NULL;
		} else {
			return storages[storagesSize - 1];
		}
	}
	
	Storage *addStorage(unsigned int leastCapacity) {
		if (storagesSize == maxStorages) {
			throw bad_alloc();
		}
		
		unsigned int newCapacity = 32;
		if (storagesSize > 0) {
			newCapacity = (unsigned int) round(storages[storagesSize - 1]->capacity * 1.5);
		}
		newCapacity = std::max(leastCapacity, newCapacity);
		
		storages[storagesSize] = new Storage(newCapacity);
		storagesSize++;
		return storages[storagesSize - 1];
	}
	
public:
	typedef T value_type;
	typedef T *pointer;
	typedef T &reference;
	typedef const T *const_pointer;
	typedef const T &const_reference;
	typedef size_t size_type;
	typedef ptrdiff_t difference_type;
	
	template<typename U>
	struct rebind {
		typedef GroupAllocator<U, maxStorages> other;
	};
	
	GroupAllocator() {
		storagesSize = 0;
		size = 0;
	}
	
	GroupAllocator(const GroupAllocator<T, maxStorages> &other) {
		storagesSize = 0;
		size = 0;
	}
	
	template<typename U>
	GroupAllocator(const GroupAllocator<U, maxStorages> &other) {
		storagesSize = 0;
		size = 0;
	}
	
	~GroupAllocator() {
		reset();
	}
	
	pointer allocate(size_type n, allocator<void>::const_pointer hint = 0) {
		if (n <= 0) {
			return 0;
		} else {
			Storage *storage = getLastStorage();
			if (storage == NULL || !storage->canAllocate(n)) {
				storage = addStorage(n);
			}
			
			void *p = storage->memory + storage->used * sizeof(T);
			pointer result = (pointer) p;
			storage->used += n;
			size += n;
			return result;
		}
	}
	
	void deallocate(pointer p, size_type n) {
		size -= n;
		if (size == 0) {
			reset();
		}
	}
	
	pointer address(reference r) const {
		return &r;
	}
	
	const_pointer address(const_reference r) const {
		return &r;
	}
	
	size_type max_size() const throw() {
		return size_t(-1) / sizeof(T);
	}
	
	void construct(pointer p, const_reference val) {
		new(p) T(val);
	}
	
	void destroy(pointer p) {
		p->~T();
	}
	
	bool operator==(const GroupAllocator<T, maxStorages> &other) {
		return true;
	}
	
	bool operator!=(const GroupAllocator<T, maxStorages> &other) {
		return !operator==(other);
	}
};

} // namespace Passenger

#endif /* _PASSENGER_GROUP_ALLOCATOR_H_ */
