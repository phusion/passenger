# About SpawningKit

SpawningKit is a subsystem that handles the spawning of web application processes in a reliable manner.

Spawning an application process is complex, involving many steps and with many failure scenarios. SpawningKit handles all this complexity while providing a simple interface that guarantees reliability.

Here is how SpawningKit is used. The caller supplies various parameters such as where the application is located, what language it's written in, what environment variables to apply, etc. SpawningKit then spawns the application process, checks whether the application spawned properly or whether it encountered an error, and then either returns an object that describes the resulting process or throws an exception that describes the failure.

Reliability and visibility are core features of SpawningKit. When SpawningKit returns, you know for sure whether the process started correctly or not. If the application did not start correctly, then the resulting exception describes the failure in a detailed enough manner that allows users to pinpoint the source of the problem. SpawningKit also enforces timeouts everywhere so that stuck processes are handled as well.

**Table of contents**:

 * Important concepts and features
   - Generic vs SpawningKit-enabled applications
   - Wrappers
   - Preloaders
   - The start command
   - Summary with examples
 * API and implementation highlights
   - Context
   - Spawners (high-level API)
   - HandshakePrepare and HandshakePerform (low-level API)
   - Configuration object
   - Exception object
   - Journey
   - ErrorRenderer
 * Overview of the spawning journey
   - When spawning a process without a preloader
   - When starting a preloader
   - When spawning a process through a preloader
   - The Journey class
   - The preparation and the HandshakePrepare class
   - The handshake and the HandshakePerform class
   - The SpawnEnvSetupper
 * The work directory
   - Structure
 * Application response properties
 * The preloader protocol
 * Subprocess journey logging
 * Error reporting
   - Information sources
   - How an error report is presented
   - How supplied information affects error report generation
 * Mechanism for waiting until the application is up

---

## Important concepts and features

### Generic vs SpawningKit-enabled applications

    All applications
     |
     +-- Generic applications
     |   (without explicit SpawningKit support; `genericApp = true`)
     |
     +-- SpawningKit-enabled applications
         (with explicit SpawningKit support; `genericApp = false`)
           |
           +-- Applications with SpawningKit support automatically injected
           |   through a "wrapper"; no manual modifications
           |   (`startsUsingWrapper = true`)
           |
           +-- Applications manually modified with SpawningKit support
               (`startsUsingWrapper = false`)

SpawningKit can be used to spawn any web application, both those with and without explicit SpawningKit support.

> A generic application corresponds to setting the SpawningKit config `genericApp = true`.

