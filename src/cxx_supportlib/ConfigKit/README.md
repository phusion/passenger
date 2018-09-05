# About ConfigKit

ConfigKit is a configuration management system that lets you define configuration keys and store configuration values, and plays well with JSON.

**Table of contents:**

<!-- MarkdownTOC levels="1,2,3,4" autolink="true" bracket="round" -->

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
    - [Types](#types)
    - [Flags](#flags)
  - [Defining default values](#defining-default-values)
  - [Defining custom validators](#defining-custom-validators)
  - [Nested schemas](#nested-schemas)
  - [Inspecting the schema](#inspecting-the-schema)
- [Using the store](#using-the-store)
  - [Putting data in the store](#putting-data-in-the-store)
    - [Updating data](#updating-data)
  - [Unregistered keys are ignored](#unregistered-keys-are-ignored)
  - [Deleting data](#deleting-data)
  - [Fetching data](#fetching-data)
  - [Default values](#default-values)
  - [Inspecting all data](#inspecting-all-data)
  - [Inspection filters](#inspection-filters)
- [Normalizing data](#normalizing-data)
  - [Normalization example 1](#normalization-example-1)
  - [Normalization example 2](#normalization-example-2)
  - [Normalizers and validation](#normalizers-and-validation)
- [ConfigKit in practice & design patterns](#configkit-in-practice--design-patterns)

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

Another challenge pertains having different ways to configure a component. Should components be configured using getters and setters for each option? Or should they have a single method that accepts a struct (or some kind of key-value map) that specifies multiple options? Should components be configurable at all after construction? It would be great if we have a unified answer for all components in Passenger.

ConfigKit provides this unified answer:

 * Components should be configurable at any time after construction. Motivation: we want to allow configuration reloads without having to restart Passenger.
 * Components should be configurable through a single method that accepts a JSON object. Motivations:

    - Applying a configuration change is potentially expensive, so you want to batch multiple configuration changes into a single transaction.
    - Configuration parameters often originate from an I/O channel. Using a struct to store all the different configuration options results in a lot of repeated code, like this:

          config.foo1 = userInput["foo1"];
          config.foo2 = userInput["foo2"];
          config.foo3 = userInput["foo3"];

      So we want some kind of key-value data structure. JSON is a pretty popular format nowadays, and is likely to be the format used in the I/O channel, so may as well optimize for JSON.

    - JSON's various data types allow us to easily describe complex configuration settings.
    - JSON is a widely-accepted format.

ConfigKit backs these answers with code that helps you implement these principles, as well as with [documented design patterns](IN_PRACTICE.md) that provide guidance.

## Status inside the Passenger codebase

At the time of writing (25 Feb 2017), ConfigKit was just introduced, so these practices and patterns documented in this README aren't yet used everywhere throughout the Passenger codebase, but the long-term plan is to adopt these practices/patterns throughout the entire codebase.

## Features and class overview

### ConfigKit::Schema

Everything starts with `ConfigKit::Schema`. This is a class that lets you define a schema of supported configuration keys, their types and other properties like default values. Default values may either be static or dynamically calculated. The type information defined in a `ConfigKit::Schema` allows data validation against the schema.

### ConfigKit::Store

`ConfigKit::Store` is a class that stores configuration values in such a way that it respects a schema. The values supplied to and stored in `ConfigKit::Store` are JSON values (i.e. of the `Json::Value` type), although Store uses the schema to validate that you are actually putting the right JSON types in the Store.

`ConfigKit::Store` also keeps track of which values are explicitly supplied and which ones are not.

### Translators

And finally there are "translator" classes: `ConfigKit::TableTranslator` and `ConfigKit::PrefixTranslator`. The role of translators are described in `IN_PRACTICE.md`, section "The special problem of conflicting overlapping configuration names and translation".

## Using the schema

### Defining the schema

Start using ConfigKit by defining a schema. There are two ways to do this. The first one is to simply create ConfigKit::Schema object and adding definitions to it with `schema.add(name, type, flags, [default value])`:

~~~c++
ConfigKit::Schema schema;

// A required string key.
schema.add("foo", ConfigKit::STRING_TYPE, ConfigKit::REQUIRED);

// An optional integer key without default value.
schema.add("bar", ConfigKit::FLOAT_TYPE, ConfigKit::OPTIONAL);

// An optional integer key, with default value 123.
schema.add("baz", ConfigKit::INTEGER_TYPE, ConfigKit::OPTIONAL, 123);

// An optional string key, without default value, marked as containing a secret.
schema.add("password", ConfigKit::STRING_TYPE, ConfigKit::OPTIONAL | ConfigKit::SECRET);

// Call this when done, otherwise the object cannot be used yet.
schema.finalize();
~~~

The second one, and the one we recommend, is to subclass ConfigKit::Schema and to add the definitions inside the subclass's constructor. It will become apparent in `IN_PRACTICE.md`, section "Synchronous examples" why this is the recommended approach.

~~~c++
struct YourSchema: public ConfigKit::Schema {
    YourSchema() {
        using namespace ConfigKit;
        add("foo", STRING_TYPE, REQUIRED);
        add("bar", FLOAT_TYPE, OPTIONAL);
        add("baz", INT_TYPE, OPTIONAL, 123);
        add("password", STRING_TYPE, OPTIONAL | SECRET);
        finalize();
    }
};

// Instantiate it:
YourSchema schema;
~~~

#### Types

The following types are available:

 * `STRING_TYPE` -- a string.
 * `INT_TYPE` -- a signed integer.
 * `UINT_TYPE` -- an unsigned integer.
 * `FLOAT_TYPE` -- a floating point number.
 * `BOOL_TYPE` -- a boolean.
 * `ARRAY_TYPE` -- usually a generic array (which may contain any values). But with a special `add()` invocation this can indicate an array of [nested ConfigKit schemas](#nested-schemas).
 * `STRING_ARRAY_TYPE` -- an array of strings.
 * `OBJECT_TYPE` -- usually a generic JSON object (which may contain any values). But with a special `add()` invocation this can indicate a map of [nested ConfigKit schemas](#nested-schemas).
 * `ANY_TYPE` -- any JSON value.

#### Flags

 * `REQUIRED` -- this field is required. Mutually exclusive with `OPTIONAL`.
 * `OPTIONAL` -- this field is optional. Mutually exclusive with `REQUIRED`.
 * `CACHE_DEFAULT_VALUE` -- use in combination with [dynamic default values](#defining-default-values). When this flag is set, the value returned by the dynamic value function is cached so that the function won't be called over and over again.
 * `READ_ONLY` -- this field can only be set once. Only the first `ConfigKit::Store::update()` call actually updates the value; subsequent calls won't. Learn more about `update()` in [Using the store -- Putting data in the store](#putting-data-in-the-store).
 * `SECRET` -- this field contains a secret. `ConfigKit::Store::previewUpdate()` and `ConfigKit::Store::inspect()` will filter out the values of such fields. Learn more this in [Using the store -- Inspecting all data](#inspecting-all-data).

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

Miscellaneous notes about custom validators:

 - They are always run, even if the normal type validation fails. For example if the caller tries to set the "foo" key to an array value (which is incompatible with the string type), then `myValidator` will still be called.
 - All registered validators are called. A validator cannot prevent other validators from running.

### Nested schemas

> **Warning:** do not confuse nested schemas with **subschemas**. The latter is a mechanism for composing multiple schemas together, not a way to define the type of a field.

It is possible to define a config field that is an array of objects, or a string map of objects, that should conform to a certain schema. We call such a schema a **nested schema**. For example:

~~~c++
ConfigKit::Schema personSchema;
personSchema.add("name", STRING_TYPE, REQUIRED);
personSchema.add("age", INT_TYPE, OPTIONAL);
personSchema.finalize();

ConfigKit::Schema mainSchema;
mainSchema.add("people", ARRAY_TYPE, personSchema, REQUIRED);
mainSchema.add("frobnicate", BOOL_TYPE, OPTIONAL, false);
mainSchema.finalize();
~~~

A field with a nested schema is defined by calling `add(<field name>, ARRAY_TYPE or OBJECT_TYPE, <nested schema object>, <flags>)`.

The above example mainSchema says the following format is valid:

~~~javascript
{
  "people": [
    {
      "name": "John",
      "age": 12 // optional
    },
    {
      "name": "Jane"
    },
    // ...
  }
  ],
  "frobinate": boolean-value
}
~~~

Nested schema fields simply accept corresponding JSON values, like this:

~~~c++
Json::Value initialValue;
initialValue["people"][0]["name"] = "John";
initialValue["people"][0]["age"] = 12;
initialValue["people"][1]["name"] = "Jane";
ConfigKit::Store store(mainSchema, initialValue);
~~~

Note that it is not possible to define a nested schema field with a default value. This is because the default values are taken from the nested schema. For example, take this code which inserts an empty object into the "people" array:

~~~c++
ConfigKit::Schema personSchema;
personSchema.add("name", STRING_TYPE, OPTIONAL, "anonymous");
personSchema.finalize();

ConfigKit::Schema mainSchema;
mainSchema.add("people", ARRAY_TYPE, personSchema, REQUIRED);
mainSchema.finalize();

Json::Value initialValue;
initialValue["people"][0] = Json::objectValue;
ConfigKit::Store store(mainSchema, initialValue);
~~~

Because personSchema defines a default value of "anonymous" on its "name" field, the above code would result in the following effective values:

~~~json
{
  "people": [
    { "name": "anonymous" }
  ]
}
~~~

### Inspecting the schema

You can inspect the schema using the `inspect()` method. It returns a Json::Value in the following format:

~~~json
{
  "foo": {
    "type": "string",
    "required": true,
    "has_default_value": "static",
    "default_value": "hello"
  },
  "bar": {
    "type": "float"
  },
  "baz": {
    "type": "integer"
  },
  "password": {
    "type": "string",
    "secret": true
  },
  "people": {
    "type": "array",
    "nested_schema": {
      "name": {
        "type": "string"
      },
      "age": {
        "type": "integer"
      }
    }
  }
}
~~~

Description of the members:

 - `type`: the schema definition's type. Could be one of "string", "integer", "unsigned integer", "float", "boolean", "array", "array of strings", "object" or "any".
 - `required`: whether this key is required.
 - `has_default_value`: "static" if a static a default value is defined, "dynamic" if a dynamic default value is defined.
 - `default_value`: the static default value. This field is absent when there is no default value, or if the default value is dynamic.
 - `nested_schema`: if this field has a [nested schema](#nested-schemas), then a description of that nested schema is stored here.

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

You can fetch an overview of all data in the store using `inspect()`. This function is normally used to allow users of a component to inspect the configuration options set for that component, without allowing them direct access to the embedded store.

This function will return a Json::Value in the following format:

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
  },
  "password": {
    "user_value": "[FILTERED]",
    "effective_value": "[FILTERED]",
    // ...members from ConfigKit::Schema::inspect() go here...
  }
}
~~~

Description of the members:

 - `user_value`: the value as explicitly set in the store. If null then it means that the value isn't set.
 - `default_value`: the default value as defined in the schema. May be absent.
 - `effective_value`: the effective value, i.e. the value that `get()` will return.

Note that `inspect()` filters out the values of fields with the `SECRET` flag (by setting the returned value to `"[FILTERED]"`), except for null values.

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

### Inspection filters

Since `inspect()` is usually used to allow users of a component to inspect that component's configuration, you may run into situations where you want the inspected return value to be different from its actual value. Inspection filters allow you to transform `inspect()` results.

A typical use case for inspection filters can be found in LoggingKit. One can configure LoggingKit to log to a specific file descriptor of an open log file. The format is like this:

~~~json
{
  "path": "/foo.log",
  "fd": 12
}
~~~

LoggingKit will internally take over ownership of the file descriptor and will perform a bunch of actions on the fd, such as redirecting stderr to that fd and closing the original fd. Because of this, the fd value is only valid at the time of configuration; it makes no sense to output the fd value in `inspect()`. Inspect filters allows LoggingKit to filter out the "fd" field when `store.inspect()` is called, but the "fd" field can still be accessed internally by LoggingKit by calling `store["target"]["fd"]`.

An inspect filter is a function takes a value and returns a transformed value. It is installed by calling `setInspectFilter()` on the object returned by `schema.add()`, like this:

~~~c++
static Json::Value filterTargetFd(const Json::Value &value) {
  Json::Value result = value;
  result.removeMember("fd");
  return result;
}

ConfigKit::Schema schema;

schema.add("target", ANY_TYPE, OPTIONAL).
  setInspectFilter(filterTargetFd);
schema.finalize();
~~~

Note that inspect filter is called *after* [normalizers](#normalizing-data), so `value` refers to a normalized value.

## Normalizing data

You sometimes may want to allow users to supply data in multiple formats. Normalizers allow you to transform user-supplied data in a canonical format, so that your configuration value handling code only has to deal with data in the canonical format instead of all possibly allowed formats.

The examples below demonstrate two possible use cases and how to implement them.

### Normalization example 1

Suppose that you have a "target" config option, which can be in one of these formats:

 1. `"/filename"`
 2. `{ "path": "/filename" }` (semantics equivalent to 1)
 3. `{ "stderr": true }`

You can write a normalizer that transforms format 1 into format 2. That way, no matter whether the user has actually supplied format 1, 2 or 3, your config handling code only has to deal with format 2 and 3.

A normalizer is a function that accepts a JSON document of effective values (in the format outputted by `ConfigKit::Store::inspectEffectiveValues()`). It is expected to return either Json::nullValue (indicating that no normalization work needs to be done), or a JSON object containing proposed normalization changes.

Normalizers are added to the corresponding schema.

~~~c++
static Json::Value myNormalizer(const Json::Value &effectiveValues) {
    Json::Value updates(Json::objectValue);

    if (effectiveValues["target"].isString()) {
        updates["target"]["path"] = effectiveValues["target"];
    }

    return updates;
}


ConfigKit::Schema schema;

schema.add("target", ANY_TYPE, OPTIONAL);
schema.addValidator(validateThatTargetIsStringOrObject);
schema.addNormalizer(myNormalizer);
schema.finalize();
~~~

### Normalization example 2

Suppose that you have a "security" config option that accepts this format:

~~~json
{
  "username": "a string",          // required
  "password": "a string",          // required
  "level": "full" | "readonly"     // optional; default value: "full"
}
~~~

If the user did not specify "level", then will want to automatically insert "level: full".

~~~c++
static Json::Value myNormalizer(const Json::Value &effectiveValues) {
    Json::Value updates(Json::objectValue);

    if (effectiveValues["security"]["level"].isNull()) {
        updates["security"] = effectiveValues["security"];
        updates["security"]["level"] = "full";
    }

    return updates;
}


ConfigKit::Schema schema;

schema.add("security", OBJECT_TYPE, OPTIONAL);
schema.addValidator(validateSecurity);
schema.addNormalizer(myNormalizer);
schema.finalize();
~~~

### Normalizers and validation

Normalizers are only run when validation passes! That way normalizers don't have to worry about validation problems.

## ConfigKit in practice & design patterns

This README has taught you how to use ConfigKit by itself. But how does ConfigKit fit in the bigger picture? [IN_PRACTICE.md](IN_PRACTICE.md) describes *good practices* and *design patterns* can that can be used throughout the overall Passenger C++ codebase. It provides practical guidance on how to use ConfigKit in the Passenger codebase.
