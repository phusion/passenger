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
#ifndef _PASSENGER_DATA_STRUCTURES_STRING_KEY_TABLE_H_
#define _PASSENGER_DATA_STRUCTURES_STRING_KEY_TABLE_H_

#include <boost/move/move.hpp>
#include <boost/config.hpp>
#include <boost/cstdint.hpp>
// for std::swap()
#if __cplusplus >= 201103L
	#include <utility>
#else
	#include <algorithm>
#endif
#include <limits>
#include <cstring>
#include <cassert>
#include <cstddef>

#include <DataStructures/HashedStaticString.h>
#include <StaticString.h>

namespace Passenger {

using namespace std;


struct SKT_EnableMoveSupport { };
struct SKT_DisableMoveSupport { };


/**
 * An optimized hash table that accepts string keys, optimized for the following workload:
 *
 *  * Inserts happen in bulk, soon after hash table creation or clearing.
 *  * Once the bulk insertion phase is over, lookups are frequent, but modifications
 *    are not.
 *
 * The hash table uses open addressing and linear probing. It also stores key data
 * in a single contiguous internal storage area, outside the cells. This reduces calls
 * to malloc(), avoids a lot of malloc space overhead and improves cache locality.
 * Because the table owns the key data, there's no need to allocate keys and to keep
 * them alive outside the hash table.
 *
 * Keys may be at most 255 bytes long. The total sum of keys may not exceed
 * 2^(24-1) bytes =~ 6 MB. This allows us to use compact indices in the Cell
 * struct instead of pointers, significantly reducing memory usage on 64-bit platforms.
 *
 * The hash table automatically doubles in size when it becomes 75% full.
 * The hash table never shrinks in size, even after clear(), unless you explicitly call
 * compact().
 *
 * This implementation is based on https://github.com/preshing/CompareIntegerMaps.
 * See also http://preshing.com/20130107/this-hash-table-is-faster-than-a-judy-array
 */
template<typename T, typename MoveSupport = SKT_DisableMoveSupport>
class StringKeyTable {
public:
	#define SKT_FIRST_CELL(hash) (m_cells + ((hash) & (m_arraySize - 1)))
	#define SKT_CIRCULAR_NEXT(c) ((c) + 1 != m_cells + m_arraySize ? (c) + 1 : m_cells)
	#define SKT_CIRCULAR_OFFSET(a, b) ((b) >= (a) ? (b) - (a) : m_arraySize + (b) - (a))

	static const unsigned int DEFAULT_SIZE = 16;
	// Fits in exactly 4 cache lines. The -16 is to account for malloc overhead.
	static const unsigned int DEFAULT_STORAGE_SIZE = 4 * 64 - 16;
	static const unsigned int MAX_KEY_LENGTH = 255;
	static const unsigned int MAX_ITEMS = 65533; // 2^16-3
	static const boost::uint32_t EMPTY_CELL_KEY_OFFSET = 16777215;
	static const unsigned short NON_EMPTY_INDEX_NONE = 65535;
	static const unsigned short NON_EMPTY_INDEX_UNKNOWN = 65534;

	struct Cell {
		boost::uint32_t keyOffset: 24;
		boost::uint8_t  keyLength;
		boost::uint32_t hash;
		T value;

		Cell()
			: keyOffset(EMPTY_CELL_KEY_OFFSET)
			{ }

		void move(Cell &target) {
			target.keyOffset = keyOffset;
			target.keyLength = keyLength;
			target.hash = hash;
			target.value = boost::move(value);
			keyOffset = 0;
			keyLength = 0;
			hash = 0;
		}
	};

private:
	Cell *m_cells;
	unsigned short m_arraySize;
	unsigned short m_population;
	// Index of a random non-empty cell
	unsigned short nonEmptyIndex;
	char *m_storage;
	unsigned int m_storageSize;
	unsigned int m_storageUsed;

	bool shouldRepopulateOnInsert() const {
		return (m_population + 1) * 4 >= m_arraySize * 3;
	}

