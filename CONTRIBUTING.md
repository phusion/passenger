# Contributors Guide

**Table of contents**

 * [Filing bug reports](#file_bugs)
 * [Contributing documentation](#contrib_docs)
 * [Contributing by bug triaging](#contrib_triag)
 * [Contributing community support](#contrib_support)
 * [Contributing code](#contrib_code)
   * [Developer QuickStart](#dev_quickstart)
   * [Design and Architecture](#design_and_architecture)
   * [Code Walkthrough](#code_walkthrough)
   * [Compilation and build system](#build_system)
   * [Running the unit tests](#unit_tests)
   * [Directory structure](#dir_structure)
   * [C++ coding style](#cxx_coding_style)
   * [Ruby coding style](#ruby_coding_style)
   * [Systems programming fundamentals](#systems_programming_fundamentals)
   * [Further reading](#further_reading)
 * [Pull requests](#pull_requests)

Thank you for your interest in Phusion Passenger. Phusion Passenger is open source so your contributions are very welcome. Although we also provide a [commercial version](https://www.phusionpassenger.com/features#premium-features) and [commercial support](https://www.phusionpassenger.com/commercial_support), the core remains open source and we remain committed to keep it that way. This guide gives you an overview of the ways with which you can contribute, as well as contribution guidelines.

You can contribute in one of the following areas:

 * Filing bugs.
 * Bug triage.
 * Documentation (user documentation, developer documentation, contributor documentation).
 * Community support.
 * Code.

We require contributors to sign our [contributor agreement](https://www.phusion.nl/contributor) before we can merge their patches.

Please submit patches in the form of a Github pull request or as a patch on the [bug tracker](https://github.com/phusion/passenger/issues). Pull requests are preferred and generally get more attention because Github has better email notifications and better discussion capabilities.

You should also install required developer tools. The following command will install everything you need:

    rake test:install_deps

If your system requires gems to be installed with root privileges, run:

    rake test:install_deps SUDO=1

<a name="file_bugs"></a>
## Filing bug reports

When filing a bug report, please ensure that you include the following information:

 * What steps will reproduce the problem?
 * What is the expected output? What do you see instead?
 * What version of Phusion Passenger are you using?
 * Which version of Ruby, Rails, Node.js or Meteor are you using? On what operating system?

<a name="contrib_docs"></a>
## Contributing documentation

All good software should have good documentation, and we take this very seriously. However writing and maintaining quality documentation is not an easy task. If you are not skilled in C++ or programming, then writing documentation is the easiest way to contribute.

Most documentation can be located in the `doc` directory, and are either written in Markdown or in Asciidoc format. They can be compiled to HTML with `rake doc`. You need [Mizuho](https://github.com/FooBarWidget/mizuho) to compile Asciidoc and [BlueCloth](http://deveiate.org/projects/BlueCloth) to compile Markdown. Both gems are automatically installed as part of the Phusion Passenger developer tools.

<a name="contrib_tiag"></a>
## Contributing by bug triaging

Users [file bug reports](https://github.com/phusion/passenger/issues) on a regular basis, but not all bug reports are legit,contain sufficient information, are equally important, etc. By helping with bug triaging you make the lives of the core developers a lot easier.

To start contributing, please submit a comment on any bug report that needs triaging. This comment should contain triaging instructions, e.g. whether a report should be considered duplicate. If you contribute regularly we'll give you moderator access to the bug tracker so that you can apply triaging labels directly.

Here are some of the things that you should look for:

 * Some reports are duplicates of each other, i.e. they report the same issue. You should mark them as duplicate and note the ID of the original report.
 * Some reported problems are caused by the reporter's machine or the reporter's application. You should explain to them what the problem actually is, that it's not caused by Phusion Passenger, and then close the report.
 * Some reports need more information. At the very least, we need specific instructions on how to reproduce the problem. You should ask the reporter to provide more information. Some reporters reply slowly or not at all. If some time has passed, you should remind the reporter about the request for more information. But if too much time has passed and the issue cannot be reproduced, you should close the report and mark it as "Stale".
 * Some bug reports seem to be limited to one reporter, and it does not seem that other people suffer from the same problem. These are reports that need _confirmation_. You can help by trying to reproduce the problem and confirming the existance of the problem.
 * Some reports are important, but have been neglected for too long. Although the core developers try to minimize the number of times this happens, sometimes it happens anyway because they're so busy. You should actively ping the core developers and remind them about it. Or better: try to actively find contributors who can help solving the issue.

**Always be polite to bug reporters.** Not all reporters are fluent in English, and not everybody may be tech-savvy. But we ask you for your patience and tolerance on this. We want to stimulate a positive and ejoyable environment.

<a name="contrib_support"></a>
## Contributing community support

You can contribute by answering support questions on [Stack Overflow](http://stackoverflow.com/search?q=passenger).

<a name="contrib_code"></a>
## Contributing code

Phusion Passenger is mostly written in C++, but the build system and various small helper scripts are in Ruby. The loaders for each supported language is written in the respective language. The source code is filled with inline comments, so look there if you want to understand how things work.

<a name="dev_quickstart"></a>
### Developer QuickStart

<a href="https://vimeo.com/phusionnl/review/97427161/15cb4cc59a"><img src="http://blog.phusion.nl/wp-content/uploads/2014/06/passenger_developer_quickstart.png"></a>

_Watch the Developer QuickStart screencast_

We provide an easy and convenient development environment that contributors can use. Learn more at the [Developer QuickStart](https://github.com/phusion/passenger/blob/master/doc/DeveloperQuickstart.md).

<a name="design_and_architecture"></a>
### Design and Architecture

Phusion Passenger's design and architecture is documented in detail in the [Design & Architecture](https://www.phusionpassenger.com/documentation/Design%20and%20Architecture.html) document.

<a name="code_walkthrough"></a>
### Code Walkthrough

<a href="http://vimeo.com/phusionnl/review/98027409/03ba678684"><img src="http://blog.phusion.nl/wp-content/uploads/2014/06/code_walkthrough.png"></a>

We have [a video](http://vimeo.com/phusionnl/review/98027409/03ba678684) which walks you through the Phusion Passenger codebase, showing you step-by-step how things fit together. It complements the [Design & Architecture](https://www.phusionpassenger.com/documentation/Design%20and%20Architecture.html) document.

<a name="build_system"></a>
### Compilation and build system

`passenger-install-apache2-module` and `passenger-install-nginx-module` are actually user-friendly wrappers around the build system. The build system is written in Rake, and most of it can be found in the `build/` directory.

Run the following command to compile everything:

    rake apache2
    rake nginx

It is recommended that you install ccache and set the `USE_CCACHE=1` environment variable. The build system will then automatically wrap all compiler calls in ccache, significantly improving recompilation times.

<a name="unit_tests"></a>
### Running the unit tests

The tests depend on the Phusion Passenger developer tools. If you're not using our [Vagrant environment](https://github.com/phusion/passenger/blob/master/doc/DeveloperQuickstart.md), you need to make sure they're installed:

    rake test:install_deps

You also need to setup the file `test/config.json`. You can find an example in `test/config.json.example`.

Run all tests:

    rake test

Run only the unit tests for the C++ components:

    rake test:cxx
    rake test:oxt

The `test:cxx` unit test suite contains many different test groups. You can run a specific one by setting the environment variable `GROUPS` to a comma-delimited list of group names, e.g.:

    rake test:cxx GROUPS='ApplicationPool2_PoolTest,UtilsTest'

You can also run specific tests within a suite. Pass the relevant test number(s) like this:

    rake test:cxx GROUPS='ApplicationPool2_PoolTest:82,83'

You can also run the C++ tests in GDB or Valgrind. We have a useful GDB config file in `test/gdbinit.example`. You should copy it to `test/.gdbinit` and edit it.

    rake test:cxx GDB=1
    rake test:cxx VALGRIND=1

Run just the unit tests for the Ruby components:

    rake test:ruby

Run just the integration tests:

    rake test:integration            # All integration tests.
    rake test:integration:apache2    # Just integration tests for Apache 2.
    rake test:integration:nginx      # Just integration tests for Nginx.

Note that some tests, such as the ones that test privilege lowering, require root privileges. Those will only be run if Rake is run as root.

<a name="dir_structure"></a>
### Directory structure

The most important directories are:

 * `src/ruby_suppportlib` <br>
   The source code for Ruby parts of Phusion Passenger.
 * `src/ruby_native_extension` <br>
   Native extension for Ruby. Phusion Passenger uses the functions in this extension for optimizing certain operations, but Phusion Passenger can also function without this extension.
 * `src/apache2_module` <br>
   Apache 2-specific source code.
 * `src/nginx_module` <br>
   Nginx-specific source code.
 * `src/cxx_supportlib` <br>
   Support code shared between all C++ components.
 * `src/agent` <br>
   Source code of the PassengerAgent executable. The agent can be started in multiple modes.
   * The Watchdog is the main Phusion Passenger process. It starts the Passenger core and the UstRouter, and restarts them when they crash. It also cleans everything up upon shut down.
   * The Core performs most of the heavy lifting. It parses requests, spawns application processes, forwards requests to the correct process and forwards application responses back to the web server.
   * The UstRouter processes Union Station data and sends them to the Union Station server.
 * `bin` <br>
   User executables.
 * `helper-scripts` <br>
   Scripts used during runtime, but not directly executed by the user. All the loaders - applications which are responsible for loading an application written in a certain language and hooking it up to Phusion Passenger - are in this directory.
 * `doc` <br>
   Various documentation.
 * `test` <br>
   Unit tests and integration tests.
 * `test/support` <br>
   Support/utility code, used in the tests.
 * `test/stub` <br>
   Stubbing and mocking code, used in the tests.

Less important directories:

 * `src/vendor-modified/boost` <br>
   A stripped-down and customized version of the [Boost C++ library](http://www.boost.org).
 * `src/oxt` <br>
   The "OS eXtensions for boosT" library, which provides various important functionality necessary for writing robust server software. It provides things like support for interruptable system calls and portable backtraces for C++. Boost was modified to make use of the functionality provided by OXT.
 * `dev` <br>
   Tools for Phusion Passenger developers. Not used during production.
 * `resources` <br>
   Various non-executable resource files, used during production.
 * `packaging/debian` <br>
   Debian packaging files.
 * `packaging/rpm` <br>
   RPM packaging files.
 * `man` <br>
   Man pages.
 * `build` <br>
   Source code of the build system.

<a name="cxx_coding_style"></a>
### C++ coding style

 * Use 4-space tabs for indentation.
 * Wrap at approximately 80 characters. This is a recommendation, not a hard guideline. You can exceed it if you think it makes things more readable, but try to minimize it.

 * Use camelCasing for function names, variables, class/struct members and parameters:

        void frobnicate();
        void deleteFile(const char *filename, bool syncHardDisk);
        int fooBar;

   Use PascalCasing for classes, structs and namespaces:

        class ApplicationPool {
        struct HashFunction {
        namespace Passenger {

 * `if` and `while` statements must always have their body enclosed by brackets:

        if (foo) {
            ...
        }

   Not:

        if (foo)
            ...

 * When it comes to `if`, `while`, `class` and other keywords, put a space before and after the opening and closing parentheses:

        if (foo) {
        while (foo) {
        case (foo) {

   Not:

        if(foo){
        while (foo) {

 * You should generally put brackets on the same line as the statement:

        if (foo) {
            ...
        }
        while (bar) {
            ...
        }

   However, if the main statement is so long that it does not fit on a single line, then the bracket should start at the next line:

        if (very very long expression
         && another very very long expression)
        {
            ...
        }

 * Do not put a space before the opening parenthesis when calling functions.

        foo(1, 2, 3);

   Not:

        foo (1, 2, 3);

 * Separate arguments and parts of expressions by spaces:

        foo(1, 2, foo == bar, 5 + 6);
        if (foo && bar) {

   Not:

        foo(1,2, foo==bar,5+6);
        if (foo&&bar) {

 * When declaring functions, puts as much on the same line as possible:

        void foo(int x, int y);

   When the declaration becomes too long, wrap at the beginning of an argument
   and indent with a tab:

        void aLongMethod(double longArgument, double longArgument2,
            double longArgument3);

   If the declaration already starts at a large indentation level (e.g. in a class) and the function has many arguments, or if the names are all very long, then it may be a good idea to wrap at each argument to make the declaration more readable:

        class Foo {
            void aLongLongLongLongMethod(shared_ptr<Foo> sharedFooInstance,
                shared_ptr<BarFactory> myBarFactory,
                GenerationDir::Entry directoryEntry);

 * When defining functions outside class declarations, put the return type and any function attributes on a different line than the function name. Put the opening bracket on the same line as the function name.

        static __attribute__((visibility("hidden"))) void
        foo() {
            ...
        }

        void
        Group::onSessionClose() {
            ...
        }

   But don't do that if the function is part of a class declarations:

        class Foo {
            void foo() {
                ...
            }
        };

   Other than the aforementioned rules, function definitions follow the same rules as function declarations.

<a name="ruby_coding_style"></a>
### Ruby coding style

The usual Ruby coding style applies. That is, 2 spaces for indenting.

<a name="systems_programming_fundamentals"></a>
### Systems programming fundamentals

Large parts of Phusion Passenger are written in C++. You can find a free C++ tutorial at [cplusplus.com](http://www.cplusplus.com/doc/tutorial/).

Phusion Passenger heavily utilizes POSIX, the API that is in use by all Unix systems. The POSIX API is heavily used for:

 * Filesystem operations.
 * Process management.
 * Sockets.

A good and comprehensive, but rather large source for learning POSIX is the [POSIX Programmer's Guide](ftp://92.42.8.18/pub/doc/books/OReilly_-_POSIX_Programmers_Guide.pdf) by Donald A. Lewine. You can find smaller but less comprehensive documents all over the Internet. In particular, you will want to familiarize yourself with [fork-exec](http://en.wikipedia.org/wiki/Fork-exec), the standard process creation pattern in Unix.

<a name="further_reading"></a>
### Further reading

 * [Coding Tips and Pitfalls](https://github.com/phusion/passenger/blob/master/doc/CodingTipsAndPitfalls.md)

<a name="pull_requests"></a>
### Pull requests

Pull requests should normally be submitted against the latest **stable** branch (e.g. **stable-5.1**), because once tested & accepted, we want users to benefit from the work as soon as possible. The stable branch is constantly tested, contains both bugfix and feature commits, and we periodically tag it to produce a new release. 