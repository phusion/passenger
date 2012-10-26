# Coding Style and Guidelines

## C++ coding style

 * Use 4-space tabs for indentation.
 * Wrap at approximately 80 characters. This is a recommendation, not a hard guideline. You can exceed it if you think it makes things more readable, but try to minimize it.

 * Use cascalCasing for function names, variables, class/struct members and parameters:

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

 * Seperate arguments and parts of expressions by spaces:

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

## Ruby coding style

The usual Ruby coding style applies, with some exceptions:

 * Use 4-space tabs for indentation.
 * Return values explicitly with `return`.

## Prefer shared_ptrs

You should prefer `shared_ptr`s over raw pointers because they make memory leaks and memory errors less likely. There are only very limited cases in which raw pointers are justified, e.g. optimizations in very hot code paths.

## Event loop callbacks

Be careful with event loop callbacks, they are more tricky than one would expect.

 * If your event loop callback ever calls user-defined functions, either explicitly or implicitly, you should obtain a `shared_ptr` to your `this` object. This is because the user-defined function could call something that would free your object. Your class should derive from `boost::enable_shared_from_this` to make it easy for you to obtain a `shared_ptr` to yourself.

       void callback(ev::io &io, int revents) {
           shared_ptr<Foo> self = shared_from_this();
           ...
       }

 * Event loop callbacks should catch expected exceptions. Letting an exception pass will crash the program. When system call failure simulation is turned on, the code can throw arbitrary SystemExceptions, so beware of those.

## Further reading

You should read ext/common/ApplicationPool2/README.md if you're interesting in working on the ApplicationPool and Spawner subsystems.
