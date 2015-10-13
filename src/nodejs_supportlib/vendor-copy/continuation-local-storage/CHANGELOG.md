### v3.1.0 (2014-07-28):

* Updated to use `async-listener@0.4.7` to pick up bug fixes.

### v3.0.0 (2013-12-14):

* Removed the notion of a "default" or "global" context per namespace. It only
  existed to create a simpler interface for developing and testing the module,
  and created the potential for nasty information disclosure bugs (see [issue
  #14](https://github.com/othiym23/node-continuation-local-storage/issues/14)
  for details). This is potentially a breaking change, if you're depending on
  the global context, so semver says we have to bump the major version.
* Added this changelog.

### v2.6.2 (2013-12-07):

* `async-listener` and `emitter-listener` dependency refresh.

### v2.6.1 (2013-11-29):

* `emitter-listener` has been extracted from `shimmer` into a standalone
  module for `namespace.bindEmitter()`.

### v2.6.0 (2013-11-27):

* When an error is thrown in a CLS-bound continuation chain, attach the active
  context for the namespace to which the chain is bound. This is necessary
  because CLS and asyncListeners actually do too good a job of cleaning up
  after errors, and so they don't escape the continuation chain. New Relic
  needs the context so it can get the transaction active when the error
  happened for error tracing.

### v2.5.2 (2013-10-30):

* `async-listener` dependency refresh for better support of node 0.8.0 - 0.8.3.

### v2.5.1 (2013-10-27):

* `async-listener` dependency refresh.

### v2.5.0 (2013-10-27):

* Relax the requirement that CLS contexts be pushed and popped from a stack,
  instead treating them as a set.  This allows context interleaving (i.e.
  using the lower-level `namespace.enter()` and `namespace.exit()` API without
  any strict ordering dependencies).  Everything works, but this still makes me
  a little uneasy.
* EEs can now be bound to multiple namespaces, although this is likely to be
  slow.

### v2.4.4 (2013-10-27):

* Even if you use an EE bound to a namespace outside a continuation chain, it
  shouldn't explode.

### v2.4.3 (2013-10-16):

* `async-listener` dependency refresh.

### v2.4.2 (2013-10-13):

* More tweaks for `async-listener` error handlers (just a dependency refresh).

### v2.4.1 (2013-10-12):

* `async-listener` error listeners have gotten lots of tweaks. Update to newest
  API.
* Only exit namespace context on error if a continuation chain is active.

### v2.4.0 (2013-10-11):

* `async-listener` now supports error listeners. Update to newest API.
* Namespace context should be exited on asynchronous errors.

### v2.3.4 (2013-10-03):

* When EEs are in the middle of emitting, make sure that calls to
  `emitter.removeListener` are testing against non-monkeypatched versions of
  the event handlers (necessary so certain Connect middleware functions, such
  as `connect.limit`, run correctly).

### v2.3.3 (2013-10-02):

* Ensure handler rebinding gets called even in case of errors.
* Be consistent about making sure contexts are kept in a sane state when errors
  are thrown in EEs.

### v2.3.2 (2013-10-02):

* Guard `on` / `addListener` remonkeypatching in `namespace.bindEmitter()` so
  that `shimmer` is only called to rebind if the monkeypatched versions have
  actually been replaced.
* Don't try to call emit if there are no listeners on a bound EE.
* Don't use `setImmediate` in tests, because it's not available in Node 0.8.x.

### v2.3.1 (2013-10-01):

* Update to newest version of `async-listener`.
* Fix typo.

### v2.3.0 (2013-09-30):

* EventEmitters can now be bound to CLS namespaces. Because EEs act as coupling
  points between asynchronous domains, it's necessary for the EE binding to
  capture the CLS context both when the listener is added, and when a matching
  handler is firing because of a matching event being emitted.

### v2.2.1 (2013-09-30):

* More tweaks to conform with `asyncListener` API changes.
* Many more test cases to ensure `asyncListener` stuff is working with Node
  0.8.x.

### v2.2.0 (2013-09-26):

* Square up with latest `async-listener` / node PR #6011 changes.

### v2.1.2 (2013-09-09):

* Document `namespace.createContext()`.
* Fix issue where a value was *always* being returned from `namespace.run()`,
  even on error.

### v2.1.1 (2013-09-03):

* Clean up minor typo in docs.

### v2.1.0 (2013-09-03):

* Incorporate documentation from failed CLS PR.
* `namespace.bind()` now also always exits the domain, even on error.
* Namespaces can be destroyed.
* `cls.reset()` allows tests to nuke all existing namespaces (use with care
  obviously).

### v2.0.0 (2013-09-01):

* Use `async-listener` polyfill instead of `cls-glue`.
* Incorporate tests from `cls-glue`.

### v1.1.1 (2013-09-01):

* Namespace exits context even on error.

### v1.1.0 (2013-07-30):

* Split createContext so it's part of the namespace API.
* Tweak error message to be more informative.

### v1.0.1 (2013-07-25):

* Correct Tim's email address.

### v1.0.0 (2013-07-25):

* Each application of CLS is allocated its own "namespace", which bind data to
  continuation chains, either using `.run()` or `.bind()` to create a new
  nested context. These nested contexts are prototype chains that point back to
  a "default" / "global" context, with the default context for each namespace
  being a prototype-free "data bag" created with `Object.create(null)`.

### v0.1.1 (2013-05-03):

* Document progress thus far.

### v0.1.0 (2013-05-03):

* First attempt: basic API, docs, and tests.