	const char *lookupCellKey(const Cell * const cell) const {
		if (!cellIsEmpty(cell)) {
			return &m_storage[cell->keyOffset];
		} else {
			return NULL;
		}
	}

	OXT_FORCE_INLINE
	bool cellIsEmpty(const Cell * const cell) const {
		return cell->keyOffset == EMPTY_CELL_KEY_OFFSET;
	}

	static bool
	compareKeys(const char *key1, unsigned int key1Length, const HashedStaticString &key2) {
		return StaticString(key1, key1Length) == key2;
	}

	static boost::uint32_t upper_power_of_two(boost::uint32_t v) {
		v--;
		v |= v >> 1;
		v |= v >> 2;
		v |= v >> 4;
		v |= v >> 8;
		v |= v >> 16;
		v++;
		return v;
	}

	boost::uint32_t appendToStorage(const StaticString &key) {
		const string::size_type keySize = key.size();

		if (m_storageUsed + key.size() + 1 > m_storageSize) {
			// Resize storage area when of insufficient size.
			unsigned int newStorageSize = (m_storageSize + key.size() + 1) * 1.5;
			char *newStorage = (char *) realloc(m_storage, newStorageSize);
			if (OXT_UNLIKELY(newStorage == NULL)) {
				throw std::bad_alloc();
			} else {
				m_storageSize = newStorageSize;
				m_storage = newStorage;
			}
		}

		// Append key to the end of the storage area, set NULL terminator.
		boost::uint32_t old_storageUsed = m_storageUsed;
		memcpy(m_storage + m_storageUsed, key.data(), keySize);
		m_storage[m_storageUsed + key.size()] = '\0';
		m_storageUsed += key.size() + 1;

		return old_storageUsed;
	}

	void repopulate(unsigned int desiredSize) {
		assert((desiredSize & (desiredSize - 1)) == 0);   // Must be a power of 2
		assert(m_population * 4  <= desiredSize * 3);

		// Get start/end pointers of old array
		Cell *oldCells = m_cells;
		Cell *end = m_cells + m_arraySize;

		// Allocate new array
		m_arraySize = desiredSize;
		m_cells = new Cell[m_arraySize];

		if (oldCells == NULL) {
			return;
		}

		// Iterate through old array
		for (Cell *oldCell = oldCells; oldCell != end; oldCell++) {
			if (!cellIsEmpty(oldCell)) {
				// Insert this element into new array
				Cell *newCell = SKT_FIRST_CELL(oldCell->hash);
				while (true) {
					if (cellIsEmpty(newCell)) {
						// Insert here
						copyOrMoveCell(*oldCell, *newCell, MoveSupport());
						break;
					} else {
						newCell = SKT_CIRCULAR_NEXT(newCell);
					}
				}
			}
		}

		// Delete old array
		delete[] oldCells;
	}

	void copyOrMoveCell(Cell &source, Cell &target, const SKT_EnableMoveSupport &t) {
		source.move(target);
	}

	void copyOrMoveCell(Cell &source, Cell &target, const SKT_DisableMoveSupport &t) {
		target = source;
	}

	void copyOrMoveValue(T &source, T &target, const SKT_EnableMoveSupport &t) {
		target = boost::move(source);
	}

	void copyOrMoveValue(const T &source, T &target, const SKT_DisableMoveSupport &t) {
		target = source;
	}

	void copyTableFrom(const StringKeyTable &other) {
		m_arraySize  = other.m_arraySize;
		m_population = other.m_population;
		m_cells      = new Cell[other.m_arraySize];
		for (unsigned int i = 0; i < m_arraySize; i++) {
			m_cells[i] = other.m_cells[i];
		}

		m_storageSize = other.m_storageSize;
		m_storageUsed = other.m_storageUsed;
		if (other.m_storage != NULL) {
			m_storage = (char *) malloc(m_storageSize);
			memcpy(m_storage, other.m_storage, other.m_storageUsed);
		} else {
			m_storage = NULL;
		}
	}

