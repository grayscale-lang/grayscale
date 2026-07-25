<p align="center">
    <img src="images/Grayscale_logo.png" alt="Grayscale Logo" width="200">

<p align="center">
  <h1 align="center">The Grayscale Programming Language</h1>
  <!-- <h2 align="center">Programming On Your Terms</h2> -->
  <p align="center"><em>A compiled language where simplicity meets flexibility.</em>
</p>



<p align="center">
  <a href="https://github.com/grayscale-lang/grayscale/actions/workflows/ci.yml"><img src="https://github.com/grayscale-lang/grayscale/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
  <a href="https://github.com/grayscale-lang/grayscale/actions/workflows/codeql-analysis.yml"><img src="https://github.com/grayscale-lang/grayscale/actions/workflows/codeql-analysis.yml/badge.svg" alt="CodeQL Security Scan"></a>
</p>

---
<p align="center">
    <img src="images/Example1.png" alt="Code_Example1" width="400" >
</p>
<p align="center">
    <img src="images/Example2.png" alt="Code_Example2" width="600" >
</p>
<p align="center">
    <img src="images/Example3.png" alt="Code_Example3" width="600" >
</p>
<p align="center">
    <img src="images/Example4.png" alt="Code_Example4" width="600" >
</p>


---

## Why Grayscale?

- **Readable syntax** — `for_each`, `as_long_as`(alias is `while`), `if`/`or`/`otherwise`(alias is `else`), `when`/`is`, `in`/`not_in`(alias is `!in`), `or_return`, `ensure`, `bit_and`/`bit_or`/`bit_xor`, etc.
- **Versatility through simplicity** — Scripts, microservices, CLI tools, or projects where you want to learn systems programming fundamentals
- **A compiler that works **for** the programmer** — Clear error messages, a built-in formatter, live reloading, and built-in language and stdlib docs via `gray man`. When questions arise, Grayscale provides the answers.
- **Safe by default** — Automatic Scope-Based Arena Management for memory, bounds-checked arrays, overflow-checked math, nil protection, **NO** pointer arithmetic. The guardrails are on unless you explicitly take them off with the `@mem` stdlib module

---

## The Standard Library

<p align="center">

`@arrays` · `@strings` · `@maps` · `@math` · `@time` · `@random` · `@json` · `@io` · `@os`
`@http` · `@server` · `@crypto` · `@encoding` · `@uuid` · `@bytes` · `@binary` · `@sqlite`
`@regex` · `@csv` · `@net` · `@threads` · `@sync` · `@channels` · `@mem` · `@atomic` · `@fmt` · `@strconv`

</p>

---

## Install

### Binary download (recommended)

Download the latest release for your platform from the [Releases page](https://github.com/grayscale-lang/grayscale/releases). No dependencies required.

### Build from source

Requires Go 1.23+ and a C compiler (gcc or clang).

```bash
git clone https://github.com/grayscale-lang/grayscale.git
cd grayscale
make build
make install
```

> **Note:** Grayscale currently supports **macOS** and **Linux** only.

---

## Commands

| Command | Description | Example |
|---------|-------------|---------|
| `gray <file>` | Compile and run | `gray main.gray` |
| `gray build <file> -o <name>` | Compile to a distributable binary | `gray build main.gray -o myapp` |
| `gray build <file> --emit-c` | Emit generated C source to a file (no binary) | `gray build main.gray --emit-c` |
| `gray check <file>` | Type check without compiling | `gray check main.gray` |
| `gray watch <file>` | Watch for changes, re-run on save | `gray watch main.gray` |
| `gray fmt <path>` | Format `.gray` source files in place | `gray fmt .` or `gray fmt ./...` |
| `gray fmt --check <path>` | Check formatting without modifying files (CI gate) | `gray fmt --check ./...` |
| `gray doc <file>` | Generate docs from `#doc` attributes | `gray doc main.gray` |
| `gray new <name>` | Scaffold a new project | `gray new myproject` |
| `gray report` | Print system info for bug reports | `gray report` |
| `gray update` | Update to the latest stable version | `gray update` |
| `gray update --pre` | Update to the latest pre-release (alpha/beta) | `gray update --pre` |
| `gray install <version>` | Install a specific version by semver | `gray install x.y.z` |
| `gray version` | Show version info | `gray version` |
| `gray man` | Show help for the man command | `gray man` |
| `gray man <module>` | Show info about a stdlib module | `gray man strings` |
| `gray man <function>` | Show info about a stdlib function | `gray man to_upper` |
| `gray man <struct>` | Show info about a stdlib struct type | `gray man HttpRequest` |

---

## Updating

```bash
gray update              # latest stable
gray update --pre        # latest pre-release Note: The latest pre-release may be very out of date
gray install x.y.z       # pin to an exact version
```

`gray update` checks for new versions, shows the changelog, and upgrades both the `gray` CLI and the compiler. Pass `--pre` to pick up the latest alpha, beta, or rc. Use `gray install <version>` to install an exact version by semver — downgrades and pre-release tags (e.g. `3.0.0-beta.2`) are supported.

---

## Learn More

- [Language standard](STANDARD.md)
- [Contributing guide](CONTRIBUTING.md)

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
