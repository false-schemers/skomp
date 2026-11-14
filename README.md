# skomp

# Whole-program R7RS Scheme compiler

SKOMP compiles an R7RS Scheme program to a native executable, or to a
WebAssembly binary, in one command. It is one executable with no dependencies
beyond a C compiler for the final link, and it does not need a Scheme system
installed: it carries its own.

```
$ skomp -o hello hello.scm
$ ./hello
```

Inside, skomp is four programs rather than one compiler. It runs them in turn
and exposes none of them:

  * **skint** hosts `(skint compile)`, which expands the whole program and every
    library it imports and emits one MARV module;
  * **marv** turns that module into Restricted C99, RISC-V 64-bit assembly, or
    an extended WebAssembly object, and drives the target's own compiler;
  * **wcpl** links the WebAssembly case into a standalone WASI `.wasm`.

The three live in skomp's own directory, not on your `PATH`. A public `skint`,
`marv` or `wcpl` installed on the same machine is neither used nor disturbed,
and neither is a `SKINT_LIBDIRS` set in your shell.

## Installation

On a Un\*x-like system:

```
git clone https://github.com/false-schemers/skomp.git
cd skomp
make
make test
sudo make install
```

`make` builds all four parts and leaves a runnable tree in the repository:
`./skomp` finds `./skomp-home` beside itself, so nothing has to be installed to
try it out. See [INSTALL.md](INSTALL.md) for the install prefix, for Windows,
and for building the parts by hand.

## Use

The command line is MARV's, with a Scheme program where MARV takes an assembly
module. The full reference is [doc/cli.md](doc/cli.md); the common cases are

```
$ skomp -o app app.scm                 # a native executable
$ skomp -t wcpl -o app.wasm app.scm    # a WASI WebAssembly binary
$ skomp -t rv64g -o app app.scm        # RISC-V 64-bit
$ skomp -c app.scm                     # a target object file, no link
$ skomp -S app.scm                     # stop at the MARV module, app.mvs
$ skomp -M build -o app app.scm        # a self-contained build directory
```

`-I DIR` and `-A DIR` add directories where your own `.sld` libraries live,
prepended and appended to skomp's own as in Skint. Programs are compiled whole,
so skomp takes one program file and there is no separate compilation step and
nothing to link on the Scheme side.

## What a compiled program gets

A compiled program runs against a narrower library set than Skint's, written for
a standalone executable rather than an interpreter. It is documented in full in
[the compiler library's own README](lib/skint/compile/README.md). In outline:

  * fixnums are 30 bit, flonums are doubles, and there is no numeric tower: a
    literal outside that range is reported at compile time rather than rounded
  * strings are sequences of Unicode codepoints
  * `(scheme eval)`, `(scheme load)` and `(scheme repl)` are not provided, and
    an import of one fails as unprovided
  * `(scheme complex)` is a shadow over reals; the two constructors that could
    only answer with a complex number report instead
  * a non-recoverable error -- the car of a number, a vector index out of range
    -- reports and exits 1

Every standard library a compiled program may import has a file under
`lib/skint/compile/clib/`, and that directory is the only place the compiler
reads a library from. Nothing is taken from the host.

## Repository layout

  * `skomp.c` -- the cover: the command line, finding its own files, and running
    the other three. It has no compiler in it.
  * `skint/`, `marv/`, `wcpl/` -- the three sister programs' sources, copied in.
    `pre/README.md` says which versions, and `pre/import.ssc` is what copies
    them.
  * `lib/` -- `(skint compile)` with its `clib/` library tree and its `srt/`
    runtime, and `skomp.ssc`, the three-line driver that runs the compiler.
  * `target/` -- MARV target descriptions (`cc`, `cl`, `clang`, `gcc`, `rv64g`,
    `rv64g-cross`, `wcpl`).
  * `doc/` -- the pipeline and the command-line reference.
  * `test/` -- end-to-end tests.

## Related projects

  * [SKINT](https://github.com/false-schemers/skint) -- the interpreter, and
    where `(skint compile)` is developed
  * [MARV](https://github.com/false-schemers/marv) -- the portable assembly
    intermediate language
  * [WCPL](https://github.com/false-schemers/wcpl) -- the C compiler and linker
    targeting WebAssembly
