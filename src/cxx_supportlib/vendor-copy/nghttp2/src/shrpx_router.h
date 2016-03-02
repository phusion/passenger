/*
 * nghttp2 - HTTP/2 C Library
 *
 * Copyright (c) 2015 Tatsuhiro Tsujikawa
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
#ifndef SHRPX_ROUTER_H
#define SHRPX_ROUTER_H

#include "shrpx.h"

#include <vector>
#include <memory>

namespace shrpx {

struct RNode {
  RNode();
  RNode(const char *s, size_t len, size_t index);
  RNode(RNode &&) = default;
  RNode(const RNode &) = delete;
  RNode &operator=(RNode &&) = default;
  RNode &operator=(const RNode &) = delete;

  // Next RNode, sorted by s[0].
  std::vector<std::unique_ptr<RNode>> next;
  // Stores pointer to the string this node represents.  Not
  // NULL-terminated.
  const char *s;
  // Length of |s|
  size_t len;
  // Index of pattern if match ends in this node.  Note that we don't
  // store duplicated pattern.
  ssize_t index;
};

class Router {
public:
  Router();
  // Adds route |pattern| of size |patlen| with its |index|.
  bool add_route(const char *pattern, size_t patlen, size_t index);
  // Returns the matched index of pattern.  -1 if there is no match.
  ssize_t match(const std::string &host, const char *path,
                size_t pathlen) const;

  void add_node(RNode *node, const char *pattern, size_t patlen, size_t index);

  void dump() const;

private:
  // The root node of Patricia tree.  This is special node and its s
  // field is nulptr, and len field is 0.
  RNode root_;
};

} // namespace shrpx

#endif // SHRPX_ROUTER_H