	template<typename ValueType, typename LocalMoveSupport>
	Cell *realInsert(const HashedStaticString &key, ValueType val, bool overwrite) {
		assert(!key.empty());
		assert(key.size() <= MAX_KEY_LENGTH);
		assert(m_population < MAX_ITEMS);

		if (OXT_UNLIKELY(m_cells == NULL)) {
			init(DEFAULT_SIZE, DEFAULT_STORAGE_SIZE);
		}

		while (true) {
			Cell *cell = SKT_FIRST_CELL(key.hash());
			while (true) {
				const char *cellKey = lookupCellKey(cell);
				if (cellKey == NULL) {
					// Cell is empty. Insert here.
					if (shouldRepopulateOnInsert()) {
						// Time to resize
						repopulate(m_arraySize * 2);
						break;
					}
					m_population++;
					cell->keyOffset = appendToStorage(key);
					cell->keyLength = key.size();
					cell->hash = key.hash();
					copyOrMoveValue(val, cell->value, LocalMoveSupport());
					nonEmptyIndex = cell - &m_cells[0];
					return cell;
				} else if (compareKeys(cellKey, cell->keyLength, key)) {
					// Cell matches.
					if (overwrite) {
						copyOrMoveValue(val, cell->value, LocalMoveSupport());
					}
					return cell;
				} else {
					cell = SKT_CIRCULAR_NEXT(cell);
				}
			}
		}

		return NULL; // Never reached
	}

public:
	StringKeyTable(unsigned int initialSize = DEFAULT_SIZE, unsigned int initialStorageSize = DEFAULT_STORAGE_SIZE) {
		init(initialSize, initialStorageSize);
	}

	StringKeyTable(const StringKeyTable &other) {
		copyTableFrom(other);
	}

	~StringKeyTable() {
		delete[] m_cells;
		free(m_storage);
	}

	StringKeyTable &operator=(const StringKeyTable &other) {
		if (this != &other) {
			delete[] m_cells;
			free(m_storage);
			copyTableFrom(other);
		}
		return *this;
	}

	void init(unsigned int initialSize, unsigned int initialStorageSize) {
		assert((initialSize & (initialSize - 1)) == 0);   // Must be a power of 2
		assert((initialSize == 0) == (initialStorageSize == 0));

		nonEmptyIndex = NON_EMPTY_INDEX_NONE;

		m_arraySize = initialSize;
		if (initialSize == 0) {
			m_cells = NULL;
		} else {
			m_cells = new Cell[m_arraySize];
		}
		m_population = 0;

		m_storageSize = initialStorageSize;
		if (initialStorageSize == 0) {
			m_storage = NULL;
		} else {
			m_storage = (char *) malloc(initialStorageSize);
		}
		m_storageUsed = 0;
	}

	Cell *lookupCell(const HashedStaticString &key) {
		assert(!key.empty());

		if (m_cells == NULL) {
			return NULL;
		}

		Cell *cell = SKT_FIRST_CELL(key.hash());
		while (true) {
			const char *cellKey = lookupCellKey(cell);
			if (cellKey == NULL) {
				// Empty cell found.
				return NULL;
			} else if (compareKeys(cellKey, cell->keyLength, key)) {
				// Non-empty cell found.
				return cell;
			} else {
				// Keep probing.
				cell = SKT_CIRCULAR_NEXT(cell);
			}
		}
	}

	const Cell *lookupCell(const HashedStaticString &key) const {
		assert(!key.empty());

		if (m_cells == NULL) {
			return NULL;
		}

		const Cell *cell = SKT_FIRST_CELL(key.hash());
		while (true) {
			const char *cellKey = lookupCellKey(cell);
			if (cellKey == NULL) {
				// Empty cell found.
				return NULL;
			} else if (compareKeys(cellKey, cell->keyLength, key)) {
				// Non-empty cell found.
				return cell;
			} else {
				// Keep probing.
				cell = SKT_CIRCULAR_NEXT(cell);
			}
		}
	}

