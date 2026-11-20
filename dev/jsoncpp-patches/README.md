# Passenger's jsoncpp modifications

These patches turn upstream jsoncpp's amalgamated source into the copy in `src/cxx_supportlib/vendor-modified/jsoncpp`. They were generated against jsoncpp 1.9.8, one patch per concern, so that a conflict during an upgrade points at a single modification.

## Upgrading jsoncpp

1. Download the new sources from https://github.com/open-source-parsers/jsoncpp/releases

2. Extract the tarball.

   This produces `dist/jsoncpp.cpp`, `dist/json/json.h` and `dist/json/json-forwards.h`.

3. Copy those three files (without the dist or json directories) into `src/cxx_supportlib/vendor-modified/jsoncpp`.

4. Apply the patches from the Passenger root:

   ```bash
   git apply --directory=src/cxx_supportlib/vendor-modified/jsoncpp dev/jsoncpp-patches/*.patch
   ```

5. If a patch doesn't apply, add `--3way` or `--reject` and resolve by hand. Check for any new `namespace Json` blocks upstream added, since the namespace patch must wrap all of them in `namespace Passenger`.

   Regenerate these patches against the new release so the next upgrade starts from a clean base. One way: commit the pristine amalgamation in a scratch git repository, commit each modification separately, and run `git format-patch --zero-commit --no-signature`.
