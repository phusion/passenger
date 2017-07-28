# About ConfigKit

ConfigKit is a configuration management system that lets you define configuration keys and store configuration values, and plays well with JSON.

**Table of contents:**

<!-- MarkdownTOC depth=3 autolink="true" bracket="round" -->

- [Motivations](#motivations)
  - [Configuration flow from high-level to low-level with a minimum of repeated code](#configuration-flow-from-high-level-to-low-level-with-a-minimum-of-repeated-code)
  - [Unifying configuration management](#unifying-configuration-management)
- [Status inside the Passenger codebase](#status-inside-the-passenger-codebase)
- [Features and class overview](#features-and-class-overview)
  - [ConfigKit::Schema](#configkitschema)
  - [ConfigKit::Store](#configkitstore)
  - [Translators](#translators)
- [Using the schema](#using-the-schema)
  - [Defining the schema](#defining-the-schema)
  - [Defining default values](#defining-default-values)
  - [Defining custom validators](#defining-custom-validators)
  - [Inspecting the schema](#inspecting-the-schema)
- [Using the store](#using-the-store)
  - [Putting data in the store](#putting-data-in-the-store)
    - [Updating data](#updating-data)
  - [Unregistered keys are ignored](#unregistered-keys-are-ignored)
  - [Deleting data](#deleting-data)
  - [Fetching data](#fetching-data)
  - [Default values](#default-values)
  - [Inspecting all data](#inspecting-all-data)
- [Putting it all together: synchronous version](#putting-it-all-together-synchronous-version)
  - [SecurityChecker example: a configurable, low-level component](#securitychecker-example-a-configurable-low-level-component)
  - [DnsQuerier example: a low-level component with post-configuration application operations](#dnsquerier-example-a-low-level-component-with-post-configuration-application-operations)
  - [Downloader example: a high-level component that combines subcomponents](#downloader-example-a-high-level-component-that-combines-subcomponents)
    - [The special problem of conflicting overlapping configuration names and translation](#the-special-problem-of-conflicting-overlapping-configuration-names-and-translation)
    - [Code example](#code-example)
  - [Main function example](#main-function-example)
- [Putting it all together: asynchronous version](#putting-it-all-together-asynchronous-version)

<!-- /MarkdownTOC -->

## Motivations

### Configuration flow from high-level to low-level with a minimum of repeated code

Passenger is architected as a tree of components. At the top of the tree there are high-level components that implement Passenger business logic, while at the bottom of the tree there are low-level, generic components that are reusable outside Passenger. Higher-level components can encapsulate lower-level components, but not the other way around.

Almost every component can be configured individually. This is fairly easy, but when you put all the components together in Passenger's tree-like architecture, you end up with the following challenge: how do higher-level components expose the configuration of their encapsulated subcomponents, and how do higher-level components allow configuring their encapsulated subcomponents, without repeating a lot of code? Suppose that you have a class A that encapsulates B:

~~~c++
class B {
public:
    int option1;
    int option2;
    int option3;
};

class A {
private:
    B b;
    int option4;

public:
    void setOption1(int val) { b.option1 = val; }
    void setOption2(int val) { b.option2 = val; }
    void setOption3(int val) { b.option3 = val; }
    void setOption4(int val) { b.option4 = val; }
};
~~~

If B has a lot of configuration options, then writing all these setters (and we haven't even looked at getters yet) becomes tiresome quickly. Worse, if there is a component X that encapsulates A, that all the setters in A have to be repeated in X too. And every time you add an option to B, you have to modify A and X too.

How do you prevent having to manually write so much code for exposing and configuring all the options exposed by lower-level components? One possible answer consists of the following aspects:

 - Configuration should be stored in some kind of key-value data structure, e.g. a Json::Value, instead of in individual class members for each option.
 - There must exist the concept of a configuration schema, and this must be introspectable during runtime, so that higher-level components can manage subcomponents' configuration through automation.

ConfigKit implements these aspects, and also provides various other useful features in the area of configuration management. ConfigKit is built around the concept of introspection during *runtime*. This is not the only approach: static introspection and code generation is another valid approach (which is used by some components in Passenger), but this is outside ConfigKit's scope.

### Unifying configuration management

Another challenge pertains having different ways to configure a component. Should components be configured using getters and setters for each option? Or should they have a single method that accepts a struct (or some kind of key-value map) that specifies multiple options? Should components be configurable at all after construction. It would be great if we have a unified answer for all components in Passenger.

ConfigKit provides this unified answer:

 * Components should be configurable at any time after construction. Motivation: we want to allow configuration reloads without having to restart Passenger.
 * Components should be configurable through a single method that accepts a JSON object. Motivations:

    - Applying a configuration change is potentially expensive, so you want to batch multiple configuration changes into a single transaction.
    - Configuration parameters often originate from an I/O channel. Using a struct to store all the different configuration options results in a lot of repeated code, like this:

          config.foo1 = userInput["foo1"];
          config.foo2 = userInput["foo2"];
          config.foo3 = userInput["foo3"];

      So we want some kind of key-value data structure. JSON is a pretty popular format nowadays, and is likely to be the format used in the I/O channel, so may as well optimize for JSON.

ConfigKit backs these answers with code that helps you implement these principles, as well as with documented design patterns that provide guidance.

## Status inside the Passenger codebase

At the time of writing (25 Feb 2017), ConfigKit was just introduced, so these practices and patterns documented in this README aren't yet used everywhere throughout the Passenger codebase, but the long-term plan is to adopt these practices/patterns throughout the entire codebase.

## Features and class overview

### ConfigKit::Schema

Everything starts with `ConfigKit::Schema`. This is a class that lets you define a schema of supported configuration keys, their types and other properties like default values. Default values may either be static or dynamically calculated. `ConfigKit::Schema` also allows data validation against the schema.

### ConfigKit::Store

There is also `ConfigKit::Store`. This is a class that stores configuration values in such a way that it respects a schema.  The values supplied to and stored in `ConfigKit::Store` are JSON values (i.e. of the `Json::Value` type), although Store uses the schema to validate that you are actually putting the right JSON types in the Store.

`ConfigKit::Store` also keeps track of which values are explicitly supplied and which ones are not.

### Translators

And finally there is a "translator" class: `ConfigKit::TableTranslator`. The role of translators are described in the section "The special problem of conflicting overlapping configuration names and translation".

## Using the schema

### Defining the schema

Start using ConfigKit by defining a schema. There are two ways to do this. The first one is to simply create ConfigKit::Schema object and adding definitions to it:

~~~c++
ConfigKit::Schema schema;

// A required string key.
schema.add("foo", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);

// An optional integer key without default value.
schema.add("bar", ConfigKit::FLOAT_TYPE, ConfigKit::OPTIONAL);

// An optional integer key, with default value 123.
schema.add("baz", ConfigKit::INTEGER_TYPE, ConfigKit::OPTIONAL, 123);

// Call this when done, otherwise the object cannot be used yet.
schema.finalize();
~~~

The second one, and the one we recommend, is to subclass ConfigKit::Schema and to add the definitions inside the subclass's constructor. It will become apparent in the section "Putting it all together: synchronous version" why this is the recommended approach.

~~~c++
struct YourSchema: public ConfigKit::Schema {
    YourSchema() {
        using namespace ConfigKit;
        add("foo", STRING_TYPE, REQUIRED);
        add("bar", FLOAT_TYPE, OPTIONAL);
        add("baz", INT_TYPE, OPTIONAL, 123);
        finalize();
    }
};

// Instantiate it:
YourSchema schema;
~~~

Tip: see ConfigKit/Common.h for all supported types and flags.

### Defining default values

The fourth parameter of `add()` is for specifying a static default value.

But ConfigKit also supports *dynamic* default values, by accepting instead of a static default value, a *function object* that returns a default value.

The main use case for dynamic default values is this: to return something based on another value in the configuration store. For example suppose that your schema defines a `connect_timeout` and a `recv_timeout` option. You want `recv_timeout` to default to two times whatever the effective value of `connect_timeout` is. You can define a default value getter function like this:

~~~c++
Json::Value getRecvTimeoutDefaultValue(const ConfigKit::Store &store) {
    return store->get("connect_timeout").getInt() * 2;
}

schema.addWithDynamicDefault("recv_timeout", INT_TYPE, OPTIONAL, getRecvTimeoutDefaultValue);
~~~

### Defining custom validators

ConfigKit::Store validates your data using the type definitions in ConfigKit::Schema. But sometimes you need custom validations of the data in a ConfigKit::Store. For example, "'bar' is required when 'foo' is specified". This sort of logic can be implemented with custom validators.

Custom validators are defined on a ConfigKit::Schema:

~~~c++
static void myValidator(const ConfigKit::Store &config, vector<ConfigKit::Error> &errors) {
    if (!config["foo"].isNull() && config["bar"].isNull()) {
        errors.push_back(ConfigKit::Error("'{{bar}}' is required when '{{foo}}' is specified"));
    }
}


ConfigKit::Schema schema;

schema.add("foo", STRING_TYPE, OPTIONAL);
schema.add("bar", STRING_TYPE, OPTIONAL);
schema.addValidator(myValidator);
schema.finalize();
~~~

### Inspecting the schema

You can inspect the schema using the `inspect()` method. It returns a Json::Value in the following format:

~~~json
{
  "foo": {
    "type": "string",
    "required": true,
    "has_default_value": true
  },
  "bar": {
    "type": "float"
  },
  "baz": {
    "type": "integer"
  }
}
~~~

Description of the members:

 - `type`: the schema definition's type. Could be one of "string", "integer", "unsigned integer", "float" or "boolean".
 - `required`: whether this key is required.
 - `has_default_value`: whether a default value is defined.

## Using the store

Once you have defined a schema you can start using ConfigKit::Store. Create it by referencing the schema that it should use. Note that it internally stores a pointer to the schema, so make sure the schema outlives the store.

~~~c++
ConfigKit::Store store(schema);
~~~

### Putting data in the store

You can populate the store using the `update()` method. The method also performs validation against the schema. The update only succeeds if validation passes.

~~~c++
vector<ConfigKit::Error> errors;
Json::Value updates1;

// Validation fails: 'foo' is missing
store.update(updates1, errors);
// => return value: false
//    errors: 1 item ("'foo' is required")

store.get("foo").isNull();
// => true, because the update failed

errors.clear();
updates1["foo"] = "strval";
store.update(updates1, errors);
// => return value: true
//    errors: empty

store.get("foo").asString();
// => "strval", because the update succeeded
~~~

#### Updating data

Any further calls to `update()` only update the keys that you actually pass to the method, not the keys that you don't pass:

~~~c++
// Assuming we are using the store from 'Putting data in the store'.
Json::Value updates2;

updates2["bar"] = 123.45;
store.update(updates2, errors);  // => true
store.get("foo").asString();     // => still "strval"
store.get("bar").asDouble();     // => 123.45
~~~

### Unregistered keys are ignored

`update()` ignores keys that aren't registered in the schema:

~~~c++
// Assuming we are using the store that went through
// 'Putting data in the store' and 'Updating data'.
Json::Value updates3;

updates3["unknown"] = true;
store.update(updates3, errors); // => true
store.get("foo").asString();    // => still "strval"
store.get("bar").asDouble();    // => still 123.45
store.get("unknown").isNull();  // => true
~~~

### Deleting data

You can delete data by calling `update()` with null values on the keys you want to delete.

~~~c++
// Assuming we are using the store that went through
// 'Putting data in the store' and 'Updating data'.
Json::Value deletionSpec;

deletionSpec["bar"] = Json::nullValue;
store.update(deletionSpec, errors);
// => return value: true
//    errors: empty

store.get("bar").isNull();   // => true
~~~

### Fetching data

Use the `get()` method (of which the `[]` operator is an alias) to fetch data from the store. They both return a Json::Value.

~~~c++
 // Assuming we are using the store that went through
 // 'Putting data in the store' and 'Updating data'.
 store.get("foo").asString();    // => "strval"
 store["foo"].asString();        // => same
~~~

### Default values

If the key is not defined then `get()` either return the default value as defined in the schema, or (if no default value is defined) a null Json::Value.

~~~c++
// Assuming we are using the store that went through
// 'Putting data in the store' and 'Updating data'.
store.get("baz").asInt();       // => 123
store.get("unknown").isNull();  // => true
~~~

### Inspecting all data

You can fetch an overview of all data in the store using `inspect()`.
This will return a Json::Value in the following format:

~~~javascript
// Assuming we are using the store that went through
// 'Putting data in the store' and 'Updating data'.

{
  "foo": {
    "user_value": "strval",
    "effective_value": "strval",
    // ...members from ConfigKit::Schema::inspect() go here...
  },
  "bar": {
    "user_value": 123.45,
    "effective_value": 123.45,
    // ...members from ConfigKit::Schema::inspect() go here...
  },
  "baz": {
    "user_value": null,
    "default_value": 123,
    "effective_value": 123,
    // ...members from ConfigKit::Schema::inspect() go here...
  }
}
~~~

Description of the members:

 - `user_value`: the value as explicitly set in the store. If null then it means that the value isn't set.
 - `default_value`: the default value as defined in the schema. May be absent.
 - `effective_value`: the effective value, i.e. the value that `get()` will return.

If you want to fetch the effective values only, then use `inspectEffectiveValues()`:

~~~javascript
// Assuming we are using the store that went through
// 'Putting data in the store' and 'Updating data'.

{
  "foo": "strval",
  "bar": 123.45,
  "baz": 123
}
~~~

## Putting it all together: synchronous version

Now that you've learned how to use ConfigKit by itself, how does fit in the bigger picture? This section describes good practices and design patterns can that can be used throughout the overall Passenger C++ codebase. At the time of writing (25 Feb 2017), ConfigKit was just introduced, so these practices and patterns aren't yet used everywhere, but the long-term plan is to adopt these practices/patterns throughout the entire codebase.

Let's demonstrate the good practices and design patterns through a number of annotated example classes:

 - `SecurityChecker` checks whether the given URL is secure to connect to, by checking a database of vulnerable sites. This example demonstrates basic usage of ConfigKit in a low-level component.
 - `DnsQuerier` looks up DNS information for a given URL, from multiple DNS servers. Because the algorithm that DnsQuerier uses is a performance-critical hot path (or so we claim for the sake of the example), it should cache certain configuration values in a variables instead of looking up the config store over and over, because the latter involve unnecessary hash table lookups. This example demonstrates caching of configuration values. More generally, it demonstrates how to perform arbitrary operations necessary for applying a configuration change.
 - `Downloader` is a high-level class for downloading a specific URL. Under the hood it utilizes `SecurityChecker` and `DnsQuerier`. This example demonstrates how to combine its own configuration with the configuration of multiple lower-level classes.

### SecurityChecker example: a configurable, low-level component

SecurityChecker checks whether the given URL is secure to connect to, by checking a database of vulnerable sites. This example demonstrates basic usage of ConfigKit in a low-level component.

A configurable component should have the following methods.

 - `previewConfigUpdate()`
 - `configure()`
 - `inspectConfig()`

These methods are further explained in the code example

~~~c++
class SecurityChecker {
private:
    // This is the internal configuration store. This is also the
    // recommended naming scheme: `config`.
    ConfigKit::Store config;

public:
    // This defines the SecurityChecker's configuration schema.
    // This is also the recommended naming scheme:
    // <YourComponentName>::Schema.
    struct Schema: public ConfigKit::Schema {
        Schema() {
            using namespace ConfigKit;
            add("db_path", STRING_TYPE, REQUIRED);
            add("url", STRING_TYPE, REQUIRED);
            add("timeout", INT_TYPE, OPTIONAL, 60);
            finalize();
        }
    };

    // The constructor takes a Schema so that we can initialize the
    // configuration store. It should also immediately accept some
    // initial configuration.
    //
    // You might wonder, how about instead of taking a Schema as a parameter,
    // we define the Schema as a global variable? After all, a
    // ConfigKit::Schema is immutable after finalization, and different
    // instances of <YourComponentName>::Schema contain the same content.
    //
    // Defining as a global variable is *also* a valid approach, but requires
    // you to declare it inside a .cpp file and adding that file to the linker
    // invocation. It's up to you really. I've found taking a schema as a
    // parameter to be the easiest.
    SecurityChecker(const Schema &schema, const Json::Value &initialConfig)
        : config(schema)
    {
        vector<ConfigKit::Error> errors;

        if (!config.update(initialConfig, errors)) {
            throw ArgumentException("Invalid initial configuration: "
                + toString(errors));
        }
    }

    // This method allows checking whether the given configuration updates
    // would result in any validation errors, without actually changing the
    // configuration.
    //
    // Every configurable component should define such a method, because
    // higher-level components need this method in order to make configuration
    // updates across multiple low-level components transactional. You
    // will learn more about this in the Downloader example.
    Json::Value previewConfigUpdate(const Json::Value &updates,
        vector<ConfigKit::Error> &errors)
    {
        // In low-level components, it's as simple as forwarding
        // the call to the configuration store.
        return config.previewUpdate(updates, errors);
    }

    // This method actually configures the component.
    bool configure(const Json::Value &updates, vector<ConfigKit::Error> &errors) {
        // In low-level components, it's as simple as forwarding
        // the call to the configuration store.
        return config.update(updates, errors);
    }

    // This method inspects the component's configuration.
    Json::Value inspectConfig() const {
        // In low-level components, it's as simple as forwarding
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
config["url"] = "http://www.example.com";
if (!securityChecker.configure(config, errors)) {
    cout << "Configuration change failed: " << toString(errors) << endl;
}

// Inspect configuration
cout << "Final configuration:" << endl;
cout << securityChecker.inspectConfig().toStyledString() << endl;
~~~

### DnsQuerier example: a low-level component with post-configuration application operations

DnsQuerier looks up DNS information for a given URL, from multiple DNS servers. Because the algorithm that DnsQuerier uses is a performance-critical hot path (or so we claim for the sake of the example), it should cache certain configuration values in a variables instead of looking up the config store over and over, because the latter involve unnecessary hash table lookups. This example demonstrates caching of configuration values. More generally, it demonstrates how to perform arbitrary operations necessary for applying a configuration change.

~~~c++
class DnsQuerier {
private:
    // The configuration store, same as with SecurityChecker.
    ConfigKit::Store config;

    // Caches the "url" configuration value.
    string url;

    // Populates the cache using values from the configuration store.
    void updateConfigCache() {
        url = config["url"].asString();
    }

public:
    // The schema, same as with SecurityChecker.
    struct Schema: public ConfigKit::Schema {
        Schema() {
            using namespace ConfigKit;
            add("url", STRING_TYPE, REQUIRED);
            add("timeout", INT_TYPE, OPTIONAL, 60);
            finalize();
        }
    };

    // The constructor, same as with SecurityChecker with only one change:
    // it populates the configuration cache before returning.
    DnsQuerier(const Schema &schema, const Json::Value &initialConfig)
        : config(schema)
    {
        vector<ConfigKit::Error> errors;

        if (!config.update(initialConfig, errors)) {
            throw ArgumentException("Invalid initial configuration: "
                + toString(errors));
        }
        updateConfigCache();
    }

    // Same as with SecurityChecker.
    Json::Value previewConfigUpdate(const Json::Value &updates,
        vector<ConfigKit::Error> &errors)
    {
        return config.previewUpdate(updates, errors);
    }

    // Configures the DnsQuerier. Same as with SecurityChecker with only one
    // modification: it populates the configuration cache if updating succeeds.
    bool configure(const Json::Value &updates, vector<ConfigKit::Error> &errors) {
        if (config.update(updates, errors)) {
            updateConfigCache();
            // In addition to updating the config cache, this is
            // the right place for performing any other operations
            // necessary for applying a configuration change.
            return true;
        } else {
            return false;
        }
    }

    // Same as with SecurityChecker.
    Json::Value inspectConfig() const {
        return config.inspect();
    }

    string query() {
        // Fictional code here for performing the lookup.
        setSocketTimeout(config["timeout"].asInt());
        for (unsigned i = 0; i < 1000; i++) {
            // Fictional hot code path that requires the url config option.
            queryNextDnsServer(url);
            receiveResponse();
        }
        return result;
    }
};
~~~

### Downloader example: a high-level component that combines subcomponents

`Downloader` is a high-level class for downloading a specific URL. Under the hood it utilizes `SecurityChecker` and `DnsQuerier`. This example demonstrates how to combine its own configuration with the configuration of multiple lower-level classes.

Downloader completely *encapsulates* its subcomponents. Users that use Downloader don't have to know that the subcomponents exist. Downloader also completely encapsulates subcomponents' configuration: subcomponents can only be configured (and their configuration only inspected) through Downloader.

Because of this, downloader's configuration schema includes not only options directly pertaining Downloader itself, but also includes options pertaining the subcomponents it uses.

Similarly, the Downloader's internal configuration store stores not only the configuration values directly pertaining to Downloader itself, but also (a copy of) the configuration values pertaining to the subcomponents.

Whenever Downloader is configured, it configures subcomponents too. But whenever Downloader's configuration is inspected, it only returns the data from its own configuration store. Because only the Downloader is allowed to configure its subcomponents, we know that the Downloader's internal configuration store contains the most up-to-date values.

#### The special problem of conflicting overlapping configuration names and translation

There is one special problem that deserves attention: a high-level component may not necessarily want to expose their subcomponents' configuration options using the same names. For example, both `SecurityChecker` and `DnsQuerier` expose a `timeout` option, but they are *different* timeouts, and are even distinct from the Downloader's own download timeout. To solve this special problem of **conflicting overlapping configuration names**, we utilize a **translation system**. We define how the Downloader's configuration keys are to be mapped a specific subcomponent's configuration keys. We obviously can't define the entire mapping, because that would return us to the original problem of having to manually write so much repeated code. There are several ways to deal with this, such as:

 - Assuming that most options don't have to be renamed, and only define exceptions to this rule. This is the approach that is demonstrated in this example. The `ConfigKit::TableTranslator` class implements this translation strategy.
 - Prefixing the subcomponents' options. This approach is left as an exercise to the reader.

#### Code example

~~~c++
class Downloader {
private:
    // The subcomponents used by Downloader.
    SecurityChecker securityChecker;
    DnsQuerier dnsQuerier;

    // The internal configuration store. As explained, this also contains (a
    // copy of) the configuration options pertaining SecurityChecker and the
    // DnsQuerier.
    ConfigKit::Store config;

public:
    // Defines the Downloader's configuration schema. As explained, this schema
    // includes not only options directly pertaining Downloader itself, but also
    // options the subcomponents.
    struct Schema: public ConfigKit::Schema {
        // For each subcomponent that Downloader uses, we define a struct member
        // that contains that subcomponent's schema, as well as a translation table
        // for mapping Downloader's config keys to the subcomponent's config keys.
        struct {
            SecurityChecker::Schema schema;
            ConfigKit::TableTranslator translator;
        } securityChecker;
        struct {
            DnsQuerier::Schema schema;
            ConfigKit::TableTranslator translator;
        } dnsQuerier;

        Schema() {
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

            // Ditto for DnsQuerier.
            dnsQuerier.translator.add("dns_timeout", "timeout");
            dnsQuerier.translator.finalize();
            addSubSchema(dnsQuerier.schema, dnsQuerier.translator);

            // Here we define Downloader's own configuration keys.
            add("download_timeout", INT_TYPE, OPTIONAL, 60);
            finalize();
        }
    };

    // The constructor creates subcomponents, passing to them translated
    // versions of the initial configuration passed.
    Downloader(const Schema &schema, const Json::Value &initialConfig)
        : securityChecker(schema.securityChecker.schema,
              schema.securityChecker.translator.translate(initialConfig)),
          dnsQuerier(schema.dnsQuerier.schema,
              schema.dnsQuerier.translator.translate(initialConfig)),
          config(schema)
    {
        vector<ConfigKit::Error> errors;

        if (!config.update(initialConfig, errors)) {
            throw ArgumentException("Invalid initial configuration: "
                + toString(errors));
        }
    }

    void download() {
        // Fictional code here for performing the download

        cout << "Downloading " << config["url"].asString() << endl;
        securityChecker.check();
        dnsQuerier.query();

        openSocket(config["url"].asString());
        sendRequest();
        receiveData(config["download_timeout"].asInt());
        closeSocket();
    }

    // In addition to calling previewUpdate() on the internal configuration
    // store, we also perform similar operations on our subcomponents.
    // We use the `ConfigKit::previewConfigUpdateSubComponent()` utility
    // function to achieve this, passing to it a corresponding translator.
    // This function assumes that the subcomponent implements the
    // `previewConfigUpdate()` method.
    Json::Value previewConfigUpdate(const Json::Value &updates,
        vector<ConfigKit::Error> &errors)
    {
        using namespace ConfigKit;
        const Schema &schema = static_cast<const Schema &>(config.getSchema());

        previewConfigUpdateSubComponent(securityChecker, updates,
            schema.securityChecker.translator, errors);
        previewConfigUpdateSubComponent(dnsQuerier, updates,
            schema.dnsQuerier.translator, errors);
        return config.previewUpdate(updates, errors);
    }

    // In addition to updating the internal configuration store, we also
    // configure the subcomponents. But we only do this after having verified
    // that *all* subcomponents (as well as Downloader itself) successfully
    // validates of the new configuration data. This is how we make
    // `configure()` *transactional*: if we don't do this then we can end up in
    // a situation where one subcomponent is configured, but another is not.
    //
    // We use the `ConfigKit::configureSubComponent()` utility function to
    // achieve this, passing it a corresponding translator. This function
    // assumes that the subcomponent implements the `configure()` method.
    bool configure(const Json::Value &updates, vector<ConfigKit::Error> &errors) {
        using namespace ConfigKit;
        const Schema &schema = static_cast<const Schema &>(config.getSchema());

        previewConfigUpdate(updates, errors);

        if (errors.empty()) {
            configureSubComponent(securityChecker, updates,
                schema.securityChecker.translator, errors);
            configureSubComponent(dnsQuerier, updates,
                schema.dnsQuerier.translator, errors);
            config.update(updates, errors);
        }

        if (errors.empty()) {
            // In addition to updating the subcomponents and the
            // internal configuration store, this is the right
            // place for performing any other operations
            // necessary for applying a configuration change.
        }

        return errors.empty();
    }

    // As explained, simply returning our internal configuration store
    // is enough to expose all the subcomponents' configuration values too.
    Json::Value inspectConfig() const {
        return config.inspect();
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
    vector<ConfigKit::Error> errors;
    config["url"] = "http://www.slashdot.org";
    if (!downloader.configure(config, errors)) {
        cout << "Configure failed!" << endl;
    }
    downloader.download();

    // Print final configuration
    cout << "Final configuration:" << endl;
    cout << downloader.inspectConfig().toStyledString() << endl;

    return 0;
}
~~~

## Putting it all together: asynchronous version

The above examples demonstrate how things would work with synchronous components. What about components that deal with asynchronous I/O? They usually run inside an event loop, and all access to their internal data should be performed from the event loop.

Our recommendation is that you implement synchronous as well as asynchronous versions of the `previewConfigUpdate()`, `configure()`, and `inspectConfig()` methods. The synchronous versions must be called from the event loop. The asynchronous versions...

 - accept a callback which is to be called with the result. 
 - schedule an operation via the event loop the perform the equivalent synchronous operation and call the callback.
 - are to be thread-safe.

The following example demonstrates how SecurityChecker would look like if introduces asynchronous methods as described above. Other classes, like Downloader, should also be modified in a similar manner.

~~~c++
class SecurityChecker {
private:
    EventLoop &eventLoop;

    // No change from synchronous version.
    ConfigKit::Store config;

public:
    // No change from synchronous version.
    struct Schema: public ConfigKit::Schema {
        Schema() {
            using namespace ConfigKit;
            add("db_path", STRING_TYPE, REQUIRED);
            add("url", STRING_TYPE, REQUIRED);
            add("timeout", INTEGER_TYPE, OPTIONAL, 60);
            finalize();
        }
    };

    // Only change from synchronous version is that we now accept
    // an event loop object.
    SecurityChecker(const Schema &schema, const EventLoopType &_eventLoop,
        const Json::Value &initialConfig)
        : eventLoop(eventLoop),
          config(schema)
    {
        // ...code omitted for sake of brevity...
    }

    // No change from synchronous version.
    Json::Value previewConfigUpdate(const Json::Value &updates,
        vector<ConfigKit::Error> &errors)
    {
        // ...code omitted for sake of brevity...
    }

    // No change from synchronous version.
    bool configure(const Json::Value &updates, vector<ConfigKit::Error> &errors) {
        // ...code omitted for sake of brevity...
    }

    // No change from synchronous version.
    Json::Value inspectConfig() const {
        // ...code omitted for sake of brevity...
    }


    /****** Introduction of asynchronous methods below ******/

    // Performs the same thing as the synchronous version, but
    // over the event loop.
    void asyncPreviewConfigUpdate(const Json::Value &updates,
        const ConfigKit::ConfigCallback &callback)
    {
        // The exact API depends on which event loop implementation
        // you use. When using libev, use SafeLibev::runLater().
        // When using Boost Asio, use io_service.post().
        //
        // We use the utility function ConfigKit::callPreviewConfigUpdateAndCallback.
        // This function assumes that SecurityChecker supports previewConfigUpdate().
        eventLoop.threadSafeRunInNextTick(boost::bind(
            ConfigKit::callPreviewConfigUpdateAndCallback<SecurityChecker>,
            this, updates, callback));
    }

    // Performs the same thing as the synchronous version, but
    // over the event loop.
    void asyncConfigure(const Json::Value &updates,
        const ConfigKit::ConfigCallback &callback)
    {
        // We use the utility function ConfigKit::callPreviewConfigUpdateAndCallback.
        // This function assumes that SecurityChecker supports configure().
        eventLoop.threadSafeRunInNextTick(boost::bind(
            ConfigKit::callConfigureAndCallback<SecurityChecker>,
            this, updates, callback));
    }

    // Performs the same thing as the synchronous version, but
    // over the event loop.
    void asyncInspectConfig(const ConfigKit::InspectCallback &callback) const {
        // We use the utility function ConfigKit::callPreviewConfigUpdateAndCallback.
        // This function assumes that SecurityChecker supports inspectConfig().
        eventLoop.threadSafeRunInNextTick(boost::bind(
            ConfigKit::callInspectConfigAndCallback<SecurityChecker>,
            this, callback));
    }

    // ...further fictional code here for performing the lookup...
};
~~~