	bool contains(const HashedStaticString &key) const {
		return (lookupCell(key) != NULL);
	}

	bool lookup(const HashedStaticString &key, const T **result) const {
		const Cell * const cell = lookupCell(key);
		if (cell != NULL) {
			*result = &cell->value;
			return true;
		} else {
			*result = NULL;
			return false;
		}
	}

	OXT_FORCE_INLINE
	bool lookup(const HashedStaticString &key, T **result) {
		return static_cast<const StringKeyTable<T, MoveSupport> *>(this)->lookup(key,
			const_cast<const T **>(result));
	}

	const T lookupCopy(const HashedStaticString &key) const {
		const T *result;
		if (lookup(key, &result)) {
			return *result;
		} else {
			return T();
		}
	}

	bool lookupRandom(HashedStaticString *key, T **result) {
		if (nonEmptyIndex < MAX_ITEMS) {
			assert(m_population > 0);
			Cell *cell = &m_cells[nonEmptyIndex];
			if (key != NULL) {
				const char *cellKey = lookupCellKey(cell);
				*key = HashedStaticString(cellKey, cell->keyLength, cell->hash);
			}
			*result = &cell->value;
			return true;
		} else if (nonEmptyIndex == NON_EMPTY_INDEX_UNKNOWN) {
			assert(m_population > 0);
			Iterator it(*this);
			nonEmptyIndex = *it - &m_cells[0];
			if (key != NULL) {
				*key = it.getKey();
			}
			*result = &it.getValue();
			return true;
		} else {
			assert(nonEmptyIndex == NON_EMPTY_INDEX_NONE);
			assert(m_population == 0);
			*result = NULL;
			return false;
		}
	}

	Cell *insert(const HashedStaticString &key, const T &val, bool overwrite = true) {
		return realInsert<const T &, SKT_DisableMoveSupport>(key, val, overwrite);
	}

	Cell *insertByMoving(const HashedStaticString &key, BOOST_RV_REF(T) val, bool overwrite = true) {
		return realInsert<BOOST_RV_REF(T), SKT_EnableMoveSupport>(key, boost::move(val), overwrite);
	}

	void erase(Cell *cell) {
		assert(cell >= m_cells && cell - m_cells < m_arraySize);
		assert(!cellIsEmpty(cell));

		if (OXT_UNLIKELY(m_cells == NULL)) {
			return;
		}

		// Remove this cell by shuffling neighboring cells so there are no gaps in anyone's probe chain
		Cell *neighbor = SKT_CIRCULAR_NEXT(cell);
		while (true) {
			if (cellIsEmpty(neighbor)) {
				// There's nobody to swap with. Go ahead and clear this cell, then return.
				// Note that this doesn't erase the key from storage.
				cell->keyOffset = EMPTY_CELL_KEY_OFFSET;
				cell->value = T();
				m_population--;
				if (m_population == 0) {
					nonEmptyIndex = NON_EMPTY_INDEX_NONE;
				} else if (&m_cells[nonEmptyIndex] == cell) {
					nonEmptyIndex = NON_EMPTY_INDEX_UNKNOWN;
				}
				return;
			}

			Cell *ideal = SKT_FIRST_CELL(neighbor->hash);
			if (SKT_CIRCULAR_OFFSET(ideal, cell) < SKT_CIRCULAR_OFFSET(ideal, neighbor)) {
				// Swap with neighbor, then make neighbor the new cell to remove.
				*cell = *neighbor;
				cell = neighbor;
			}
			neighbor = SKT_CIRCULAR_NEXT(neighbor);
		}
	}

	bool erase(const HashedStaticString &key) {
		Cell *cell = lookupCell(key);
		if (cell != NULL) {
			erase(cell);
			return true;
		} else {
			return false;
		}
	}

