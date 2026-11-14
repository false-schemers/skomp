# How SKOMP is put together

skomp is not a compiler. It is a cover over four programs, three of which are
whole projects of their own, imported as sources and built into a private
directory the user never names. This page says what each part does and what
passes between them. The command-line reference is [cli.md](cli.md).

## The pipeline

```
   app.scm
      |
      |  skint, hosting (skint compile)
      v
   app.mvs            a MARV module: one module for the whole program
      |
      |  marv
      v
   app.c / app.wo / app.s     plus the runtime's scmrt.c and scmi.c
      |
      |  cc / wcpl / an assembler
      v
   app  or  app.wasm
```

### 1. skomp

`skomp.c`, the only part a user sees. It parses the command line, finds its own
private root, arranges four environment variables, and runs the two steps. It
contains no compiler and no knowledge of Scheme.

It does make two checks that MARV cannot make for itself, both described in
[cli.md](cli.md): that a `-M` directory is empty, and that the working directory
holds no `.mvh` file that would shadow the runtime's own. The first is the
reason skomp reads a directory at all -- MARV is written in the C subset WCPL
accepts, which cannot.

`skomp.c` is held close to that same subset even though it is not compiled by
WCPL: C99, fixed buffers with checked appends, `system()` to run a tool, and
platform tests nested inside `#ifndef __WCPL__` rather than combined with it.
The three parts it drives are held to the subset in earnest, and a cover written
in a different dialect is a cover that drifts.

### 2. skint and `(skint compile)`

The Scheme side is `lib/skomp.ssc`, three lines that import `(skint compile)`
and call `compile-program`. skomp runs it with its own `skint` and with
`SKINT_LIBDIRS` pointing at `lib/`, which holds `(skint compile)` and nothing
else of a library tree.

The compiler expands the program and every library it imports, shakes out what
is unreachable, and writes one MARV module. The libraries it serves are its own,
under `lib/skint/compile/clib/`: they describe the world a standalone
executable provides, not the world the interpreter provides, and the compiler
reads a standard library name from there and from nowhere else. What that world
is and where it stops is documented in
[the library's own README](../lib/skint/compile/README.md).

The hosted `skint` is built with `OPT_TOWER` and `OPT_UNICODE`, and these are
not options:

  * the runtime's strings are arrays of codepoints, so an ASCII host would read
    a multi-byte literal in a source file as several characters and compile a
    different program;
  * the code generator carries 32-bit constants and a `2^30` fixnum modulus,
    which want exact arithmetic to come out as integers.

`OPT_ENHTTY` is absent: skomp never enters a REPL, and the files it would need
are not imported.

### 3. marv

MARV reads the module and writes Restricted C99, RISC-V 64-bit assembly, or an
extended WebAssembly object, according to a target description in `target/`.
Unless `-c` or `-M` is given it then runs the target's own compiler to produce
an executable.

Generated modules name two system modules, `$scmrt` and `$scmi`, and MARV
resolves both to `lib/skint/compile/srt/`, which skomp passes as `-L` and `-I`.
That directory holds the Scheme runtime: the heap and collector, the printer,
the ports, and the helpers the generated instructions call. It belongs to the
compiler rather than to MARV, because the two agree on the object layout bit for
bit; MARV ships a copy of its own, and skomp does not use it.

MARV's own `lib/` -- its C library adapters -- is not imported, because no
generated module has ever named one. `test/run-tests.sh` checks the generated
modules for exactly that, so the day the code generator reaches for `math.vec`
or `stdio` the test says so rather than the build failing in a user's hands.

### 4. wcpl

For the `wcpl` target, MARV's link command is plain `wcpl -o ~exe ~src`. skomp
puts its own `bin/` at the front of `PATH` so that reaches its own WCPL, and
sets `WCPL_INCLUDE_PATH` and `WCPL_LIBRARY_PATH` to the runtime directory so
that WCPL can resolve `scmi.c`'s `#include "scmrt.h"` to a module. WCPL needs no
library files of its own: its headers and libraries are embedded in its binary.

## Why the parts are private

The three helpers are versions, not dependencies. `(skint compile)` and the
`srt` runtime agree on the object layout; the compiler's output is read by a
particular MARV; MARV's WebAssembly output is linked by a particular WCPL. A
machine with a newer `marv` on its `PATH` should not change what skomp produces,
and installing skomp should not change what that `marv` does. So the four are
built together, installed under one root, and found by skomp alone.

The same reasoning is why they live under one root rather than split between
`libexec` and `share`: a private tree that can be half-replaced is a tree that
can be half-replaced by accident.

## Writing the C

The three imported projects are held to the C subset WCPL reads, because each of
them is compiled by WCPL for the WebAssembly target. Three rules bite often
enough to be worth stating here, and all three have broken the WebAssembly build
while the native ones stayed green:

  * every `#if` condition must name `__WCPL__`, and an inactive branch is
    skipped whole, so any other platform test has to **nest** inside a
    `__WCPL__` one rather than stand beside it;
  * no `static` or `extern` declaration inside a function body -- a constant
    table belongs at file scope;
  * an array name does not decay to a pointer in an argument position: pass
    `&buf[0]` or a pointer variable.

Nothing in the usual build or benchmark loop runs WCPL, so none of these shows
up until something asks for the WebAssembly target. `make test` asks, whenever
a wasm host is installed.
