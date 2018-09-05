# The wrapper registry

The wrapper registry (the Registry class) describes for which languages Passenger has wrappers available. Each entry (the Entry class) describes:

 * An identifier for this language (ex: `rack`). Related to `passenger_app_type`.
 * The path to the wrapper (ex. `rack-loader.rb`).
 * The title that the spawned process should assume (ex: `Passenger RubyApp`).
 * The default interpreter command for this language (ex: `ruby`). Related to `passenger_ruby`, among others.
 * Zero or more names for the default startup file (ex: `config.ru`, `app.js`, `index.js`). Related to `passenger_startup_file`.

## Synopsis

After construction, one can add entries to the registry. When done, one must call `finalize()` after which no more mutations are allowed. The Registry will contain some built-in, hardcoded entries after construction.

Given a language identifier, the Registry can lookup the corresponding Entry.

~~~c++
Registry reg;
reg.finalize();

const Entry &ruby = reg.lookup("ruby");
cout << ruby.language << endl; // => ruby
cout << ruby.path << endl;     // => rack-loader.rb
                               // etc
~~~

## Properties

The Registry, after finalization, is a static, immutable, in-memory database. Default, built-in, hardcoded entries are inserted during object creation. Registry allows adding additional entries after creation, but this functionality is not actually used and is reserved for the future. So at present, the Registry will only contain a small number of known entries.

After finalization, Registry is thread-safe because of its immutability.

Entries' lifetimes are the same as that of the Registry itself.

## Relationships

The wrapper registry's main use is to be used by the AppTypeDetector, to detect what kind of application lives in a certain directory, and (if applicable) which wrapper should be used.