	/** Does not resize the array. */
	void clear() {
		if (OXT_UNLIKELY(m_cells == NULL)) {
			return;
		}

		for (unsigned int i = 0; i < m_arraySize; i++) {
			m_cells[i].keyOffset = EMPTY_CELL_KEY_OFFSET;
			m_cells[i].value = T();
		}
		m_population = 0;
		m_storageUsed = 0;
		nonEmptyIndex = NON_EMPTY_INDEX_NONE;
	}

	void freeMemory() {
		delete[] m_cells;
		m_cells = NULL;
		m_arraySize  = 0;
		m_population = 0;

		free(m_storage);
		m_storage = NULL;
		m_storageUsed = 0;
		m_storageSize = 0;

		nonEmptyIndex = NON_EMPTY_INDEX_NONE;
	}

	void compact() {
		repopulate(upper_power_of_two((m_population * 4 + 3) / 3));
	}

	unsigned int size() const {
		return m_population;
	}

	unsigned int arraySize() const {
		return m_arraySize;
	}

	bool empty() const {
		return m_population == 0;
	}

	void swap(StringKeyTable<T, MoveSupport> &other) BOOST_NOEXCEPT_OR_NOTHROW {
		std::swap(m_cells, other.m_cells);
		std::swap(m_arraySize, other.m_arraySize);
		std::swap(m_population, other.m_population);
		std::swap(nonEmptyIndex, other.nonEmptyIndex);
		std::swap(m_storage, other.m_storage);
		std::swap(m_storageSize, other.m_storageSize);
		std::swap(m_storageUsed, other.m_storageUsed);
	}


	friend class Iterator;
	class Iterator {
	private:
		StringKeyTable *m_table;
		Cell *m_cur;

	public:
		Iterator(StringKeyTable &table)
			: m_table(&table)
		{
			if (m_table->m_cells != NULL) {
				m_cur = &m_table->m_cells[0];
				if (m_table->cellIsEmpty(m_cur)) {
					next();
				}
			} else {
				m_cur = NULL;
			}
		}

		Cell *next() {
			if (m_cur == NULL) {
				// Already finished.
				return NULL;
			}

			Cell *end = m_table->m_cells + m_table->m_arraySize;
			while (++m_cur != end) {
				if (!m_table->cellIsEmpty(m_cur)) {
					return m_cur;
				}
			}

			// Finished
			return m_cur = NULL;
		}

		inline Cell *operator*() const {
			return m_cur;
		}

		inline Cell *operator->() const {
			return m_cur;
		}

		HashedStaticString getKey() const {
			const char *theKey = m_table->lookupCellKey(m_cur);
			return HashedStaticString(theKey, m_cur->keyLength, m_cur->hash);
		}

		T &getValue() const {
			return m_cur->value;
		}
	};

	friend class ConstIterator;
	class ConstIterator {
	private:
		const StringKeyTable *m_table;
		const Cell *m_cur;

	public:
		ConstIterator(const StringKeyTable &table)
			: m_table(&table)
		{
			if (m_table->m_cells != NULL) {
				m_cur = &m_table->m_cells[0];
				if (m_table->cellIsEmpty(m_cur)) {
					next();
				}
			} else {
				m_cur = NULL;
			}
		}

		const Cell *next() {
			if (m_cur == NULL) {
				// Already finished.
				return NULL;
			}

			const Cell *end = m_table->m_cells + m_table->m_arraySize;
			while (++m_cur != end) {
				if (!m_table->cellIsEmpty(m_cur)) {
					return m_cur;
				}
			}

			// Finished
			return m_cur = NULL;
		}

		inline const Cell *operator*() const {
			return m_cur;
		}

		inline const Cell *operator->() const {
			return m_cur;
		}

		HashedStaticString getKey() const {
			const char *theKey = m_table->lookupCellKey(m_cur);
			return HashedStaticString(theKey, m_cur->keyLength, m_cur->hash);
		}

		const T &getValue() const {
			return m_cur->value;
		}
	};
};

} // namespace Passenger

#endif /* _PASSENGER_DATA_STRUCTURES_STRING_KEY_TABLE_H_ */
