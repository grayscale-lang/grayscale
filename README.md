<p align="center">
    <img src="images/Grayscale_logo.png" alt="Grayscale Logo" width="200">

<p align="center">
  <h1 align="center">The Grayscale Programming Language</h1>
  <p align="center"><em>Simple to write. safe to run.</em>
</p>



<p align="center">
  <a href="https://github.com/grayscale-lang/grayscale/actions/workflows/ci.yml"><img src="https://github.com/grayscale-lang/grayscale/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/grayscale-lang/grayscale/actions/workflows/codeql-analysis.yml"><img src="https://github.com/grayscale-lang/grayscale/actions/workflows/codeql-analysis.yml/badge.svg" alt="CodeQL Security Scan"></a>
</p>

---

```grayscale
import @strconv

const urgentFlag u8 = 1 bit_shift_left 0

const Status enum {
    Todo
    Doing(string)
    Done(i64)
}

const Task struct {
    title string
    status Status
    flags u8

    do is_urgent(self Task) -> bool {
        return (self.flags bit_and urgentFlag) != 0
    }
}

do get_first_task(T generic, items [T]) -> T {
    return items[0]
}

do describe_status(status Status) -> string {
    when status {
        is Status.Todo { return "to do" }
        is Status.Doing(who) { return "in progress (${who})" }
        is Status.Done(minutes) { return "done in ${minutes}m" }
        default { return "" }
    }
}

do main() {
    mut tasks [Task] = {
        Task{title: "Fix parser bug", status: Status.Doing("Marshall"), flags: urgentFlag},
        Task{title: "Write docs", status: Status.Done(45), flags: 0},
        Task{title: "Ship release", status: Status.Todo, flags: 0}
    }

    for_each task in tasks {
        if task.is_urgent() {
            println("! ${task.title}: ${describe_status(task.status)}")
        } otherwise {
            println("  ${task.title}: ${describe_status(task.status)}")
        }
    }

    mut lead = get_first_task(Task, tasks)
    mut estimate = strconv.to_i64("soon") or_default 30
    println("first: ${lead.title}, estimate ${estimate}m")
}
```


---

## Why Grayscale?

