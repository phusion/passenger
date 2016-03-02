/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2014 Tatsuhiro Tsujikawa
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
 * LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
 * OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#ifndef MEMCHUNK_H
#define MEMCHUNK_H

#include "nghttp2_config.h"

#include <limits.h>
#include <sys/uio.h>

#include <cassert>
#include <cstring>
#include <memory>
#include <array>
#include <algorithm>
#include <string>

#include "template.h"

namespace nghttp2 {

template <size_t N> struct Memchunk {
  Memchunk(std::unique_ptr<Memchunk> next_chunk)
      : pos(std::begin(buf)), last(pos), knext(std::move(next_chunk)),
        next(nullptr) {}
  size_t len() const { return last - pos; }
  size_t left() const { return std::end(buf) - last; }
  void reset() { pos = last = std::begin(buf); }
  std::array<uint8_t, N> buf;
  uint8_t *pos, *last;
  std::unique_ptr<Memchunk> knext;
  Memchunk *next;
  static const size_t size = N;
};

template <typename T> struct Pool {
  Pool() : pool(nullptr), freelist(nullptr), poolsize(0) {}
  T *get() {
    if (freelist) {
      auto m = freelist;
      freelist = freelist->next;
      m->next = nullptr;
      m->reset();
      return m;
    }

    pool = make_unique<T>(std::move(pool));
    poolsize += T::size;
    return pool.get();
  }
  void recycle(T *m) {
    m->next = freelist;
    freelist = m;
  }
  void clear() {
    freelist = nullptr;
    pool = nullptr;
    poolsize = 0;
  }
  using value_type = T;
  std::unique_ptr<T> pool;
  T *freelist;
  size_t poolsize;
};

template <typename Memchunk> struct Memchunks {
  Memchunks(Pool<Memchunk> *pool)
      : pool(pool), head(nullptr), tail(nullptr), len(0) {}
  Memchunks(const Memchunks &) = delete;
  Memchunks(Memchunks &&other) noexcept : pool(other.pool),
                                          head(other.head),
                                          tail(other.tail),
                                          len(other.len) {
    // keep other.pool
    other.head = other.tail = nullptr;
    other.len = 0;
  }
  Memchunks &operator=(const Memchunks &) = delete;
  Memchunks &operator=(Memchunks &&other) noexcept {
    if (this == &other) {
      return *this;
    }

    reset();

    pool = other.pool;
    head = other.head;
    tail = other.tail;
    len = other.len;

    other.head = other.tail = nullptr;
    other.len = 0;

    return *this;
  }
  ~Memchunks() {
    if (!pool) {
      return;
    }
    for (auto m = head; m;) {
      auto next = m->next;
      pool->recycle(m);
      m = next;
    }
  }
  size_t append(char c) {
    if (!tail) {
      head = tail = pool->get();
    } else if (tail->left() == 0) {
      tail->next = pool->get();
      tail = tail->next;
    }
    *tail->last++ = c;
    ++len;
    return 1;
  }
  size_t append(const void *src, size_t count) {
    if (count == 0) {
      return 0;
    }

    auto first = static_cast<const uint8_t *>(src);
    auto last = first + count;

    if (!tail) {
      head = tail = pool->get();
    }

    for (;;) {
      auto n = std::min(static_cast<size_t>(last - first), tail->left());
      tail->last = std::copy_n(first, n, tail->last);
      first += n;
      len += n;
      if (first == last) {
        break;
      }

      tail->next = pool->get();
      tail = tail->next;
    }

    return count;
  }
  template <size_t N> size_t append(const char(&s)[N]) {
    return append(s, N - 1);
  }
  size_t append(const std::string &s) { return append(s.c_str(), s.size()); }
  size_t append(const StringRef &s) { return append(s.c_str(), s.size()); }
  size_t remove(void *dest, size_t count) {
    if (!tail || count == 0) {
      return 0;
    }

    auto first = static_cast<uint8_t *>(dest);
    auto last = first + count;

    auto m = head;

    while (m) {
      auto next = m->next;
      auto n = std::min(static_cast<size_t>(last - first), m->len());

      assert(m->len());
      first = std::copy_n(m->pos, n, first);
      m->pos += n;
      len -= n;
      if (m->len() > 0) {
        break;
      }
      pool->recycle(m);
      m = next;
    }
    head = m;
    if (head == nullptr) {
      tail = nullptr;
    }

    return first - static_cast<uint8_t *>(dest);
  }
  size_t drain(size_t count) {
    auto ndata = count;
    auto m = head;
    while (m) {
      auto next = m->next;
      auto n = std::min(count, m->len());
      m->pos += n;
      count -= n;
      len -= n;
      if (m->len() > 0) {
        break;
      }

      pool->recycle(m);
      m = next;
    }
    head = m;
    if (head == nullptr) {
      tail = nullptr;
    }
    return ndata - count;
  }
  int riovec(struct iovec *iov, int iovcnt) const {
    if (!head) {
      return 0;
    }
    auto m = head;
    int i;
    for (i = 0; i < iovcnt && m; ++i, m = m->next) {
      iov[i].iov_base = m->pos;
      iov[i].iov_len = m->len();
    }
    return i;
  }
  size_t rleft() const { return len; }
  void reset() {
    for (auto m = head; m;) {
      auto next = m->next;
      pool->recycle(m);
      m = next;
    }
    len = 0;
    head = tail = nullptr;
  }

  Pool<Memchunk> *pool;
  Memchunk *head, *tail;
  size_t len;
};

// Wrapper around Memchunks to offer "peeking" functionality.
template <typename Memchunk> struct PeekMemchunks {
  PeekMemchunks(Pool<Memchunk> *pool)
      : memchunks(pool), cur(nullptr), cur_pos(nullptr), cur_last(nullptr),
        len(0), peeking(true) {}
  PeekMemchunks(const PeekMemchunks &) = delete;
  PeekMemchunks(PeekMemchunks &&other) noexcept
      : memchunks(std::move(other.memchunks)),
        cur(other.cur),
        cur_pos(other.cur_pos),
        cur_last(other.cur_last),
        len(other.len),
        peeking(other.peeking) {
    other.reset();
  }
  PeekMemchunks &operator=(const PeekMemchunks &) = delete;
  PeekMemchunks &operator=(PeekMemchunks &&other) noexcept {
    if (this == &other) {
      return *this;
    }

    memchunks = std::move(other.memchunks);
    cur = other.cur;
    cur_pos = other.cur_pos;
    cur_last = other.cur_last;
    len = other.len;
    peeking = other.peeking;

    other.reset();

    return *this;
  }
  size_t append(const void *src, size_t count) {
    count = memchunks.append(src, count);
    len += count;
    return count;
  }
  size_t remove(void *dest, size_t count) {
    if (!peeking) {
      count = memchunks.remove(dest, count);
      len -= count;
      return count;
    }

    if (count == 0 || len == 0) {
      return 0;
    }

    if (!cur) {
      cur = memchunks.head;
      cur_pos = cur->pos;
    }

    // cur_last could be updated in append
    cur_last = cur->last;

    if (cur_pos == cur_last) {
      assert(cur->next);
      cur = cur->next;
    }

    auto first = static_cast<uint8_t *>(dest);
    auto last = first + count;

    for (;;) {
      auto n = std::min(last - first, cur_last - cur_pos);

      first = std::copy_n(cur_pos, n, first);
      cur_pos += n;
      len -= n;

      if (first == last) {
        break;
      }
      assert(cur_pos == cur_last);
      if (!cur->next) {
        break;
      }
      cur = cur->next;
      cur_pos = cur->pos;
      cur_last = cur->last;
    }
    return first - static_cast<uint8_t *>(dest);
  }
  size_t rleft() const { return len; }
  size_t rleft_buffered() const { return memchunks.rleft(); }
  void disable_peek(bool drain) {
    if (!peeking) {
      return;
    }
    if (drain) {
      auto n = rleft_buffered() - rleft();
      memchunks.drain(n);
      assert(len == memchunks.rleft());
    } else {
      len = memchunks.rleft();
    }
    cur = nullptr;
    cur_pos = cur_last = nullptr;
    peeking = false;
  }
  void reset() {
    memchunks.reset();
    cur = nullptr;
    cur_pos = cur_last = nullptr;
    len = 0;
    peeking = true;
  }
  Memchunks<Memchunk> memchunks;
  // Pointer to the Memchunk currently we are reading/writing.
  Memchunk *cur;
  // Region inside cur, we have processed to cur_pos.
  uint8_t *cur_pos, *cur_last;
  // This is the length we have left unprocessed.  len <=
  // memchunk.rleft() must hold.
  size_t len;
  // true if peeking is enabled.  Initially it is true.
  bool peeking;
};

using Memchunk16K = Memchunk<16_k>;
using MemchunkPool = Pool<Memchunk16K>;
using DefaultMemchunks = Memchunks<Memchunk16K>;
using DefaultPeekMemchunks = PeekMemchunks<Memchunk16K>;

#define DEFAULT_WR_IOVCNT 16

#if defined(IOV_MAX) && IOV_MAX < DEFAULT_WR_IOVCNT
#define MAX_WR_IOVCNT IOV_MAX
#else // !defined(IOV_MAX) || IOV_MAX >= DEFAULT_WR_IOVCNT
#define MAX_WR_IOVCNT DEFAULT_WR_IOVCNT
#endif // !defined(IOV_MAX) || IOV_MAX >= DEFAULT_WR_IOVCNT

inline int limit_iovec(struct iovec *iov, int iovcnt, size_t max) {
  if (max == 0) {
    return 0;
  }
  for (int i = 0; i < iovcnt; ++i) {
    auto d = std::min(max, iov[i].iov_len);
    iov[i].iov_len = d;
    max -= d;
    if (max == 0) {
      return i + 1;
    }
  }
  return iovcnt;
}

} // namespace nghttp2

#endif // MEMCHUNK_H
