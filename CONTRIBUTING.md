# Contributors Guide

Phusion Passenger is open source so your contributions are very welcome. Although we provide a [commercial version](https://www.phusionpassenger.com/features#premium-features) and [commercial support](https://www.phusionpassenger.com/commercial_support), the core remains open source and we remain committed to keep it that way. This guide gives you an overview of the ways with which you can contribute, as well as contribution guidelines.

You can contribute in one of the following areas:

 * Filing bugs.
 * Bug triage.
 * Documentation (user documentation, developer documentation, contributor documentation).
 * Community support.
 * Code.

We require contributors to sign our [contributor agreement](https://www.phusion.nl/contributor) before we can merge their patches.

## Filing bug reports

When filing a bug report, please ensure that you include the following information:

 * What steps will reproduce the problem?
 * What is the expected output? What do you see instead?
 * What version of Passenger are you using?
 * Which version of Ruby, Rails, Node.js or Meteor are you using? On what operating system?

## Contributing documentation

You can contribute documentation through the [passenger_library](https://github.com/phusion/passenger_library) repo.

## Contributing by bug triaging

Users [file bug reports](https://github.com/phusion/passenger/issues) on a regular basis, but not all bug reports are legit,contain sufficient information, are equally important, etc. By helping with bug triaging you make the lives of the core developers a lot easier.

To start contributing, please submit a comment on any bug report that needs triaging. This comment should contain triaging instructions, e.g. whether a report should be considered duplicate. If you contribute regularly we'll give you moderator access to the bug tracker so that you can apply triaging labels directly.

Here are some of the things that you should look for:

 * Some reports are duplicates of each other, i.e. they report the same issue. You should mark them as duplicate and note the ID of the original report.
 * Some reported problems are caused by the reporter's machine or the reporter's application. You should explain to them what the problem actually is, that it's not caused by Passenger, and then close the report.
 * Some reports need more information. At the very least, we need specific instructions on how to reproduce the problem. You should ask the reporter to provide more information. Some reporters reply slowly or not at all. If some time has passed, you should remind the reporter about the request for more information. But if too much time has passed and the issue cannot be reproduced, you should close the report and mark it as "Stale".
 * Some bug reports seem to be limited to one reporter, and it does not seem that other people suffer from the same problem. These are reports that need _confirmation_. You can help by trying to reproduce the problem and confirming the existance of the problem.
 * Some reports are important, but have been neglected for too long. Although the core developers try to minimize the number of times this happens, sometimes it happens anyway because they're so busy. You should actively ping the core developers and remind them about it. Or better: try to actively find contributors who can help solving the issue.

**Always be polite to bug reporters.** Not all reporters are fluent in English, and not everybody may be tech-savvy. But we ask you for your patience and tolerance on this. We want to stimulate a positive and ejoyable environment.

## Contributing community support

You can contribute by answering support questions on [Stack Overflow](http://stackoverflow.com/search?q=passenger).

## Contributing code

Passenger is mostly written in C++, but the build system and various small helper scripts are in Ruby. The loaders for each supported language is written in the respective language.

Consult the [Development handbook](doc/DevHandbook.md).
