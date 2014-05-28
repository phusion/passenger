# Coding tips and common coding pitfalls

## Prefer shared_ptrs

You should prefer `shared_ptr`s over raw pointers because they make memory leaks and memory errors less likely. There are only very limited cases in which raw pointers are justified, e.g. optimizations in very hot code paths.

### Unexpected shared_ptr invalidation

Suppose you have a function which takes a reference to a shared_ptr, like this:

    void foo(const shared_ptr<Client> &client) {
        performNetworking();
        log(client->getAddress());
    }

You might think that `client` is never going to be destroyed during the scope of the function, right? Wrong. If `client` came from a non-local variable, and your function calls another function (perhaps indirectly) which resets that variable, then `client` is going to be invalidated. Consider a contrived example like this:

    static shared_ptr<Client> client;

    void performNetworking() {
        if (write(...) == -1) {
            // An error occurred, disconnect client.
            client.reset();
        }
    }

More realistically, this kind of bug is likely to occur if `foo()` calls some function which invokes a user-defined callback, which unreferences the original object under some conditions. Thus, if your code might call an arbitrary user-defined function, you should increase the reference count locally:

    void foo(const shared_ptr<Client> &client) {
        shared_ptr<Client> extraReference = client;
        performNetworking();
        log(client->getAddress());
    }

A variant of this pitfall is documented under "Event loop callbacks".

## Event loop callbacks

### Obtain extra shared_ptr reference on `this`

If your event loop callback ever calls user-defined functions, either explicitly or implicitly, you should obtain a `shared_ptr` to your `this` object. This is because the user-defined function could call something that would free your object. The problem is documented in detail in "Unexpected shared_ptr invalidation".

Your class should derive from `boost::enable_shared_from_this` to make it easy for you to obtain a `shared_ptr` to yourself.

        void callback(ev::io &io, int revents) {
            shared_ptr<Foo> extraReference = shared_from_this();
            ...
        }

### Exceptions

Event loop callbacks should catch expected exceptions. Letting an exception pass will crash the program. When system call failure simulation is turned on, the code can throw arbitrary SystemExceptions, so beware of those.

## Thread interruption and RAII destructors

When using thread interruption, make sure that RAII destructors are non-interruptable. If your code is interrupted and then a `thread_interrupted` is thrown, make sure that RAII destructors don't check for the interruption flag and then throw `thread_interrupted` again. This not only fails to clean things up properly, but also confuses the exception system, resulting in strange errors such as "terminate called without an active exception".
