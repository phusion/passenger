# C++ mocking strategy

- We don't use any mocking library.
- We implement mocking by splitting mockable code to protected virtual methods. Inside test suites, we create a subclass in which we override the methods we want to mock.
- Best practices for the override:
   - Opt-in for mocking on a per-test basis.
   - When not opted-in, call the parent class's method instead of duplicating its code.

Example:

```c++
class Greeter {
protected:
    virtual const char *name() {
        return "john";
    }

public:
    void greet() {
        std::cout << "hello " << name() << std::endl;
    }
};

// In the test suite:
class TestGreeter: public Greeter {
protected:
    virtual const char *name() override {
        if (mockName != nullptr) {
            return mockName;
        } else {
            return Greeter::name();
        }
    }

public:
    const char *mockName; // Set to non-nullptr to mock the name

    TestGreeter()
        : mockName(nullptr)
        { }
};
```
