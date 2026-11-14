Installing SKOMP
================

skomp is four programs: the cover, a Scheme interpreter to host the compiler,
the MARV assembler, and the WCPL WebAssembly backend. All four are built from
the C sources in this repository and there is nothing to fetch. A C compiler is
also needed at run time, to compile what skomp emits for the native targets.

Traditional build (Un\*x & MacOS)
---------------------------------

1. **Get the sources**
   ```bash
   git clone https://github.com/false-schemers/skomp.git
   cd skomp
   ```

2. **(Optional) configure the install prefix**
   Default is `/usr/local`, with the private tree under
   `/usr/local/libexec/skomp`.
   ```bash
   ./configure --prefix=$HOME/opt/skomp
   ```
   `--libexecdir=PATH` puts the private tree somewhere else; Debian-style
   packaging prefers `$(PREFIX)/lib`.

3. **Build**
   ```bash
   make
   ```
   This builds all four parts and leaves a runnable tree in the repository:
   `./skomp` finds `./skomp-home` beside itself, so you can try it before
   installing.

4. **Test** (a wasm host is used if one is installed, and skipped otherwise)
   ```bash
   make test
   ```

5. **Install**
   ```bash
   sudo make install        # or just `make install` if the prefix is writable
   ```

After installing, `skomp` is the only name added to `$PATH`. The three helpers
go under `$(PREFIX)/libexec/skomp/bin`, which is not a `$PATH` directory, so
they cannot be confused with public copies of the same tools.

Uninstall:

```bash
sudo make uninstall
```

Building the parts by hand
--------------------------

Each part is a handful of C files in its own directory. From the repository
root:

```bash
mkdir -p skomp-home/bin
cc -O2 -DNDEBUG -D NAN_BOXING -D OPT_TOWER -D OPT_UNICODE \
   -o skomp-home/bin/skint skint/s.c skint/k.c skint/i.c skint/n.c skint/t.c -lm
cc -O2 -DNDEBUG -o skomp-home/bin/marv marv/m.c marv/r.c marv/v.c marv/a.c -lm
cc -O2 -DNDEBUG -o skomp-home/bin/wcpl wcpl/c.c wcpl/l.c wcpl/p.c wcpl/w.c -lm
cp -a lib target skomp-home/
cc -O2 -DNDEBUG -o skomp skomp.c
```

Notes on the flags:

- `-D OPT_TOWER` and `-D OPT_UNICODE` are **not optional** for the hosted
  interpreter. The compiled runtime's strings are arrays of Unicode codepoints,
  so an ASCII host would read a multi-byte literal in a source file as several
  characters and compile a different program; and the code generator carries
  32-bit constants that want exact arithmetic. `OPT_ENHTTY` is not used: skomp
  never enters a REPL, and `opt/n_enhtty.*` is not imported.
- `-D NAN_BOXING` assumes the upper 16 bits of a heap pointer are zero, which
  holds on 64-bit Linux, MacOS and Windows. Leave it off on a 32-bit host.
- `-D SKOMP_HOME_DIR=...` on `skomp.c` compiles in where the private tree was
  installed. Without it, skomp finds `skomp-home` beside its own executable,
  or wherever `SKOMP_HOME` says.

Windows (MSVC / MinGW)
----------------------

**MSVC**, from a Developer Command Prompt in the repository root:

```bat
mkdir skomp-home\bin
cl /nologo /O2 /DNDEBUG /DNAN_BOXING /DOPT_TOWER /DOPT_UNICODE ^
   /Fe"skomp-home\bin\skint.exe" skint\s.c skint\k.c skint\i.c skint\n.c skint\t.c
cl /nologo /O2 /DNDEBUG /Fe"skomp-home\bin\marv.exe" marv\m.c marv\r.c marv\v.c marv\a.c
cl /nologo /O2 /DNDEBUG /Fe"skomp-home\bin\wcpl.exe" wcpl\c.c wcpl\l.c wcpl\p.c wcpl\w.c
xcopy /e /i /y lib skomp-home\lib
xcopy /e /i /y target skomp-home\target
cl /nologo /O2 /DNDEBUG /Fe"skomp.exe" skomp.c
```

**MinGW** uses the Un\*x flags with `-o skomp.exe`.

There is no installer. Put `skomp.exe` wherever you like with `skomp-home`
beside it, and add that directory to `PATH`:

```
C:\tools\skomp\skomp.exe
C:\tools\skomp\skomp-home\bin\{skint.exe, marv.exe, wcpl.exe}
C:\tools\skomp\skomp-home\lib\...
C:\tools\skomp\skomp-home\target\...
```

`SKOMP_HOME` overrides that if you want the tree elsewhere.

The default target on Windows is `cl`, so a Developer Command Prompt (or a
`PATH` that reaches `cl.exe`) is needed to build an executable. `-t wcpl` needs
nothing but skomp itself.

What you need at run time
-------------------------

| target | needs |
|---|---|
| `cc`, `gcc`, `clang` | that C compiler on `PATH` |
| `cl` | MSVC, from a Developer Command Prompt |
| `rv64g` | a RISC-V assembler and linker, or `-c` to stop at the `.s` |
| `wcpl` | nothing: skomp carries its own WCPL |

A path problem to know about
----------------------------

MARV restricts the paths it hands to the backend compiler to ASCII letters and
digits, space, `_`, `-`, `.`, `/`, `\` and `:`. The Scheme runtime's adapter
files are among those paths and they live in the private tree, so skomp cannot
be installed under a directory whose name has other characters in it -- a
non-ASCII user name, for instance. The diagnostic comes from MARV and names the
path.

Need help?
----------
Open an issue at
<https://github.com/false-schemers/skomp/issues>
