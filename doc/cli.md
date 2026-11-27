# SKOMP Command-Line Reference

`skomp` compiles one R7RS Scheme program to a target object file or an
executable. The command line is MARV's, with a Scheme program where MARV takes
an assembly module; this page says what each option does and where skomp's
differs.

## Synopsis

```sh
skomp [-t TARGDES] [-c] [OPTION]... FILE
```

`FILE` is one Scheme program. The compiler is a whole-program compiler: it
expands the program and every library it imports into a single module, so there
is nothing to compile separately and nothing to link on the Scheme side. A
second input file is an error.

## Options

| Option | Long Option | Argument | Description |
|---|---|---|---|
| `-t` | `--target=TARGDES` | `TARGDES` | Target description name, or a path to a `.mvt` file (default: `cl` on Windows, `cc` elsewhere). |
| `-T` | `--target-dir=DIR` | `DIR` | Add `DIR` to MARV's `.mvt` lookup path, for target descriptions of your own. |
| `-c` | `--compile` | *(none)* | Compile to a target object file (`.c`, `.wo` or `.s`) without linking. |
| `-S` | `--marv-source` | *(none)* | Stop after writing the MARV module (`.mvs`). |
| `-o` | `--output=FILE` | `FILE` | Output pathname. Without it, the output is named after the input and the target. |
| `-O` | `--optimize=LEVEL` | `0`..`3` | Optimization level for the whole pipeline: passed to MARV for the backend C compiler, and to the Scheme compiler as `cp-optimize`. A `cl` target saturates the backend at 2, because MSVC has no `/O3`; the Scheme compiler is told the level as given. Without it, each half keeps its own default. |
| `-D` | `--define=FEATURE` | `FEATURE` | Add `FEATURE` to the features the program is compiled against, as SRFI 138 says `-D` does. `NDEBUG` is the one name that takes something away; see below. |
| `-I` | `--prepend-libdir=DIR` | `DIR` | Prepend a Scheme library search directory. |
| `-A` | `--append-libdir=DIR` | `DIR` | Append a Scheme library search directory. |
| `-P` | `--stack-pages=N` | `N` | The compiled program's root stack, in pages of 65536 eight-byte words (default 128, which is 64 MiB). Minimum 1. |
| `-K` | `--split-funcs=N` | `N` | Passed to MARV: on a `cc` target, cut the generated C into pieces of at most `N` functions. |
| `-v` | `--verbose` | *(none)* | Print each command before running it. |
| `-n` | `--dry-run` | *(none)* | Print the backend commands instead of running them. |
| `-M` | `--makefile-dir=DIR` | `DIR` | Write a self-contained build directory with a makefile instead of running the backend. |
| `-h` | `--help` | *(none)* | Print usage. |
| `-V` | `--version` | *(none)* | Print skomp's version, the commit of each imported part, and the private root in use. |

Options follow POSIX shapes: short flags may be clustered (`-vc`), and a short
option argument may be separate (`-t cc`) or attached (`-tcc`). A long option
takes its argument either joined with `=` (`--target=cc`) or as the next
argument (`--target cc`).
A `--` argument ends the options, and options come before the input file.

## Features and `-D`

`cond-expand`, in the program and in every library it imports, is answered
from the compiler's `cp-features` parameter, which describes the world a
compiled program runs in rather than the interpreter that compiled it. Its
default is

```scheme
(r7rs skint full-unicode debug)
```

and each `-D FEATURE` adds one name to it, which is what SRFI 138's `-D`
means. The names are added in the order they were written, and a name already
there is not added twice. Nothing but the Scheme half sees them.

The same list is what a compiled program's own `(features)` answers: it is
written into the module as a literal, so what `cond-expand` was told and what
the program reports cannot disagree. Nothing of the host or of the target is
added to it -- one MARV module is assembled for any of the targets, so skomp is
in no position to claim an operating system or a byte order, and `-D linux` is
how a caller who knows claims one.

`debug` is in the default, and it is the one name with a visible effect on a
program that mentions no feature at all: `(srfi 145)`'s `assume` elides its
check when neither `assumptions` nor `debug` is a feature, so with the default
an assumption is checked, as `assert` is in a C program compiled without
`NDEBUG`.

`-D NDEBUG` is therefore the exception, and the one name that takes something
away rather than adding it:

- it drops `debug` from the feature list, so the assumptions compile away, and
- it is passed on to MARV as `-D NDEBUG`, which reaches the target compiler
  where the target description's `~D` placeholder says it goes, so the C of
  the runtime is compiled with `NDEBUG` too.

It is matched exactly, as C spells it, and does not itself become a feature.
Since the names are applied in order, `-D NDEBUG -D debug` ends with `debug`
in the list and `NDEBUG` still on MARV's command line.

```sh
skomp -o app app.scm                 # assumptions checked
skomp -D NDEBUG -o app app.scm       # assumptions compiled away
skomp -D my-build -o app app.scm     # (cond-expand (my-build ...) ...)
```

A feature name here is written with letters, digits and `-_.+?!:`; skomp
rejects anything else, because the name travels as one word of a command line
and a name needing quotes is not one a `cond-expand` would test.

## Differences from MARV's command line

| | MARV | skomp |
|---|---|---|
| input | one or more `.mvs` files and object files | exactly one Scheme program |
| `-I` | add a local `.mvh` lookup directory | prepend a Scheme library directory |
| `-L` | add a system `.mvh` and `.c` lookup directory | *(not an option)* |
| `-S` | *(not an option)* | stop after the `.mvs` |
| `-M DIR` | refuses to overwrite a makefile it recognises | refuses unless `DIR` is empty or absent |

