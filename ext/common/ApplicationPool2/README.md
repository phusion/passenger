# Overview

ApplicationPool2 is a subsystem in Phusion Passenger that takes care of
dynamically calculating how many processes are needed, spawning application
processes, shutting down processes, restarting processes, forwarding requests to
the right process, etc. Pretty much all important application process management
is encapculated in this subsystem.

It does not handle the actual request/response I/O with the application
processes: that's left to the caller of the ApplicationPool2 subsystem.

Here's a quick rundown of the available classes:

 * Pool
   This is the core of the subsystem. It contains high-level process management
   logic but not the low-level details of spawning processes. The code is
   further divided into the following classes, each of which contain the core
   code managing its respective domain:
   * SuperGroup
     A logical collection of different applications. Can contain one or more
     Groups. In the current version of Phusion Passenger, a SuperGroup only
     contains exactly 1 Group.
   * Group
     Represents an application and can contains multiple processes, all
     belonging to the same application.
   * Process
     Represents an OS process; an instance of a certain application. A process
     may have multiple server sockets on which it listens. This is represented
     by the `Socket` class:
     * Socket

 * Spawner
   Encapsulates all low-level process spawning logic. Pool calls Spawner
   whenever it needs to spawn another application process.

   Spawner is an interface. There are multiple implementations that all
   spawn processes in a different way. These are:
   * DirectSpawner
   * SmartSpawner
   * DummySpawner

   The spawn method is user-configurable. To avoid convoluting the Pool code
   with spawner implementation selection logic, we have:
   * SpawnerFactory

 * Session
   A session represents a single interaction with an application process, e.g.
   a single request/response session.

 * Options
   A configuration object for the Pool::get() method.

The `Pool` class's `get` method is the main interface into the ApplicationPool2
subsystem. When an HTTP request comes in, call `Pool::get()` with the
appropriate arguments, and it will automatically spawn a process for you when
needed, open a session with that process and give you the session object.


## Threading notes

ApplicationPool2 depends on an event loop for handling timers and I/O. The I/O
that it handles is not the request/response I/O with application processes, but
things like forwarding the processes' stderr output to our stderr. In order not
to block the event loop with long-running operations, it uses a lot of
background threads. ApplicationPool2 is designed to be entirely thread-safe.
That said, if one's not careful, one may cause deadlocks, so read this section
carefully.

* Many Spawner methods are blocking because they wait for a subprocess to do
  something (initializing, shutting down, etc). The process may output I/O
  which is supposed to be handled by the main loop. If the event loop is blocked
  on waiting for the process, and the process is blocked on a write() to the I/O
  channel, then we have a deadlock. Therefore Spawner methods (including the
  Spawner destructor) must always be called outside the event loop thread, and
  the event loop must be available while the Spawner is doing its work. The only
  exceptions are Spawner methods which are explicitly documented as not
  depending on the event loop.

  Pool must only call Spawner methods from background threads. There's still a
  caveat though: Pool's destructor waits for all background threads to finish.
  Therefore one must not destroy Pool from the event loop. Instead, I recommend
  running the event loop in a separate thread, destroy Pool from the main
  thread, and stop the event loop after Pool is destroyed.

  Calling other Pool methods from the event loop is ok. Calling SpawnerFactory
  from the event loop is ok.

* Many classes contain libev watchers and unregisters them in their destructor.
  In order for this unregistration to succeed, one of the following conditions
  must hold:
  1. The destructor is called from the event loop.
  2. The destructor is not called from the event loop, but the event loop is
     still running.

  Therefore I recommend that you destroy all ApplicationPool2-related objects
  before stopping the event loop thread.
