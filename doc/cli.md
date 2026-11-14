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
| `-o` | `--output=FILE` | `FILE` | Output pathname. Required unless `-c` or `-S` is given. |
| `-O` | — | `0`..`3` | Optimization level for the backend C compiler, passed to MARV. |
| `-I` | `--prepend-libdir=DIR` | `DIR` | Prepend a Scheme library search directory. |
| `-A` | `--append-libdir=DIR` | `DIR` | Append a Scheme library search directory. |
| `-L` | — | `DIR` | Synonym for `-A`. |
| | `--scheme-optimize=N` | `N` | The compiler's own optimization level, 0..3 (default 1). |
| | `--split-funcs=N` | `N` | Passed to MARV: on a `cc` target, cut the generated C into pieces of at most `N` functions. |
| `-v` | `--verbose` | *(none)* | Print each command before running it. |
| `-n` | `--dry-run` | *(none)* | Print the backend commands instead of running them. |
| `-M` | `--makefile-dir=DIR` | `DIR` | Write a self-contained build directory with a makefile instead of running the backend. |
| `-h` | `--help` | *(none)* | Print usage. |
| `-V` | `--version` | *(none)* | Print skomp's version, the commit of each imported part, and the private root in use. |

Options follow POSIX shapes: short flags may be clustered (`-vc`), and an option
argument may be separate (`-t cc`) or attached (`-tcc`, `--target=cc`).

## Differences from MARV's command line

| | MARV | skomp |
|---|---|---|
| input | one or more `.mvs` files and object files | exactly one Scheme program |
| `-I` | add a local `.mvh` lookup directory | prepend a Scheme library directory |
| `-L` | add a system `.mvh` and `.c` lookup directory | append a Scheme library directory |
| `-S` | *(not an option)* | stop after the `.mvs` |
| `-M DIR` | refuses to overwrite a makefile it recognises | refuses unless `DIR` is empty or absent |

MARV's `-I` and `-L` are not exposed because skomp supplies them itself: the only
`.mvh` headers and `.c` adapters in play are the Scheme runtime's, which live in
skomp's own tree. The two letters carry Skint's meanings instead, which is what a
Scheme program needs.

## Operating modes

### Compile and link (the default)

`-o FILE` names the executable: a native binary for `cc`, `cl`, `gcc`, `clang`
and `rv64g`, or a `.wasm` for `wcpl`. skomp writes the MARV module to a
temporary file, hands it to MARV, and removes it afterwards.

```sh
skomp -o app app.scm
skomp -t wcpl -o app.wasm app.scm
```

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
target). `-o` names the executable inside `DIR`. Nothing is compiled.

```sh
skomp -M build -o app app.scm
make -C build
```

skomp requires `DIR` to be empty or not yet to exist, and says so otherwise.
MARV cannot make that check: it is written in the C subset WCPL accepts, which
has no way to read a directory, so MARV can only refuse to overwrite a makefile
of the kind it is about to write. `-M` cannot be combined with `-c` or `-S`.

## What skomp arranges for its children

skomp sets four environment variables for the programs it runs, and nothing
else. They apply to those children only.

  * `SKINT_LIBDIRS` is **set**, not extended: the `-I` directories, then
    skomp's own library directory, then the `-A` and `-L` ones. A value
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
   makefile sets to `$(PREFIX)/libexec/skomp`);
3. `skomp-home` beside the `skomp` executable itself, when `argv[0]` carries a
   directory. This is what makes an unpacked tree work where it stands, and
   what `make` leaves behind.

Paths skomp passes to MARV, and that MARV passes to the backend compiler, are
limited to ASCII letters and digits, space, `_`, `-`, `.`, `/`, `\` and `:`.
That is MARV's rule, and the private root is one of those paths, so skomp cannot
be installed under a directory with other characters in its name.

## Exit status

0 on success. On failure, the exit code of whichever part failed, or 1 when that
cannot be represented. A diagnostic always comes from the part that produced it.