MARV's `-I` and `-L` are not exposed because skomp supplies them itself: the only
`.mvh` headers and `.c` adapters in play are the Scheme runtime's, which live in
skomp's own tree. `-I` carries Skint's meaning instead, which is what a Scheme
program needs, and `-L` is not a skomp option at all: a Scheme library directory
is prepended with `-I` or appended with `-A`.

## Operating modes

### Compile and link (the default)

`-o FILE` names the executable: a native binary for `cc`, `cl`, `gcc`, `clang`
and `rv64g`, or a `.wasm` for `wcpl`. skomp writes the MARV module to a
temporary file, hands it to MARV, and removes it afterwards.

```sh
skomp -o app app.scm
skomp -t wcpl -o app.wasm app.scm
```

Without `-o`, the executable is named after the input, beside it, with the
extension its product wears: nothing for a native binary on a Un*x host,
`.exe` on Windows, `.wasm` for a `wcpl` target, and nothing for `rv64g`,
whose product is a binary for another machine whatever host it was built on.
This is what SRFI 138 asks of `compile-r7rs`, which compiles `prog.scm` to
`prog`.

```sh
skomp app.scm                       # app, or app.exe on Windows
skomp -t wcpl app.scm               # app.wasm
```

An input whose name the derivation cannot shorten -- one with no `.scm`
suffix, where the target adds no extension either -- would name its own
output, and skomp asks for `-o` instead of overwriting it.

### Compile only (`-c`)

The program becomes a target object file and nothing is linked. Without `-o`,
the object is written beside the input with the extension the target description
gives (`.c` for `cc`, `.wo` for `wcpl`, `.s` for `rv64g`), which is where MARV
would have put it.

```sh
skomp -c app.scm                    # app.c, for the default cc target
skomp -t wcpl -c -o app.wo app.scm
```

### MARV module only (`-S`)

Stops one step earlier, at the MARV module. Without `-o`, the module is written
beside the input as `FILE.mvs`. This is the form to read when you want to see
what the compiler emitted.

```sh
skomp -S app.scm                    # app.mvs
```

### Makefile directory (`-M DIR`)

MARV writes `DIR` as a self-contained build directory: the generated target
files, the runtime adapters it needs, and a `Makefile` (`nmakefile` for the `cl`
target). `-o` names the executable inside `DIR`, and without it the name is
derived as it is for a plain link. Nothing is compiled.

```sh
skomp -M build -o app app.scm
make -C build
```

On a `cl` target the makefile is an `nmakefile`, so the second line is
`nmake -f nmakefile` run from inside `DIR`.

skomp requires `DIR` to be empty or not yet to exist, and says so otherwise.
MARV cannot make that check: it is written in the C subset WCPL accepts, which
has no way to read a directory, so MARV can only refuse to overwrite a makefile
of the kind it is about to write. `-M` cannot be combined with `-c` or `-S`.

## What skomp arranges for its children

skomp sets four environment variables for the programs it runs, and nothing
else. They apply to those children only.

  * `SKINT_LIBDIRS` is **set**, not extended: the `-I` directories, then
    skomp's own library directory, then the `-A` ones. A value
    inherited from your shell cannot add a library the compiler would then
    serve.
  * `PATH` gains skomp's own `bin` directory at the front, so MARV's stock
    `wcpl.mvt` -- whose link command is plain `wcpl -o ~exe ~src` -- reaches
    skomp's WCPL rather than a public one.
  * `WCPL_INCLUDE_PATH` and `WCPL_LIBRARY_PATH` name the Scheme runtime's
    directory, which is how WCPL resolves `scmi.c`'s include of `scmrt.h` to a
    module. Without them the WebAssembly target cannot build the runtime.

MARV is given the target description as an absolute path, which it uses
directly, so a `target/` directory in the working directory cannot shadow it.
Its `-L` and `-I` directories are **appended** to MARV's own defaults, which
include `lib/`, `libs/` and `./` relative to the working directory; a
`scmrt.mvh`, `scmi.mvh` or `scmn.mvh` sitting in one of those would be preferred
to skomp's. skomp tests for that and refuses rather than building against the
wrong runtime headers.

## Finding its own files

skomp looks for the directory holding `bin/`, `lib/` and `target/` in this
order:

1. `SKOMP_HOME`, if it is set;
2. the path compiled in at build time (`-D SKOMP_HOME_DIR=...`, which the
   Un*x makefile sets to `$(PREFIX)/libexec/skomp` and the nmakefile leaves
   unset unless `SKOMPHOME` is given);
3. `skomp-home` beside the `skomp` executable itself. This is what makes an
   unpacked tree work where it stands, and what `make` leaves behind. Where
   that executable is comes from the system on Windows, and from `argv[0]`
   elsewhere; when `argv[0]` is a bare name, which is what a shell hands over
   after finding skomp on `PATH`, `PATH` is walked for it. A copy with no tree
   beside it is reported as not found, rather than borrowing the tree of
   another copy further along `PATH`.

Paths skomp passes to MARV, and that MARV passes to the backend compiler, are
limited to ASCII letters and digits, space, `_`, `-`, `.`, `/`, `\` and `:`.
That is MARV's rule, and the private root is one of those paths, so skomp cannot
be installed under a directory with other characters in its name.

## Exit status

0 on success. On failure, the exit code of whichever part failed, or 1 when that
cannot be represented. A diagnostic always comes from the part that produced it.
