# ConfigKit in practice & design patterns

[The ConfigKit README](#README.md) taught you how to use ConfigKit by itself. But how does ConfigKit fit in the bigger picture? This section describes *good practices* and *design patterns* can that can be used throughout the overall Passenger C++ codebase.

At the time of writing (25 Feb 2017), ConfigKit was just introduced, so these practices and patterns aren't yet used everywhere, but the long-term plan is to adopt these practices/patterns throughout the entire codebase.

<!-- MarkdownTOC levels="1,2,3,4" autolink="true" bracket="round" -->

- [The "component" pattern](#the-component-pattern)
    - [Components are composable](#components-are-composable)
    - [Configuration mechanism](#configuration-mechanism)
        - [Typical invocation](#typical-invocation)
        - [Design rationale](#design-rationale)
    - [Inspecting a component's configuration](#inspecting-a-components-configuration)
- [Asynchronous components](#asynchronous-components)
- [Synchronous examples](#synchronous-examples)
    - [SecurityChecker example: a configurable, low-level component](#securitychecker-example-a-configurable-low-level-component)
    - [DnsQuerier example: a low-level component with post-configuration application operations](#dnsquerier-example-a-low-level-component-with-post-configuration-application-operations)
    - [HappyDnsQuerier example: subclassing components](#happydnsquerier-example-subclassing-components)
    - [Downloader example: a high-level component that composes subcomponents](#downloader-example-a-high-level-component-that-composes-subcomponents)
        - [The special problem of conflicting overlapping configuration names and translation](#the-special-problem-of-conflicting-overlapping-configuration-names-and-translation)
        - [Code example](#code-example)
    - [Main function example](#main-function-example)
- [Asynchronous examples](#asynchronous-examples)
- [Implementation considerations](#implementation-considerations)
    - [Error handling in prepareChangeRequest and commitChangeRequest](#error-handling-in-preparechangerequest-and-commitchangerequest)
    - [Concurrency](#concurrency)

<!-- /MarkdownTOC -->

## The "component" pattern

_This section only provides an introduction to the component pattern concept. The pattern will be described in more detail in the various examples, as well as in the [Implementation considerations](#implementation-considerations) section._

A component is an entity (usually a class) that is configurable. A component has a schema, and contains its own configuration: it embeds a ConfigKit::Store. You cannot access or modify this store directly: all access is encapsulated by the component.

### Components are composable

A component is *composable*. Parent components contain child components. The configurations of such child components are encapsulated: they cannot be directly modified by the outside world, and the parent component is solely responsible for updating child components' configuration. Parent components can even choose to completely hide the existance of child components.

Thus, a parent component's schema is *usually* a superset of the union of all its child components' schemas: it usually allows access to all of its child components' config options, and may even introduce some config options of its own.

But sometimes there are child config options that make no sense outside the parent components. Parent components can choose to hide those options from the outside world.

There may also be child config options that should be exposed to the outside world under a different name or different form. Parent components are free to expose any schema they want to the outside world.

### Configuration mechanism

For the purpose of changing configuration, a component should:

 * Expose a `ConfigChangeRequest` type.
 * Expose two methods: `prepareConfigChange()` and `commitConfigChange()`.

This is what they are supposed to do:

 * `bool prepareConfigChange(updates, &errors, &req)` accepts proposed configuration updates, similar to `ConfigKit::Store::update()`. It performs validation, outputs any errors in `errors` and returns whether preparation succeeded (i.e. whether there are no errors).

   If validation passes, then instead of changing the configuration directly, it merely performs *preparation work* necessary to commit the configuration change later. The entire state of this preparation work is to be stored in the `req` object (which is of type `ConfigChangeRequest`).

   As long as the preparation work is not commited, the existance of such preparation work must not change the component's behavior or configuration.

 * `void commitConfigChange(&req) noexcept` accepts a ConfigChangeRequest object -- which was processed by prepareConfigChange -- and commits the preparation work.

   **Big caveat:** this method is not allowed to throw any exceptions! If anything can go wrong, it should have been detected during prepareConfigChange!

   If any cleanup work -- which may fail/throw -- needs to be done, then that work must be done outside this method. You can do that for example by making the ConfigChangeRequest destructor responsible for cleaning things up.

You can learn more about all this in the examples provided in this document, as well as in the section [Implementation considerations](#implementation-considerations).

#### Typical invocation

Here is how a typical invocation looks like, given a `Component component` object:

~~~c++
Json::Value updates;
updates["url"] = "http://www.google.com";
updates["debug"] = true;

Component::ConfigChangeRequest req;
vector<ConfigKit::Error> errors;
if (component.prepareConfigChange(updates, errors, req)) {
    component.commitConfigChange(req);
} else {
    // Log the errors
}
~~~

#### Design rationale

Why two methods? Why not just a single `configure(updates, errors)` method similar to `ConfigKit::Store::update()`? The answer: to allow composability and atomicity.

If a parent component configures multiple child components, and one of them fails, then the parent is left in an inconsistent in which only some child components have been configured. We want a configuration change to be atomic across all child components.

Atomicity *could* be implemented with a rollback system: if the parent detects that a child component failed, then it rolls back the configuration of already-configured child components. But rollback can be difficult to implement.

Instead, we think that a preparation+commit system is easier and cleaner. A parent configures child components with the following pseudocode:

~~~c++
Child1::ConfigChangeRequest req1;
Child2::ConfigChangeRequest req2;
Child3::ConfigChangeRequest req3;

child1.prepareConfigChange(updates, errors, req1);
if (errors.empty()) {
    child2.prepareConfigChange(updates, errors, req2);
}
if (errors.empty()) {
    child3.prepareConfigChange(updates, errors, req3);
}

// No config has *actually* been changed so far.

if (errors.empty()) {
    // Atomically apply all config changes. None of these will fail.
    child1.commitConfigChange(req1);
    child2.commitConfigChange(req2);
    child2.commitConfigChange(req3);
}
~~~

### Inspecting a component's configuration

A component should expose a `Json::Value inspectConfig() const` method which returns a JSON object in the same format as `ConfigKit::Store::inspect()`.

## Asynchronous components

We've assumed so far that components are synchronous. What about components that deal with asynchronous I/O? They usually run inside an event loop, and all access to their internal data should be performed from the event loop.

Our recommendation is that you implement synchronous as well as asynchronous versions of the recommended component methods. The synchronous versions must be called from the event loop. The asynchronous versions...

 - accept a callback which is to be called with the result of the related synchronous method.
 - schedule an operation via the event loop the perform the equivalent synchronous operation and call the callback.
 - are to be thread-safe.

This approach will be explained further under [Asynchronous examples](#asynchronous-examples).

## Synchronous examples

Let's demonstrate the component concept through a number of annotated example classes:

 - `SecurityChecker` checks whether the given URL is secure to connect to, by checking a database of vulnerable sites.
   This example demonstrates basic usage of ConfigKit in a low-level component.

 - `DnsQuerier` looks up DNS information for a given URL, from multiple DNS servers. Because the algorithm that DnsQuerier uses is a performance-critical hot path (or so we claim for the sake of the example), it should cache certain configuration values in variables instead of looking up the config store over and over, because the latter involve unnecessary hash table lookups and memory allocations.

   This example demonstrates caching of configuration values. More generally, it demonstrates how to perform arbitrary operations necessary for applying a configuration change.

 - `HappyDnsQuerier` subclasses `DnsQuerier` to add additional behavior. When the `query()` method is called, it prints a configurable message.

   This example demonstrates subclassing of components.

 - `Downloader` is a high-level class (parent component) for downloading a specific URL. Under the hood it utilizes `SecurityChecker` and `HappyDnsQuerier` (subcomponents).

   This example demonstrates how to combine its own configuration with the configuration of multiple lower-level classes.

### SecurityChecker example: a configurable, low-level component

SecurityChecker checks whether the given URL is secure to connect to, by checking a database of vulnerable sites. This example demonstrates basic usage of ConfigKit in a low-level component.

~~~c++
 #include <boost/config.hpp>
 #include <boost/scoped_ptr.hpp>
 #include <string>
 #include <vector>
 #include <ConfigKit/ConfigKit.h>

using namespace std;
using namespace Passenger;

class SecurityChecker {
public:
    // A component class should start with a public section that defines 2-3 things:
    // - A schema.
    // - A ConfigRealization type (optional; will be covered in DnsQuerier example).
    // - A ConfigChangeRequest type.

    // This defines the SecurityChecker's configuration schema.
    // This is also the recommended naming scheme:
    // <YourComponentName>::Schema.
    //
    // Defining the schema should happen in an `initialize()` method,
    // not directly in the constructor. There should also be two
    // constructors: a default one that calls `finalize()`,
    // and one that takes a boolean and does not call `finalize()`.
    // This is to allow subclassing, which the HappyDnsQuerier example
    // will cover.
    class Schema: public ConfigKit::Schema {
    private:
        void initialize() {
            using namespace ConfigKit;
            add("db_path", STRING_TYPE, REQUIRED);
            add("url", STRING_TYPE, REQUIRED);
            add("timeout", INT_TYPE, OPTIONAL, 60);
        }

    public:
        Schema() {
            initialize();
            finalize();
        }

        Schema(bool _subclassing) {
            initialize();
        }
    };

    // This defines the SecurityChecker's configuration change request
    // type. This is also the recommended naming scheme:
    // <YourComponentName>::ConfigChangeRequest
    struct ConfigChangeRequest {
        // In simple low-level components this usually only
        // contains a smart pointer to a ConfigKit::Store.
        boost::scoped_ptr<ConfigKit::Store> config;
    };

protected:
    // This is the internal configuration store. This is also the
    // recommended naming scheme: `config`.
    //
    // We recommend `protected` visibility in order to allow
    // subclasses to access this.
    ConfigKit::Store config;

public:
    // The constructor takes a Schema so that we can initialize the
    // configuration store. It also immediately accepts some
    // initial configuration.
    //
    // You might wonder, how about instead of taking a Schema as a parameter,
    // we define the Schema as a global variable? Well, that would interfere
    // with subclassing; see the HappyDnsQuerier example.
    //
    // This constructor accepts a translator: it is necessary to allow parent
    // components to compose child components. You will see the usage
    // of this constructor in the Downloader example.
    SecurityChecker(const Schema &schema, const Json::Value &initialConfig,
        const Translator &translator)
        : config(schema, initialConfig, translator)
        { }

    // This is the configuration preparation change method, as described
    // in "Configuring a component".
    bool prepareConfigChange(const Json::Value &updates,
        vector<ConfigKit::Error> &errors, ConfigChangeRequest &req)
    {
        // In simple low-level components, it simply merges the current
        // configuration and the given updates, into a new config
        // store that is placed inside `req`. Or if validation fails,
        // it outputs errors.
        req.config.reset(new ConfigKit::Store(config, updates, errors));
        return errors.empty();
    }

    // This is the configuration preparation commit method, as described
    // in "Configuring a component".
    //
    // Note that we use the `BOOST_NOEXCEPT_OR_NOTHROW` macro so that it
    // is compatible with C++03.
    void commitConfigChange(ConfigChangeRequest &req) BOOST_NOEXCEPT_OR_NOTHROW {
        // In simple low-level components, we simply swap the internal
        // config store with the config store in the req object.
        //
        // Recall that commitConfigChange() is required to be infallible:
        // it may not throw. So we encourage you to only perform a bunch of
        // swap() operations and other simple non-fallible operations here.
        // ConfigKit::Store::swap() and most other swap() methods never
        // throw.
        config.swap(*req.config);

        // A less obvious but important thing: swap() places the old
        // configuration store into req. The caller is responsible for
        // destroying req (and thus the old configuration store). So if
        // anything goes wrong during destroying the old configuration
        // store, the exception is thrown outside commitConfigChange.
        // This further satisfies the requirement that commitConfigChange()
        // may not fail.
    }

    // This method inspects the component's configuration.
    Json::Value inspectConfig() const {
        // In simple low-level components, it's as simple as forwarding
        // the call to the configuration store.
        return config.inspect();
    }

    bool check() const {
        // Fictional code here for performing the lookup.
        openDatabase(config["db_path"].asString());
        Entry entry = lookupEntry(config["url"].asString(),
            config["timeout"].asInt());
        closeDatabase();
        return !entry.isNull();
    }
};
~~~

Use SecurityChecker like this:

~~~c++
SecurityChecker::Schema schema;
Json::Value config;
config["db_path"] = "/db";
config["url"] = "http://www.google.com";

// Initiate SecurityChecker with initial configuration
SecurityChecker securityChecker(schema, config);
securityChecker.check();

// Change configuration
vector<ConfigKit::Error> errors;
SecurityChecker::ConfigChangeRequest req;
config["url"] = "http://www.example.com";

if (securityChecker.prepareConfigChange(config, errors, req)) {
    securityChecker.commitConfigChange(req);
} else {
    cout << "Configuration change failed: " << ConfigKit::toString(errors) << endl;
}

// Inspect configuration
cout << "Final configuration:" << endl;
cout << securityChecker.inspectConfig().toStyledString() << endl;
~~~

### DnsQuerier example: a low-level component with post-configuration application operations

DnsQuerier looks up DNS information for a given URL, from multiple DNS servers.

Because the algorithm that DnsQuerier uses is a performance-critical hot path (or so we claim for the sake of the example), it should cache certain configuration values in a variables instead of looking up the config store over and over, because the latter involve unnecessary hash table lookups.

DnsQuerier also logs its progress to a log file. This log file is opened during construction and at the time of configuration change.

This example demonstrates caching of configuration values, and it demonstrates how to perform arbitrary operations necessary for applying a configuration change (in this case, opening a new log file and closing the previous one).

~~~c++
// for std::swap()
 #if __cplusplus >= 201103L
     #include <utility>
 #else
     #include <algorithm>
 #endif
 #include <cstdio>
 #include <cstddef>
 #include <string>
 #include <vector>
 #include <boost/config.hpp>
 #include <boost/scoped_ptr.hpp>
 #include <ConfigKit/ConfigKit.h>
 #include <Exceptions.h>

using namespace std;
using namespace Passenger;

class DnsQuerier {
public:
    // The schema, same as with SecurityChecker.
    class Schema: public ConfigKit::Schema {
    private:
        void initialize() {
            using namespace ConfigKit;
            add("url", STRING_TYPE, REQUIRED);
            add("timeout", INT_TYPE, OPTIONAL, 60);
            add("log_file", STRING_TYPE, REQUIRED);
        }

    public:
        Schema() {
            initialize();
            finalize();
        }

        Schema(bool _subclassing) {
            initialize();
        }
    };

    // When a component is initially created, and every time configuration
    // changes, we create a new "config realization" object. Inside
    // this object we store caches of important config options.
    // We also store any other state closely related to the config values:
    // in this case, an open handle to the log file.
    struct ConfigRealization {
        string url;
        FILE *logStream;

        // Config realization objects are always created from
        // a config store.
        ConfigRealization(const ConfigKit::Store &config)
            : url(config["url"].asString()),
              logStream(fopen(config["log_file"].asCString(), "a"))
        {
            if (logStream == NULL) {
                throw RuntimeException("Cannot open log file "
                    + config["log_file"].asString());
            }
        }

        // Config realization objects own all their fields so it's responsible
        // for them cleaning up.
        ~ConfigRealization() {
            fclose(logStream);
        }

        // Config realization objects must have a working swap()
        // method that does not throw, because commitConfigChange()
        // relies on it.
        void swap(ConfigRealization &other) BOOST_NOEXCEPT_OR_NOTHROW {
            url.swap(other.url);
            std::swap(logStream, other.logStream);
        }
    };

    // Just like with SecurityChecker we define a ConfigChangeRequest.
    struct ConfigChangeRequest {
        // Same as with SecurityChecker.
        boost::scoped_ptr<ConfigKit::Store> config;

        // We extend it with one more field: a smart
        // pointer to a config realization object. This is
        // because during a configuration change we
        // want to create a new one.
        boost::scoped_ptr<ConfigRealization> configRlz;
    };

protected:
    // This is the internal configuration store, same as
    // with SecurityChecker.
    ConfigKit::Store config;

private:
    // This is the internal config realization object. This
    // is also the recommended naming scheme: `configRlz`.
    //
    // Unlike with `config`, there is no reason why subclasses
    // should be able to access this, so it should have the
    // `private` visibility.
    ConfigRealization configRlz;

public:
    // Same as with SecurityChecker, but also initializes
    // the config realization object.
    DnsQuerier(const Schema &schema, const Json::Value &initialConfig,
        const Translator &translator = ConfigKit::DummyTranslator())
        : config(schema, initialConfig, translator),
          configRlz(config)
        { }

    // Same as with SecurityChecker, but also creates a new
    // config realization object from the new config.
    // Note that we only do that if there are no validation errors.
    bool prepareConfigChange(const Json::Value &updates,
        vector<ConfigKit::Error> &errors, ConfigChangeRequest &req)
    {
        req.config.reset(new ConfigKit::Store(config, updates, errors));
        if (errors.empty()) {
            req.configRlz.reset(new ConfigRealization(*req.config));
        }
        return errors.empty();
    }

    // Same as with SecurityChecker, but also swaps the newly-created
    // config realization object with the internal one.
    void commitConfigChange(ConfigChangeRequest &req) BOOST_NOEXCEPT_OR_NOTHROW {
        config.swap(*req.config);
        configRlz.swap(*req.configRlz);
    }

    // Same as with SecurityChecker.
    Json::Value inspectConfig() const {
        return config.inspect();
    }

    string query() {
        // Fictional code here for performing the lookup.
        setSocketTimeout(config["timeout"].asInt());
        for (unsigned i = 0; i < 1000; i++) {
            // Fictional hot code path that requires the url and logStream config
            // options.
            fprintf(configRlz.logStream, "Querying %s (loop %u)\n",
                configRlz.url.c_str(), i);
            queryNextDnsServer(configRlz.url);
            receiveResponse();
        }
        return result;
    }
};
~~~

### HappyDnsQuerier example: subclassing components

Components can be subclassed. A subclass may add additional configuration options and may perform additional config realization.

However, a subclass may not rename the parent's config options, or remove any of them. If you want to do any of that then you should use composition instead. See the Downloader example.

The following example demonstrates HappyDnsQuerier: a DnsQuerier subclass that prints a configurable message whenever `query()` is called.

~~~c++
 // for std::swap()
 #if __cplusplus >= 201103L
     #include <utility>
 #else
     #include <algorithm>
 #endif
 #include <iostream>
 #include <boost/config.hpp>
 #include <boost/scoped_ptr.hpp>
 #include <ConfigKit/ConfigKit.h>

class HappyDnsQuerier: public DnsQuerier {
public:
    // Defines the HappyDnsQuerier's configuration schema. As explained, this schema
    // includes not only options directly pertaining DnsQuerier itself, but also
    // its parent's options. This is done by subclassing the parent's schema.
    class Schema: public DnsQuerier::Schema {
    private:
        void initialize() {
            using namespace ConfigKit;
            add("happy_message", STRING_TYPE, REQUIRED);
        }

    public:
        Schema()
            // We call the parent's constructor that takes a boolean,
            // in order to prevent it from calling `finalize()`.
            // We will do that ourselves after having added our own
            // fields.
            : DnsQuerier::Schema(true)
        {
            initialize();
            finalize();
        }

        Schema(bool _subclassing)
            : DnsQuerier::Schema(true)
        {
            initialize();
        }
    };

    // HappyDnsQuerier has its own config realization object. Its
    // structure is similar to how it was implemented in DnsQuerier.
    //
    // This has got nothing to do with the parent's config realization
    // object. Subclasses do not care at all whether the parent
    // uses config realization or not.
    struct ConfigRealization {
    public:
        string happyMessage;

        ConfigRealization(const ConfigKit::Store &config) {
            happyMessage = config["happy_message"].asString();
        }

        void swap(ConfigRealization &other) BOOST_NOEXCEPT_OR_NOTHROW {
            happyMessage.swap(other.happyMessage);
        }
    };

    struct ConfigChangeRequest {
        // A subclass encapsulates the configuration of its
        // parent component, so its ConfigChangeRequest must
        // have a field for storing the parent's ConfigChangeRequest.
        DnsQuerier::ConfigChangeRequest forParent;

        // This is the subclass's own new config realization
        // object. It has a similar function to the
        // one in DnsQuerier::ConfigChangeRequest.
        boost::scoped_ptr<ConfigRealization> configRlz;
    };

private:
    // The subclass has its own config realization object.
    ConfigRealization configRlz;

public:
    // Same as with DnsQuerier, but also initializes our own
    // private config realization object.
    HappyDnsQuerier(const Schema &schema, const Json::Value &initialConfig,
        const Translator &translator = ConfigKit::DummyTranslator())
        : DnsQuerier(schema, initialConfig, translator),
          configRlz(config)
        { }

    // Same as with DnsQuerier, but also creates our own
    // config realization object (but only if validation passes).
    bool prepareConfigChange(const Json::Value &updates,
        vector<ConfigKit::Error> &errors, ConfigChangeRequest &req)
    {
        if (DnsQuerier::prepareConfigChange(updates, errors, req.forParent)) {
            req.configRlz.reset(new ConfigRealization(*req.forParent.config));
        }
        return errors.empty();
    }

    // Same as with DnsQuerier, but also swaps our own private
    // config realization object.
    void commitConfigChange(ConfigChangeRequest &req) BOOST_NOEXCEPT_OR_NOTHROW {
        DnsQuerier::commitConfigChange(req.forParent);
        configRlz.swap(*req.configRlz);
    }

    // No need to override inspectConfig().

    void query() {
        std::cout << "Hurray! " << configRlz.happyMessage << endl;
        DnsQuerier::query();
    }
};
~~~

### Downloader example: a high-level component that composes subcomponents

`Downloader` is a high-level component for downloading a specific URL. Under the hood it utilizes `SecurityChecker` and `DnsQuerier`. This example demonstrates how to combine its own configuration with the configuration of multiple subcomponents.

Downloader completely *encapsulates* its subcomponents. Users of Downloader don't have to know that the subcomponents exist. Downloader also completely encapsulates subcomponents' configuration: subcomponents can only be configured (and their configuration only inspected) through Downloader.

Because of this, downloader's configuration schema includes not only options directly pertaining Downloader itself, but also includes options pertaining the subcomponents it uses.

Similarly, the Downloader's internal configuration store stores not only the configuration values directly pertaining to Downloader itself, but also (a copy of) the configuration values pertaining to the subcomponents.

Whenever Downloader is configured, it configures subcomponents too. But whenever Downloader's configuration is inspected, it only returns the data from its own configuration store. Because only the Downloader is allowed to configure its subcomponents, we know that the Downloader's internal configuration store contains the most up-to-date values.

#### The special problem of conflicting overlapping configuration names and translation

There is one special problem that deserves attention: a high-level component may not necessarily want to expose their subcomponents' configuration options using the same names, or at all.

For example, both `SecurityChecker` and `DnsQuerier` expose a `timeout` option, but they are *different* timeouts, and are even distinct from the Downloader's own download timeout. To solve this special problem of **conflicting overlapping configuration names**, we utilize a **translation system**. We define how the Downloader's configuration keys are to be mapped a specific subcomponent's configuration keys. We obviously can't define the entire mapping, because that would return us to the original problem of having to manually write so much repeated code. There are several ways to deal with this, such as:

 - Assuming that most options don't have to be renamed, and only define exceptions to this rule. This is the approach that is demonstrated in this example. The `ConfigKit::TableTranslator` class implements this translation strategy.
 - Prefixing the subcomponents' options. The `ConfigKit::PrefixTranslator` class implements this translation strategy.

In this Downloader example, we will demonstrate TableTranslator only.

#### Code example

~~~c++
 #include <ConfigKit/ConfigKit.h>
 #include <ConfigKit/TableTranslator.h>
 #include <ConfigKit/SubComponentUtils.h>

class Downloader {
public:
    // Defines the Downloader's configuration schema. As explained, this schema
    // includes not only options directly pertaining Downloader itself, but also
    // options pertaining the subcomponents.
    class Schema: public ConfigKit::Schema {
    private:
        void initialize() {
            using namespace ConfigKit;

            // Here we define how to map Downloader configuration keys to
            // SecurityChecker configuration keys. We add the SecurityChecker's
            // configuration schema to our own, taking into account the
            // translations.
            //
            // Note that everything not defined in these translation mapping
            // definitions are simply not translated (i.e. left as-is). So
            // the Downloader's "url" option maps directly to SecurityChecker's
            // "url" option.
            securityChecker.translator.add("security_checker_db_path", "db_path");
            securityChecker.translator.add("security_checker_timeout", "timeout");
            securityChecker.translator.finalize();
            addSubSchema(securityChecker.schema, securityChecker.translator);

            // Ditto for HappyDnsQuerier.
            happyDnsQuerier.translator.add("dns_timeout", "timeout");
            happyDnsQuerier.translator.add("dns_query_log_file", "log_file");
            happyDnsQuerier.translator.finalize();
            addSubSchema(happyDnsQuerier.schema, happyDnsQuerier.translator);

            // Here we define Downloader's own configuration keys.
            add("download_timeout", INT_TYPE, OPTIONAL, 60);
        }

    public:
        // For each subcomponent that Downloader uses, we define a struct member
        // that contains that subcomponent's schema, as well as a translation table
        // for mapping Downloader's config keys to the subcomponent's config keys.
        struct {
            SecurityChecker::Schema schema;
            ConfigKit::TableTranslator translator;
        } securityChecker;
        struct {
            HappyDnsQuerier::Schema schema;
            ConfigKit::TableTranslator translator;
        } happyDnsQuerier;

        Schema() {
            initialize();
            finalize();
        }

        Schema(bool _subclassing) {
            initialize();
        }
    };

    // Downloader manages the configurations of its subcomponents, so
    // Downloader::ConfigChangeRequest also contains
    // ConfigChangeRequest objects pertaining to its subcomponents.
    struct ConfigChangeRequest {
        SecurityChecker::ConfigChangeRequest forSecurityChecker;
        HappyDnsQuerier::ConfigChangeRequest forHappyDnsQuerier;
        boost::scoped_ptr<ConfigKit::Store> config;
    };


protected:
    // The internal configuration store. As explained, this also contains (a
    // copy of) the configuration options pertaining SecurityChecker and the
    // DnsQuerier.
    //
    // This MUST be declared before the subcomponents because
    // the construction of subcomponents depends on this object.
    ConfigKit::Store config;

private:
    // The subcomponents used by Downloader.
    SecurityChecker securityChecker;
    HappyDnsQuerier happyDnsQuerier;

public:
    // The constructor creates subcomponents, passing to them the effective
    // configuration values from our internal configuration store,
    // as well as corresponding translators.
    //
    // Why do we pass effective values instead of `initialConfig()`?
    // It is because the parent component may define different default
    // values than subcomponents. By passing effective values,
    // such default value overrides are respected.
    Downloader(const Schema &schema, const Json::Value &initialConfig,
        const Translator &translator = ConfigKit::DummyTranslator())
        : config(schema, initialConfig, translator),
          securityChecker(schema.securityChecker.schema,
              config.inspectEffectiveValues(),
              schema.securityChecker.translator),
          happyDnsQuerier(schema.happyDnsQuerier.schema,
              config.inspectEffectiveValues(),
              schema.happyDnsQuerier.translator)
        { }

    bool prepareConfigChange(const Json::Value &updates,
        vector<ConfigKit::Error> &errors, ConfigChangeRequest &req)
    {
        const Schema &schema = static_cast<const Schema &>(config.getSchema());

        // We first merge updates into Downloader's own new config store.
        req.config.reset(new ConfigKit::Store(config, updates, errors));

        // Next, we call prepareConfigChange() on our subcomponents,
        // passing it the effective values from Downloader's config store.
        // ConfigKit provides a utility method for doing that.
        //
        // This utility method takes a translator and takes care of
        // translating config options and errors, as defined in the
        // schema. This utility method assumes that the subcomponent
        // implements the prepareConfigChange() method and defines
        // the ConfigChangeRequest type.
        ConfigKit::prepareConfigChangeForSubComponent(
            securityChecker, schema.securityChecker.translator,
            req.config->inspectEffectiveValues(),
            errors, req.forSecurityChecker);
        ConfigKit::prepareConfigChangeForSubComponent(
            happyDnsQuerier, schema.happyDnsQuerier.translator,
            req.config->inspectEffectiveValues(),
            errors, req.forHappyDnsQuerier);

        // Because Downloader's schema also contains subcomponents'
        // schemas, some validations may be performed multiple times,
        // resulting in duplicate error messages. Always call
        // ConfigKit::deduplicateErrors() at the end to get rid of
        // duplicates.
        errors = ConfigKit::deduplicateErrors(errors);

        return errors.empty();
    }

    void commitConfigChange(ConfigChangeRequest &req) BOOST_NOEXCEPT_OR_NOTHROW {
        // This call here is similar to SecurityChecker and (Happy)DnsQuerier.
        config.swap(*req.config);

        // We also need to call commitConfigChange() on our subcomponents.
        securityChecker.commitConfigChange(req.forSecurityChecker);
        happyDnsQuerier.commitConfigChange(req.forHappyDnsQuerier);
    }

    // As explained, simply returning our internal configuration store
    // is enough to expose all the subcomponents' configuration values too.
    Json::Value inspectConfig() const {
        return config.inspect();
    }

    void download() {
        // Fictional code here for performing the download
        cout << "Downloading " << config["url"].asString() << endl;
        securityChecker.check();
        happyDnsQuerier.query();

        openSocket(config["url"].asString());
        sendRequest();
        receiveData(config["download_timeout"].asInt());
        closeSocket();
    }
};
~~~

### Main function example

To close this section, here is an example of how the main function would look like to utilize the aforementioned components:

~~~c++
int
main() {
    // Set configuration
    Json::Value config;
    config["url"] = "http://www.google.com";
    config["security_checker_db_path"] = "/db";
    config["dns_query_log_file"] = "dns.log";
    config["dns_timeout"] = 30;
    config["download_timeout"] = 20;

    // Instantiate and print schema
    Downloader::Schema schema;
    cout << "Configuration schema:" << endl;
    cout << schema.inspect().toStyledString() << endl;

    // Instantiate a Downloader and perform a download
    Downloader downloader(schema, config);
    downloader.download();

    // Change configuration, perform another download
    cout << "Changing configuration" << endl;
    Downloader::ConfigChangeRequest req;
    vector<ConfigKit::Error> errors;
    config["url"] = "http://www.slashdot.org";

    if (downloader.prepareConfigChange(config, errors, req)) {
        downloader.commitConfigChange(req);
    } else {
        cout << "Configure failed! " << Passenger::toString(errors) << endl;
    }
    downloader.download();

    // Print final configuration
    cout << "Final configuration:" << endl;
    cout << downloader.inspectConfig().toStyledString() << endl;

    return 0;
}
~~~

## Asynchronous examples

The above examples demonstrate how things would work with synchronous components. What about components that deal with asynchronous I/O? They usually run inside an event loop, and all access to their internal data should be performed from the event loop.

Our recommendation is that you implement synchronous as well as asynchronous versions of the `prepareConfigChange()`, `commitConfigChange()`, and `inspectConfig()` methods. The synchronous versions must be called from the event loop. The asynchronous versions...

 - accept a callback which is to be called with the result.
 - schedule an operation via the event loop the perform the equivalent synchronous operation and call the callback.
 - are to be thread-safe.

The following example demonstrates how SecurityChecker would look like if introduces asynchronous methods as described above. Other classes, like Downloader, should also be modified in a similar manner.

~~~c++
 #include <ConfigKit/AsyncUtils.h>

class SecurityChecker {
public:
    // No change from synchronous version.
    class Schema: public ConfigKit::Schema {
    private:
        void initialize() {
            using namespace ConfigKit;
            add("db_path", STRING_TYPE, REQUIRED);
            add("url", STRING_TYPE, REQUIRED);
            add("timeout", INTEGER_TYPE, OPTIONAL, 60);
        }

    public:
        Schema() {
            initialize();
            finalize();
        }

        Schema(bool _subclassing) {
            initialize();
        }
    };

    // No change from synchronous version.
    struct ConfigChangeRequest {
        boost::scoped_ptr<ConfigKit::Store> config;
    };

private:
    EventLoop &eventLoop;

    // No change from synchronous version.
    ConfigKit::Store config;

public:
    // Only change from synchronous version is that we now accept
    // an event loop object.
    SecurityChecker(const Schema &schema, const Json::Value &initialConfig,
        const EventLoopType &_eventLoop)
        : eventLoop(eventLoop),
          config(schema, initialConfig)
        { }

    SecurityChecker(const Schema &schema, const Json::Value &initialConfig,
        const EventLoopType &_eventLoop,
        const Translator &translator = ConfigKit::DummyTranslator())
        : eventLoop(eventLoop),
          config(schema, initialConfig, translator)
        { }

    // No change from synchronous version.
    bool prepareConfigChange(const Json::Value &updates,
        vector<ConfigKit::Error> &errors, ConfigChangeRequest &req)
    {
        req.config.reset(new ConfigKit::Store(config, updates, errors));
        return errors.empty();
    }

    // No change from synchronous version.
    void commitConfigChange(ConfigChangeRequest &req) BOOST_NOEXCEPT_OR_NOTHROW {
        config.swap(*req.config);
    }

    // No change from synchronous version.
    Json::Value inspectConfig() const {
        return config.inspect();
    }


    /****** Introduction of asynchronous methods below ******/

    // Performs the same thing as the synchronous version, but
    // over the event loop.
    void asyncPrepareConfigChange(const Json::Value &updates,
        ConfigChangeRequest &req,
        const ConfigKit::CallbackTypes<SecurityChecker>::PrepareConfigChange &callback)
    {
        // The exact API depends on which event loop implementation
        // you use. When using libev, use SafeLibev::runLater().
        // When using Boost Asio, use io_service.post().
        //
        // We use the utility function ConfigKit::callPrepareConfigChangeAndCallback.
        // This function assumes that SecurityChecker supports prepareConfigChange().
        eventLoop.threadSafeRunInNextTick(boost::bind(
            ConfigKit::callPrepareConfigChangeAndCallback<SecurityChecker>,
            this, updates, &req, callback));
    }

    // Performs the same thing as the synchronous version, but
    // over the event loop.
    void asyncCommitConfigChange(ConfigChangeRequest &req,
        const ConfigKit::CallbackTypes<SecurityChecker>::CommitConfigChange &callback)
        BOOST_NOEXCEPT_OR_NOTHROW
    {
        // We use the utility function ConfigKit::callCommitConfigChangeAndCallback.
        // This function assumes that SecurityChecker supports commitConfigChange().
        eventLoop.threadSafeRunInNextTick(boost::bind(
            ConfigKit::callCommitConfigChangeAndCallback<SecurityChecker>,
            this, &req, callback));
    }

    // Performs the same thing as the synchronous version, but
    // over the event loop.
    void asyncInspectConfig(const ConfigKit::CallbackTypes<SecurityChecker>::InspectConfig &callback) const {
        // We use the utility function ConfigKit::callInspectConfigAndCallback.
        // This function assumes that SecurityChecker supports inspectConfig().
        eventLoop.threadSafeRunInNextTick(boost::bind(
            ConfigKit::callInspectConfigAndCallback<SecurityChecker>,
            this, callback));
    }

    // ...further fictional code here for performing the lookup...
};
~~~

## Implementation considerations

### Error handling in prepareChangeRequest and commitChangeRequest

If anything *can* go wrong, then the error *should* have been detected by `prepareConfigChange()`. If anything goes wrong within `commitChangeRequest()` then you only have two choices: log and ignore the error, or abort the entire program.

This means that memory allocations, opening files, etc. should be done as much as possible by `prepareConfigChange()`, not `commitConfigChange()`. Ideally `commitConfigChange()` does not allocate any memory or perform any system calls at all.

In some cases `commitConfigChange()` needs to perform cleanup work (such as freeing old data structures or closing the previous log file handle), which may fail. We recommend that you structure your code in such a way that such cleanup is performed by the ConfigChangeRequest destructor. The design patterns outlined in this document guarantee that the ConfigChangeRequest destructor is called outside commitConfigChange.

The examples that we have shown make use of `swap()`, which never fails and does not allocate memory, and also ensures that cleanup work is performed by the ConfigChangeRequest destructor.

### Concurrency

Since preparing and committing a config change are two separate operations, ABA problems can occur. Consider the following timeline:

    1. Thread A: c.prepareConfigChange(..., req1)
    2. Thread B: c.prepareConfigChange(..., req2)
    3. Thread A: c.commitConfigChange(req1)
    4. Thread B: c.commitConfigChange(req2)

Depending on how the data structure for `req1` looks like and how `commitConfigChange()` is implemented, line 4 can end up overwriting the config changes prepared on line 2.

In concurrent environments, one should ensure a serialization of such calls. One should prevent a next prepareConfigChange from running, until a previous preparation has been committed. One way to implement this is by serializing operations with a queue.
