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
