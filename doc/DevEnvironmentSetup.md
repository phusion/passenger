# Development environment setup

## Basic requirements

- A C/C++ compiler toolchain
    - Ubuntu: `apt install build-essential`
- Ruby, including development headers
    - Ubuntu: `apt install ruby ruby-dev`
- For working on Node.js support: Node.js and NPM
    - Ubuntu: `apt install nodejs`
- For working on the Apache module: Apache and its development headers
    - Ubuntu: `apt install apache2 apache2-dev`
- For working on the Nginx module: Nginx and its source code

## Setup test suites

Install the testing dependencies:

- `bundle install`
- To run the Node.js test suite: `npm install`

Configure the C++ unit test suite:

- Create `test/config.json` (see the `.example` file)

## C++ development tooling recommendations

## Clangd support

[Clangd](https://clangd.llvm.org/) is great for IDE/editor auto-complete and symbol navigation.

Use [bear](https://github.com/rizsotto/bear) to generate `compile_commands.json` once:

```bash
bear -- drake -j4 nginx apache2 test:cxx:build buildout/test/oxt/main
```

## Build caching

Use [ccache](https://ccache.dev/). Set this environment variable to make our build system prefix all compiler invocations with `ccache`:

```
export USE_CCACHE=1
```

## C++ debugging

- If using gdb to debug the C++ test suite: create `test/.gdbinit` (see `test/gdbinit.example`).
- When using Visual Studio Code, we recommend the [CodeLLDB](https://marketplace.visualstudio.com/items?itemName=vadimcn.vscode-lldb) extension.