When SpawningKit is used to spawn a generic application (without explicit SpawningKit support), the only requirement is that the application can be instructed to start and to listen on a specific TCP port on localhost. The user needs to specify a command string that tells SpawningKit how that is to be done. SpawningKit then looks for a free port that the application may use and executes the application using the supplied command string, telling it to listen on that specific port. (This approach is inspired by Heroku's Procfile system.) SpawningKit waits until the application is up by pinging the port. If the application fails (e.g. by terminating early or by not responding to pings in time) then SpawningKit will abort, reporting the application's stdout and stderr output.

> A SpawningKit-enabled application corresponds to setting the SpawningKit config `genericApp = false`.

Applications can also be modified with explicit SpawningKit support. Such applications can improve performance by telling SpawningKit that it wishes to listen on a Unix domain socket instead of a TCP socket; and they can provide more feedback about any spawning failures, such as with HTML-formatted error messages or by providing more information about where internally in the application or web framework the failure occurred.

### Wrappers

As we said, apps with explicit SpawningKit support is preferred (nicer experience, better performance). There are two ways to add SpawningKit support to an app:

 1. By manually modifying the application's code to add SpawningKit support.
 2. By automatically injecting SpawningKit support into the app, without any manual code modifications.

Option 2 is the most desirable, and is available to apps written in interpreted languages. This works by executing the application through a *wrapper* instead of directly. The wrapper, which is typically written in the same language as the app, loads the application and injects SpawningKit support.

Passenger comes with a few wrappers for specific languages, but SpawningKit itself is more generic and requires the caller to specify which wrapper to use (if at all).

For example, Ruby applications are typically spawned through the Passenger-supplied Ruby wrapper. The Ruby wrapper activates the Gemfile, loads the application, sets up a lightweight server that listens on a Unix domain socket, and reports this socket address back to SpawningKit.

### Preloaders

Applications written in certain languages are able to save memory and to improve application startup time by using a technique called pre-forking. This works by starting an application process, and instead of using that process to handle requests, we use that process to fork (but not `exec()`) additional child processes that in turn are actually used for processing requests.

In SpawningKit terminology, we call the former a "preloader". Processes that actually handle requests (and these processes may either be forked from a preloader or be spawned directly without a preloader) usually do not have a specific name. But for the sake of clarity, let's call the latter -- within the context of this document only -- "worker processes".

                                             Requests
                                                |
                                                |
                                               \ /
                                                .

    +-----------+                      +------------------+
    | Preloader |  --- forks many ---> | Worker processes |
    +-----------+                      +------------------+

Memory is saved because the preloader and its worker processes are able to share all memory that was already present in the preloader during forking, and that has not been modified in the worker processes. This concept is called Copy-on-Write (CoW) and works through the virtual memory system of modern operating systems.

For example, in Ruby applications a significant amount of memory is taken up by the bytecode representation of dependent libraries (e.g. the Rails framework). Loading all the dependent libraries typically takes time in the order of many seconds. By using a preloader to fork worker processes (instead of starting the worker processes without a preloader), all worker processes can share the memory taken up by the dependent libraries, as well as the application code itself and possibly any resources that the preloder loaded (e.g. a geo-IP database loaded from a file). Forking a worker process from a preloader is also extremely fast, in the order of milliseconds -- much faster than starting a worker processes without a preloader.

SpawningKit provides facilities to use this preforking technique. Obviously, this technique can only be used if the target programming language actually supports forking. This is the case with e.g. C, Ruby (using MRI) and Python (using CPython), but not with e.g. Node.js, Ruby (using JRuby), Go and anything running on the JVM.

Using the preforking technique through SpawningKit requires either application code modifications, or the existance of a wrapper that supports this technique.

### The start command

Regardless of whether SpawningKit is used to spawn an application directly with or without explicit SpawningKit support, and regardless of whether a wrapper is used and whether the application/wrapper can function as a preloader, SpawningKit asks the caller to supply a "start command" that tells it how to execute the wrapper or the application. SpawningKit then uses the handshaking procedure (see: "Overview of the spawning journey") to communicate with the wrapper/application whether it should start in preloader mode or not.


## API and implementation highlights

### Context

Before using SpawningKit, one must create a Context object. A Context contains global state, global configuration and references dependencies that SpawningKit needs.

Create a Context object, set some initial configuration, inject some dependencies, then call `finalize()`.

~~~c++
SpawningKit::Context::Schema schema;
SpawningKit::Context context(schema);

context.resourceLocator = ...;
context.integrationMode = "standalone";
context.finalize();
~~~

### Spawners (high-level API)

Use Spawners to spawn application processes. There are two main types of Spawners:

 * DirectSpawner, which corresponds to `PassengerSpawnMethod direct`.
 * SmartSpawner, which corresponds to `PassengerSpawnMethod smart`.

See also [the background documentation on spawn methods](https://www.phusionpassenger.com/library/indepth/ruby/spawn_methods/) and the "Preloaders" section in this README.

You can create Spawners directly, but it's recommended to use a Factory instead. A Factory accepts an ApplicationPool::Options object, and depending on `options.spawnMethod`, creates either a DirectSpawner or a SmartSpawner.

Once you have obtained a Spawner object, call `spawn(options)` which spawns an application process. It returns a `Result` object.

~~~c++
ApplicationPool::options;

options.appRoot = "/foo/bar/public";
options.appType = "rack";
options.spawnMethod = "smart";
...

SpawningKit::Factory factory(&context);
SpawningKit::SpawnerPtr spawner = factory->create(options);

SpawningKit::Result result = spawner->spawn(options);
P_WARN("Application process spawned, PID is " << result.pid);
~~~

There is also a DummySpawner class, which is only used during unit tests.

### HandshakePrepare and HandshakePerform (low-level API)

Inside SmartSpawner and DirectSpawner, HandshakePrepare and HandshakePerform are used to perform a lot of the heavy lifting. See "Overview of the spawning journey" -- HandshakePrepare and HandshakePerform are responsible for most of the stuff described there.

In fact, DirectSpawner is just a thin wrapper around HandshakePrepare and HandshakePerform. It first runs HandshakePrepare, then forks a subprocess that executes SpawnEnvSetupper, and then (in the parent process) runs HandshakePerform.

SmartSpawner is a bit bigger because it needs to implement the whole preloading mechanism (see section "Preloaders"), but it still uses HandshakePrepare and HandshakePerform to spawn the preloader, and to negotiate with the subprocess created by the preloader.

### Configuration object

HandshakePrepare and HandshakePerform do not accept an ApplicationPool::Options object, but a SpawningKit::Config object. It contains the configuration that HandshakePrepare/Perform need to perform a single spawn. SmartSpawner and DirectSpawner internally convert an ApplicationPool::Options into a SpawningKit::Config.

You can see this at work in `DirectSpawner::setConfigFromAppPoolOptions()` and `SmartSpawner::setConfigFromAppPoolOptions()`.

### Exception object

If HandshakePrepare, HandshakePerform, or `Spawner::spawn()` fails, then they will throw a SpawningKit::SpawnException object. Learn more about what this exception represents in section "Error reporting".

### Journey

The aforementioned exception object contains a SpawningKit::Journey object which describes how the spawning journey looked like and where in the journey something failed. It can be obtained through `exception.getJourney()`. Learn more about journeys in section "Overview of the spawning journey".

### Error renderer

The ErrorRenderer class helps you render an error page, both one with and one without details. It uses the assets in `resources/templates/error_page`.

## Overview of the spawning journey

Spawning a process can take one of three routes:

 * If we are spawning a worker process, then it is either (1) spawned through a preloader, or (2) it isn't.
 * We may also (3) spawn the application as a preloader instead of as a worker.

We refer to the walking of this route (performing all the steps involved in a route) a "journey". Below follows an overview of the routes.

In the following descriptions, "(In SpawningKit)" refers to the process that runs SpawningKit, which is typically the Passenger Core.

### When spawning a process without a preloader

The journey looks like this when no preloader is used:

       (In SpawningKit)                  (In subprocess)

        Preparation
          |
        Fork subprocess   --------------> Before first exec
          |                                  |
        Handshake                         Execute SpawnEnvSetupper to setup spawn env (--before)
          |                                  |
        Finish                            Load OS shell (if option enabled)
                                             |
                                          Execute SpawnEnvSetupper (--after)
                                             |
                                          Execute wrapper (if applicable)
                                             |
                                          Execute/load app
                                             |
                                          Start listening
                                             |
                                          Finish

### When starting a preloader

The journey looks like this when starting a preloader:

       (In SpawningKit)                  (In subprocess)
    
        Preparation
           |
        Fork preloader   --------------> Before first exec
           |                                |
        Handshake                        Execute SpawnEnvSetupper to setup spawn env (--before)
           |                                |
        Finish                           Load OS shell (if option enabled)
                                            |
                                         Execute SpawnEnvSetupper (--after)
                                            |
                                         Execute wrapper in preloader mode
                                         (if applicable)
                                            |
                                         Load application
                                         (and if applicable, do so in
                                         preloader mode)
                                            |
                                         Start listening for commands
                                            |
                                         Finish

### When spawning a process through a preloader

The journey looks like this when using a preloader to spawn a process:
    
       (In SpawningKit)                 (In preloader)          (In subprocess)
    
        Preparation
           |
        Tell preloader to spawn  ------> Preparation
           |                              |
        Receive, process                 Fork  ----------------> Preparation
        preloader response                |                         |
           |                             Send response           Start listening
        Handshake                         |                         |
           |                             Finish                  Finish
        Finish

### The Journey class

The Journey class represents a journey. It records all the steps taken so far, which steps haven't been taken yet, at which step we failed, and how long each step took.

A journey consists of a few parallel actors, with each actor having multiple steps and the ability to invoke another actor. Each step can be in one of three states:

 * Step has not started yet. Inside error pages, this will be visualized with an empty placeholder.
 * Step is currently in progress. Will be visualized with a spinner.
 * Step has already been performed successfully. Will be visualized with a green tick.
 * Step has failed. Will be visualized with a red mark.

Steps that have performed successfully or failed also have an associated begin time. The duration of each step is inferred from the begin time of that step, vs either the end time of that step or (if available) the begin time of the next step.

### The preparation and the HandshakePrepare class

Inside the process running SpawningKit, before forking a subprocess (regardless of whether that is going to be a preloader or a worker), various preparation needs to be done. This preparation work is implemented in Handshake/Prepare.h, in the HandshakePrepare class.

Here is a list of the work involved in preparation:

 * Creating a temporary directory for the purpose of performing a handshake with the subprocess. This directory is called a "work directory". Learn more in sections "The handshake and the HandshakePerform class" and "The work directory".

   **Note**: the "work directory" in this context refers to this directory, not to the Unix concept of current working directory (`getpwd()`). This directory also has got nothing to do with the instance directory that Passenger uses to keep track of running Passenger instances; that is a separate concept and mechanism.
 * If the application is not SpawningKit-enabled, or if the caller explicitly instructed so, HandshakePrepare finds a free port for the worker process to listen on. This port number will be passed to the worker process.
 * Dumping, into the work directory, important information that the subprocess should know of. For example: whether it's going to be started in development or production mode, the process title to assume, etc. This information is called the _spawn arguments_.
 * Calculating which exact arguments need to be passed to the `exec()` call. Because it's unsafe to do this after forking.

### The handshake and the HandshakePerform class

Once a process (whether preloader or worker) is spawned, SpawningKit needs to wait until it's up. If the spawning failed for whatever reason, then SpawningKit needs to infer that reason from information that the subprocess may have dumped into the work directory, and from the stdout/stderr output.

This is implemented in Handshake/Perform.h, in the HandshakePerform class.

### The SpawnEnvSetupper

The first thing the subprocess does is execute the SpawnEnvSetupper (which is contained inside PassengerAgent and can be invoked through a specific argument). This program performs various basic preparation in the subprocess such as:

 * Changing the current working directory to that of the application.
 * Setting environment variables, ulimits, etc.
 * Changing the UID and GID of the process.

It does all this by reading arguments from the work directory (see: "The work directory").

The reason why this program exists is because all this work is unsafe to do inside the process that runs SpawningKit. Because after a `fork()`, one is only allowed to call async-signal-safe code. That means no memory allocations, or even calling `setenv()`.

You can see in the diagrams that SpawnEnvSetupper is called twice, once before and once after loading the OS shell. The OS shell could arbitrarily change the environment (environment variables, ulimits, current working directory, etc.), sometimes without the user knowing about this. The main job that the SpawnEnvSetupper performs after the OS shell, is restoring some of the environment that the SpawningKit caller requested (e.g. specific environment variables, ulimits), as well as dumping the entire environment to the work directory so that the user can debug things when something is wrong.

The SpawnEnvSetupper is implemented in SpawnEnvSetupperMain.cpp.

## The work directory

The work directory is a temporary directory created at the very beginning of the spawning journey, during the SpawningKit preparation step. Note that this "work directory" is distinct from the Unix concept of current working directory (`getpwd()`).

The work directory's purpose is to:

 1. ...store information about the spawning procedure that the subprocess should know (the _spawn arguments_).
 2. ...receive information from the subprocess about how spawning went (the _response_). For example the subprocess can use it to signal.
 3. ...receive information about the subprocess's environment, so that this information can be displayed to the user for debugging purposes in case something goes wrong.

Here, "subprocess" does not only refer to the worker process, but also to the SpawnEnvSetupper, the wrapper (if applicable) and even the shell. All of these can (not not necessarily *have to*) make use of the work directory. For example, the SpawnEnvSetupper dumps environment information into the work directory. Some wrappers may also dump environment information.

The work directory's location is communicated to subprocesses through the `PASSENGER_SPAWN_WORK_DIR` environment variable.

### Structure

The work directory has the following structure. Entries that are created during the SpawningKit preparation step are marked with "[P]". All other entries may be created by the subprocess.

~~~
Work directory
  |
  +-- args.json         [P]
  |
  +-- args/             [P]
  |     |
  |     +-- app_root    [P]
  |     +-- log_level   [P]
  |     +-- ...etc...   [P]
  |
  +-- stdin             [P] (only when spawning through a preloader)
  +-- stdout_and_err    [P] (only when spawning through a preloader)
  |
  +-- response/         [P]
  |     |
  |     +-- finish      [P]
  |     |
  |     +-- properties.json
  |     |
  |     +-- error/      [P]
  |     |     |
  |     |     +-- category
  |     |     |
  |     |     +-- summary
  |     |     |
  |     |     +-- problem_description.txt
  |     |     +-- problem_description.html
  |     |     |
  |     |     +-- advanced_problem_details
  |     |     |
  |     |     +-- solution_description.txt
  |     |     +-- solution_description.html
  |     |
  |     +-- steps/      [P]
  |           |
  |           +-- subprocess_spawn_env_setupper_before_shell/  [P]
  |           |     |
  |           |     +-- state
  |           |     |
  |           |     +-- begin_time
  |           |     |   -OR-
  |           |     |   begin_time_monotonic
  |           |     |
  |           |     +-- end_time
  |           |         -OR-
  |           |         end_time_monotonic
  |           |
  |           +-- ...
  |           |
  |           +-- subprocess_listen/  [P]
  |                 |
  |                 +-- state
  |                 |
  |                 +-- begin_time
  |                 |   -OR-
  |                 |   begin_time_monotonic
  |                 |
  |                 +-- end_time
  |                     -OR-
  |                     end_time_monotonic
  |
  +-- envdump/            [P]
        |
        +-- envvars
        |
        +-- user_info
        |
        +-- ulimits
        |
        +-- annotations/  [P]
              |
              +-- some name
              |
              +-- some other name
              |
              +-- ...
   
~~~

There are two entries representing the spawn arguments:

 * `args.json` is a JSON file containing the arguments.

   ~~~
   { "app_root": "/path-to-app", "log_level": 3, ... }
   ~~~

 * `args/` is a directory containing the arguments. Inside this directory there are files, with each file representing a single argument. This directory provides an alternative way for subprocesses to read the arguments, which is convenient for subprocesses that don't have easy access to a JSON parser (e.g. Bash).

The `response/` directory represents the response:

 * `finish` is a FIFO file. If a wrapper is used, or if the application has explicit support for SpawningKit, then either of them can write to this FIFO file to indicate that it has done spawning. See "Mechanism for waiting until the application is up" for more information.
 * If a wrapper is used, or if the application has explicit support for SpawningKit, then one of them may create a `properties.json` in order to communicate back to SpawningKit information about the spawned worker process. For example, if the application process started listening on a random port, then this file can be used to tell SpawningKit which port the process is listening on. See "Application response properties" for more information.
 * `stdin` and `stdout_and_err` are FIFO files. They are only created (by the preloader) when using a preloader to spawn a new worker process. These FIFOs refer to the spawned worker process's stdin, stdout and stderr.
 * If the subprocess fails, then it can communicate back specific error messages through the `error/` directory. See "Error reporting" (especially "Information sources") for more information.
 * The subprocess must regularly update the contents of the `steps/` directory to allow SpawningKit to know which step in the journey the subprocess is executing, and what the state and and begin time of each step is. See "Subprocess journey logging" for more information.

The subprocess should dump information about its environment into the `envdump/` directory. Information includes environment variables (`envvars`), ulimits (`ulimits`), UID/GID (`user_info`), and anything else that the subprocess deems relevant (`annotations/`). If spawning fails, then the information reported in this directory will be included in the error report (see "Error reporting").

## Application response properties

If a wrapper is used or if the application has explicit support for SpawningKit, then either of them may create a `properties.json` inside the `response/` subdirectory of the work directory, in order to communicate back to SpawningKit information about the spawned application process. For example, if the application process started listening on a random port, then this file can be used to tell SpawningKit which port the process is listening on.

`properties.json` may only contain the following keys:

 * `sockets` -- an array objects describing the sockets on which the application listens for requests. The format is as follows. All fields are required unless otherwise specified.

       [
           {
               "address": "tcp://127.0.0.1:1234" | "unix:/path-to-unix-socket",
               "protocol": "http" | "session" | "preloader" | "arbitrary-other-value",
               "concurrency": <integer>,
               "accept_http_requests": true | false,         // optional; default: false
               "description": "description of this socket"   // optional
           },
           ...
       ]

    The `address` field describes the address of the socket, which is either a TCP address or a Unix domain socket path. In case of the latter, the Unix domain socket **must** have the same file owner as the application process.

    The `protocol` field describes the protocol that this socket speaks. The value "http" is obvious; the value "session" refers to an internal SCGI-ish protocol that the Ruby and Python wrappers speak with Passenger. The value "preloader" means that this socket is used for receiving preloader commands (only preloaders are supposed to report such sockets; see "The preloader protocol"). Other arbitrary values are also allowed.

    The `concurrency` field describes how many concurrent requests this socket can handle. The special value 0 means unlimited.

    If the spawned process is a worker process (i.e. not a preloader process) then there must be at least one socket for which `accept_http_requests` is set to true. This field tells Passenger that HTTP traffic may be forwarded to this particular socket. You may wonder: why does this exist? Isn't it already enough if the application reports at least one socket that speaks the "http" protocol? The answer is no: whether Passenger should forward HTTP traffic to a specific socket has got nothing to do with whether that socket speaks HTTP. For example Passenger forwards HTTP traffic to the Ruby and Python wrappers using the "session" protocol. Furthermore, the Ruby wrapper spawns an HTTP socket, but it's for debugging purposes only and is slow, and so it should not be used for receiving live HTTP traffic. Note that a socket with `accept_http_requests` set to true **must** speak either the "http" or the "session" protocol. Other protocols are not allowed.

    The `description` field may be used in the future to display additional information about an application process, for example inside admin tools, but currently it is not used.

## The preloader protocol

The "Tell preloader to spawn" and "Receive, process preloader response" steps in the spawn journey work as follows.

Upon starting the preloader, the preloader listens for commands on a Unix domain socket. SpawningKit tells the preloader to spawn a worker process by sending a command over the socket. The command is a JSON document on a single line:

~~~json
{ "command": "spawn", "work_dir": "/path-to-work-dir" }
~~~

The preloader then forks a child process, and (before the next step in the journey is performed) immediately responds with either a success or an error response:

~~~json
{ "result": "ok", "pid": 1234 }
{ "result": "error", "message": "something went wrong" }
~~~

The worker process's stdin, stdout and stderr are stored in FIFO files inside the work directory. SpawningKit then opens these FIFOs and proceeds with handshaking with the worker process.

## Subprocess journey logging

It is the Passenger Core (running SpawningKit) that initiates a spawning journey and that reports errors to users. Some steps in the journey are performed by actors that are not the Passenger Core (e.g. the preloader and the subprocess). How do these actors communicate to the SpawningKit code running inside the Passenger Core about the state of *their* part of the journey?

This is done through the `steps/` subdirectory in the work directory. Subprocesses write to files in `steps/` regularly. Before SpawningKit's code returns, it loads information from `steps`/ and loads these into the journey object.

Each subdirectory in `steps/` represents a step in the part of the journey that a subprocess is responsible for. The files inside such a subdirectory communicate the state of that step:

 * `state` must contain one of `STEP_NOT_STARTED`, `STEP_IN_PROGRESS`, `STEP_PERFORMED` or `STEP_ERRORED`.
 * Either `begin_time` or `begin_time_monotonic` (the latter is preferred) must exist.

   `begin_time` must contain a Unix timestamp representing the wall clock time at which this step began. The number may be a floating point number for sub-second precision.

   `begin_time_monotonic` is the same, but must contain a timestamp obtained from the monotonic clock instead of the wall clock. It is recommended that subprocesses create this file, not `begin_time`, because the monotonic clock is not influenced by clock skews (e.g. daylight savings, time zone changes or NTP updates).

   Because not all programming languages allow access to the monotonic clock, SpawningKit allows both mechanisms.
 * Either `end_time` or `end_time_monotonic` (the latter is preferred) must exist.

   The purpose of these files are anologous to `begin_time`/`begin_time_monotonic`, but instead record the time at which this step ended.

## Error reporting

When something goes wrong during spawning, SpawningKit generates an error report. This report contains all the details you need to pinpoint the source of the problem, and is represented by the SpawnException class.

A report contains the following information:

 * A broad **category** in which the problem belongs. Is it an internal program error? An operating system (system call) error? A filesystem error? An I/O error?
 * A **summary** of the problem. This typically consists of a single line line and is in plain text format.
 * A detailed **problem description**, in HTML format. This is a longer piece of narrative that is typically structured in two parts: a high-level description of the problem, meant for beginners; as well as a **advanced problem details** part that aids debugging. For example, if a file system permission error was encountered, the high-level description could explain what a file system permission is and that it's not Passenger's fault. The advanced information part could display the filename in question, as well as the OS error code and OS error message.
 * A detailed **solution description**, in HTML format. This is a longer piece of narrative that explains in a detailed manner how to solve the problem.
 * Various **auxiliary details** which are not directly related to the error, but may be useful or necessary for the purpose of debugging the problem: stdout and stderr output so far; ulimits; environment variables; UID, GID of the process in which the error occurred; system metrics such as CPU and RAM; etcetera.
 * A description of the **journey**: which steps inside the journey have been performed, are in progress, or have failed; and how long each step took.

### Information sources

SpawningKit generates an error report (a SpawnException object) by gathering information from multiple sources. One source is SpawningKit itself: if something goes wrong within the preparation step for example, then SpawningKit knows that the error originated from there, and so it will only use internal information to generate an error report.

The other source is the subprocess. If something goes wrong during the handshake step, then true source of the problem can either be in SpawningKit itself (e.g. it ran out of file descriptors), or in the subprocess (e.g. the subprocess encountered a filesystem permission error). Subprocesses can communicate to SpawningKit about errors that they have encountered by dumping information into the `response/error/` subdirectory of the work directory. SpawningKit will use this information in generating an error report.

### How an error report is presented

The error report is presented in two ways:

 1. In the terminal or in log files.
 2. In an HTML page.

The summary is only meant to be displayed in the terminal or in the log files as a one-liner. It can contain basic details (such as the OS error code) but is not meant to contain finer details such as the subprocess stdout/stderr output, the environment variable dump, etc.

Everything else is meant to be displayed in an HTML page. The HTML page explicitly does not include the summary, so the summary must not contain any information that isn't available in all the other fields.

The advanced problem details are only displayed in the HTML page if one does not explicitly supply a problem description. See "Generating an error report" for more information.

### How supplied information affects error report generation

Only a _category_ and a _journey description_ are required for generating an error report. The SpawnException class is capable of automatically generating an appropriate (albeit generic) summary, problem description and solution description based on the category and which step in the journey failed.

If one doesn't supply a problem description, but does supply advanced problem details, then the automatically-generated problem description will include the advanced problem details. The advanced problem details aren't used in any other way, so if one does supply a problem description then one must take care of including the advanced problem details.

Inside the SpawningKit codebase, an error report is generated by creating a SpawnException object. Subprocesses such as the preloader, the SpawnEnvSetupper, the wrapper and the app, can aid in generating the error report by providing their own details through the `error/` and `envdump/` subdirectories inside the work directory. In particular: subprocesses can provide the problem description and the solution description in one of two formats: either plain-text or HTML. So e.g. only one of `problem_description.txt` or `problem_description.html` need to exist, not both.

## Mechanism for waiting until the application is up

SpawningKit utilizes two mechanisms to wait until the application is up. It invokes both mechanisms at the same time and waits until at least one succeeds.

The first mechanism the `response/finish` file in the work directory. A wrapper or a SpawningKit-enabled application can write to that file to tell SpawningKit that it is done, either successfully (by writing `1`) or with an error (by writing `0`).

The second mechanism is only activated when the caller has told SpawningKit that the application is generic, or when the caller has told SpawningKit to find a free port for the application. In this case, SpawningKit will wait until the port that it has found can be connected to.