- **Simplicity** — Readable, opinionated syntax. Bitwise and membership operators are real keywords (`bit_and`, `bit_xor`, `not_in`) rather than punctuation. Familiar keyword aliases (`fn`, `switch`/`case`, `elif`/`else`, `while`, `defer`) for those who prefer them. Helpful compile-time errors & warnings and runtime panics. Useful CLI commands like `gray man`, `gray check`, `gray new`, and `gray watch`.
- **Flexibility** — Build scripts and dev tooling, cross-platform CLI utilities, HTTP/JSON backend services, SQLite-backed apps, network daemons, and batch data processing, or learning systems programming fundamentals.
- **Modularity** — Beyond a small builtin core, everything else needs an import. Stdlib modules, your own `.gray`/`.grayscale` files, and C headers can all be imported.
- **Safety** — An automatic scope-based arena management memory model with a compile-time [pointer checker](STANDARD.md#117-memory-safety) that proves no pointer is ever readable after its memory is reclaimed, bounds-checked arrays, strings, and maps, overflow-checked arithmetic, division-by-zero protection, nil pointer checks, stack depth guards, **NO** pointer arithmetic. The guardrails are on unless you explicitly opt in to unsafe operations like raw pointers (`raw()`), manual memory management (`@mem`), threading (`@threads`), wrapping arithmetic (`wrap_add()`), or C interop (`extern import`)
---

## Standard Library Modules

<p align="center">

`@arrays` · `@atomic` · `@binary` · `@channels` · `@chars` · `@crypto` · `@csv` · `@encoding` · `@fmt` · `@http`
`@io` · `@json` · `@maps` · `@math` · `@mem` · `@net` · `@os` · `@path` · `@random` · `@regex`
`@runtime` · `@server` · `@sqlite` · `@strconv` · `@strings` · `@sync` · `@threads` · `@time` · `@uuid`

</p>

---

## Install

### Binary download (recommended)

Download the latest release for your platform from the [Releases page](https://github.com/grayscale-lang/grayscale/releases). No dependencies required.

For now, releases are experimental builds named `experimental-YYYYMMDD-build#`: the UTC date of the merge into `main`, then a build number that counts up from 1 within that date.

If you are on 0.14.1 or earlier, `gray update` cannot see experimental builds. Download one from the Releases page and replace your `gray` binary once. After that, `gray update` installs every newer release.

### Build from source

Requires a C11 compiler with GNU extensions: GCC 11+, Clang 14+, or TinyCC 0.9.27+. Grayscale uses the first of `cc`, `gcc`, or `clang` it finds. Pass `--cc <compiler>` to `gray` or `gray build` to choose another for one run (`gray main.gray --cc tcc`), or set `GRAY_CC` or `CC`. `--cc` takes precedence over both. `GRAY_CC` is the compiler `gray` uses for `.gray` programs; `CC` is the compiler `make` uses to build the runtime library, and `gray` falls back to it only when `GRAY_CC` is unset (`CC=clang GRAY_CC=gcc make selfhost build`).

Targets that need a compiler take its name first: `make selfhost <target>` uses the compiler written in Grayscale, which is the one to use. Run `make help` to list the make targets, or `make help TARGET=build` to see what one target does and which variables it reads.

| Compiler | macOS | Linux | Windows |
|---|---|---|---|
| **Clang** | Supported | Supported | Not supported |
| **GCC** | Supported (Homebrew GCC) | Supported | Supported (MinGW-w64, until TinyCC replaces it) |
| **TinyCC** | Supported | Not yet supported | Planned |

On macOS, `cc` and `gcc` are both Apple Clang unless GCC is installed separately.

```bash
git clone https://github.com/grayscale-lang/grayscale.git
cd grayscale
make selfhost build
make selfhost install
```

The compiler is written in Grayscale, so building it needs a `gray` that is already installed. Download a release from the [Releases page](https://github.com/grayscale-lang/grayscale/releases) first, and either put `gray` on your `PATH` or in the repository root, or pass `GRAY=/path/to/gray` to `make`.

`make install`, `gray update`, and `gray install` also install tab completion
for your shell (bash, zsh, fish, or PowerShell) when its completion directory
is writable, and never edit an rc file. See `gray completion` in `STANDARD.md`
for where each shell's script goes.

### Windows

Grayscale builds on Windows with [MinGW-w64](https://www.mingw-w64.org/).

```powershell
winget install BrechtSanders.WinLibs.POSIX.UCRT.Base   # or: choco install mingw
git clone https://github.com/grayscale-lang/grayscale.git
cd grayscale
make grayc build
```

There is no Windows release yet, so there is no `gray` to build the compiler with, and Windows builds the C compiler: use `make grayc build` and `make grayc test`. `make clean` is unchanged. `make` needs a POSIX shell, which MSYS2 and Git Bash both provide.
The compiler finds MinGW in its default install locations automatically —
it does not need to be on `PATH`.

---

## Commands

Source files end in `.gray` or `.grayscale`; both work everywhere and can be mixed in one project.

| Command | Description | Example |
|---------|-------------|---------|
| `gray <file>` | Compile and run | `gray main.gray` |
| `gray build <file> -o <name>` | Compile to a distributable binary | `gray build main.gray -o myapp` |
| `gray build <file> --emit-c` | Emit generated C source to a file (no binary) | `gray build main.gray --emit-c` |
| `gray build <file> --arena-limit=<size>` | Cap arena memory (KB/MB/GB; default: 1GB) | `gray build main.gray --arena-limit=256MB` |
| `gray <file> --cc <compiler>` | Compile and run with a chosen C compiler (also on `gray build`) | `gray main.gray --cc tcc` |
| `gray check <file>` | Type check without compiling | `gray check main.gray` |
| `gray test [path...]` | Compile and run `#test` functions | `gray test ./src/...` |
| `gray watch <file>` | Watch for changes, re-run on save | `gray watch main.gray` |
| `gray fmt <path>` | Format `.gray` and `.grayscale` source files in place | `gray fmt .` or `gray fmt ./...` |
| `gray fmt --check <path>` | Check formatting without modifying files (CI gate) | `gray fmt --check ./...` |
| `gray doc <file>` | Generate docs from `#doc` attributes | `gray doc main.gray` |
| `gray new <name>` | Scaffold a new project | `gray new myproject` |
| `gray report` | Print system info for bug reports | `gray report` |
| `gray verify` | Run the built-in language verification test suite | `gray verify` |
| `gray update` | Update to the latest stable version | `gray update` |
| `gray update --pre` | Update to the latest pre-release (alpha/beta) | `gray update --pre` |
| `gray install <version>` | Install a specific version by exact semver | `gray install v0.11.5` |
| `gray version` | Show version info | `gray version` |
| `gray man` | Show help for the man command | `gray man` |
| `gray man <module>` | Show info about a stdlib module | `gray man strings` |
| `gray man <function>` | Show info about a stdlib function | `gray man strings.to_upper` |
| `gray man <struct>` | Show info about a stdlib struct type | `gray man HttpRequest` |
| `gray cross build <file> --target <target>` | Cross-compile for another platform via Zig | `gray cross build main.gray --target linux-amd64` |
| `gray cross targets` | List supported cross-compilation targets | `gray cross targets` |

Every command prints its own usage and flags with `gray [command] --help`, or equivalently `gray help [command]`:

```bash
gray build --help
```

> **Note:** Zig is only required for cross-compilation (`gray cross`). Native builds use the system C compiler.

---

## Updating

```bash
gray update              # latest stable
gray update --pre        # latest pre-release Note: The latest pre-release may be very out of date
gray install v0.11.5     # pin to an exact version
```

`gray update` checks for new versions, shows the changelog, and upgrades both the `gray` CLI and the compiler. Pass `--pre` to pick up the latest alpha, beta, or rc. Use `gray install <version>` to install an exact version by semver — downgrades and pre-release tags (e.g. `0.2.0-beta.1`) are supported.

---

## Showcase

- [GrayUtils](https://github.com/SchoolyB/GrayUtils) — a collection of useful utils written in Grayscale

---

## Learn More

- [Documentation site](https://grayscale-lang.github.io/grayscale-lang/)
- [Language standard](STANDARD.md)
- [Contributing guide](CONTRIBUTING.md)
- [Benchmark suite](benchmarks/README.md) — `make selfhost benchmark` times real-shaped programs

## Tooling
- [The Grayscale Language Server Protocol](https://github.com/grayscale-lang/grayls)
---

## Status

Grayscale is in active development. The language is usable for personal projects and dev tools. Breaking changes may occur frequently.

---

## License

MIT License - Copyright (c) 2025-Present Marshall A Burns

See [LICENSE](LICENSE) for details.

---

## Contributors

Thank you to everyone who has contributed to Grayscale!

<a href="https://github.com/akamikado"><img src="https://github.com/akamikado.png" width="50" height="50" alt="akamikado"/></a>
<a href="https://github.com/CobbCoding1"><img src="https://github.com/CobbCoding1.png" width="50" height="50" alt="CobbCoding1"/></a>
<a href="https://github.com/CFFinch62"><img src="https://github.com/CFFinch62.png" width="50" height="50" alt="CFFinch62"/></a>
<a href="https://github.com/Aryan-Shrivastva"><img src="https://github.com/Aryan-Shrivastva.png" width="50" height="50" alt="Aryan-Shrivastva"/></a>
<a href="https://github.com/arjunpathak072"><img src="https://github.com/arjunpathak072.png" width="50" height="50" alt="arjunpathak072"/></a>
<a href="https://github.com/deepika1214"><img src="https://github.com/deepika1214.png" width="50" height="50" alt="deepika1214"/></a>
<a href="https://github.com/blackgirlbytes"><img src="https://github.com/blackgirlbytes.png" width="50" height="50" alt="blackgirlbytes"/></a>
<a href="https://github.com/majiayu000"><img src="https://github.com/majiayu000.png" width="50" height="50" alt="majiayu000"/></a>
<a href="https://github.com/prjctimg"><img src="https://github.com/prjctimg.png" width="50" height="50" alt="prjctimg"/></a>
<a href="https://github.com/jaideepkathiresan"><img src="https://github.com/jaideepkathiresan.png" width="50" height="50" alt="jaideepkathiresan"/></a>
<a href="https://github.com/Abhishek022001"><img src="https://github.com/Abhishek022001.png" width="50" height="50" alt="Abhishek022001"/></a>
<a href="https://github.com/Scanf-s"><img src="https://github.com/Scanf-s.png" width="50" height="50" alt="Scanf-s"/></a>
<a href="https://github.com/HCH1212"><img src="https://github.com/HCH1212.png" width="50" height="50" alt="HCH1212"/></a>
<a href="https://github.com/elect0"><img src="https://github.com/elect0.png" width="50" height="50" alt="elect0"/></a>
<a href="https://github.com/jgafnea"><img src="https://github.com/jgafnea.png" width="50" height="50" alt="jgafnea"/></a>
<a href="https://github.com/madhav-murali"><img src="https://github.com/madhav-murali.png" width="50" height="50" alt="madhav-murali"/></a>
<a href="https://github.com/preettrank53"><img src="https://github.com/preettrank53.png" width="50" height="50" alt="preettrank53"/></a>
<a href="https://github.com/TechLateef"><img src="https://github.com/TechLateef.png" width="50" height="50" alt="TechLateef"/></a>
<a href="https://github.com/dtee1"><img src="https://github.com/dtee1.png" width="50" height="50" alt="dtee1"/></a>
<a href="https://github.com/SAY-5"><img src="https://github.com/SAY-5.png" width="50" height="50" alt="SAY-5"/></a>
<a href="https://github.com/mvanhorn"><img src="https://github.com/mvanhorn.png" width="50" height="50" alt="mvanhorn"/></a>
<a href="https://github.com/kas2804"><img src="https://github.com/kas2804.png" width="50" height="50" alt="kas2804"/></a>
<a href="https://github.com/su-s2008"><img src="https://github.com/su-s2008.png" width="50" height="50" alt="su-s2008"/></a>
<a href="https://github.com/rb152080"><img src="https://github.com/rb152080.png" width="50" height="50" alt="rb152080"/></a>
<a href="https://github.com/confucius40"><img src="https://github.com/confucius40.png" width="50" height="50" alt="confucius40"/></a>
<a href="https://github.com/sjh9714"><img src="https://github.com/sjh9714.png" width="50" height="50" alt="sjh9714"/></a>
<a href="https://github.com/coincoin57660-byte"><img src="https://github.com/coincoin57660-byte.png" width="50" height="50" alt="coincoin57660-byte"/></a>
<a href="https://github.com/KeuqS"><img src="https://github.com/KeuqS.png" width="50" height="50" alt="KeuqS"/></a>
