## Build caching (ccache)

Set this environment variable to make our build system prefix all compiler invocations with [ccache](https://ccache.dev/):

```bash
export USE_CCACHE=1
```

You also need:

```bash
# Required for caching test files (involves precompiled headers)
export CCACHE_SLOPPINESS=pch_defines,time_macros
```

When using multiple worktrees, configure cache sharing:

```bash
export CCACHE_BASEDIR=<full path to common parent directory of the worktrees>
# Prevent debug info from distinguishing between worktrees
export EXTRA_CFLAGS=-fdebug-prefix-map="$PWD"=.
export EXTRA_CXXFLAGS=-fdebug-prefix-map="$PWD"=.
```
