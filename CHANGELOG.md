# Changelog

## [0.1.3](https://github.com/grayscale-lang/grayscale/compare/grayscale-v0.1.2...grayscale-v0.1.3) (2026-07-26)


### Bug Fixes

* **codegen:** emit overflow checks for compound assignment through pointer dereference ([#2164](https://github.com/grayscale-lang/grayscale/issues/2164)) ([f3d4624](https://github.com/grayscale-lang/grayscale/commit/f3d4624551f53b1815be13b91584c19deaec12a1))
* **codegen:** use gray_string_concat for interpolation to preserve null bytes ([#2195](https://github.com/grayscale-lang/grayscale/issues/2195)) ([f0951b8](https://github.com/grayscale-lang/grayscale/commit/f0951b8a5ecc84e2d5add5b65727b9859b24ae02))
* **parser:** enforce 64KB limit on string interpolation expressions ([#2177](https://github.com/grayscale-lang/grayscale/issues/2177)) ([43e05c3](https://github.com/grayscale-lang/grayscale/commit/43e05c35ca475563bac79f624d967373127f4a04))
* **tests:** correct e2e test count double-counting in test_e2e_divide_by_zero ([46acb35](https://github.com/grayscale-lang/grayscale/commit/46acb35afd6ad1909ace5e10e73da326c3e05603))
* **typechecker:** allow addr() on const variables, catch mutation at write sites ([#2191](https://github.com/grayscale-lang/grayscale/issues/2191)) ([626809a](https://github.com/grayscale-lang/grayscale/commit/626809aa99d4ad7d67ed34251c4eaa300a90d1a9))
* **typechecker:** catch const mutation through nested access and postfix ([#2162](https://github.com/grayscale-lang/grayscale/issues/2162)) ([dbb9df5](https://github.com/grayscale-lang/grayscale/commit/dbb9df5a0a497ab507da09a82c5081546634c544))

## [0.1.2](https://github.com/grayscale-lang/grayscale/compare/grayscale-v0.1.1...grayscale-v0.1.2) (2026-07-25)


### Bug Fixes

* **cli:** filter releases to grayscale-v* tags, excluding legacy EZ releases ([e3805ce](https://github.com/grayscale-lang/grayscale/commit/e3805ce85040e92ed66499abca72fa2151f5779c))
* **cli:** filter releases to grayscale-v* tags, excluding legacy EZ releases ([#2201](https://github.com/grayscale-lang/grayscale/issues/2201)) ([05636ae](https://github.com/grayscale-lang/grayscale/commit/05636aea32add6fb7983d8abf90964b05927b46e))

## [0.1.1](https://github.com/grayscale-lang/grayscale/compare/grayscale-v0.1.0...grayscale-v0.1.1) (2026-07-25)


### Bug Fixes

* **codegen:** decrement .iterating guard on early return from for_each ([#2163](https://github.com/grayscale-lang/grayscale/issues/2163)) ([ff2581d](https://github.com/grayscale-lang/grayscale/commit/ff2581deb20841ac019003d1225a03e2364f3606))
* **codegen:** scope ref_vars and bigint_vars per function to prevent cross-function leaks ([#2142](https://github.com/grayscale-lang/grayscale/issues/2142)) ([6e59b16](https://github.com/grayscale-lang/grayscale/commit/6e59b16582917d0b3aa69ef26e7c7128577ee7bd))
* **codegen:** use 64-bit shift for flags enum bit values ([#2186](https://github.com/grayscale-lang/grayscale/issues/2186)) ([04facce](https://github.com/grayscale-lang/grayscale/commit/04faccebc2868641a2d2c429ade4ec93de22d798))
* **lexer:** cap identifier length at 255 characters ([#2182](https://github.com/grayscale-lang/grayscale/issues/2182)) ([eef5f87](https://github.com/grayscale-lang/grayscale/commit/eef5f87bc1525836af4b238d04cad51cf0ca0bf7))
* **lexer:** saturate line and column counters at INT_MAX ([#2183](https://github.com/grayscale-lang/grayscale/issues/2183)) ([1209418](https://github.com/grayscale-lang/grayscale/commit/1209418dedcfc93ac0f879ed7cafe73d867ba4fa))
* **parser:** add recursion depth limit for block statement nesting ([#2168](https://github.com/grayscale-lang/grayscale/issues/2168)) ([738a388](https://github.com/grayscale-lang/grayscale/commit/738a3885ab9ea68f71afd860d5f731c9d44fde50))
* **parser:** cap array type nesting depth at 64 levels ([#2169](https://github.com/grayscale-lang/grayscale/issues/2169)) ([3101a8f](https://github.com/grayscale-lang/grayscale/commit/3101a8f82dea7d5cfb0e02ca42c7b81b46cbd772))
* **parser:** replace strdup with arena allocation in error paths ([#2184](https://github.com/grayscale-lang/grayscale/issues/2184)) ([a92d62f](https://github.com/grayscale-lang/grayscale/commit/a92d62ff9fde4ed28166d3accc324988c4260c1d))
* **runtime:** correct 256-bit subtraction borrow propagation ([#2192](https://github.com/grayscale-lang/grayscale/issues/2192)) ([5a77320](https://github.com/grayscale-lang/grayscale/commit/5a77320ec3bd0a306c063b45cc79c076286f4a31))
* **runtime:** include source location in runtime panics ([#2188](https://github.com/grayscale-lang/grayscale/issues/2188)) ([714accf](https://github.com/grayscale-lang/grayscale/commit/714accfe0a21931a2c814aabd2e8e212025f6ffa))
* **runtime:** panic on wide integer division by zero ([#2178](https://github.com/grayscale-lang/grayscale/issues/2178)) ([7056543](https://github.com/grayscale-lang/grayscale/commit/7056543dc7f7f8d9f4648eb04038dfe6a3a97e4d))
* **runtime:** prevent int32 overflow in array capacity doubling ([#2166](https://github.com/grayscale-lang/grayscale/issues/2166)) ([c16947b](https://github.com/grayscale-lang/grayscale/commit/c16947b3d6ce01e49e15be1fb0e31ac441af31bd))
* **runtime:** prevent size_t overflow in arena allocation check ([#2171](https://github.com/grayscale-lang/grayscale/issues/2171)) ([5001941](https://github.com/grayscale-lang/grayscale/commit/50019413f4818cb000c485d90f1eecfd7c2c9295))
* **runtime:** validate digits in bigint decimal parser ([#2179](https://github.com/grayscale-lang/grayscale/issues/2179)) ([9ded657](https://github.com/grayscale-lang/grayscale/commit/9ded657ebfd2a1593e7d561982dd83b20670cee7))
* **typechecker:** allow bitwise operators on #flags enums ([#2133](https://github.com/grayscale-lang/grayscale/issues/2133)) ([a57e764](https://github.com/grayscale-lang/grayscale/commit/a57e764954ec33329e2e042f8181f8950e326ac9))
* **typechecker:** auto-deref pointer receivers for struct function dispatch ([#2131](https://github.com/grayscale-lang/grayscale/issues/2131)) ([889be08](https://github.com/grayscale-lang/grayscale/commit/889be0838463c699a006b9e3a82434ad79e70e02))


### Performance Improvements

* **codegen:** single categorization pass over program statements ([#2148](https://github.com/grayscale-lang/grayscale/issues/2148)) ([4a0ffc6](https://github.com/grayscale-lang/grayscale/commit/4a0ffc6d66f82815aa6f4634945e93be70be6cd3))
* **parser:** remove prescan that double-lexed entire source file ([#2143](https://github.com/grayscale-lang/grayscale/issues/2143)) ([cc102eb](https://github.com/grayscale-lang/grayscale/commit/cc102eb7a36ce80e675b404f0ef37ae059bb4a3f))
* **typechecker:** table-driven stdlib return type resolution ([#2145](https://github.com/grayscale-lang/grayscale/issues/2145)) ([6b5c27c](https://github.com/grayscale-lang/grayscale/commit/6b5c27cf2459bc5f850cd5ab420d21f29eea0f1f))


### Documentation

* regenerate ERRORS.md from error_codes.h ([57b2e2b](https://github.com/grayscale-lang/grayscale/commit/57b2e2be4f67a6846948bd390d5312083d813ecb))
* regenerate ERRORS.md from error_codes.h ([1139ae5](https://github.com/grayscale-lang/grayscale/commit/1139ae56a4911b97a91b3c4ccf7a4b42ff53f3d9))
* update CONTRIBUTING.md project structure to reflect current layout ([04b6580](https://github.com/grayscale-lang/grayscale/commit/04b6580ed9c76e07218cdbe7713edc9bf9ba2bd1))
* update tagline ([5c2f4f6](https://github.com/grayscale-lang/grayscale/commit/5c2f4f6ed3282ee5547bbd72908a250e17a68ac5))

## [0.1.0](https://github.com/grayscale-lang/grayscale/compare/v0.0.4...v0.1.0) (2026-07-23)


### Features

* **stdlib:** add `write_bytes()`, `append_bytes()`, `temp_file()`, `temp_dir()` to `io` module ([6e761f5](https://github.com/grayscale-lang/grayscale/commit/6e761f59a39e0062b1ec72d0dbd79d85ef5c68f8))
* **stdlib:** add write_bytes, append_bytes, temp_file, temp_dir to io module ([#1868](https://github.com/grayscale-lang/grayscale/issues/1868)) ([5875f3e](https://github.com/grayscale-lang/grayscale/commit/5875f3e8775ddfefe73d7ec7ce176d0f06e35157))

## [0.0.4](https://github.com/grayscale-lang/grayscale/compare/v0.0.3...v0.0.4) (2026-07-23)


### Bug Fixes

* **codegen:** evaluate when match expression once into a temporary ([#2126](https://github.com/grayscale-lang/grayscale/issues/2126)) ([50ea594](https://github.com/grayscale-lang/grayscale/commit/50ea594bf5357b7f49b2de934ba84c716fee6905))
* **codegen:** hoist map collection into temporary for for_each iteration ([#2128](https://github.com/grayscale-lang/grayscale/issues/2128)) ([358ff60](https://github.com/grayscale-lang/grayscale/commit/358ff608c9e60c6b08924f13e54da768b926b487))
* **codegen:** inject default params for struct function calls ([#2130](https://github.com/grayscale-lang/grayscale/issues/2130)) ([187e5bc](https://github.com/grayscale-lang/grayscale/commit/187e5bc9adf150b2320c5026df4906ca02a88b21))
* **parser:** emit E2002 for type keywords as enum binding names ([#2129](https://github.com/grayscale-lang/grayscale/issues/2129)) ([9bfe428](https://github.com/grayscale-lang/grayscale/commit/9bfe428853fc6934c73780bd03f4b6b6c75053a2))
* **typechecker:** add child scopes for while and loop bodies ([#2127](https://github.com/grayscale-lang/grayscale/issues/2127)) ([aa7e86e](https://github.com/grayscale-lang/grayscale/commit/aa7e86e1a23e2559c08271e2bc7600d34514d991))
* **typechecker:** emit E3040 for multi-return calls in aggregate contexts ([#2114](https://github.com/grayscale-lang/grayscale/issues/2114)) ([013566b](https://github.com/grayscale-lang/grayscale/commit/013566b05edbaed10ff5af6aca4a55a086c65b7d))
* **typechecker:** emit E3130 for bare func as struct field type ([#2121](https://github.com/grayscale-lang/grayscale/issues/2121)) ([ec7232c](https://github.com/grayscale-lang/grayscale/commit/ec7232c3df53423d55f10def71aa4d8b7282e098))
* **typechecker:** reject global constant forward references ([#2132](https://github.com/grayscale-lang/grayscale/issues/2132)) ([05865bc](https://github.com/grayscale-lang/grayscale/commit/05865bc378e2af9a243b024fad2a3e37e6785ddb))
* **typechecker:** validate types for compound assignment operators ([#2125](https://github.com/grayscale-lang/grayscale/issues/2125)) ([307ce27](https://github.com/grayscale-lang/grayscale/commit/307ce27ff66157d151d55b5cb14aab06ea66f531))


### Documentation

* regenerate ERRORS.md from error_codes.h ([c09d816](https://github.com/grayscale-lang/grayscale/commit/c09d816fce751b23ee701ae693af446b1699326e))
* Updated STANDARD.md ([456fef5](https://github.com/grayscale-lang/grayscale/commit/456fef5adc28be7785f4af4396e76f582fed2674))

## [0.0.3](https://github.com/grayscale-lang/grayscale/compare/v0.0.2...v0.0.3) (2026-07-21)


### Bug Fixes

* **cli:** suppress usage text on runtime panic exit ([bc0322b](https://github.com/grayscale-lang/grayscale/commit/bc0322b0ea8fee4f5e6f655aafac5eac6ad602c8))
* **codegen:** add instance dispatch for struct functions ([134ff8a](https://github.com/grayscale-lang/grayscale/commit/134ff8af291ea02895a6c1b04c87107908bc3f4c))
* **codegen:** add source location to runtime panics ([#2113](https://github.com/grayscale-lang/grayscale/issues/2113)) ([f1e1790](https://github.com/grayscale-lang/grayscale/commit/f1e17906c147c1140d5bd69e186a5931bcf550af))
* **codegen:** correct buffer size for wildcard function name mangling ([649c958](https://github.com/grayscale-lang/grayscale/commit/649c9588ff48de629a6dadb4ad9a01754cedf783))
* **codegen:** include bigint.h unconditionally as a core header ([2c87be0](https://github.com/grayscale-lang/grayscale/commit/2c87be0832c4d4fd2464d02f7b4018f6de07b4e2))
* **codegen:** inject instance for all struct function dispatch patterns ([42bc3ee](https://github.com/grayscale-lang/grayscale/commit/42bc3eec97e79c3d3c9c06e9c2f257b42ab3949c))
* **codegen:** inject self param for both mutable and immutable struct functions ([5f528e3](https://github.com/grayscale-lang/grayscale/commit/5f528e3b02036aa5e2de9e0a49d56226dc465f1f))
* **codegen:** preserve module prefix for user-defined types in type resolution ([f19c004](https://github.com/grayscale-lang/grayscale/commit/f19c004312d51660580f78f1ede4d4b1c079cd27))
* **codegen:** prevent infinite recursion in cross-module struct type resolution ([2d333cd](https://github.com/grayscale-lang/grayscale/commit/2d333cdd36c9bb26e2a6351439420de960424901))
* **imports:** detect module name collisions across import statements ([8e1cca4](https://github.com/grayscale-lang/grayscale/commit/8e1cca47b0c44b0c6b82a408a7d50b70c51aea09))
* **parser:** reject wildcard ? in struct field types ([#2079](https://github.com/grayscale-lang/grayscale/issues/2079)) ([f0e55f4](https://github.com/grayscale-lang/grayscale/commit/f0e55f48ecc0840dcfb9cb9e15f08349f35de82f))
* **parser:** use correct length when stripping .gray extension from imports ([b2297f0](https://github.com/grayscale-lang/grayscale/commit/b2297f0d0a66cbfba7552c303c0f42bd07cd8ed6))
* **runtime:** add char and byte variants for array contains operator ([d220201](https://github.com/grayscale-lang/grayscale/commit/d22020171492b556bbef0e6ae44c7b4a23827db3))
* **runtime:** add memory barrier to ARM64 CAS failure paths ([#2069](https://github.com/grayscale-lang/grayscale/issues/2069)) ([0fd1da3](https://github.com/grayscale-lang/grayscale/commit/0fd1da39a0f3a66548ec167981d87232ae2e8fa3))
* **runtime:** make spinlock_destroy idempotent to prevent double-free crash ([#2068](https://github.com/grayscale-lang/grayscale/issues/2068)) ([79687e7](https://github.com/grayscale-lang/grayscale/commit/79687e71f2e68c19b8cc29763ba99c9ab45eb7c7))
* **stdlib:** resolve isfinite linker failure on macOS arm64 ([700fa79](https://github.com/grayscale-lang/grayscale/commit/700fa79b34f913166e4524acb5755379c918e867))
* **stdlib:** use overflow-checked subtraction in time.diff ([#2067](https://github.com/grayscale-lang/grayscale/issues/2067)) ([328f2be](https://github.com/grayscale-lang/grayscale/commit/328f2be6b809e558f3ebbe4ed80a74903ee9d1de))
* **tests:** resolve 6 failing integration tests ([5f20f14](https://github.com/grayscale-lang/grayscale/commit/5f20f14a35f201e08ad25fed270761b976d2c4c1))
* **test:** use correct endpoint for HEAD request in http_custom_headers ([7e9e89c](https://github.com/grayscale-lang/grayscale/commit/7e9e89c5b15f0f8f9d3920caf952a562e9382508))
* **typechecker:** add arg type validation for stdlib functions missing coverage ([d3a4114](https://github.com/grayscale-lang/grayscale/commit/d3a41149527235299af8f41a61bd244dc55d513d))
* **typechecker:** emit E3027 for non-self instance dispatch with & params ([#2116](https://github.com/grayscale-lang/grayscale/issues/2116)) ([3df4549](https://github.com/grayscale-lang/grayscale/commit/3df4549148a66645a46aafad4e9bf4e8b65d1410))
* **typechecker:** register inferred-type vars from wildcard expressions ([#2080](https://github.com/grayscale-lang/grayscale/issues/2080)) ([19b05ee](https://github.com/grayscale-lang/grayscale/commit/19b05ee5de1e62ce506e4f048380fe8a4adb84f3))
* **typechecker:** reject runtime function calls in const initializers ([#2104](https://github.com/grayscale-lang/grayscale/issues/2104)) ([00aa411](https://github.com/grayscale-lang/grayscale/commit/00aa411da4effdddd34141f2b4fc08750f807172))
* **typechecker:** route instance dispatch through semantic analysis ([10c48b3](https://github.com/grayscale-lang/grayscale/commit/10c48b357ef7c64cd5919ceaf3cc911252fec817))
* **typechecker:** validate arg types for time.diff, os.set_env, os.unset_env ([#2064](https://github.com/grayscale-lang/grayscale/issues/2064)) ([ef12499](https://github.com/grayscale-lang/grayscale/commit/ef12499c9c2c534696cfabca80c383c0a6e99c80))
* **typechecker:** validate callback param types for arrays.any/all/filter/map/reduce ([#2063](https://github.com/grayscale-lang/grayscale/issues/2063)) ([ba32c3e](https://github.com/grayscale-lang/grayscale/commit/ba32c3ee9c71eef0a83cf9fd00a6239c32007bf8))


### Performance Improvements

* **typechecker:** free owned ret_types arrays in scope_destroy ([#2071](https://github.com/grayscale-lang/grayscale/issues/2071)) ([663dba1](https://github.com/grayscale-lang/grayscale/commit/663dba162c8e96e1cfbf4fd50a6f92c21208949f))


### Documentation

* regenerate ERRORS.md from error_codes.h ([0138049](https://github.com/grayscale-lang/grayscale/commit/013804933c2420d4e7ac6d1ac1c01b604431b120))

## [0.0.2](https://github.com/grayscale-lang/grayscale/compare/v0.0.1...v0.0.2) (2026-07-18)


### Bug Fixes

* **compiler:** suppress -Winfinite-recursion warning from leaking to user output ([#2102](https://github.com/grayscale-lang/grayscale/issues/2102)) ([69729f5](https://github.com/grayscale-lang/grayscale/commit/69729f54a029b0dabeb413386b3a9ff891bd8924))

## Grayscale 0.0.1

The Grayscale Programming Language — formerly known as EZ.

Grayscale 0.0.1 is the first release under the new name. The language, compiler,
and tooling are identical to EZ 3.9.0 with the following changes:

- Language renamed from EZ to Grayscale
- CLI binary renamed from `ez` to `gray`
- Compiler binary renamed from `ezc` to `grayc`
- File extension changed from `.ez` to `.gray`
- Runtime library renamed from `libezrt.a` to `libgrayrt.a`
- All `ez_` / `EZ_` prefixes renamed to `gray_` / `GRAY_`
- Repository moved to `grayscale-lang/grayscale`

See tag `ez-3.9.0-final` for the last EZ release.

---

## Prehistory (as EZ)

The following changelog entries are from the project's prior life as EZ.

## [3.8.6](https://github.com/SchoolyB/EZ/compare/v3.8.5...v3.8.6) (2026-07-10)


### Bug Fixes

* add depth limit to json.is_valid to prevent stack overflow on deeply nested input ([#2020](https://github.com/SchoolyB/EZ/issues/2020)) ([0914ab7](https://github.com/SchoolyB/EZ/commit/0914ab70c955166ed93e90f760b4fc2760c55563))
* add optional host parameter to net.listen() and server.listen() to allow interface-specific binding ([#2017](https://github.com/SchoolyB/EZ/issues/2017)) ([63e84e6](https://github.com/SchoolyB/EZ/commit/63e84e6e37d01f6fd33be293064711f2a96b883e))
* add scope_destroy() to release scope memory at all typechecker call sites ([b6a1ac6](https://github.com/SchoolyB/EZ/commit/b6a1ac6a680402b8debe9b4d7f1812a594641147))
* add type_pool_reset() to free heap strings from type pool on shutdown ([f9ef806](https://github.com/SchoolyB/EZ/commit/f9ef806a08b0338d0d6766e86adf1456f650e1d9))
* add typechecker_free() and typetable_free() to release typechecker memory ([dce12dc](https://github.com/SchoolyB/EZ/commit/dce12dc12ab8fb72bec24c79f06b6e9da6d9fc2a))
* cap HTTP server at 1024 concurrent connections and apply 30s read timeout per connection ([#2011](https://github.com/SchoolyB/EZ/issues/2011)) ([7d2d30d](https://github.com/SchoolyB/EZ/commit/7d2d30d29b2a81cd139dddd4ec8a60c509408d46))
* cap os.exec stdout/stderr collection at 64 MiB per stream to prevent memory exhaustion ([#2010](https://github.com/SchoolyB/EZ/issues/2010)) ([ae9b26d](https://github.com/SchoolyB/EZ/commit/ae9b26db48c628769a33671682a73e39de62c7b6))
* correct element type parsing for nested bracket types in array initializers ([2b61fff](https://github.com/SchoolyB/EZ/commit/2b61fff149f65a6a4ad6bc9e714ef0f43d0c3272))
* free global_init buffer in codegen_destroy() ([6b9ea93](https://github.com/SchoolyB/EZ/commit/6b9ea93349af0dca1700660d2bdbdee50841aae4))
* heap-allocate builtin struct field arrays to prevent free-of-static crash ([60e273b](https://github.com/SchoolyB/EZ/commit/60e273bb9a4c32d241604f3164beb9efff162c6c))
* initialize ns_func_names fields in codegen_create to prevent garbage free ([1ab0464](https://github.com/SchoolyB/EZ/commit/1ab0464c95d21b36884fb5745b0fc58d4944fa64))
* json.encode always quotes map[string:string] values, escape all control chars ([#2019](https://github.com/SchoolyB/EZ/issues/2019)) ([c490066](https://github.com/SchoolyB/EZ/commit/c490066a89efd5224aede9e83487c3b9f40aa828))
* reject CORS origins containing CR or LF to prevent HTTP header injection (P0101) ([#2009](https://github.com/SchoolyB/EZ/issues/2009)) ([3188d1d](https://github.com/SchoolyB/EZ/commit/3188d1d7ab7b176136ae71ecc5532d0087b743e8))
* reject https:// URLs in http module with explicit error instead of silent plaintext fallback ([#2007](https://github.com/SchoolyB/EZ/issues/2007)) ([b25c633](https://github.com/SchoolyB/EZ/commit/b25c633b6812c83b3d92c526d4a2acfff4609f6a))
* repair result-clobbering NULL and dangling stack pointers in typechecker ([#2040](https://github.com/SchoolyB/EZ/issues/2040), [#2045](https://github.com/SchoolyB/EZ/issues/2045)) ([e74831a](https://github.com/SchoolyB/EZ/commit/e74831a2218effa27963f1924316f3890bdfc90e))
* report 'unexpected end of interpolation expression' instead of EOF in ${...} sub-parser ([#2018](https://github.com/SchoolyB/EZ/issues/2018)) ([ef0369d](https://github.com/SchoolyB/EZ/commit/ef0369d46d8806bd43127dadb5803935a93402d6))
* resolve rvalue address-of bug in stdlib codegen ([#2053](https://github.com/SchoolyB/EZ/issues/2053), [#2052](https://github.com/SchoolyB/EZ/issues/2052)) ([2657125](https://github.com/SchoolyB/EZ/commit/2657125851c6e9b75bcccc6b3463071999570557))
* resolve string enum element type in array indexing and iteration ([1a0956a](https://github.com/SchoolyB/EZ/commit/1a0956a2fb21c16bb3871a2774411c1e779f7a07))
* resolve wildcard-returned enums as TK_ENUM instead of TK_STRUCT ([#2054](https://github.com/SchoolyB/EZ/issues/2054)) ([a90e756](https://github.com/SchoolyB/EZ/commit/a90e756db0e24193af8c65173ee010613c6481d1))
* send HTTP request headers and body separately to remove 8KB body size limit ([#2013](https://github.com/SchoolyB/EZ/issues/2013)) ([8df4f3e](https://github.com/SchoolyB/EZ/commit/8df4f3edfbb94298d3bb67b937bc8aff03983121))
* strdup type_array inputs and resolve remaining free-of-non-heap crashes ([ca7aa46](https://github.com/SchoolyB/EZ/commit/ca7aa46dffcc595affb2cf3b5e24f991ae73d4a8))
* track and free struct-namespaced function name strings in codegen ([480c807](https://github.com/SchoolyB/EZ/commit/480c80772fc1de968ec7511e1067514b0c80377d))
* use arena allocation for named-arg reorder buffer in tc_resolve_named_args() ([58cc8b4](https://github.com/SchoolyB/EZ/commit/58cc8b4aad74b134fec8db07566a8b30472e50c7))
* use correct C type for integer enum array element access ([bb62a4a](https://github.com/SchoolyB/EZ/commit/bb62a4a37b72b85bc7fd00bda908b5456d9195d2))
* use lvalue check in addr-of wrapping to preserve mutation semantics ([#2053](https://github.com/SchoolyB/EZ/issues/2053), [#2052](https://github.com/SchoolyB/EZ/issues/2052)) ([c8becba](https://github.com/SchoolyB/EZ/commit/c8becbaf1d4e5f61c0ec77ed195aa9cd32f6caa3))


### Performance Improvements

* add early-out for equal names in tc_same_enum_type and tc_same_struct_type ([#2043](https://github.com/SchoolyB/EZ/issues/2043)) ([79d9dc3](https://github.com/SchoolyB/EZ/commit/79d9dc3e73d0e92f6b25b60e3921c0bd4cadc11a))
* add parallel index array to find_enum_index to eliminate O(n) scan after bsearch ([#2042](https://github.com/SchoolyB/EZ/issues/2042)) ([963cbfc](https://github.com/SchoolyB/EZ/commit/963cbfca6da9f3bc3f69bd85bd02295cf7b4ac0d))
* cache tc_lookup_using_constant result to avoid double call ([#2040](https://github.com/SchoolyB/EZ/issues/2040)) ([180d0d7](https://github.com/SchoolyB/EZ/commit/180d0d794fc002cade869c814b34f38a116dd86c))
* eliminate O(n) inner loop in tc_lookup_using_constant via pre-computed import index ([#2041](https://github.com/SchoolyB/EZ/issues/2041)) ([ce92138](https://github.com/SchoolyB/EZ/commit/ce921380d323f847601c8cb8c343fb544db9a964))
* fix several remaining memory leaks in typechecker and codegen ([5f513eb](https://github.com/SchoolyB/EZ/commit/5f513eb055c4511cea4da502c80f9fb974d77bcf))
* replace 202 unconditional stack diagnostic buffers with arena allocation via tc_fmt helper ([#2044](https://github.com/SchoolyB/EZ/issues/2044)) ([287ec00](https://github.com/SchoolyB/EZ/commit/287ec00c0a1a618770de997617e0efa3df3ba274))
* replace strlen+sprintf heap allocs with snprintf into stack buffer in name-mangling paths ([#2045](https://github.com/SchoolyB/EZ/issues/2045)) ([1ee7731](https://github.com/SchoolyB/EZ/commit/1ee77310ecb043954076183602dcc228b0817d9b))

## [3.8.5](https://github.com/SchoolyB/EZ/compare/v3.8.4...v3.8.5) (2026-07-07)


### Bug Fixes

* correct lower bound in uint-to-int cast error message ([178bcf3](https://github.com/SchoolyB/EZ/commit/178bcf388a51f0c4de690481259182aa7d34f66b))
* emit correct HTTP reason phrase and Location header for redirects ([611016e](https://github.com/SchoolyB/EZ/commit/611016ec0ee64f1a3bddced58d4d28b3d8a6ae04))
* improve compiler parsing, HTTP behavior, and CLI safety ([67e1e56](https://github.com/SchoolyB/EZ/commit/67e1e564ccc49575ef6f59facef091ed95893bb5))
* parse fixed-size array of maps type annotation [map[K:V], N] ([1b183d1](https://github.com/SchoolyB/EZ/commit/1b183d16cab357b796e6c86f1c73c8a266f9f89b))
* prompt for confirmation before ez pz --force deletes existing directory ([d34a65c](https://github.com/SchoolyB/EZ/commit/d34a65cd5e856085b5ace77923e43f2f9dedd3cd))
* warn when ez doc --output writes outside the current directory ([83adb16](https://github.com/SchoolyB/EZ/commit/83adb1661159d57bddfc0dcc0a8a030b18d60b3a))

## [3.8.4](https://github.com/SchoolyB/EZ/compare/v3.8.3...v3.8.4) (2026-07-06)


### Bug Fixes

* add missing ez_i128_to_u64 and range-check all wide-to-scalar casts ([#2029](https://github.com/SchoolyB/EZ/issues/2029), [#2030](https://github.com/SchoolyB/EZ/issues/2030)) ([f6a45ae](https://github.com/SchoolyB/EZ/commit/f6a45aea00c87753a01b916b1f1946c99923c68a))

## [3.8.3](https://github.com/SchoolyB/EZ/compare/v3.8.2...v3.8.3) (2026-07-05)


### Bug Fixes

* add range checks to cast between int/uint and i64/u64 ([#2026](https://github.com/SchoolyB/EZ/issues/2026)) ([fab59e3](https://github.com/SchoolyB/EZ/commit/fab59e342590eeb5ca71a67670233068f302fc36))
* add runtime panic for invalid bit shift amounts ([#2025](https://github.com/SchoolyB/EZ/issues/2025)) ([37afcb3](https://github.com/SchoolyB/EZ/commit/37afcb34c51c4d151105689f35420406c8b5a423))
* emit E5039 for const expression overflow, suppress C error ([#2028](https://github.com/SchoolyB/EZ/issues/2028)) ([1c517f1](https://github.com/SchoolyB/EZ/commit/1c517f1073e5a6e1714b3d7dbb08c9df164f0be8))
* overflow-check range loop step increment ([#2027](https://github.com/SchoolyB/EZ/issues/2027)) ([248316f](https://github.com/SchoolyB/EZ/commit/248316f996f004857b8b6efbb6a9ba49e10e401d))
* reject incompatible bigint types in arithmetic and comparison ([#2004](https://github.com/SchoolyB/EZ/issues/2004)) ([ab27912](https://github.com/SchoolyB/EZ/commit/ab27912ac303e574124722ec07e02193e28d55e3))
* reject string index assignment with E3004 instead of leaking C error ([#2005](https://github.com/SchoolyB/EZ/issues/2005)) ([594dd13](https://github.com/SchoolyB/EZ/commit/594dd1351b270fd2fc675221ef9405a9b62df768))

## [3.8.2](https://github.com/SchoolyB/EZ/compare/v3.8.1...v3.8.2) (2026-06-30)


### Bug Fixes

* **cli:** cap update download at 256 MiB to prevent disk exhaustion ([#1980](https://github.com/SchoolyB/EZ/issues/1980)) ([2b14809](https://github.com/SchoolyB/EZ/commit/2b14809f18b3b382a3b345a635b7ac827438404c))
* **cli:** redact home directory from C compiler path in ez report ([#1983](https://github.com/SchoolyB/EZ/issues/1983)) ([26eeeb0](https://github.com/SchoolyB/EZ/commit/26eeeb0cce7d71ae82704e5d029653d3068857a5))
* **cli:** reject path traversal sequences in ez pz project name ([#1979](https://github.com/SchoolyB/EZ/issues/1979)) ([badd948](https://github.com/SchoolyB/EZ/commit/badd948405be3979cab3aeb95de96b9d06f6302a))
* **cli:** remove unsupported sized integer/float types from ez man docs ([#1976](https://github.com/SchoolyB/EZ/issues/1976)) ([f660865](https://github.com/SchoolyB/EZ/commit/f660865924b59aa29c5a33ea48fcf9b0ae64f346))
* **cli:** restrict update download URLs to trusted GitHub release origin ([#1978](https://github.com/SchoolyB/EZ/issues/1978)) ([599f8c2](https://github.com/SchoolyB/EZ/commit/599f8c2f839ded18f363556c4a6e4cb117c909fa))
* **codegen:** cast dividend to int64_t in div/mod overflow check to silence Wtautological-constant-out-of-range-compare ([2b753be](https://github.com/SchoolyB/EZ/commit/2b753be1a4b138cfdfe55246960842077c95505f))
* **codegen:** cast of negative float to uint/u64 now panics with P0091 ([#1964](https://github.com/SchoolyB/EZ/issues/1964)) ([37f4d51](https://github.com/SchoolyB/EZ/commit/37f4d519d5f377cb634e4ac282b6813f5af9b38a))
* **codegen:** emit T * const for const pointer variables, not const T * ([#1937](https://github.com/SchoolyB/EZ/issues/1937)) ([381b662](https://github.com/SchoolyB/EZ/commit/381b6628d2a0b68269104a6fa4c4c1f3c4098310))
* **codegen:** fixed-size array [T, N] with partial init now has length N ([#1965](https://github.com/SchoolyB/EZ/issues/1965)) ([9810873](https://github.com/SchoolyB/EZ/commit/9810873d12b81884e2d1863f5667bf7be702c09e))
* **codegen:** map compound assignment now reads existing value before applying op ([#1971](https://github.com/SchoolyB/EZ/issues/1971)) ([c19d6e8](https://github.com/SchoolyB/EZ/commit/c19d6e8c8959b5b850bda4d42788809182ae5e49))
* **codegen:** promote integer literals to double in [float]/[f32]/[f64] array literals ([#1963](https://github.com/SchoolyB/EZ/issues/1963)) ([c4c39b8](https://github.com/SchoolyB/EZ/commit/c4c39b8412462e1e7c8152a416e1f2f25f915246))
* **codegen:** string(Error) now returns the error message instead of leaking C error ([#1977](https://github.com/SchoolyB/EZ/issues/1977)) ([72d3f2d](https://github.com/SchoolyB/EZ/commit/72d3f2d7a7c7a4d64bdfd481730a23057316626e))
* consolidate reserved name lists into shared reserved.h ([#1954](https://github.com/SchoolyB/EZ/issues/1954)) ([a42a410](https://github.com/SchoolyB/EZ/commit/a42a41019a197e05dbd1bf3b15a69df03af3ab83))
* **leaks:** print clear PASS/FAIL summary at end of make leaks output ([#1975](https://github.com/SchoolyB/EZ/issues/1975)) ([141b947](https://github.com/SchoolyB/EZ/commit/141b947b90d10ab539dc3ca8fec947f76e957998))
* **lexer:** catch physical newline in string literal as E1023 ([#1959](https://github.com/SchoolyB/EZ/issues/1959)) ([683203b](https://github.com/SchoolyB/EZ/commit/683203b013b038ea2e041d3aa07f4857bc0d96f3))
* **parser:** accept complex types in multi-return destructuring positions 2+ ([#1962](https://github.com/SchoolyB/EZ/issues/1962)) ([a2b0454](https://github.com/SchoolyB/EZ/commit/a2b0454d0eb523e95a154565627300a2caed7b7e))
* **parser:** emit E2086 when in/not_in/!in has no left-hand value ([#1960](https://github.com/SchoolyB/EZ/issues/1960)) ([de90608](https://github.com/SchoolyB/EZ/commit/de906080604f2534a3cdeb30952a3e4eea3ae919))
* **parser:** support blank identifier _ as for loop variable in range iteration ([#1972](https://github.com/SchoolyB/EZ/issues/1972)) ([3dc99b3](https://github.com/SchoolyB/EZ/commit/3dc99b3de1428de7cc4f7ccf64571288576caddd))
* **runtime:** drain overlong input() lines to prevent stdin contamination ([#1981](https://github.com/SchoolyB/EZ/issues/1981)) ([0576927](https://github.com/SchoolyB/EZ/commit/057692798834c959f4b128fd0c2d3229dcb42b75))
* **runtime:** use pre-addition overflow check in ez_string_concat to eliminate signed UB ([#1984](https://github.com/SchoolyB/EZ/issues/1984)) ([c1af77d](https://github.com/SchoolyB/EZ/commit/c1af77d7bbda0ea927467d02c10fda383001c419))
* **scripts:** handle escaped quotes in error message extraction ([790e2cf](https://github.com/SchoolyB/EZ/commit/790e2cf079d895f3fe328a668c59aa21ca00a602))
* **typechecker:** allow range(n, n) as empty sequence per spec start &lt;= end ([#1969](https://github.com/SchoolyB/EZ/issues/1969)) ([f4683f9](https://github.com/SchoolyB/EZ/commit/f4683f9e0415d69f55e756945301c2821f1e72c4))
* **typechecker:** reject == / != on tagged enums with E3124 ([#1967](https://github.com/SchoolyB/EZ/issues/1967)) ([ce02e3e](https://github.com/SchoolyB/EZ/commit/ce02e3e714ef1caabe999ef044d4d1bf34552428))
* **typechecker:** reject for_each with both positions as _ with E3123 ([#1966](https://github.com/SchoolyB/EZ/issues/1966)) ([7bda22b](https://github.com/SchoolyB/EZ/commit/7bda22b7103d037b3e3a1beefadbc49332641b44))
* **typechecker:** reject tagged enum passed to print functions with E5038 ([#1968](https://github.com/SchoolyB/EZ/issues/1968)) ([d83c720](https://github.com/SchoolyB/EZ/commit/d83c720edf9a3bd92d384fb716d290be49357c97))
* **typechecker:** replace misleading 'only structs' phrasing in E3013 messages ([b0d1695](https://github.com/SchoolyB/EZ/commit/b0d1695160eeee23de1f1130db04188a9610ca36))


### Performance Improvements

* **codegen:** collapse buf_append_indent loop into a single buf_appendn call ([#1953](https://github.com/SchoolyB/EZ/issues/1953)) ([b27f4ed](https://github.com/SchoolyB/EZ/commit/b27f4ede1fe8ddda1219e8253a384c696d3e9a1f))
* **codegen:** eliminate double vsnprintf in emitf for short emissions ([#1952](https://github.com/SchoolyB/EZ/issues/1952)) ([aaa214d](https://github.com/SchoolyB/EZ/commit/aaa214d2866172055ce6a3227704125c9585a28d))
* **codegen:** replace linear scan in is_c_keyword with bsearch ([#1956](https://github.com/SchoolyB/EZ/issues/1956)) ([d2ac0a2](https://github.com/SchoolyB/EZ/commit/d2ac0a2c246ac4eda91621f8a95b68259ffdb936))
* **error:** add cached error/warning counters to DiagnosticList ([#1943](https://github.com/SchoolyB/EZ/issues/1943)) ([b7ac109](https://github.com/SchoolyB/EZ/commit/b7ac10985b2a1ae5ff15bc6404c8c8072005bac6))
* **error:** build line-offset index in diag_set_source ([#1942](https://github.com/SchoolyB/EZ/issues/1942)) ([e0c5a5a](https://github.com/SchoolyB/EZ/commit/e0c5a5a7e2de5dde865c822935ae009a68f49476))
* **error:** replace single-file source cache with 4-slot LRU cache ([#1947](https://github.com/SchoolyB/EZ/issues/1947)) ([48ee72e](https://github.com/SchoolyB/EZ/commit/48ee72e1a1bc974d51a01c8a748690b6526451f1))
* **imports:** limit rewrite_labels outer pass to original main-program nodes ([#1946](https://github.com/SchoolyB/EZ/issues/1946)) ([22b8643](https://github.com/SchoolyB/EZ/commit/22b864314f522771f5d226c2612584b8aa841936))
* **imports:** replace O(n) import-seen scan with FNV-1a open-addressing hash set ([#1951](https://github.com/SchoolyB/EZ/issues/1951)) ([8f5f9e2](https://github.com/SchoolyB/EZ/commit/8f5f9e2217fe4ef869b3db5801d20b2b86f0fdf4))
* **imports:** replace while+rescan with a pending import queue ([#1945](https://github.com/SchoolyB/EZ/issues/1945)) ([b073b80](https://github.com/SchoolyB/EZ/commit/b073b80d6eb830af533d0f45de682889565cbb31))
* **scan:** replace bubble sort in scan_ez_files with qsort ([#1944](https://github.com/SchoolyB/EZ/issues/1944)) ([1946c88](https://github.com/SchoolyB/EZ/commit/1946c888af092c5d2456947a02f9875121b9ea6d))
* **typechecker:** replace O(n) scans in display name and enum helpers with bsearch ([#1948](https://github.com/SchoolyB/EZ/issues/1948)) ([ea19b7c](https://github.com/SchoolyB/EZ/commit/ea19b7cb70dc7c885c287b91d9f0f82c42e84acf))
* **typechecker:** replace O(n) scans in fallible stdlib lookups with bsearch ([#1949](https://github.com/SchoolyB/EZ/issues/1949)) ([521246d](https://github.com/SchoolyB/EZ/commit/521246d2002ddb3d3af85f70f0f255ec07d6c0b0))
* **typechecker:** sort stdlib lookup tables in typechecker_create ([#1955](https://github.com/SchoolyB/EZ/issues/1955)) ([b54a2f6](https://github.com/SchoolyB/EZ/commit/b54a2f67f3dd68883db645b4e0aa4784aef709b3))
* **typechecker:** use stack buffer in levenshtein to avoid malloc on every call ([#1950](https://github.com/SchoolyB/EZ/issues/1950)) ([75f9ed4](https://github.com/SchoolyB/EZ/commit/75f9ed4d87b98f81baafedd2b8847cf349feba23))

## [3.8.1](https://github.com/SchoolyB/EZ/compare/v3.8.0...v3.8.1) (2026-06-23)


### Bug Fixes

* **main:** suppress ld warnings from cc link command with -Wl,-w ([3ab0df7](https://github.com/SchoolyB/EZ/commit/3ab0df775f07e195e61fa42da485df18ceb7e74e))

## [3.8.0](https://github.com/SchoolyB/EZ/compare/v3.7.0...v3.8.0) (2026-06-23)


### Features

* support for_each over inline array literals ([#1931](https://github.com/SchoolyB/EZ/issues/1931)) ([cadb6a8](https://github.com/SchoolyB/EZ/commit/cadb6a8e6894dc27724d5f4e09a82a3b2390d6ba))


### Bug Fixes

* panic P0090 on range() zero step instead of hanging ([#1924](https://github.com/SchoolyB/EZ/issues/1924)) ([#1933](https://github.com/SchoolyB/EZ/issues/1933)) ([efb9739](https://github.com/SchoolyB/EZ/commit/efb9739825739ee4820734d86ddd8567b6e9126b))
* reject addr() on const variables with E3122 ([#1930](https://github.com/SchoolyB/EZ/issues/1930)) ([de5f604](https://github.com/SchoolyB/EZ/commit/de5f604979b09fb3ae7fda17539b2611ca1426dc))
* reject copy() on pointer types with E5037 ([#1928](https://github.com/SchoolyB/EZ/issues/1928)) ([#1936](https://github.com/SchoolyB/EZ/issues/1936)) ([c7a8fac](https://github.com/SchoolyB/EZ/commit/c7a8face9aa9655fe9c5335f4099f14817fb8150))
* reject duplicate default branches in when statements with E2085 ([#1926](https://github.com/SchoolyB/EZ/issues/1926)) ([#1935](https://github.com/SchoolyB/EZ/issues/1935)) ([1074521](https://github.com/SchoolyB/EZ/commit/10745214f5f5d75aa7a5034e5c803e9579d3198f))
* reject nil comparison with non-pointer types with E3092 ([#1929](https://github.com/SchoolyB/EZ/issues/1929)) ([a903d7c](https://github.com/SchoolyB/EZ/commit/a903d7c8b6f204d4236b5c453a569e322028e50f))
* reject pointer ordering comparisons with E3120 ([#1923](https://github.com/SchoolyB/EZ/issues/1923)) ([#1932](https://github.com/SchoolyB/EZ/issues/1932)) ([f035594](https://github.com/SchoolyB/EZ/commit/f0355947944d75c29592cf52f861224cbbe8db2e))
* reject struct, array, map, and pointer as when conditions with E3121 ([#1925](https://github.com/SchoolyB/EZ/issues/1925)) ([#1934](https://github.com/SchoolyB/EZ/issues/1934)) ([2ca1702](https://github.com/SchoolyB/EZ/commit/2ca170218e7bcf81379e6ff59aede37f18ab2c98))

## [3.7.0](https://github.com/SchoolyB/EZ/compare/v3.6.3...v3.7.0) (2026-06-22)


### Features

* **stdlib:** add `os.exec()` for subprocess execution with stderr capture ([c29a8f5](https://github.com/SchoolyB/EZ/commit/c29a8f52181ea3a462824b19f7752c73544a0b6c))
* **stdlib:** add character classification functions to strings module ([#1919](https://github.com/SchoolyB/EZ/issues/1919)) ([4e1608a](https://github.com/SchoolyB/EZ/commit/4e1608a3600f7936303ef515db58ecbf257990cf))
* **stdlib:** add character classification functions to strings module ([#1919](https://github.com/SchoolyB/EZ/issues/1919)) ([9f172c6](https://github.com/SchoolyB/EZ/commit/9f172c6c24e20a018778ee4efabe7447902124fe))
* **stdlib:** add os.exec() for subprocess execution with stderr capture ([#1447](https://github.com/SchoolyB/EZ/issues/1447)) ([9d8741b](https://github.com/SchoolyB/EZ/commit/9d8741bf25721c43c57192334a2a7ee36804e2c6))
* **stdlib:** capture stdout in os.exec, return (int, string, string, bool) ([ec9eaf1](https://github.com/SchoolyB/EZ/commit/ec9eaf1dd5304673ba7ce9a9a16f903f38b01c70))


### Bug Fixes

* **cli:** separate bare ez output from help output ([#1917](https://github.com/SchoolyB/EZ/issues/1917)) ([5eee078](https://github.com/SchoolyB/EZ/commit/5eee07853ab0c76a4964fe7cfd78eec65e020ac6))
* **typechecker:** add stdlib arg count and type validation for math, maps, mem, threads, sync, channels, atomic, fmt, server ([#1918](https://github.com/SchoolyB/EZ/issues/1918)) ([65818c9](https://github.com/SchoolyB/EZ/commit/65818c9992a0e31f19008b8239d1a1eecb9de418))

## [3.6.3](https://github.com/SchoolyB/EZ/compare/v3.6.2...v3.6.3) (2026-06-22)


### Bug Fixes

* **typechecker:** fix segfault from null result in tc_lookup_using_constant ([#1912](https://github.com/SchoolyB/EZ/issues/1912)) ([971760c](https://github.com/SchoolyB/EZ/commit/971760cf1658be4c1d33804b4765c4b44acd7692))
* **typechecker:** reject arrays.contains() with struct element types ([#1901](https://github.com/SchoolyB/EZ/issues/1901)) ([a3e2ef4](https://github.com/SchoolyB/EZ/commit/a3e2ef47f2b39ee40b7252b2eeaf85ea54faa13e))
* **typechecker:** reject maps.contains_value() with struct value types ([#1902](https://github.com/SchoolyB/EZ/issues/1902)) ([1d1774c](https://github.com/SchoolyB/EZ/commit/1d1774ceb96aae47dd7f62059789706634b9ac29))


### Performance Improvements

* **codegen:** replace malloc/free with stack buffer in NODE_MEMBER_EXPR ([#1911](https://github.com/SchoolyB/EZ/issues/1911)) ([eb2a80b](https://github.com/SchoolyB/EZ/commit/eb2a80b263f3f7ceb5341268a9d0d95dca0814de))
* **typechecker:** add hash index to type pool for O(1) lookup ([#1909](https://github.com/SchoolyB/EZ/issues/1909)) ([624a035](https://github.com/SchoolyB/EZ/commit/624a0352940d083eaa4a923fde3f8dbdbddeedef))
* **typechecker:** add hash table per scope for O(1) symbol lookup ([#1910](https://github.com/SchoolyB/EZ/issues/1910)) ([dd092e9](https://github.com/SchoolyB/EZ/commit/dd092e9ec2a9334d22d47830369a3621b2c3c1b9))
* **typechecker:** combine tc_is_using/tc_resolve_using into single-pass lookup ([#1912](https://github.com/SchoolyB/EZ/issues/1912)) ([b3a5ec2](https://github.com/SchoolyB/EZ/commit/b3a5ec2732bba2e66305857a7e9a6290439e4938))
* **typechecker:** replace is_struct_name and is_enum_name linear scans with sorted bsearch ([#1906](https://github.com/SchoolyB/EZ/issues/1906)) ([80fa62e](https://github.com/SchoolyB/EZ/commit/80fa62e98f78f3243d0ed49e57d9186818790325))
* **typechecker:** replace module and builtin name linear scans with bsearch ([#1908](https://github.com/SchoolyB/EZ/issues/1908)) ([2b3975e](https://github.com/SchoolyB/EZ/commit/2b3975ea3edfd4fb1e932b3e6c304e8e94d967f3))
* **typechecker:** replace strdup with arena_strdup in error paths ([#1904](https://github.com/SchoolyB/EZ/issues/1904)) ([06a68a3](https://github.com/SchoolyB/EZ/commit/06a68a36e6b1bc68884a8db68024cb6a5e965cd9))
* **typechecker:** sort stdlib arg tables and use bsearch for lookup ([#1907](https://github.com/SchoolyB/EZ/issues/1907)) ([486edbc](https://github.com/SchoolyB/EZ/commit/486edbc76703eb5aa2403385572bfe02477ad8a3))
* **typechecker:** use sorted bsearch for find_func ([#1905](https://github.com/SchoolyB/EZ/issues/1905)) ([93926c7](https://github.com/SchoolyB/EZ/commit/93926c7f98dd1670fca99f4cc45f416fe8a2899d))

## [3.6.2](https://github.com/SchoolyB/EZ/compare/v3.6.1...v3.6.2) (2026-06-19)


### Bug Fixes

* **cli:** correct errors in pz server and client scaffold templates ([#1888](https://github.com/SchoolyB/EZ/issues/1888)) ([88c709b](https://github.com/SchoolyB/EZ/commit/88c709b49b426d073a5bc84f089c18d334e23b43))
* **cli:** replace return with exit(0) in pz cli template main() ([#1890](https://github.com/SchoolyB/EZ/issues/1890)) ([6ddb39c](https://github.com/SchoolyB/EZ/commit/6ddb39ccff271e8ef8b9fab0b28b79440caa66af))
* **codegen:** escape Error return values from function arena before destruction ([#1895](https://github.com/SchoolyB/EZ/issues/1895)) ([8c94e57](https://github.com/SchoolyB/EZ/commit/8c94e5706ef69affa66e2ee203839f0970d25893))
* **codegen:** materialize rvalue arrays and maps in string interpolation ([#1894](https://github.com/SchoolyB/EZ/issues/1894)) ([159fd7a](https://github.com/SchoolyB/EZ/commit/159fd7a776d4655f54a5cc8f97f3b13411abde03))
* **codegen:** materialize rvalue maps and arrays in json.encode() ([#1898](https://github.com/SchoolyB/EZ/issues/1898)) ([6818385](https://github.com/SchoolyB/EZ/commit/6818385dae0463c6bac855f9f4cf7d6f373dc175))
* **codegen:** recognize or_return temp prefix in fallible stdlib result checks ([#1893](https://github.com/SchoolyB/EZ/issues/1893)) ([1eeb0fc](https://github.com/SchoolyB/EZ/commit/1eeb0fc95debf3938f26aea2da488cff27609549))
* **typechecker:** reject fixed-size array types in function parameters ([#1892](https://github.com/SchoolyB/EZ/issues/1892)) ([257dfda](https://github.com/SchoolyB/EZ/commit/257dfda91c48b464d74210c0b7058838140e6c5b))
* **typechecker:** reject sized integer and float type names used as function calls ([#1896](https://github.com/SchoolyB/EZ/issues/1896)) ([fb3570f](https://github.com/SchoolyB/EZ/commit/fb3570f34a39a0b4eea3450ddafc798f885def3e))
* **typechecker:** validate argument types for server response builder functions ([#1887](https://github.com/SchoolyB/EZ/issues/1887)) ([4c62066](https://github.com/SchoolyB/EZ/commit/4c62066e3df7c6e5e1f8dc624063496f18a37761))

## [3.6.1](https://github.com/SchoolyB/EZ/compare/v3.6.0...v3.6.1) (2026-06-17)


### Bug Fixes

* **codegen:** emit 0 instead of {0} for scalar or_return slots ([#1882](https://github.com/SchoolyB/EZ/issues/1882)) ([d9e6a23](https://github.com/SchoolyB/EZ/commit/d9e6a233da99eba0458d83b8d7674d3af0a38726))
* **codegen:** use outer arena for struct deep copy inside scoped blocks ([#1880](https://github.com/SchoolyB/EZ/issues/1880)) ([c0140aa](https://github.com/SchoolyB/EZ/commit/c0140aac5abeaa698741073383ac2f63e7fb5bd1))
* **parser:** reject blank identifier declarations missing '=' ([#1878](https://github.com/SchoolyB/EZ/issues/1878)) ([c7ec8ad](https://github.com/SchoolyB/EZ/commit/c7ec8adb3517c753a6b98aa3ef73afc30c122db5))
* **tests:** update enum tests ([#1885](https://github.com/SchoolyB/EZ/issues/1885)) ([a56358d](https://github.com/SchoolyB/EZ/commit/a56358d67060680a66b6bf4c58754ede589470c0))
* **typechecker:** reject integer comparison and assignment with enum types ([#1884](https://github.com/SchoolyB/EZ/issues/1884)) ([ad2334e](https://github.com/SchoolyB/EZ/commit/ad2334e818b6b2897c9e275960e2d5842e5c8ee7))
* **typechecker:** unify struct array types across imported files ([#1879](https://github.com/SchoolyB/EZ/issues/1879)) ([ab424fe](https://github.com/SchoolyB/EZ/commit/ab424fe8a83b8205bf8c780721f8d2a2697ee201))

## [3.6.0](https://github.com/SchoolyB/EZ/compare/v3.5.15...v3.6.0) (2026-06-16)


### Features

* **arrays:** add map, filter, reduce higher-order functions ([#1795](https://github.com/SchoolyB/EZ/issues/1795)) ([706ba17](https://github.com/SchoolyB/EZ/commit/706ba174711a23da05af9b7e8102caba21b61be9))
* **compiler:** add tagged union enums with pattern destructuring ([#1709](https://github.com/SchoolyB/EZ/issues/1709)) ([2af9502](https://github.com/SchoolyB/EZ/commit/2af9502783e28815ed91edbd54b4af575359973b))
* **fmt:** add printfln, eprintf, eprintfln, sprintfln functions ([#1838](https://github.com/SchoolyB/EZ/issues/1838)) ([ac94620](https://github.com/SchoolyB/EZ/commit/ac9462050f5110c84a84c4832bf6d5d76572974c))
* implicit enum selector .VARIANT syntax ([#1850](https://github.com/SchoolyB/EZ/issues/1850)) ([61fd916](https://github.com/SchoolyB/EZ/commit/61fd916dcfb0ad76dc1e88aad14292f9ae2671ad))
* named function arguments ([639dd40](https://github.com/SchoolyB/EZ/commit/639dd40722fd01c6f1fbbaebaa478ada5fc1379b))
* **parser,typechecker:** support named function arguments ([#1840](https://github.com/SchoolyB/EZ/issues/1840)) ([52171e1](https://github.com/SchoolyB/EZ/commit/52171e1a904c0df6afd33bf7fc73e6ac7c598927))
* **stdlib:** add `map`, `filter`, `reduce` functions to arrays stdlib module ([3ee4f3c](https://github.com/SchoolyB/EZ/commit/3ee4f3cb4dbbc31de2550989305a3de5d2a9153a))
* **stdlib:** add `printfln`, `eprintf`, `eprintfln`, `sprintfln` to `fmt` stdlib module ([9b2e27a](https://github.com/SchoolyB/EZ/commit/9b2e27af406f8c75945de946a784f6fdebdaaffc))
* struct default values ([b444ee9](https://github.com/SchoolyB/EZ/commit/b444ee91670779c0d779ab4eda0a19c52b579905))
* **structs:** add default field values for struct declarations ([#1794](https://github.com/SchoolyB/EZ/issues/1794)) ([ad26dd2](https://github.com/SchoolyB/EZ/commit/ad26dd2afe9622bad3bec9e33e8d6db5f5e953f6))
* tagged union enums with pattern destructuring ([7f66e96](https://github.com/SchoolyB/EZ/commit/7f66e96f1e2b3694c6c73739e4232bbb2e752c61))
* **typechecker:** add implicit enum selector .VARIANT syntax ([#1850](https://github.com/SchoolyB/EZ/issues/1850)) ([f154522](https://github.com/SchoolyB/EZ/commit/f1545224898d1786f4d508742cf411339f896fdf))
* **uuid:** add UUID struct type to uuid module ([5b92b93](https://github.com/SchoolyB/EZ/commit/5b92b9318af3ba9a3726be3c4faee3af7160ed2f))
* **uuid:** add UUID struct type to uuid module ([#1870](https://github.com/SchoolyB/EZ/issues/1870)) ([5f0b2a7](https://github.com/SchoolyB/EZ/commit/5f0b2a79cce0003417cb1ebec6f9599fbfe9b81e))


### Bug Fixes

* **codegen:** handle default params and mutable refs in cross-module using calls ([#1872](https://github.com/SchoolyB/EZ/issues/1872)) ([c6ce374](https://github.com/SchoolyB/EZ/commit/c6ce374eda13b2d045f78aeb8f9700bb85829ad1))
* **codegen:** prefix default-value bindings to prevent variable shadowing ([#1840](https://github.com/SchoolyB/EZ/issues/1840)) ([bf0e393](https://github.com/SchoolyB/EZ/commit/bf0e393bd33e45bae229abfb653d677687c1b2c1))
* **codegen:** strip module prefixes from struct/enum names in println output ([9b0bcf4](https://github.com/SchoolyB/EZ/commit/9b0bcf4ec45cf1911ca478711ce62396120bbd49))
* **codegen:** use bare param names in default-value statement expressions ([a076141](https://github.com/SchoolyB/EZ/commit/a076141fdee3e99d20684507ed0cbc4d7a75418f))
* **parser:** also reject reserved type names as struct field names ([#1859](https://github.com/SchoolyB/EZ/issues/1859)) ([62ca179](https://github.com/SchoolyB/EZ/commit/62ca179e3d5afe6b4acd37130be75e3fdce493ba))
* **parser:** enforce separate lines for all struct fields and enum variants ([#1867](https://github.com/SchoolyB/EZ/issues/1867)) ([f50b5c3](https://github.com/SchoolyB/EZ/commit/f50b5c3ae1e6ed2c6c4fee3b4e9a10aadad4777e))
* **parser:** reject reserved keywords as struct field names ([#1859](https://github.com/SchoolyB/EZ/issues/1859)) ([db595c5](https://github.com/SchoolyB/EZ/commit/db595c57f1b6af856355c2ede54592c828d92737))
* **parser:** restrict 'for x in' to range() and add for_each shadowing warning ([d3b4384](https://github.com/SchoolyB/EZ/commit/d3b4384d1ea71329e79b09288fb05eef36b6733c))
* **stdlib:** remove function name aliases csv.decode, csv.format, json.format ([189b7f1](https://github.com/SchoolyB/EZ/commit/189b7f1e261a9b377ceb23309c14f13f742973ec))
* **tests:** remove stale E3042 unit and integration tests ([a076141](https://github.com/SchoolyB/EZ/commit/a076141fdee3e99d20684507ed0cbc4d7a75418f))
* **tests:** remove stale E3042 unit test ([28bc4a0](https://github.com/SchoolyB/EZ/commit/28bc4a024be6509b956c86ca35a145d45c9f84f9))
* **tests:** update tests using reserved builtin/stdlib/type names ([7987209](https://github.com/SchoolyB/EZ/commit/798720984a444c504b3a4a5b963aeb67bb1d90bd))
* **typechecker:** accept stdlib struct types in struct field declarations ([#1870](https://github.com/SchoolyB/EZ/issues/1870)) ([5ede4d1](https://github.com/SchoolyB/EZ/commit/5ede4d150f3583d0fa12dec2f6e3f34ada4eab2d))
* **typechecker:** account for default params in cross-module function calls ([#1872](https://github.com/SchoolyB/EZ/issues/1872)) ([cc7c6cd](https://github.com/SchoolyB/EZ/commit/cc7c6cdbfea7fd612f8198fb68af1afc35723020))
* **typechecker:** add missing arg type validation for strings stdlib functions ([#1875](https://github.com/SchoolyB/EZ/issues/1875)) ([308108a](https://github.com/SchoolyB/EZ/commit/308108ab1d163ee8e45da838af344adbced177bf))
* **typechecker:** allow instance dispatch for all struct functions ([#1848](https://github.com/SchoolyB/EZ/issues/1848)) ([367ef74](https://github.com/SchoolyB/EZ/commit/367ef744231f1f5ca6436466688e3ec450f43b59))
* **typechecker:** block builtin and stdlib names from all identifier positions ([#1859](https://github.com/SchoolyB/EZ/issues/1859)) ([3fbb037](https://github.com/SchoolyB/EZ/commit/3fbb037c8d3454e558b4a1d8efb84bfeedd80bf0))
* **typechecker:** compare display names for cross-module struct/enum type matching ([#1873](https://github.com/SchoolyB/EZ/issues/1873)) ([3165652](https://github.com/SchoolyB/EZ/commit/3165652112166b20abac2602b9788fd0a3434ab3))
* **typechecker:** prevent transitive import type leaking ([bc37dd9](https://github.com/SchoolyB/EZ/commit/bc37dd96291701167dcf788fa4c68d91011c670e))
* **typechecker:** reject arithmetic and ordering on enum variables ([#1858](https://github.com/SchoolyB/EZ/issues/1858)) ([0cb5d15](https://github.com/SchoolyB/EZ/commit/0cb5d15bd49ba61ca724c673c8282b03a69b7048))
* **typechecker:** reject comparison of different enum types ([#1865](https://github.com/SchoolyB/EZ/issues/1865)) ([f64aa67](https://github.com/SchoolyB/EZ/commit/f64aa6799f3e65f4c3b7d9131284489d840ffec9))
* **typechecker:** reject enum constants passed to mutable parameters ([#1844](https://github.com/SchoolyB/EZ/issues/1844)) ([0e7f608](https://github.com/SchoolyB/EZ/commit/0e7f608c070f9fe7d9ee98cb905a397b6f26451b))
* **typechecker:** reject mixed enum types in array literals ([#1862](https://github.com/SchoolyB/EZ/issues/1862)) ([0838368](https://github.com/SchoolyB/EZ/commit/0838368bce6d6c62c10f528269a3198bac370583))
* **typechecker:** reject multi-return destructuring with too few variables ([#1860](https://github.com/SchoolyB/EZ/issues/1860)) ([afbad4c](https://github.com/SchoolyB/EZ/commit/afbad4c32abfeff4bf621a7ef545b883deecf341))
* **typechecker:** reject return of wrong enum type ([#1864](https://github.com/SchoolyB/EZ/issues/1864)) ([3f5fb02](https://github.com/SchoolyB/EZ/commit/3f5fb0205af099354f66954e4c8402cfbb4f3dc2))
* **typechecker:** reject string() on array, map, struct, pointer with E3043 ([#1853](https://github.com/SchoolyB/EZ/issues/1853), [#1854](https://github.com/SchoolyB/EZ/issues/1854)) ([f4146fa](https://github.com/SchoolyB/EZ/commit/f4146faebe8387b2d58819498e11394bcd507427))
* **typechecker:** reject string() on string values with E3043 ([#1852](https://github.com/SchoolyB/EZ/issues/1852)) ([f3d42ee](https://github.com/SchoolyB/EZ/commit/f3d42ee50e9be6267003e033528d922861d66938))
* **typechecker:** reject wrong enum type in map literal keys and values ([#1863](https://github.com/SchoolyB/EZ/issues/1863)) ([1b6d845](https://github.com/SchoolyB/EZ/commit/1b6d8459cd0c55785e4a83348c239478f185fdb6))
* **typechecker:** skip early resolve for implicit enum args, propagate array-of-enum expected_type ([#1850](https://github.com/SchoolyB/EZ/issues/1850)) ([acc0857](https://github.com/SchoolyB/EZ/commit/acc08573c641fc2961cf27d979410642c28b93db))
* **typechecker:** strip module prefix from struct name in all error messages ([#1866](https://github.com/SchoolyB/EZ/issues/1866)) ([25aff14](https://github.com/SchoolyB/EZ/commit/25aff1451688664729c5e46fd266f2dc2e6d4717))
* **typechecker:** strip module prefixes from user-facing error messages ([cfeffde](https://github.com/SchoolyB/EZ/commit/cfeffde793b6ea0935ac0e0f52b5e6c54854d716))
* **typechecker:** use type_display_name in struct function arg errors ([#1866](https://github.com/SchoolyB/EZ/issues/1866)) ([8c4af9a](https://github.com/SchoolyB/EZ/commit/8c4af9ad2649845ddeae699e2a3f64bfa625881d))
* **typechecker:** validate mutable params in instance dispatch path ([#1845](https://github.com/SchoolyB/EZ/issues/1845)) ([fb98a9c](https://github.com/SchoolyB/EZ/commit/fb98a9ca6a24a4a76c40b73872eeb3c600941701))
* **typechecker:** warn when function parameter shadows enum variant ([#1861](https://github.com/SchoolyB/EZ/issues/1861)) ([26f8e1d](https://github.com/SchoolyB/EZ/commit/26f8e1d19913e856ce259d372d9db306f0d25d52))

## [3.5.15](https://github.com/SchoolyB/EZ/compare/v3.5.14...v3.5.15) (2026-06-13)


### Bug Fixes

* **parser:** backfill default values across grouped params ([#1823](https://github.com/SchoolyB/EZ/issues/1823)) ([ce8b999](https://github.com/SchoolyB/EZ/commit/ce8b9996c9daf2470c532d75007d3664feebfe8c))
* typechecker & parser improvements ([7b42747](https://github.com/SchoolyB/EZ/commit/7b427476c577e5207d111477f1a20ba14059582e))
* **typechecker:** enforce private on struct function references ([#1835](https://github.com/SchoolyB/EZ/issues/1835)) ([3b82944](https://github.com/SchoolyB/EZ/commit/3b829443e148c89b493a591132f376b80aab8be3))
* **typechecker:** reject func references to builtin and stdlib functions ([#1832](https://github.com/SchoolyB/EZ/issues/1832)) ([2306015](https://github.com/SchoolyB/EZ/commit/23060154f7215a3bf31f865daa2e680f43f5ad44))
* **typechecker:** reject func-typed fields on #json structs ([#1836](https://github.com/SchoolyB/EZ/issues/1836)) ([63746f3](https://github.com/SchoolyB/EZ/commit/63746f355497fbbbc5b7e8bdef76f5ac69f24b91))
* **typechecker:** reject struct functions declared on #json structs ([#1837](https://github.com/SchoolyB/EZ/issues/1837)) ([962cc8f](https://github.com/SchoolyB/EZ/commit/962cc8fa60e1e2e19a033a6eb9c9cb9978c4325e))
* **typechecker:** reject unknown directives, arg count mismatches, and dangling % in fmt format strings ([#1839](https://github.com/SchoolyB/EZ/issues/1839)) ([e306837](https://github.com/SchoolyB/EZ/commit/e30683734c6d5a42078c9ea5d390ecce2421a5fc))

## [3.5.14](https://github.com/SchoolyB/EZ/compare/v3.5.13...v3.5.14) (2026-06-11)


### Bug Fixes

* **codegen:** println(ref(x)) now prints value instead of address ([#1819](https://github.com/SchoolyB/EZ/issues/1819)) ([7764562](https://github.com/SchoolyB/EZ/commit/7764562915b6dea71ad8ecc4b9e4fe6d94f855e8))
* **codegen:** use -&gt; instead of . when calling func reference in struct field via pointer ([#1830](https://github.com/SchoolyB/EZ/issues/1830)) ([6fcee13](https://github.com/SchoolyB/EZ/commit/6fcee1338e7b454302928c9848b900d5c4a68d81))
* **parser,codegen:** allow bare func as map value type ([#1831](https://github.com/SchoolyB/EZ/issues/1831)) ([0b6163f](https://github.com/SchoolyB/EZ/commit/0b6163fb065a85038798ba9f5400e3db777694a7))
* **parser:** reject typed func signature as array element type with E2082 ([e6882a8](https://github.com/SchoolyB/EZ/commit/e6882a800460b9a5744ec290bef5ef2a2a145ed6))
* **tests:** update func ref tests to use const and remove invalid reassign test ([4558676](https://github.com/SchoolyB/EZ/commit/45586765cdceab92b50bb6787e5344f24aebe3f5))
* **typechecker:** reject copy() on func references ([#1820](https://github.com/SchoolyB/EZ/issues/1820)) ([77cca61](https://github.com/SchoolyB/EZ/commit/77cca61161c8d21fe24fda38433dfd783b016341))
* **typechecker:** reject fmt.println as unknown fmt function ([b964d64](https://github.com/SchoolyB/EZ/commit/b964d641a35410cc696d9de776367122a2b4e833))
* **typechecker:** reject func references passed to print functions ([#1821](https://github.com/SchoolyB/EZ/issues/1821)) ([f49a271](https://github.com/SchoolyB/EZ/commit/f49a27126d7617b3f9e7dc99ec4e87a37fc121a0))
* **typechecker:** reject func-type return values assigned to variables and chained calls ([#1829](https://github.com/SchoolyB/EZ/issues/1829)) ([1e70edf](https://github.com/SchoolyB/EZ/commit/1e70edf790e4582137acf0b35d860be987c0b166))
* **typechecker:** reject mismatched func ref assigned to struct field ([#1822](https://github.com/SchoolyB/EZ/issues/1822)) ([c675fee](https://github.com/SchoolyB/EZ/commit/c675fee42ec225c3270743c8b34f2ffdc72afa40))
* **typechecker:** reject mismatched func ref in typed return statement ([#1817](https://github.com/SchoolyB/EZ/issues/1817)) ([2459c5b](https://github.com/SchoolyB/EZ/commit/2459c5b0e989f4891f4f4c9572a6cf10b431da0d))
* **typechecker:** reject mut on func reference variable declarations ([#1818](https://github.com/SchoolyB/EZ/issues/1818)) ([2271ba4](https://github.com/SchoolyB/EZ/commit/2271ba41077c11aefa9b3bbf2bfb0dc4277b4b10))

## [3.5.13](https://github.com/SchoolyB/EZ/compare/v3.5.12...v3.5.13) (2026-06-10)


### Bug Fixes

* **codegen:** cast integer literals to int64_t in shift expressions to prevent UB for shifts &gt;= 32 ([62fd12e](https://github.com/SchoolyB/EZ/commit/62fd12e97805ae2cd555a741245dcb179fce5685))
* **codegen:** emit bigint constructor/extractor functions for wide integer casts ([#1803](https://github.com/SchoolyB/EZ/issues/1803)) ([eea42c8](https://github.com/SchoolyB/EZ/commit/eea42c8a7c368e1b0acd750184f72bcb5b60a3dd))
* **codegen:** implicit scalar-to-bigint assignment and compound operators on wide integer types ([#1805](https://github.com/SchoolyB/EZ/issues/1805), [#1806](https://github.com/SchoolyB/EZ/issues/1806)) ([27a3356](https://github.com/SchoolyB/EZ/commit/27a33563a75d16685e00cb439c56b43a8b46fc6d))
* **codegen:** prevent double-wrap when assigning cast(expr, bigint) to bigint var ([#1803](https://github.com/SchoolyB/EZ/issues/1803)) ([e0d9ae9](https://github.com/SchoolyB/EZ/commit/e0d9ae93463685151720116a55a92fbac3d81554))
* **codegen:** prevent double-wrap when negating bigint variable ([676999d](https://github.com/SchoolyB/EZ/commit/676999d5da1453bf2b56efd68a0ee01c4f95e3c8))
* **codegen:** size_of on literals returns correct size, type_of on inferred wide ints returns specific type ([#1809](https://github.com/SchoolyB/EZ/issues/1809), [#1810](https://github.com/SchoolyB/EZ/issues/1810)) ([191dd5e](https://github.com/SchoolyB/EZ/commit/191dd5e2bb327e84583136f2c490f34424c6b9e8))
* **codegen:** use bigint eq functions in when/is for wide integer types ([#1804](https://github.com/SchoolyB/EZ/issues/1804)) ([51089e7](https://github.com/SchoolyB/EZ/commit/51089e7ffc96a9d3d70b70fd65f33cddf7335e9e))
* **codegen:** use println_str for string enum values in println/print/eprint ([#1801](https://github.com/SchoolyB/EZ/issues/1801)) ([ad90143](https://github.com/SchoolyB/EZ/commit/ad9014313032b00aca7eb908bba894d6065c141f))
* **errors:** remove angle bracket placeholders from E3065 and E3069 error messages ([225eaef](https://github.com/SchoolyB/EZ/commit/225eaefced5fcba8128e97e33b47fb1bb3529ecb))
* **parser:** support custom fallback values in or_return ([#1808](https://github.com/SchoolyB/EZ/issues/1808)) ([da9f01d](https://github.com/SchoolyB/EZ/commit/da9f01dd5a88a88725066fc89ac2875320f00434))
* **tests:** remove cast(string, int) test case from cast_keyword (rejected by E3043) ([3639475](https://github.com/SchoolyB/EZ/commit/3639475e813c8813fa5ea20ad308ae32c40ee78b))
* **tests:** remove cast(string, int/float) test cases from cast_conversions (rejected by E3043) ([780e9bb](https://github.com/SchoolyB/EZ/commit/780e9bb877b788c146429976dea5d8181a210e07))
* **tests:** use typed enum array in stress_enums (correct element sizing) ([69341e3](https://github.com/SchoolyB/EZ/commit/69341e3724a65b4591a1404da4bfc0207553c1c9))
* **typechecker,codegen:** restore string enum ↔ string interop ([#1801](https://github.com/SchoolyB/EZ/issues/1801)) ([f67c08d](https://github.com/SchoolyB/EZ/commit/f67c08d9006fb9147d4e18022a0e4af7e9e9943f))
* **typechecker,codegen:** string enum type tracking and comparisons ([#1801](https://github.com/SchoolyB/EZ/issues/1801)) ([158a6c9](https://github.com/SchoolyB/EZ/commit/158a6c9f0901ca6beb859f9c93adc0498dea1a95))
* **typechecker:** reject cast(string, int/float/uint) with E3043 ([#1807](https://github.com/SchoolyB/EZ/issues/1807)) ([c5294c3](https://github.com/SchoolyB/EZ/commit/c5294c35fe7cfdc854f5cda9326e154bf840c442))
* **typechecker:** string enum interop with when/is, return types, and array element assignment ([#1801](https://github.com/SchoolyB/EZ/issues/1801)) ([1ccf16a](https://github.com/SchoolyB/EZ/commit/1ccf16a57312470f0d3446c9c186beeef0cca9cf))

## [3.5.12](https://github.com/SchoolyB/EZ/compare/v3.5.11...v3.5.12) (2026-06-07)


### Bug Fixes

* **cli:** restore formatEZSource for fmt unit tests ([24e28ff](https://github.com/SchoolyB/EZ/commit/24e28ffec5f5092940ce5f919cc6b9c5989eae9c))
* **codegen:** correct uuid.generate() mapping and wire missing generate_compact ([aa28123](https://github.com/SchoolyB/EZ/commit/aa28123b6b01e514fc0769d83826437e66142381))
* **codegen:** restore correct uuid.generate() → compact mapping ([67ddafc](https://github.com/SchoolyB/EZ/commit/67ddafcbd6f1ac492bce0aa53c7540dd8230eed0))
* **fmt:** implement lexer-based indentation engine for ez fmt ([984425f](https://github.com/SchoolyB/EZ/commit/984425ff00308b5bc251f5e804897e5f27407455))
* **fmt:** implement lexer-based indentation engine for ez fmt ([bdc0fe8](https://github.com/SchoolyB/EZ/commit/bdc0fe87c580a00e1d42d59f248d89ba7bda3303))
* **parser:** reject postfix ^ in return type with E2081 ([223e3c7](https://github.com/SchoolyB/EZ/commit/223e3c763801305e51961f8f48f3182dc83cc906))
* **tests:** replace httpbin.org with httpbun.com for reliable CI ([6989944](https://github.com/SchoolyB/EZ/commit/69899447a0a153e33e716e4386dea79ec3e0c1f1))
* **typechecker:** add W3005 and W3006 warnings for unsafe when statement patterns ([55bcfe7](https://github.com/SchoolyB/EZ/commit/55bcfe7b6271291b3e5006fe87e2fa1f8b2693ff))
* **typechecker:** catch struct/enum type names used as value arguments ([#1797](https://github.com/SchoolyB/EZ/issues/1797)) ([6f2ac19](https://github.com/SchoolyB/EZ/commit/6f2ac197809cd565ac92af3b7110926631dab727))
* **typechecker:** remove phantom arrays.insert, set, and sum functions ([13f6214](https://github.com/SchoolyB/EZ/commit/13f621405839599ab9bcc34afa6660e4f69edb19))
* **typechecker:** remove phantom math.mod/pi/e/tau function registrations ([b952bc0](https://github.com/SchoolyB/EZ/commit/b952bc0df01db20836f429962e11fdc9809a64fc))
* **typechecker:** remove phantom threads.sleep_s/sleep_ms/sleep_ns — sleep() is the only threads sleep, _s/_ms/_ns are builtins ([ab8d7bd](https://github.com/SchoolyB/EZ/commit/ab8d7bde15a096159d983f756982db41be259b10))
* **typechecker:** resolve imported constants in global array initializers ([611d3f2](https://github.com/SchoolyB/EZ/commit/611d3f246f2d0eceefe601106bc052cb9ec811ee))
* **typechecker:** skip false-positive E3001 when wildcard arg is unresolved in main pass ([3b2d2fb](https://github.com/SchoolyB/EZ/commit/3b2d2fb7a751262520fe4e219cf80c15a53ef0fc))
* **typechecker:** validate io.write_file and io.append_file content arg type ([dcf4b61](https://github.com/SchoolyB/EZ/commit/dcf4b61f015016f7c13a8b076c3815452604c264))

## [3.5.11](https://github.com/SchoolyB/EZ/compare/v3.5.10...v3.5.11) (2026-06-06)


### Bug Fixes

* **codegen:** add cycle guards to prevent segfault on circular struct references ([#1784](https://github.com/SchoolyB/EZ/issues/1784)) ([0fbc1ac](https://github.com/SchoolyB/EZ/commit/0fbc1ace2f17c915e427d4f19bf88b6d98175a38))
* **codegen:** deep-copy array fields from struct member access on assignment ([#1789](https://github.com/SchoolyB/EZ/issues/1789)) ([e1d9314](https://github.com/SchoolyB/EZ/commit/e1d93149548ffb3a151fa74edcf49f78cd1e6c73))
* **codegen:** fix addr() on pointer-deref field generating invalid C ([#1785](https://github.com/SchoolyB/EZ/issues/1785)) ([40a1010](https://github.com/SchoolyB/EZ/commit/40a1010d543748a324ed3bbdae1f0b66d6a54166))
* **codegen:** fix array field indexing through struct pointer generating invalid C ([#1787](https://github.com/SchoolyB/EZ/issues/1787)) ([f1ad1b9](https://github.com/SchoolyB/EZ/commit/f1ad1b920c72f43398c53b0a9c9735dd78a7a0cb))
* **codegen:** infer correct type for pointer dereference assignments ([#1786](https://github.com/SchoolyB/EZ/issues/1786)) ([a336d74](https://github.com/SchoolyB/EZ/commit/a336d74a548409c4fda8943c2db70b81586a610a))
* **main:** use fork+execv instead of system() in run mode ([#1285](https://github.com/SchoolyB/EZ/issues/1285)) ([f36dab9](https://github.com/SchoolyB/EZ/commit/f36dab9251fcf4388b9f9aacc73833b62f16d4cb))
* pointer/struct bug fixes ([4f72660](https://github.com/SchoolyB/EZ/commit/4f72660d844535c23966e842eda48fdc6bb1fdb6))
* **typechecker:** catch struct type mismatch in pointer dereference assignment ([#1788](https://github.com/SchoolyB/EZ/issues/1788)) ([8019d7a](https://github.com/SchoolyB/EZ/commit/8019d7add8d0f84e387bcd5a8cf08b594338ef7b))
* **typechecker:** catch struct type mismatch in struct field assignment and nested literals ([#1790](https://github.com/SchoolyB/EZ/issues/1790)) ([f5b015b](https://github.com/SchoolyB/EZ/commit/f5b015b393e0a127bf7f98bca9238ee2a974ba8a))
* **typechecker:** reject reserved stdlib type names as struct names ([#1791](https://github.com/SchoolyB/EZ/issues/1791)) ([9f4c4ac](https://github.com/SchoolyB/EZ/commit/9f4c4ac7a8efe8a96cb404fd65bf5f68051aa99d))

## [3.5.10](https://github.com/SchoolyB/EZ/compare/v3.5.9...v3.5.10) (2026-06-05)


### Bug Fixes

* **compiler:** quote output path in run-mode shell execution ([#1703](https://github.com/SchoolyB/EZ/issues/1703)) ([e340b2e](https://github.com/SchoolyB/EZ/commit/e340b2ea5a13de41745a6abf235788a75fc67aee))
* **stdlib:** reject CR/LF in HTTP URL host and path components ([#1703](https://github.com/SchoolyB/EZ/issues/1703)) ([8b7a5ef](https://github.com/SchoolyB/EZ/commit/8b7a5ef7b8b6f8df1d6db38b8e189cb18539d3ea))
* **typechecker:** validate embed() path stays within source directory ([#1703](https://github.com/SchoolyB/EZ/issues/1703)) ([cab23c0](https://github.com/SchoolyB/EZ/commit/cab23c016477c6dc2a5b3d1bfde87e23ddd857a8))

## [3.5.9](https://github.com/SchoolyB/EZ/compare/v3.5.8...v3.5.9) (2026-06-05)


### Bug Fixes

* codegen & typechecker bugs ([73aa742](https://github.com/SchoolyB/EZ/commit/73aa742b04f0db34d5f4c9b43e57fa3c8e6e08b2))
* **codegen/typechecker:** monomorphize wildcard struct functions per call site ([591c330](https://github.com/SchoolyB/EZ/commit/591c330870808adb7b3182dd9a3f42cc281bf21c))
* **codegen:** emit EzMulti typedef for struct-scoped multi-return functions ([3e62879](https://github.com/SchoolyB/EZ/commit/3e62879cf5e6214175beea5de78bb3319fbd57e8)), closes [#1776](https://github.com/SchoolyB/EZ/issues/1776)
* **codegen:** resolve correct print variant for wildcard params ([1bca397](https://github.com/SchoolyB/EZ/commit/1bca39732126bdadfa0c40524af5f737166abd98)), closes [#1774](https://github.com/SchoolyB/EZ/issues/1774)
* **stdlib/codegen:** add missing eprint builtin variants and fix dispatch ([a9a37cc](https://github.com/SchoolyB/EZ/commit/a9a37ccf8e08febce5a9d608a3bb6be94b07b4d6))
* **stdlib:** add missing eprintln float and bool builtin variants ([b9652d5](https://github.com/SchoolyB/EZ/commit/b9652d5e02232e43c6cb1757a5badfa6ad058a0c))
* **typechecker:** allow '?' as struct field type in field-type validation ([f986db8](https://github.com/SchoolyB/EZ/commit/f986db827f0b3805665956d1208f5749ce401ef7))
* **typechecker:** auto-deref pointer fields in nested member access ([dc9f42e](https://github.com/SchoolyB/EZ/commit/dc9f42ec6354620541ed8277d430c9237bcd486d)), closes [#1777](https://github.com/SchoolyB/EZ/issues/1777)

## [3.5.8](https://github.com/SchoolyB/EZ/compare/v3.5.7...v3.5.8) (2026-06-04)


### Bug Fixes

* **codegen,typechecker:** remove unimplemented http module stubs request and json_body ([#1773](https://github.com/SchoolyB/EZ/issues/1773)) ([4fb7169](https://github.com/SchoolyB/EZ/commit/4fb7169a79c87fd799a4da717a1cdfd65f4c3b39))
* **codegen,typechecker:** remove unimplemented server.parse_json stub ([#1701](https://github.com/SchoolyB/EZ/issues/1701)) ([87f3f2e](https://github.com/SchoolyB/EZ/commit/87f3f2e2c36a20ce960a7f043a216cf42205a623))
* **typechecker:** correct json module tracking table entry from pretty to pretty_print ([#1773](https://github.com/SchoolyB/EZ/issues/1773)) ([2244d83](https://github.com/SchoolyB/EZ/commit/2244d8321ced74f9c3d6e4cf6ea7c91429a7811e))
* **typechecker:** json.decode() now returns concrete map[string:string] type when destructured ([#1773](https://github.com/SchoolyB/EZ/issues/1773)) ([62fb26d](https://github.com/SchoolyB/EZ/commit/62fb26dd5d799a62e5dc4a6922cc3ba3d1714f07))

## [3.5.7](https://github.com/SchoolyB/EZ/compare/v3.5.6...v3.5.7) (2026-06-03)


### Bug Fixes

* type system improvements and struct field validation ([c954917](https://github.com/SchoolyB/EZ/commit/c954917955309e10b3369613b30588470b8708a0))
* **typechecker,parser:** infer parameter type from enum member default value ([8e19118](https://github.com/SchoolyB/EZ/commit/8e19118a74450ba2413f916b78bbc1f09ef54e44))
* **typechecker:** catch undefined types in struct fields for arrays, maps, and pointers recursively ([27d8466](https://github.com/SchoolyB/EZ/commit/27d84665e9a0334e401137f73ae06ea5d46edc38))
* **typechecker:** intern dynamic types and raise pool cap to 4096 ([#1770](https://github.com/SchoolyB/EZ/issues/1770)) ([73cf194](https://github.com/SchoolyB/EZ/commit/73cf1947fae1c4f8c0d434f5fd33b47999a0c64b))

## [3.5.6](https://github.com/SchoolyB/EZ/compare/v3.5.5...v3.5.6) (2026-06-03)


### Bug Fixes

* **codegen,runtime:** byte + negative int fires P0016 instead of P0015 ([#1755](https://github.com/SchoolyB/EZ/issues/1755)) ([69f21d5](https://github.com/SchoolyB/EZ/commit/69f21d5326c521190b2bd1ebd81905c96d8600fb))
* **codegen,typechecker:** arrays.append pointer C error + E3097 dangling addr ([c9926ee](https://github.com/SchoolyB/EZ/commit/c9926ee63108662d660e0d092a04b36f2ef97c22))
* **codegen:** runtime panic on narrowing integer assignment and call args ([#1753](https://github.com/SchoolyB/EZ/issues/1753)) ([853f103](https://github.com/SchoolyB/EZ/commit/853f103457b09831dec2f3949d0afe7c4ff926c1))
* compiler fixes ([b79b857](https://github.com/SchoolyB/EZ/commit/b79b85746ec47ca854787e1966e51eaff24b58ee))
* **runtime,codegen:** deep-copy string key data in ez_map_copy and emit_map_deep_copy ([d860f39](https://github.com/SchoolyB/EZ/commit/d860f39e3bd599472610425ca96eb1b9c1ba0916))
* **typechecker,codegen:** assert double message ([#1766](https://github.com/SchoolyB/EZ/issues/1766)) + E3097 for addr() in call args ([#1767](https://github.com/SchoolyB/EZ/issues/1767)) ([b3b0df8](https://github.com/SchoolyB/EZ/commit/b3b0df8282f6e4e7cb5b0cd65e2083e5a067b405))
* **typechecker,codegen:** byte() returns byte type; bit_not byte masks to 8 bits; W3004→E3097 ([50c0057](https://github.com/SchoolyB/EZ/commit/50c00575cdf3b0578b41f47635069f30390d63da))
* **typechecker:** catch pointer-pointee mismatches across all assignment and comparison paths ([#1746](https://github.com/SchoolyB/EZ/issues/1746)-[#1751](https://github.com/SchoolyB/EZ/issues/1751)) ([39ac98c](https://github.com/SchoolyB/EZ/commit/39ac98cd4c4c775d8d603f9975af880c72460fa4))
* **typechecker:** catch pointer-to-pointer type mismatch in var decls ([#1745](https://github.com/SchoolyB/EZ/issues/1745)) ([1c5d00f](https://github.com/SchoolyB/EZ/commit/1c5d00f77a277269371664b50d4f8980ab6b264c))
* **typechecker:** narrow bigint rank check to vr&gt;=5 to avoid false positives on int literals ([057ae83](https://github.com/SchoolyB/EZ/commit/057ae830ab9712be6bc710854095249e784348cc))
* **typechecker:** raise E3001 on implicit bigint narrowing in var_decl, reassignment, and call args ([#1759](https://github.com/SchoolyB/EZ/issues/1759)) ([5978c49](https://github.com/SchoolyB/EZ/commit/5978c49016e8c3bea259cf27648a267f7d512bdc))

## [3.5.5](https://github.com/SchoolyB/EZ/compare/v3.5.4...v3.5.5) (2026-06-02)


### Bug Fixes

* handle big-int pointer dereference in println and deref-assign ([9a06c7c](https://github.com/SchoolyB/EZ/commit/9a06c7c6a848b33e2eca3f49e95877243932b9e9)), closes [#1761](https://github.com/SchoolyB/EZ/issues/1761)
* reject implicit byte/u8 assignment — they are distinct types ([7d6e55f](https://github.com/SchoolyB/EZ/commit/7d6e55f1fc80401a2299c26ab1bda32e2e95a75b)), closes [#1752](https://github.com/SchoolyB/EZ/issues/1752)
* reject negation of unsigned integer types (E3096) ([f0eef54](https://github.com/SchoolyB/EZ/commit/f0eef5427c0585d9968f6abd66104b6526457f88)), closes [#1757](https://github.com/SchoolyB/EZ/issues/1757)
* type safety and big-int bug fixes ([db5527a](https://github.com/SchoolyB/EZ/commit/db5527a7f7550b48f7be7092018565590e731498))
* widen mixed-width integer arithmetic to larger operand type ([989abc2](https://github.com/SchoolyB/EZ/commit/989abc2edc4355d17e6c0bd40908bfa669cd03ad)), closes [#1756](https://github.com/SchoolyB/EZ/issues/1756)
* widen smaller integer operands in big-int arithmetic expressions ([0fb52b9](https://github.com/SchoolyB/EZ/commit/0fb52b97e5e63093a312af9e890d5c07fc07c842)), closes [#1758](https://github.com/SchoolyB/EZ/issues/1758)

## [3.5.4](https://github.com/SchoolyB/EZ/compare/v3.5.3...v3.5.4) (2026-06-01)


### Bug Fixes

* check fputs return value in write_file ([#1741](https://github.com/SchoolyB/EZ/issues/1741)) ([767da19](https://github.com/SchoolyB/EZ/commit/767da19ec71accb494892115e30f77d2b6cc1f42))

## [3.5.3](https://github.com/SchoolyB/EZ/compare/v3.5.2...v3.5.3) (2026-06-01)


### Bug Fixes

* **cli:** quote runtime_dir and output_file in shell command, reject single quotes ([8dfbe66](https://github.com/SchoolyB/EZ/commit/8dfbe66d71141f565796cbd848e011b82a32f256))
* **codegen:** free prefixed string in emit_call after user-module lookup ([dffc51e](https://github.com/SchoolyB/EZ/commit/dffc51eabe3617aa2ac3bfcb770270655c82ff02))
* **codegen:** route uint ++ and -- through overflow/underflow checks ([90aacad](https://github.com/SchoolyB/EZ/commit/90aacadc0b4231dd311f78f4ce529ca52773af77))
* **runtime:** guard realloc return in main.c and ez_server.c ([f12298a](https://github.com/SchoolyB/EZ/commit/f12298a38987942b677a516e6b79548d7901ad02))
* **stdlib:** add sys/random.h include for getentropy on Linux ([cf504d9](https://github.com/SchoolyB/EZ/commit/cf504d903e6053691469852bdbad23b84739c569))
* **stdlib:** cap send() length at buffer size in HTTP server and client ([282ffcd](https://github.com/SchoolyB/EZ/commit/282ffcd179da8e08519eb73c6420778ec8663e8b))
* **stdlib:** check malloc and pthread_create in server accept loop ([2a3a369](https://github.com/SchoolyB/EZ/commit/2a3a3697db2603ac8fe50fcec6e5fe3b68041d75))
* typechecker, runtime, stdlib, and codegen bug fixes ([f301bbd](https://github.com/SchoolyB/EZ/commit/f301bbda9943d39ef9f43604f81a0c2aeec6761f))
* **typechecker:** catch addr() of wrong struct type passed to pointer parameter ([41cae70](https://github.com/SchoolyB/EZ/commit/41cae700a3a21eab73095b37bd64acb1673c3429))
* **typechecker:** catch pointer assigned to non-pointer variable ([1bcb0c2](https://github.com/SchoolyB/EZ/commit/1bcb0c298d02317a42ca3d1e7feb384a5a1e6ad1))
* **typechecker:** catch struct-to-different-struct reassignment ([1f20af6](https://github.com/SchoolyB/EZ/commit/1f20af6c565aacd993c6a527d79b35ccfb23d4a5))
* **typechecker:** emit E3003 when float is used as string index ([b2302e3](https://github.com/SchoolyB/EZ/commit/b2302e333493d515fa6920641df85ac0e28163a5))
* **typechecker:** emit E3095 when 'in' is used on a non-array/map/string type ([6449e2d](https://github.com/SchoolyB/EZ/commit/6449e2d9e82048d7b67c20a8a4de0bfc2e34ba4a))
* **typechecker:** tighten array/map element type coercion checks ([57b0041](https://github.com/SchoolyB/EZ/commit/57b0041e17289365618503a11124194e8ddce968))


### Performance Improvements

* minor optimizations across Go CLI and C compiler ([7311a50](https://github.com/SchoolyB/EZ/commit/7311a50bc12819d2df672e4208d16c2c740e8714))

## [3.5.2](https://github.com/SchoolyB/EZ/compare/v3.5.1...v3.5.2) (2026-05-31)


### Bug Fixes

* **ci:** remove deleted lineeditor package from test-go target ([4bc65ab](https://github.com/SchoolyB/EZ/commit/4bc65abf5da5485e8ec2dc122f5466f4266dc36d))
* **ezc:** handle realloc failure in read_file NUL-termination step ([9fee714](https://github.com/SchoolyB/EZ/commit/9fee714ad68317ffaffc51b0311db5e7f96db001))
* typechecker bugfixes ([#1722](https://github.com/SchoolyB/EZ/issues/1722)) ([f1a9fa8](https://github.com/SchoolyB/EZ/commit/f1a9fa8c6543cf7cc6a4b18e5c06addeb3b84a91))
* **typechecker:** catch array element type mismatches in assignment, args, and return ([1be4947](https://github.com/SchoolyB/EZ/commit/1be49479e890e5c04c1e24a231f8766833802349))
* **typechecker:** catch map key/value type mismatches in assignment, args, and return ([466dcbd](https://github.com/SchoolyB/EZ/commit/466dcbd0fa9d6290b05ee65f4173656bdecd8fb6))
* **typechecker:** catch struct returned from primitive-typed function ([dfec4c2](https://github.com/SchoolyB/EZ/commit/dfec4c2797d7a948d11a80bf1acdb027c76b1f74))
* **typechecker:** emit E3010 for invalid field on chained struct access ([f23ee97](https://github.com/SchoolyB/EZ/commit/f23ee97f259224f4770542a5d2f44752162e9985))
* **typechecker:** emit E3090 when '!' is used on a non-bool type ([c61f652](https://github.com/SchoolyB/EZ/commit/c61f6529385e2b23bc3319f8cf700e361cea39c0))
* **typechecker:** emit E3091 when non-scalar type is used as if/for condition ([d844bf9](https://github.com/SchoolyB/EZ/commit/d844bf9110a6fa453ab2500699801e195a3b32df))
* **typechecker:** emit E3092 when struct, map, or array is compared to nil ([d929ee2](https://github.com/SchoolyB/EZ/commit/d929ee297c6823957ce9f1fc51a0fb45f55f1f68))
* **typechecker:** emit E3093 when arithmetic operators are used on map, array, or struct ([6a4c862](https://github.com/SchoolyB/EZ/commit/6a4c8629fbad30e6d84eb9343805edcaa4a69f0c))
* **typechecker:** emit E3094 when wrong type is assigned to array element by index ([ad869e0](https://github.com/SchoolyB/EZ/commit/ad869e0cf42064dcd96788d27640f222b2dce78c))

## [3.5.1](https://github.com/SchoolyB/EZ/compare/v3.5.0...v3.5.1) (2026-05-31)


### Bug Fixes

* **parser:** suppress struct literal parsing in when subject expression ([ee74f85](https://github.com/SchoolyB/EZ/commit/ee74f85c94759e84325caa5e0b49a41ab80aaed1))

## [3.5.0](https://github.com/SchoolyB/EZ/compare/v3.4.3...v3.5.0) (2026-05-30)


### Features

* **cli:** add ez man command for builtin function documentation ([f812762](https://github.com/SchoolyB/EZ/commit/f812762f1b6a28a3219e87e7dd34634b49c70bcd))
* **cli:** add ez man stdlib support, starting with math module ([536aa1a](https://github.com/SchoolyB/EZ/commit/536aa1a69ee16bda7b554042ab77fdca7f60e132))
* **cli:** add strings module to ez man ([ef357dc](https://github.com/SchoolyB/EZ/commit/ef357dc4ede71ee9a16e8772fd73fe0a0d2dd7d8))
* **cli:** add type documentation support to ez man ([479677a](https://github.com/SchoolyB/EZ/commit/479677a1852c8357f265a43dd9e02a72bc37bf26))
* **lang:** add bitwise keyword operators (bit_and, bit_or, bit_xor, bit_not, bit_shift_left, bit_shift_right) ([466d4d6](https://github.com/SchoolyB/EZ/commit/466d4d687967d671b0b0127efb3e6d39c1230b33)), closes [#1686](https://github.com/SchoolyB/EZ/issues/1686)
* **runtime:** structured P0001–P0077 panic codes with consistent formatting ([b9804cc](https://github.com/SchoolyB/EZ/commit/b9804cc02dc9626700a17b7bb9440b77d6bc8667)), closes [#1696](https://github.com/SchoolyB/EZ/issues/1696)
* **stdlib:** improve io module with new functions and consistency fixes ([faf2364](https://github.com/SchoolyB/EZ/commit/faf236400b617403eb1546391c9c92d3e6052002))
* **typechecker,codegen:** implement embed() compile-time file embedding builtin ([#1704](https://github.com/SchoolyB/EZ/issues/1704)) ([34064ff](https://github.com/SchoolyB/EZ/commit/34064ffacbe3d62c95a6f4d34f06daa600d4c44e))
* **typechecker:** complete using_funcs[] whitelist for all stdlib modules ([53a4ff1](https://github.com/SchoolyB/EZ/commit/53a4ff12dfca495ffb1d67fe21b3ccb0359e4a5d))
* **typechecker:** enforce error handling for fallible stdlib functions ([#1703](https://github.com/SchoolyB/EZ/issues/1703)) ([5a6f3c3](https://github.com/SchoolyB/EZ/commit/5a6f3c39410d89d770726414eb667d4036702943))
* **typechecker:** recursive struct pointer fields with exhaustive validation ([#1687](https://github.com/SchoolyB/EZ/issues/1687)) ([9501e53](https://github.com/SchoolyB/EZ/commit/9501e53f78b8b4359f88091b7d1d2910e50e8191))


### Bug Fixes

* **cli:** suppress update prompt on dev and dirty builds ([751e526](https://github.com/SchoolyB/EZ/commit/751e5263bf533918d2306c71a9ccc2b434d6b083))
* **codegen+typechecker:** server.listen arg order, E5026 for stdlib arg type mismatches ([f0d34fb](https://github.com/SchoolyB/EZ/commit/f0d34fbc1e80df70e48b549602d744f666466a07))
* **codegen:** add missing io functions to E5011 void-call whitelist ([cc5e04a](https://github.com/SchoolyB/EZ/commit/cc5e04a6e766f402b4b692230596e7f0324fa650))
* **codegen:** correct array/map interpolation for uint, byte, and char elements ([8c2c691](https://github.com/SchoolyB/EZ/commit/8c2c6912ed61092ccb61708c4f77604081d02b36)), closes [#1679](https://github.com/SchoolyB/EZ/issues/1679)
* **codegen:** implement bool() builtin conversion ([#1702](https://github.com/SchoolyB/EZ/issues/1702)) ([d2c9e26](https://github.com/SchoolyB/EZ/commit/d2c9e26139c3945b2981615a91dbff15525ee71f))
* **codegen:** multi-return functions now escape _func_arena and restore ez_default_arena ([#1688](https://github.com/SchoolyB/EZ/issues/1688)) ([a64af6b](https://github.com/SchoolyB/EZ/commit/a64af6b97578dc1f71da57a26da25b4a43862e09))
* **codegen:** support variable negative step in for range loops ([230722c](https://github.com/SchoolyB/EZ/commit/230722cdc4fb40cb48addfc978c52d708a9fd799)), closes [#1683](https://github.com/SchoolyB/EZ/issues/1683)
* **codegen:** unwind scratch arena before break and continue ([0b85251](https://github.com/SchoolyB/EZ/commit/0b85251438a383da58e6efa579d08eb116ab4e1e)), closes [#1684](https://github.com/SchoolyB/EZ/issues/1684)
* complete panic[Pxxx] migration and fix nested-loop break/continue arena bug ([9b27408](https://github.com/SchoolyB/EZ/commit/9b274087b33e4f2c2fb1e6cfacd48402e8069978))
* **errors:** introduce E8xxx series for bitwise operator errors ([f5cb33b](https://github.com/SchoolyB/EZ/commit/f5cb33bdfb1a493461fa5ba259cb5e94e0043d95))
* **io:** register io panic codes in error_codes.h and fix P0077 message ([ca1dc2d](https://github.com/SchoolyB/EZ/commit/ca1dc2d193bdfa0140799cebf2a0f37be1c63d3b))
* new() use-after-free, for while-style parsing, when enum ptr deref ([#1687](https://github.com/SchoolyB/EZ/issues/1687)) ([09ceac6](https://github.com/SchoolyB/EZ/commit/09ceac68abaeadf574a6778b83fd06cea4d2797f))
* **parser:** consume optional trailing comma after struct field type ([f69d1a2](https://github.com/SchoolyB/EZ/commit/f69d1a2bd2d196d334c283f21d6d0012a5395a86))
* **parser:** correct line/column offsets for diagnostics inside interpolated strings ([9fed78d](https://github.com/SchoolyB/EZ/commit/9fed78de157736e92d9e307ae2796addd290b5ed))
* **runtime:** fix ez_map_set tombstone scan creating duplicate keys ([898dd41](https://github.com/SchoolyB/EZ/commit/898dd4151d8d8d5967dce78fe27cc052d1db3a41)), closes [#1680](https://github.com/SchoolyB/EZ/issues/1680)
* **runtime:** make ez_default_arena thread-local to eliminate arena data races ([566f4f9](https://github.com/SchoolyB/EZ/commit/566f4f9ac448918ba7b1409f5dc4dbe0257e0207)), closes [#1689](https://github.com/SchoolyB/EZ/issues/1689)
* **stdlib:** arrays.get_first/last/remove_first/last return correct value for all element types ([#1681](https://github.com/SchoolyB/EZ/issues/1681)) ([2c9e84a](https://github.com/SchoolyB/EZ/commit/2c9e84ac79cc086e18a3a58cf846162054268b6e))
* **stdlib:** arrays.remove_at panics on out-of-bounds index ([#1682](https://github.com/SchoolyB/EZ/issues/1682)) ([541cf71](https://github.com/SchoolyB/EZ/commit/541cf71745e0e1decc5b1516de440fa05312867e))
* **stdlib:** patch server stack buffer overflow and arena struct leak ([#1691](https://github.com/SchoolyB/EZ/issues/1691)) ([9dbd537](https://github.com/SchoolyB/EZ/commit/9dbd5375e424ef163117f180913c963b7ff9c9e3))
* **tests:** correct inline struct syntax in test_valid_recursive_pointer_struct ([#1687](https://github.com/SchoolyB/EZ/issues/1687)) ([116ee4a](https://github.com/SchoolyB/EZ/commit/116ee4a4b4d47a92a4c422beb3300010aa09f47f))
* **typechecker:** add E3004 check for string index assignment with wrong type ([9afe081](https://github.com/SchoolyB/EZ/commit/9afe081540ff819be568661c7890bbf4e885303a)), closes [#1694](https://github.com/SchoolyB/EZ/issues/1694)
* **typechecker:** implement 6 missing compile-time checks ([6c5b5d6](https://github.com/SchoolyB/EZ/commit/6c5b5d6427e09a94f44f64edf2a63a02670dbd2f))
* **typechecker:** replace 'fallible' with 'can fail' in E3089 message ([#1703](https://github.com/SchoolyB/EZ/issues/1703)) ([8043866](https://github.com/SchoolyB/EZ/commit/8043866e92c2553a36f99faaa0867b77e7b7bae6))

## [3.4.3](https://github.com/SchoolyB/EZ/compare/v3.4.2...v3.4.3) (2026-05-28)


### Bug Fixes

* **codegen:** recursive wildcard binding at call sites for nested composite types ([d62ccbe](https://github.com/SchoolyB/EZ/commit/d62ccbebfb6bc9fa586852d602699077e8922744))
* **stdlib:** forward-declare arc4random_buf to fix macOS build ([835fde6](https://github.com/SchoolyB/EZ/commit/835fde68228a6ca639063a0055493c27631ee570))
* **stdlib:** reject leading whitespace in strconv, fix url_decode invalid %, fix regex find_all OOB ([f521496](https://github.com/SchoolyB/EZ/commit/f521496393302ff91b47663cf2b8dd65fccb0b0e))
* **stdlib:** replace rand()/srand() in crypto.random_hex with platform CSPRNG ([8e47bb0](https://github.com/SchoolyB/EZ/commit/8e47bb030e795caeea50c66250539727f5e7f74b)), closes [#1648](https://github.com/SchoolyB/EZ/issues/1648)
* **typechecker:** allow auto-deref field assignment on pointer parameters ([daa819f](https://github.com/SchoolyB/EZ/commit/daa819ff0c49bfd4480d25bf9c2850d4272dc85a)), closes [#1653](https://github.com/SchoolyB/EZ/issues/1653)
* **typechecker:** wildcard unifier recurses into nested composite types ([cfef697](https://github.com/SchoolyB/EZ/commit/cfef697019fe1463c2299b61f3e8c0e6db0f3ce3)), closes [#1652](https://github.com/SchoolyB/EZ/issues/1652)

## [3.4.2](https://github.com/SchoolyB/EZ/compare/v3.4.1...v3.4.2) (2026-05-21)


### Bug Fixes

* **typechecker:** reject ref()/addr() on map index access path ([#1673](https://github.com/SchoolyB/EZ/issues/1673)) ([5f8f0ee](https://github.com/SchoolyB/EZ/commit/5f8f0ee75758c88c4dbf620eccc88f0dfb6003d0)), closes [#1654](https://github.com/SchoolyB/EZ/issues/1654)

## [3.4.1](https://github.com/SchoolyB/EZ/compare/v3.4.0...v3.4.1) (2026-05-21)


### Bug Fixes

* codegen and typechecker bugs in module-aware compilation ([#1671](https://github.com/SchoolyB/EZ/issues/1671)) ([b9c658d](https://github.com/SchoolyB/EZ/commit/b9c658d6a27152278b38032559b9c77977e2c164))

## [3.4.0](https://github.com/SchoolyB/EZ/compare/v3.3.1...v3.4.0) (2026-05-20)


### Features

* **builtins:** add here() returning SourceLocation for source-position capture ([#1660](https://github.com/SchoolyB/EZ/issues/1660)) ([07fa474](https://github.com/SchoolyB/EZ/commit/07fa47455747830ae510305fc8eb261654706adc))
* here() builtin returning SourceLocation ([#1660](https://github.com/SchoolyB/EZ/issues/1660)) ([efeb3a3](https://github.com/SchoolyB/EZ/commit/efeb3a352364a682976a898838dd864432ab564f))
* threads detach, current, yield, sleep, is_alive, thread_count ([#1453](https://github.com/SchoolyB/EZ/issues/1453)) ([ec273dc](https://github.com/SchoolyB/EZ/commit/ec273dc70e08ee8c4f0675bb6162618ac8acf582))
* **threads:** add detach, current, yield, sleep, is_alive, thread_count ([#1453](https://github.com/SchoolyB/EZ/issues/1453)) ([e78f57d](https://github.com/SchoolyB/EZ/commit/e78f57d26c93d8e1251b26f13b2a56ac359ae0be))
* uuid parse, generate_random, generate_time_ordered, NIL_UUID ([#1452](https://github.com/SchoolyB/EZ/issues/1452)) ([7df9ae2](https://github.com/SchoolyB/EZ/commit/7df9ae22bda510f367934a2a628960a3326494ae))
* **uuid:** add parse, generate_random, generate_time_ordered, NIL_UUID ([#1452](https://github.com/SchoolyB/EZ/issues/1452)) ([ed9e789](https://github.com/SchoolyB/EZ/commit/ed9e789f6e20c88486a3eb6bb76d1cf0c3db95bc))


### Bug Fixes

* **codegen:** multi-file struct codegen — three findings from Marlowe port ([d3e75d5](https://github.com/SchoolyB/EZ/commit/d3e75d5e66e997e2495110c217b50c921007e09b))
* **codegen:** thread struct field type into empty array literal sizing ([#1664](https://github.com/SchoolyB/EZ/issues/1664)) ([deb12da](https://github.com/SchoolyB/EZ/commit/deb12dacd304d4903e5378d3bb4ece3c6fa18654))
* **import:** rewrite composite type names in imported declarations ([#1665](https://github.com/SchoolyB/EZ/issues/1665)) ([b205a5e](https://github.com/SchoolyB/EZ/commit/b205a5e05cb5dd01eac361817099676a03d1193e))
* **typechecker:** reject 'here' as user variable/function/type/param (E5016) ([3d66ec1](https://github.com/SchoolyB/EZ/commit/3d66ec191cb2aadf74390b0507bb0b6b82af181b))

## [3.3.1](https://github.com/SchoolyB/EZ/compare/v3.3.0...v3.3.1) (2026-05-19)


### Bug Fixes

* **codegen:** map stdlib opaque types to their C typedefs ([#1645](https://github.com/SchoolyB/EZ/issues/1645)) ([#1657](https://github.com/SchoolyB/EZ/issues/1657)) ([30564e9](https://github.com/SchoolyB/EZ/commit/30564e90c68a2d9c1f0c2faa95e2f3a2b2890481))
* **parser:** accept bare '_ = expr' at statement position ([#1647](https://github.com/SchoolyB/EZ/issues/1647)) ([#1656](https://github.com/SchoolyB/EZ/issues/1656)) ([83877c5](https://github.com/SchoolyB/EZ/commit/83877c59aebe3a44f553dde1596235ae886b99d8))
* **parser:** reject throwaway '_' with a non-call RHS (E5012) ([c676763](https://github.com/SchoolyB/EZ/commit/c67676320935c7e47455f01ac5b4aab115632796))
* **typechecker:** reject file-scope initializers that call functions (E5013) ([92de338](https://github.com/SchoolyB/EZ/commit/92de338ae30b3f677b5744c09bcc67da891e6a74))

## [3.3.0](https://github.com/SchoolyB/EZ/compare/v3.2.2...v3.3.0) (2026-05-12)


### Features

* **cli:** add `ez fmt` command for code formatting ([#1638](https://github.com/SchoolyB/EZ/issues/1638)) ([8e9027b](https://github.com/SchoolyB/EZ/commit/8e9027ba8ca2a2503793aca217acb11bc1434a1e)), closes [#1564](https://github.com/SchoolyB/EZ/issues/1564)


### Bug Fixes

* **cli:** remove --diff flag from ez fmt ([#1564](https://github.com/SchoolyB/EZ/issues/1564)) ([a0f2180](https://github.com/SchoolyB/EZ/commit/a0f2180b3f10f38b06f4178ea21e9d8268f1216c))
* **parser:** reject invalid characters in C header import paths ([#1618](https://github.com/SchoolyB/EZ/issues/1618)) ([6646d68](https://github.com/SchoolyB/EZ/commit/6646d68694d7b345bb8bdcdb8b7a0ece4ce3bbae))
* runtime security bugs ([f0d1151](https://github.com/SchoolyB/EZ/commit/f0d11516795c9648cd68e5c6aa45797ca7bcbc3a))
* **runtime:** cast to unsigned char at all ctype.h call sites ([#1616](https://github.com/SchoolyB/EZ/issues/1616)) ([d40683b](https://github.com/SchoolyB/EZ/commit/d40683b7c43397b15f5041acd74e76f2604ec087))
* **stdlib:** compute exact buffer size in csv stringify to prevent heap overflow ([#1617](https://github.com/SchoolyB/EZ/issues/1617)) ([6f3ae23](https://github.com/SchoolyB/EZ/commit/6f3ae23ad0a55dfbc20b0bbe2c99662c8eabb696))
* **stdlib:** use correct comparators for float and string array sorts ([#1632](https://github.com/SchoolyB/EZ/issues/1632)) ([24afcd8](https://github.com/SchoolyB/EZ/commit/24afcd8a1d6fa8ea6297410c0d28968ce282696f))
* **typechecker:** reject dynamic format strings and validate directives in fmt.printf/sprintf/format ([#1633](https://github.com/SchoolyB/EZ/issues/1633)) ([35b31c1](https://github.com/SchoolyB/EZ/commit/35b31c1469d587d10a51cd8813c6307db4b24e3d))

## [3.2.2](https://github.com/SchoolyB/EZ/compare/v3.2.1...v3.2.2) (2026-05-10)


### Bug Fixes

* **codegen:** grow ensure cleanup buffer dynamically ([3bfeecd](https://github.com/SchoolyB/EZ/commit/3bfeecd966843ea404eca93bc7f05d5f1ad28b54)), closes [#1630](https://github.com/SchoolyB/EZ/issues/1630)
* **runtime/arrays:** panic on mutation during for_each iteration ([3a40baa](https://github.com/SchoolyB/EZ/commit/3a40baa31a312f1a6b865dcaa1870ed900b7e03b)), closes [#1628](https://github.com/SchoolyB/EZ/issues/1628)
* runtime/codegen/stdlib fixes ([f294b4e](https://github.com/SchoolyB/EZ/commit/f294b4e4146ab889ba2233f775c50d1a29bc06a0))
* **runtime/maps:** normalize float keys so +0.0 and -0.0 collide ([21e1476](https://github.com/SchoolyB/EZ/commit/21e1476904c590d3dee3640d7206061aeb57b8bf)), closes [#1631](https://github.com/SchoolyB/EZ/issues/1631)
* **runtime:** make c_string() safe — NULL-check, copy onto arena, clamp length ([2b65ba6](https://github.com/SchoolyB/EZ/commit/2b65ba6bd066cc3b91ce582747129e02b301a3f4))
* **stdlib/arrays:** bounds-check insert_at and drop fixed 32-byte stack buffer ([#1622](https://github.com/SchoolyB/EZ/issues/1622)) ([#1625](https://github.com/SchoolyB/EZ/issues/1625)) ([8693f7c](https://github.com/SchoolyB/EZ/commit/8693f7c725f4b53eff68841a26742115cc6e2779))
* **stdlib/encoding:** validate base64_decode input and reject malformed data ([47f09fc](https://github.com/SchoolyB/EZ/commit/47f09fc9393c5396ec9b61f5a696cd734e09a533))
* **stdlib/random:** size shuffle scratch buffer to actual element width ([e1da7ff](https://github.com/SchoolyB/EZ/commit/e1da7ff18d9d4dbec4d9dc0962c0a191d8c5a23f))
* **stdlib/strings:** reject overflowing repeat and replace results ([9618ac5](https://github.com/SchoolyB/EZ/commit/9618ac5908d9e3d514a987b3a8587d26984dbf1f)), closes [#1615](https://github.com/SchoolyB/EZ/issues/1615)
* **stdlib:** handle ftell failure on non-seekable inputs in read_file ([#1626](https://github.com/SchoolyB/EZ/issues/1626)) ([4e9b451](https://github.com/SchoolyB/EZ/commit/4e9b45181aa893b0df92af76df57641e8b8f47c7))

## [3.2.1](https://github.com/SchoolyB/EZ/compare/v3.2.0...v3.2.1) (2026-05-05)


### Bug Fixes

* **codegen:** drop extra stream arg in non-container char print ([81bfbdf](https://github.com/SchoolyB/EZ/commit/81bfbdf036362700eb6694eb679220957097a65d))
* **codegen:** heap-allocate mangled buffer in monomorphisation forward decl ([01a1842](https://github.com/SchoolyB/EZ/commit/01a184297a94036664497b2fc61885cf8fac7e81))
* **io:** create copy_file destination with mode 0644 instead of 0666 ([e444039](https://github.com/SchoolyB/EZ/commit/e44403993e4004a8202077fa652fd76e08ac53f2))


### Performance Improvements

* **codegen:** build func-field index once instead of nested struct scan ([eced4b2](https://github.com/SchoolyB/EZ/commit/eced4b29144cb457703cc8931291f3bcf352cf45))
* **codegen:** replace find_func linear scan with sorted bsearch index ([2dbb8ff](https://github.com/SchoolyB/EZ/commit/2dbb8ff3554b2e1e4a621e366760d916e4450b67))
* **error_codes:** replace chained strcmp with sorted bsearch lookup ([5d36216](https://github.com/SchoolyB/EZ/commit/5d36216321a18462ef149f5ded52abae46b169f7))
* **typechecker:** replace chained strcmp in type_from_name with bsearch table ([e62f607](https://github.com/SchoolyB/EZ/commit/e62f607c417d79cc48ed62e1d19dc9f6c199fcea))

## [3.2.0](https://github.com/SchoolyB/EZ/compare/v3.1.2...v3.2.0) (2026-05-04)


### Features

* pass --quiet and --no-color flags through ez watch ([#1606](https://github.com/SchoolyB/EZ/issues/1606)) ([e35016e](https://github.com/SchoolyB/EZ/commit/e35016ea4d56054ba0d81be1a740f6dc323e0513)), closes [#1573](https://github.com/SchoolyB/EZ/issues/1573)
* **stdlib:** add base parameter to strconv.to_int/to_uint ([#1603](https://github.com/SchoolyB/EZ/issues/1603)) ([3fd62c8](https://github.com/SchoolyB/EZ/commit/3fd62c899378b192ceb631c85ad5f710b81ec93b))
* **stdlib:** add strconv module ([#1603](https://github.com/SchoolyB/EZ/issues/1603)) ([fbc995e](https://github.com/SchoolyB/EZ/commit/fbc995e64717428d6f12870f769b8e328ae9f8db))
* **stdlib:** add strconv module for string/type conversions ([#1603](https://github.com/SchoolyB/EZ/issues/1603)) ([ae8a310](https://github.com/SchoolyB/EZ/commit/ae8a3104e7059e4c3148d438370e005636f49f42))


### Bug Fixes

* **cli:** remove duplicate --no-color flag on watchCmd ([1935e83](https://github.com/SchoolyB/EZ/commit/1935e83625e05725b040caee92f0b485bbff5c43))
* **parser:** replace uppercase heuristic with struct name prescan ([e72004b](https://github.com/SchoolyB/EZ/commit/e72004b32cf3782b2c5ee7ee375bd4578556b741))
* **parser:** suppress struct literal parsing on RHS of comparisons ([0db8b88](https://github.com/SchoolyB/EZ/commit/0db8b889c41283d081bc9cdaf2e57cde3d0d9ec6))
* **repo:** enable blank issues in issue template config ([3f8db8f](https://github.com/SchoolyB/EZ/commit/3f8db8f91ff60fd8d37caf0ac1f09418660105ca))
* **stdlib:** complete strconv module wiring (Makefile, fallible tables) ([#1603](https://github.com/SchoolyB/EZ/issues/1603)) ([20e7ee0](https://github.com/SchoolyB/EZ/commit/20e7ee054d6a4a3c67f59bbfce8a3c8366dc8e35))
* **typechecker:** allow type names in size_of while rejecting bare builtin functions ([b67ee7c](https://github.com/SchoolyB/EZ/commit/b67ee7c8f219c084cf208af9d4c79a0bf06a8fe8))
* **typechecker:** enforce private access, reject nonexistent struct functions, catch struct type mismatches ([#1599](https://github.com/SchoolyB/EZ/issues/1599)) ([4151ca7](https://github.com/SchoolyB/EZ/commit/4151ca71990cf3cb882f75c360b96b7dca488d89))
* **typechecker:** reject bare builtin names used as values ([3e48888](https://github.com/SchoolyB/EZ/commit/3e48888ed635c0ac1d1e5cb9ae9971535d179850))
* **typechecker:** suppress false W1002 warnings for sub-file imports ([5df0121](https://github.com/SchoolyB/EZ/commit/5df01218580099a1009170a59dfa2593c79629a3))

## [3.1.2](https://github.com/SchoolyB/EZ/compare/v3.1.1...v3.1.2) (2026-05-02)


### Bug Fixes

* **parser,typechecker:** parse complex multi-return types and hide internal name prefixes ([88c5c3b](https://github.com/SchoolyB/EZ/commit/88c5c3bf0db6c3169bf57643310dad7e1fac906b))
* **typechecker:** handle multi-return and arg validation for module calls ([5669924](https://github.com/SchoolyB/EZ/commit/5669924daad80f2f1cf96026ca35acecd3a41926))
* **typechecker:** handle range exprs in import rewriting and in/!in type checks ([6d2f8c1](https://github.com/SchoolyB/EZ/commit/6d2f8c1904ba9e84570f3e0cf800a8d467f9f028))
* **typechecker:** validate argument types for stdlib function calls ([6f34119](https://github.com/SchoolyB/EZ/commit/6f3411985dd7aaa4e6f31028e4797cf44d348a5c))
* wire arrays.remove() function ([bfc8f03](https://github.com/SchoolyB/EZ/commit/bfc8f0317d62cbc8a6e3b4bc8caf4a27e66f2563))

## [3.1.1](https://github.com/SchoolyB/EZ/compare/v3.1.0...v3.1.1) (2026-05-01)


### Bug Fixes

* **codegen:** remove two unused-variable warnings ([121647d](https://github.com/SchoolyB/EZ/commit/121647d7de0ba2230e4a0c5191e06dfd0e3c55af))
* **codegen:** remove two unused-variable warnings ([#1597](https://github.com/SchoolyB/EZ/issues/1597)) ([8cbc765](https://github.com/SchoolyB/EZ/commit/8cbc7654275f3b7cec5664ea43cb55627f984fd1))
* **codegen:** resolve struct function return types in resolve_print_suffix ([#1595](https://github.com/SchoolyB/EZ/issues/1595)) ([8b4647a](https://github.com/SchoolyB/EZ/commit/8b4647ac36e67df696adb4e2175c27a7ecb3039d))
* import system, cast system, and code quality improvements ([542ecd0](https://github.com/SchoolyB/EZ/commit/542ecd03894c22cb1e87fa81ce95fbe0bf43219a))
* **imports:** propagate stdlib imports from transitive files ([#1596](https://github.com/SchoolyB/EZ/issues/1596)) ([9bc943f](https://github.com/SchoolyB/EZ/commit/9bc943f3829286311532f820db3e7e29d11944cb))
* **imports:** resolve transitive paths, diamond deps, and directory imports ([#1596](https://github.com/SchoolyB/EZ/issues/1596)) ([377c170](https://github.com/SchoolyB/EZ/commit/377c170d65ef9a2507fc6a71843fde7f195d7a45))
* **imports:** sibling cross-references, spurious W2013, and redundant file warning ([#1596](https://github.com/SchoolyB/EZ/issues/1596)) ([e9814a9](https://github.com/SchoolyB/EZ/commit/e9814a935da50401c4c748256a8df8767807b8e7))
* **imports:** sibling struct access, W2015 namespace hint, W2013 transitive suppression ([#1596](https://github.com/SchoolyB/EZ/issues/1596)) ([144bbc3](https://github.com/SchoolyB/EZ/commit/144bbc3ed80aa300162fb215905a1720fd1cd4e7))
* **test:** correct relative path in dir-transitive test ([#1596](https://github.com/SchoolyB/EZ/issues/1596)) ([6827f2b](https://github.com/SchoolyB/EZ/commit/6827f2b1a869b515a6b9cea7b71dc8704e5bf0e5))
* **typechecker:** various bugs in the cast() system ([#1598](https://github.com/SchoolyB/EZ/issues/1598)) ([b6dfc20](https://github.com/SchoolyB/EZ/commit/b6dfc2048113693cbab729365f6c21d94989c8e7))

## [3.1.0](https://github.com/SchoolyB/EZ/compare/v3.0.0...v3.1.0) (2026-04-29)


### Features

* **cli:** add -o/--output flag to ez doc ([3f590e8](https://github.com/SchoolyB/EZ/commit/3f590e8c0dd224ebc79fb1f56d365eb399a71896))
* **cli:** add -o/--output flag to ez doc ([0916278](https://github.com/SchoolyB/EZ/commit/09162785a00cbaba53e6b60d08543048e4a2f87e))


### Bug Fixes

* **codegen:** map HttpRequest/HttpResponse to correct C types ([#1585](https://github.com/SchoolyB/EZ/issues/1585)) ([2cc6dbd](https://github.com/SchoolyB/EZ/commit/2cc6dbd6037531551ad23cc1c1f959c64332398d))
* **codegen:** resolve wildcard type in string interpolation ([#1584](https://github.com/SchoolyB/EZ/issues/1584)) ([646f1cb](https://github.com/SchoolyB/EZ/commit/646f1cbaa337c191742c14ffad51cf2c0106f781))
* **codegen:** revert string/char quote encapsulation in print functions ([#1580](https://github.com/SchoolyB/EZ/issues/1580)) ([f0cdb28](https://github.com/SchoolyB/EZ/commit/f0cdb283931bb3335ecb31df202a916efb0aa1fc))
* **stdlib:** wire 7 partially implemented stdlib functions end-to-end ([#1580](https://github.com/SchoolyB/EZ/issues/1580)) ([8d6b8bd](https://github.com/SchoolyB/EZ/commit/8d6b8bdf658205f675989e657c9e0f3bc793bddf))
* typechecker hardening, stdlib wiring, and code scanning fixes ([dd5fe8b](https://github.com/SchoolyB/EZ/commit/dd5fe8b5be97d12599a5043f66069cf474c69423))
* **typechecker,codegen:** validate 'in' operator types and wire string containment ([#1589](https://github.com/SchoolyB/EZ/issues/1589), [#1590](https://github.com/SchoolyB/EZ/issues/1590)) ([ddbd4f5](https://github.com/SchoolyB/EZ/commit/ddbd4f5ed71a39365a91829958578c4aedf798e7))
* **typechecker:** allow enum values in 'in' operator for matching collections ([#1589](https://github.com/SchoolyB/EZ/issues/1589)) ([c85bb8c](https://github.com/SchoolyB/EZ/commit/c85bb8cedd4a2e36650beeacf4e697e2e7f05590))
* **typechecker:** exempt built-in Error type from E4016 and fix missed e2e expectations ([#1585](https://github.com/SchoolyB/EZ/issues/1585)) ([014a814](https://github.com/SchoolyB/EZ/commit/014a814eef5f143e12a070d9637e725d73b3db53))
* **typechecker:** format array/map types in error messages ([#1589](https://github.com/SchoolyB/EZ/issues/1589)) ([bdb6570](https://github.com/SchoolyB/EZ/commit/bdb65709e3e50a743d5a232bc7193566c4e1310e))
* **typechecker:** register HttpRequest type and scope HTTP types to imports ([#1585](https://github.com/SchoolyB/EZ/issues/1585)) ([58b3bbe](https://github.com/SchoolyB/EZ/commit/58b3bbed4a8b096817c1ed77089372d16322fb77))
* **typechecker:** reject return statements with too many values ([#1588](https://github.com/SchoolyB/EZ/issues/1588)) ([7397aee](https://github.com/SchoolyB/EZ/commit/7397aee55612b621a4d6a64ea6696f2e68d2bc52))
* **typechecker:** reject type names passed to type_of() ([#1586](https://github.com/SchoolyB/EZ/issues/1586)) ([f573e12](https://github.com/SchoolyB/EZ/commit/f573e122a8c7f421a5deabd1837fbc3ea51cb883))
* **typechecker:** reject undefined/unimported struct types ([#1585](https://github.com/SchoolyB/EZ/issues/1585)) ([da55c33](https://github.com/SchoolyB/EZ/commit/da55c33145a649c5840a582ec9af101a8b5e6463))
* **typechecker:** scope HttpRequest to server module only ([#1585](https://github.com/SchoolyB/EZ/issues/1585)) ([7095990](https://github.com/SchoolyB/EZ/commit/70959902a7c6aa53a32441ea4ac1a63b1b3383e0))
* **typechecker:** validate argument count for struct function calls ([#1587](https://github.com/SchoolyB/EZ/issues/1587)) ([fd0695e](https://github.com/SchoolyB/EZ/commit/fd0695ef451731c210f73e3e94cca5d121a5ca83))
* **typechecker:** validate argument types for range, assert, exit, panic builtins ([#1591](https://github.com/SchoolyB/EZ/issues/1591)) ([ef21472](https://github.com/SchoolyB/EZ/commit/ef214725f77a274d655deaad8f05b690a879e6aa))

## [3.0.0](https://github.com/SchoolyB/EZ/compare/v2.0.0...v3.0.0) (2026-04-28)

EZ 3.0 is a "from the ground up rewrite". The Go-based interpreter has been replaced by a **compiled backend** that emits C and produces native binaries. Every stage of the pipeline — lexer, parser, typechecker, and code generator is now written in C. The Go CLI remains as the user-facing tooling wrapper.

### Breaking Changes

- **Compiled, not interpreted.** EZ programs now compile to native binaries via C. The Go interpreter is gone.
- **`@std` module removed.** All former `@std` functions (`println`, `print`, `len` `input`, `assert`, `panic`, `exit`, etc.) are now **builtins** — always available, no import needed.
- **`module` keyword removed.** Modules are identified by their filesystem path. Using `module` now produces E2061.
- **`#suppress` attribute removed.** Warning suppression moved to the CLI: `ez build -q W1001,W2003`.
- **`#enum(type)` attribute removed.** Enum variants are always integer-valued.
- **`nil` is no longer a type.** `nil` is a value only — use `Error`, pointer types, or optionals for nullability.
- **`ez run` removed.** Compile and run files directly with `ez <file.ez>`.
- **`temp` renamed to `mut`.** All mutable variable declarations now use `mut`.
- **`import & use` syntax replaced** by `import and use`. The `&` shorthand no longer works.
- **`new()` now returns a pointer.** `new(StructName)` returns `^Struct`, not a value.
- **`bare func` type removed.** Function references require full signatures: `func(int, int) -> int`.
- **`sleep_s`, `sleep_ms`, `sleep_ns` moved from time module to builtins.**
- **Many more!**

### New Features

**Compiler & Type System**
- Native compilation via C backend source to binary in one step
- Scope-based automatic memory management — every scope gets its own arena allocator. Allocations are freed when the scope exits. Values that escape (via return, outer assignment, or container storage) are automatically copied to the parent scope. No garbage collector, no manual free for normal code.
- Overflow-checked integer arithmetic at runtime
- Division-by-zero runtime checks
- Wildcard type `?` for generic-style functions monomorphised per call site
- Implicit int-to-float coercion in assignments, arguments, and returns
- `private` keyword for functions and constants
- Named return values in function signatures
- Default parameter values for functions
- Struct functions — functions defined inside structs with instance dispatch (`point.distance()`)
- `for_each i, item in arr` — index-value destructuring for arrays
- `for_each k, v in myMap` — key-value destructuring for maps
- `while` keyword as an alias for `as_long_as`
- `#json` attribute for struct-based JSON serialization/deserialization
- Wildcard struct fields
- `or_return` for error propagation

**Pointers**
- `^Type` pointer syntax — e.g. `^int`, `^Point`
- `new(StructName)` now returns a `^Struct` pointer to a heap-allocated struct
- `addr(variable)` returns the memory address of a variable as a pointer
- Dereference pointers with `p^`
- Auto-deref for struct field access — `ptr.field` works without explicit deref
- Pointer comparison limited to `==`/`!=` with `nil` only. No pointer arithmetic

**Functions as First-Class Types**
- `func` is a type — functions can be stored in variables, passed as arguments, and returned from functions
- Full signature syntax: `func(int, int) -> int`
- `()func` call syntax for invoking function references

**Breaking: `temp` renamed to `mut`**
- The `temp` keyword for mutable variables is now `mut`

**New Builtins**
- `sleep_s()`, `sleep_ms()`, `sleep_ns()` — sleep functions (formerly in the @time module, now builtins)
- `addr()` — returns a pointer to a variable

**Wide Integer Types**
- `i256`, `u256` — new wide integer sizes
- `i128`, `u128` — reimplemented as portable struct-based types (previously interpreter-only)

**C Interop**
- `import c"header.h"` to include C headers
- Call C functions via `c.func()` syntax
- `c_string()` builtin to convert C `char*` back to EZ strings
- Access C constants via `c.CONSTANT`
- Type mapping between EZ and C primitives

**Module System**
- Filesystem-based module identity (directory name = module name)
- Directory imports merge all `.ez` files into one namespace
- Import aliasing: `import m @math`
- `import and use` combined syntax (replaces the former `import & use`)
- Module-qualified struct literals: `lib.Point{x: 1}`
- Module-qualified enum access
- Extensionless file and directory imports
- Transitive imports and import caching
- Duplicate module name detection with E6001

**Standard Library**

All 27 stdlib modules now run on the compiled C backend. Modules that existed in the v2 interpreter (`@http`, `@server`, `@regex`, `@csv`, `@threads`, etc.) have been reimplemented in C.

New modules:
- `@net` — TCP sockets and DNS
- `@atomic` — lock-free assembly-backed operations (x86_64 + ARM64)
- `@mem` — manual arena-based memory management
- `@fmt` — formatted output, padding, number formatting
- `@sync` — mutexes (split from former `@threads`)
- `@channels` — typed channels (split from former `@threads`)

Notable changes to existing modules:
- `@io` — added 23 filesystem and path manipulation functions
- All fallible stdlib functions now have `_result` variants that return `(T, Error)` instead of panicking
- `to_char()` and `char_count()` builtins for Unicode codepoint access

**CLI Tooling**

New commands:
- `ez report` — system info for bug reports (OS, CPU, RAM, C compiler, install path)
- `ez test` — unified test runner with aggregated counts
- `ez install <version>` — pin to a specific version

New flags:
- `--pre` — install pre-release versions via `ez update --pre`
- `-q` / `--quiet` — suppress warnings (global or per-code, e.g. `-q W1001,W2003`)
- `--no-color` — plain output without ANSI colors
- `--emit-c` — inspect the generated C source
- `--time` — show compilation timing

Other changes:
- Single embedded binary — `ezc` compiler and runtime are embedded in the `ez` binary, no separate install needed
- `ez version` now shows installed, latest stable, and latest pre-release

**Error System**
- Centralized error code registry (`error_codes.h`) with auto-generated `ERRORS.md`
- Rust-inspired diagnostics: error code, message, source location, caret pointing, optional help hints
- 100+ compile-time error checks across lexer, parser, typechecker
- Warning system with per-code suppression
- Runtime panics with file/line/column info for division-by-zero, nil deref, array OOB, stack overflow

**Testing & CI**
- 1,200+ tests: unit (C), end-to-end, integration (pass + fail + warning + stress + multi-file)
- UBSan and ASan sanitizer builds in CI

---

## [2.0.0](https://github.com/SchoolyB/EZ/compare/v1.4.9...v2.0.0) (2026-02-14)


### ⚠ BREAKING CHANGES

* The lowercase `error` type alias has been removed. Use `Error` (capitalized) for type annotations instead.

### Features

* add [@server](https://github.com/server) HTTP server stdlib module ([#439](https://github.com/SchoolyB/EZ/issues/439)) ([acd7f16](https://github.com/SchoolyB/EZ/commit/acd7f162813fb9b9fbf950f06f291dd810b39a4d))
* add #doc attribute and ez doc command for documentation generation ([2d26a37](https://github.com/SchoolyB/EZ/commit/2d26a37f7dfc11c689b7b506cb5a77164f9a88b9)), closes [#764](https://github.com/SchoolyB/EZ/issues/764)
* add `[@server](https://github.com/server)` HTTP server stdlib module ([bc6d815](https://github.com/SchoolyB/EZ/commit/bc6d815dae237dd0e7271a412ad9b7a15c315100))
* add `ez pz` project scaffolding command ([a422c15](https://github.com/SchoolyB/EZ/commit/a422c15be13cc8b01c084fdd8beee685b219f44f))
* add `ez pz` project scaffolding command ([a1adcc0](https://github.com/SchoolyB/EZ/commit/a1adcc0f4381a5d6e160ca7c329301aa46608738))
* Add named return variables support ([#1131](https://github.com/SchoolyB/EZ/issues/1131)) ([6e4d70e](https://github.com/SchoolyB/EZ/commit/6e4d70ee910d6088fddd7d778e5157516a978d23))
* add optional index variable to `for_each` loops ([#1139](https://github.com/SchoolyB/EZ/issues/1139)) ([7a8a5a2](https://github.com/SchoolyB/EZ/commit/7a8a5a26a41df5343e928bf41c1807a01149bbc6))
* add optional index variable to for_each loops ([#1139](https://github.com/SchoolyB/EZ/issues/1139)) ([4b0378d](https://github.com/SchoolyB/EZ/commit/4b0378d859fc3c766f74fc235296316f4e2742ef))
* add server and client templates to ez pz scaffolding ([9f873bf](https://github.com/SchoolyB/EZ/commit/9f873bf51577aceff8834defb5ac093076cf2332))
* **cmd:** add `ez watch` command for live reloading ([741c279](https://github.com/SchoolyB/EZ/commit/741c2790c981d2619c929542a31433e663058e96))
* **cmd:** add `ez watch` command for live reloading ([4910b73](https://github.com/SchoolyB/EZ/commit/4910b733e64128046af203ee8ac58b53db29df95)), closes [#871](https://github.com/SchoolyB/EZ/issues/871)
* **errors:** add color formatting to parser, evaluator, and stdlib error messages ([#810](https://github.com/SchoolyB/EZ/issues/810)) ([cc9748f](https://github.com/SchoolyB/EZ/commit/cc9748f8f2370365b3c8a81ec69e4b9a13969f38))
* **errors:** add color formatting to remaining stdlib module error messages ([#810](https://github.com/SchoolyB/EZ/issues/810)) ([9b666d1](https://github.com/SchoolyB/EZ/commit/9b666d124d972b12c185c18228e9e337908dd242))
* **errors:** add color formatting to typechecker error messages ([837c258](https://github.com/SchoolyB/EZ/commit/837c258b4ef31e293b2a12cec549d284a228e7ec))
* **errors:** add color formatting to typechecker error messages ([#810](https://github.com/SchoolyB/EZ/issues/810)) ([023914a](https://github.com/SchoolyB/EZ/commit/023914a6a4a25dd54ff88f49697ffb8583dcc879))
* **loader:** add multi-file main module support ([fe0335e](https://github.com/SchoolyB/EZ/commit/fe0335e73e2fd183937876c366a3f8e8288ef0e1))
* **parser:** add named return variables support ([#1131](https://github.com/SchoolyB/EZ/issues/1131)) ([a1eea1c](https://github.com/SchoolyB/EZ/commit/a1eea1cc81bfe2cd1e826db94c433b4ac6a66d8d))
* **stdlib/csv:** add [@csv](https://github.com/csv) module for reading and writing CSV files ([4ec274b](https://github.com/SchoolyB/EZ/commit/4ec274b1e0650a2ca2fd2ea7e7f05bcf130000b6))
* **stdlib/csv:** add [@csv](https://github.com/csv) module for reading and writing CSV files ([d6af096](https://github.com/SchoolyB/EZ/commit/d6af09690cc43a49acc380c8e0efab8a7d204f57)), closes [#965](https://github.com/SchoolyB/EZ/issues/965)
* **stdlib/math:** add NaN/finite checks and float constants ([#1123](https://github.com/SchoolyB/EZ/issues/1123)) ([b22667b](https://github.com/SchoolyB/EZ/commit/b22667b77687f5045496daaa364e7ab589de307f)), closes [#1024](https://github.com/SchoolyB/EZ/issues/1024)
* **stdlib/regex:** add [@regex](https://github.com/regex) module for regular expression operations ([de8063c](https://github.com/SchoolyB/EZ/commit/de8063c379cd3d0b873264196d21696714a55105))
* **stdlib/regex:** add `[@regex](https://github.com/regex)` module for regular expression operations ([931bdcc](https://github.com/SchoolyB/EZ/commit/931bdccd0bcc475db594414762637b2f007e2338))
* **stdlib/strings:** add 12 new string utility functions ([6cb2a2d](https://github.com/SchoolyB/EZ/commit/6cb2a2df0f166cd041df37f2af47122cbebb18ed))
* **stdlib/strings:** add 12 new string utility functions ([a4ad38f](https://github.com/SchoolyB/EZ/commit/a4ad38f2bfb8009a0ee4a9ad4afbde0a3264b699)), closes [#1020](https://github.com/SchoolyB/EZ/issues/1020)
* **typechecker:** add W1005 warning for typed blank identifiers ([b9a4c62](https://github.com/SchoolyB/EZ/commit/b9a4c623844bb089a0520c1f19343942c85e10c9))
* **typechecker:** add W1005 warning for typed blank identifiers ([df1a334](https://github.com/SchoolyB/EZ/commit/df1a3346653255083b305933df394855587c0b29))


### Bug Fixes

* add encoding module to multi-return type inference ([#1138](https://github.com/SchoolyB/EZ/issues/1138)) ([23d806b](https://github.com/SchoolyB/EZ/commit/23d806b4fbb9f800eecb50afe1c19b1f5f508b54))
* allow enum constants in `when/is` when value is explicitly typed ([#1143](https://github.com/SchoolyB/EZ/issues/1143)) ([03e99d3](https://github.com/SchoolyB/EZ/commit/03e99d3a8c278401430cadb426d6971d5533480a))
* allow enum constants in when/is when value is explicitly typed ([#1143](https://github.com/SchoolyB/EZ/issues/1143)) ([1832e66](https://github.com/SchoolyB/EZ/commit/1832e66d5106bb109b05b05f3201bc225d6eef93))
* arrays.join no longer wraps string elements in quotes ([#1144](https://github.com/SchoolyB/EZ/issues/1144)) ([5b9c1a3](https://github.com/SchoolyB/EZ/commit/5b9c1a35df238beafee9b45eb526b517a2e71ae6))
* clarify misleading error messages in json.decode, db module, and typechecker ([028359b](https://github.com/SchoolyB/EZ/commit/028359bd9a03a82044c9ecb4d2a6d851f7580ae4))
* complete tuple type inference for all stdlib modules ([#1138](https://github.com/SchoolyB/EZ/issues/1138)) ([2c30882](https://github.com/SchoolyB/EZ/commit/2c30882f4a0c141b4eba85ea44d21f398bb6406f))
* eliminate race condition by removing global eval state ([#1122](https://github.com/SchoolyB/EZ/issues/1122)) ([e17f686](https://github.com/SchoolyB/EZ/commit/e17f68618a0ca39d8fd58c5ab54e6f24e9e6c26e)), closes [#949](https://github.com/SchoolyB/EZ/issues/949)
* **interpreter:** use correct error code E5011 for unused return values ([116ba16](https://github.com/SchoolyB/EZ/commit/116ba1652aa9c9a2c68e286831c774fde553d987))
* **language:** remove extra quotes from string enum casting ([70aeac2](https://github.com/SchoolyB/EZ/commit/70aeac26b2c3c4d5d5b323ba8c812985c4625f82))
* nested array types in multi-return declarations ([#1137](https://github.com/SchoolyB/EZ/issues/1137)) ([3186097](https://github.com/SchoolyB/EZ/commit/31860970dfe9e314de50f0a1e15b4c9362b6ae0f))
* **parser,typechecker:** fix #suppress attribute not working ([cb216a2](https://github.com/SchoolyB/EZ/commit/cb216a22dccbc64c4ad31b84798e118e4a33a6a2))
* **parser:** use user-friendly descriptions in assignment error messages ([e9d8d41](https://github.com/SchoolyB/EZ/commit/e9d8d419faf05410c36076b28c988fae7b8077fd))
* **parser:** validate #doc attribute placement ([e34198c](https://github.com/SchoolyB/EZ/commit/e34198c21111355d56a751f00c717b00be8eff1d))
* **parser:** validate `#doc` attribute placement ([01f9ef5](https://github.com/SchoolyB/EZ/commit/01f9ef5a7877c6e9e6a171283c17e716079832b1))
* **pz:** correct EZ syntax in project scaffolding templates ([ff51760](https://github.com/SchoolyB/EZ/commit/ff5176070553fe1c98f9a1f4cab435905ed6ea21))
* remove lowercase `error` type alias, keep only `Error` ([cc2c36f](https://github.com/SchoolyB/EZ/commit/cc2c36f0fa57429248f63b5aab5173dd305cd360)), closes [#1039](https://github.com/SchoolyB/EZ/issues/1039)
* respect explicit type annotations in multi-return declarations ([#1137](https://github.com/SchoolyB/EZ/issues/1137)) ([e3838fc](https://github.com/SchoolyB/EZ/commit/e3838fcde532faba9f18308de3c78b28f801c3e8))
* **stdlib/arrays:** `arrays.join` no longer wraps string elements in quotes ([#1144](https://github.com/SchoolyB/EZ/issues/1144)) ([333f53e](https://github.com/SchoolyB/EZ/commit/333f53e5868d10bff33db4ebbb414c0c778ceddd))
* **stdlib:** correct test expectations to match implementation ([58478b3](https://github.com/SchoolyB/EZ/commit/58478b33f45cde3501ef13ff66f85c4cbcca6830))
* **typechecker:** disallow function calls in file-scope variable initializers ([1d78de8](https://github.com/SchoolyB/EZ/commit/1d78de8ab4f408f2df84d565708940bf720119e1))
* **typechecker:** prevent W2010 false positive on nested struct initialization ([f7058b0](https://github.com/SchoolyB/EZ/commit/f7058b0d46ce439a8b2bf5855ccd01be42c135b0))
* **typechecker:** prevent W2010 false positive on nested struct initialization ([7d25b8a](https://github.com/SchoolyB/EZ/commit/7d25b8af31715f7df9e3a9f2ca3ccaea06646d7d)), closes [#1107](https://github.com/SchoolyB/EZ/issues/1107)

## [1.4.9](https://github.com/SchoolyB/EZ/compare/v1.4.8...v1.4.9) (2026-02-04)


### Bug Fixes

* multiple bug fixes for type inference and expressions ([f3853dd](https://github.com/SchoolyB/EZ/commit/f3853dde73565115af45f3487ec76a88fcad80ae))
* multiple bug fixes for type inference and expressions ([0817610](https://github.com/SchoolyB/EZ/commit/081761062e0f02498b38692ac4d98caf717ae7f6))

## [1.4.8](https://github.com/SchoolyB/EZ/compare/v1.4.7...v1.4.8) (2026-01-30)


### Bug Fixes

* `range()`, module imports, and error file tracking ([#1093](https://github.com/SchoolyB/EZ/issues/1093), [#1094](https://github.com/SchoolyB/EZ/issues/1094), [#1095](https://github.com/SchoolyB/EZ/issues/1095)) ([331b0b4](https://github.com/SchoolyB/EZ/commit/331b0b4eb0a0c4a1bde7771432c088e85f480953))
* mark modules as used when referenced in type annotations ([#1093](https://github.com/SchoolyB/EZ/issues/1093)) ([#1096](https://github.com/SchoolyB/EZ/issues/1096)) ([6aa9a24](https://github.com/SchoolyB/EZ/commit/6aa9a2420938b399c7763b65b50ec37e5d94677b))
* set correct file in runtime errors from builtin functions ([#1094](https://github.com/SchoolyB/EZ/issues/1094)) ([#1097](https://github.com/SchoolyB/EZ/issues/1097)) ([9f41400](https://github.com/SchoolyB/EZ/commit/9f414000683eaec7b106f1c42abdcc36d7437e5f))
* support negative step in range() for backwards iteration ([#1095](https://github.com/SchoolyB/EZ/issues/1095)) ([#1098](https://github.com/SchoolyB/EZ/issues/1098)) ([b12e474](https://github.com/SchoolyB/EZ/commit/b12e474609feaa47c6751dda4411295db3b30312))

## [1.4.7](https://github.com/SchoolyB/EZ/compare/v1.4.6...v1.4.7) (2026-01-30)


### Bug Fixes

* const ref now creates read-only view of referenced value ([#1089](https://github.com/SchoolyB/EZ/issues/1089)) ([4b04eed](https://github.com/SchoolyB/EZ/commit/4b04eedf59f422fa1b786c819c9d2920acd4395d))
* mutability semantics fixes ([b88a35e](https://github.com/SchoolyB/EZ/commit/b88a35ea0be3720e177d8c06dfa173dd2f880014))
* support const keyword with multi-return function assignment ([#1087](https://github.com/SchoolyB/EZ/issues/1087)) ([2375f3c](https://github.com/SchoolyB/EZ/commit/2375f3c6ada883e8426b9e02d3ff44722f9e8878))
* sync temp/const mutability with value's Mutable field ([#1085](https://github.com/SchoolyB/EZ/issues/1085)) ([5f0ffb7](https://github.com/SchoolyB/EZ/commit/5f0ffb78b66d794d5da802f3c558e527d0c0c0a4))

## [1.4.6](https://github.com/SchoolyB/EZ/compare/v1.4.5...v1.4.6) (2026-01-29)


### Bug Fixes

* arrays module bugs - remove() deleted, remove_at() now in-place ([#1083](https://github.com/SchoolyB/EZ/issues/1083)) ([92e224e](https://github.com/SchoolyB/EZ/commit/92e224ee1cec5a067f9c105d780c34690bb17b59))

## [1.4.5](https://github.com/SchoolyB/EZ/compare/v1.4.4...v1.4.5) (2026-01-29)


### Bug Fixes

* allow empty arrays as elements when type is explicitly declared ([#1076](https://github.com/SchoolyB/EZ/issues/1076)) ([9c7be92](https://github.com/SchoolyB/EZ/commit/9c7be9274dada568d7125af088521f66db3a235b)), closes [#1064](https://github.com/SchoolyB/EZ/issues/1064)
* bugfixes 1-29-2026 ([8c0980a](https://github.com/SchoolyB/EZ/commit/8c0980aae9ec5931d42c9c98775ff7dae375e98b))
* expand 'did you mean?' suggestions across typechecker errors ([#1075](https://github.com/SchoolyB/EZ/issues/1075)) ([3dfcee3](https://github.com/SchoolyB/EZ/commit/3dfcee348716261d545cc403d09e036dc91dbc3a))
* handle file close error explicitly in io.copy ([44ea553](https://github.com/SchoolyB/EZ/commit/44ea55382f4d4eba90562dd4133c84bc697d34fa))
* suppress W2010 warning when nil-check is present ([#1077](https://github.com/SchoolyB/EZ/issues/1077)) ([82789fb](https://github.com/SchoolyB/EZ/commit/82789fb05c740832bd3029da0ea9b704cdc93683))

## [1.4.4](https://github.com/SchoolyB/EZ/compare/v1.4.3...v1.4.4) (2026-01-28)


### Bug Fixes

* code quality improvements and performance optimizations ([99523bf](https://github.com/SchoolyB/EZ/commit/99523bf016efdf07b9c85d34e918729631547b87))


### Performance Improvements

* optimize hot paths and eliminate code duplication ([e88ad38](https://github.com/SchoolyB/EZ/commit/e88ad38ea3405ed24c8745c347e9d84557c3f79c))
* optimize parser, tokenizer, errors, lineeditor, and db packages ([a52c4b6](https://github.com/SchoolyB/EZ/commit/a52c4b63a47c63fc9c8e4dc20690603ebf7374f8))

## [1.4.3](https://github.com/SchoolyB/EZ/compare/v1.4.2...v1.4.3) (2026-01-25)


### Bug Fixes

* apply bug fixes from recent PRs ([979d5e3](https://github.com/SchoolyB/EZ/commit/979d5e32c7e3d70883ade6a054a4eee29686c8d5))

## [1.4.2](https://github.com/SchoolyB/EZ/compare/v1.4.1...v1.4.2) (2026-01-24)


### Bug Fixes

* **typechecker:** resolve multi-return type inference for stdlib calls via 'using' ([#1062](https://github.com/SchoolyB/EZ/issues/1062)) ([0c21737](https://github.com/SchoolyB/EZ/commit/0c21737f4e3a6b0181aa5d87b330623c7675f0af)), closes [#977](https://github.com/SchoolyB/EZ/issues/977)

## [1.4.1](https://github.com/SchoolyB/EZ/compare/v1.4.0...v1.4.1) (2026-01-23)


### Bug Fixes

* **typechecker:** add E3041 check for global fixed-size array overflow ([#1060](https://github.com/SchoolyB/EZ/issues/1060)) ([56de4f1](https://github.com/SchoolyB/EZ/commit/56de4f1657d2d7bd53187854a39584e08b2640f3)), closes [#1058](https://github.com/SchoolyB/EZ/issues/1058)

## [1.4.0](https://github.com/SchoolyB/EZ/compare/v1.3.0...v1.4.0) (2026-01-22)


### Features

* **operators:** add map support for in/not_in operators ([#1053](https://github.com/SchoolyB/EZ/issues/1053)) ([4239d20](https://github.com/SchoolyB/EZ/commit/4239d2043a45ed65fede9d1c858aa2f98030687e)), closes [#1007](https://github.com/SchoolyB/EZ/issues/1007)


### Bug Fixes

* **parser:** allow nil as function return type ([#1054](https://github.com/SchoolyB/EZ/issues/1054)) ([473b26e](https://github.com/SchoolyB/EZ/commit/473b26eadffe4c2d5d287757b6832a83a8e79989)), closes [#1044](https://github.com/SchoolyB/EZ/issues/1044)
* **stdlib/math:** preserve precision for add, sub, mul, mod ([45ab8c4](https://github.com/SchoolyB/EZ/commit/45ab8c45337df3554acdc5d91f3b7bf52cd15f9c))
* **stdlib/math:** preserve precision for factorial, gcd, lcm ([d7dc223](https://github.com/SchoolyB/EZ/commit/d7dc2232997223e678a36b28e26dab85f3d2ceed))
* **stdlib/math:** preserve precision for large integers ([88187fb](https://github.com/SchoolyB/EZ/commit/88187fb3c4662b83a8cd6944b977223e86e6ca51))
* **stdlib/math:** preserve precision for min, max, clamp ([b9ba598](https://github.com/SchoolyB/EZ/commit/b9ba5989d851cb63a831e0f74284b352147c3dfe))
* **stdlib/math:** preserve precision for pow, sum ([e6eada0](https://github.com/SchoolyB/EZ/commit/e6eada0a6f9fc6481dfccdc9126ea5a2cf1b8788))
* **stdlib/math:** preserve precision for sign ([c7ba2f1](https://github.com/SchoolyB/EZ/commit/c7ba2f18b69049bdb438f5b30f7927662388f2ec))
* **tests:** remove E17004_db_corrupted test ([#1052](https://github.com/SchoolyB/EZ/issues/1052)) ([275bdb3](https://github.com/SchoolyB/EZ/commit/275bdb352388c5b911cb15e310e6960985b44341)), closes [#1051](https://github.com/SchoolyB/EZ/issues/1051)

## [1.3.0](https://github.com/SchoolyB/EZ/compare/v1.2.0...v1.3.0) (2026-01-19)


### Features

* **stdlib:** Add Unix timestamp functions and date utilities to time module ([#1023](https://github.com/SchoolyB/EZ/issues/1023)) ([#1043](https://github.com/SchoolyB/EZ/issues/1043)) ([bdb24e8](https://github.com/SchoolyB/EZ/commit/bdb24e8bd29ca113ed294330e9eae4c4ad1dc08e))

## [1.2.0](https://github.com/SchoolyB/EZ/compare/v1.1.0...v1.2.0) (2026-01-19)


### Features

* **lexer:** add \x hex escape sequence support in strings and chars ([#1046](https://github.com/SchoolyB/EZ/issues/1046)) ([3d4eae7](https://github.com/SchoolyB/EZ/commit/3d4eae7764ab4f53c3488357aa70b55dae877674)), closes [#1045](https://github.com/SchoolyB/EZ/issues/1045)

## [1.1.0](https://github.com/SchoolyB/EZ/compare/v1.0.2...v1.1.0) (2026-01-18)


### Features

* stdlib enhancements, bug fixes, and type system improvements ([2362596](https://github.com/SchoolyB/EZ/commit/2362596910747cccbe7ba80726aa038a22aff045))
* **stdlib:** add `db.entries()` function ([#1035](https://github.com/SchoolyB/EZ/issues/1035)) ([093742f](https://github.com/SchoolyB/EZ/commit/093742fed2fe3b99a23ba78a02e20b39f08be6e4)), closes [#1025](https://github.com/SchoolyB/EZ/issues/1025)
* **stdlib:** add arrays.equals() function ([#1033](https://github.com/SchoolyB/EZ/issues/1033)) ([67258bd](https://github.com/SchoolyB/EZ/commit/67258bd0ebf7352f711f94d4254fa72e2701fadf)), closes [#964](https://github.com/SchoolyB/EZ/issues/964)
* **stdlib:** add db.values() function ([#1032](https://github.com/SchoolyB/EZ/issues/1032)) ([3dd7dd0](https://github.com/SchoolyB/EZ/commit/3dd7dd02a828fbafbe23412a6cdb9f5090d8450e)), closes [#943](https://github.com/SchoolyB/EZ/issues/943)
* **stdlib:** add http.head, options, download, parse_url, build_url ([#1034](https://github.com/SchoolyB/EZ/issues/1034)) ([e89b899](https://github.com/SchoolyB/EZ/commit/e89b8998304898e4f5d5ff93e58d3b0f8f6aeb2a)), closes [#1022](https://github.com/SchoolyB/EZ/issues/1022)


### Bug Fixes

* context-aware return type inference for array literals ([#1008](https://github.com/SchoolyB/EZ/issues/1008)) ([31ed9fa](https://github.com/SchoolyB/EZ/commit/31ed9fa5a077345c569e581952479d98566d6f82))
* **interpreter:** context-aware return type inference for array literals ([1353cc1](https://github.com/SchoolyB/EZ/commit/1353cc181743ac6a76b45b56fd682efa29277584)), closes [#1008](https://github.com/SchoolyB/EZ/issues/1008)
* **interpreter:** infer element type for array literals ([45a18dc](https://github.com/SchoolyB/EZ/commit/45a18dc664aa554784f84a57d9015b4f146f1f01)), closes [#1008](https://github.com/SchoolyB/EZ/issues/1008)
* **typechecker:** context-aware return type validation for array literals ([2920d77](https://github.com/SchoolyB/EZ/commit/2920d77425da7e56c59410bfb32a9a32bef99a48))
* **typechecker:** recognize when/default as exhaustive return coverage ([9db5c52](https://github.com/SchoolyB/EZ/commit/9db5c521acb273b5f328032f72264eb1fe9d21b1))
* **typechecker:** recognize when/default as exhaustive return coverage ([978a4d1](https://github.com/SchoolyB/EZ/commit/978a4d1fda24199f9e43775798ebf801b396e337)), closes [#918](https://github.com/SchoolyB/EZ/issues/918)

## [1.0.2](https://github.com/SchoolyB/EZ/compare/v1.0.1...v1.0.2) (2026-01-17)


### Bug Fixes

* **typechecker:** error when fixed-size array has too many elements ([#1030](https://github.com/SchoolyB/EZ/issues/1030)) ([7d09424](https://github.com/SchoolyB/EZ/commit/7d09424f2bd6128e624a772b8ce2165b15b98c7e)), closes [#1029](https://github.com/SchoolyB/EZ/issues/1029)

## [1.0.1](https://github.com/SchoolyB/EZ/compare/v1.0.0...v1.0.1) (2026-01-15)


### Bug Fixes

* **interpreter:** add error codes and location info to runtime errors ([#1009](https://github.com/SchoolyB/EZ/issues/1009)) ([#1013](https://github.com/SchoolyB/EZ/issues/1013)) ([d1bb289](https://github.com/SchoolyB/EZ/commit/d1bb2897768307a194bbe71b5df8574b976ba272))
* **interpreter:** add range checking for integer type narrowing ([#962](https://github.com/SchoolyB/EZ/issues/962)) ([#1016](https://github.com/SchoolyB/EZ/issues/1016)) ([02f953b](https://github.com/SchoolyB/EZ/commit/02f953bb6e4efb2b42c47af5708ac5826abd5723))
* **stdlib:** handle empty database files in db.open() ([#941](https://github.com/SchoolyB/EZ/issues/941)) ([#1012](https://github.com/SchoolyB/EZ/issues/1012)) ([eadc48d](https://github.com/SchoolyB/EZ/commit/eadc48d3edb7817aeddf8b405b01697d6015b3ff))
* **stdlib:** improve error message when db file contains JSON array ([#942](https://github.com/SchoolyB/EZ/issues/942)) ([#1014](https://github.com/SchoolyB/EZ/issues/1014)) ([bea2f65](https://github.com/SchoolyB/EZ/commit/bea2f659dd141a09014e4a3d7fefd31796380050))
* **typechecker:** use correct error code E5010 for continue outside loop ([#963](https://github.com/SchoolyB/EZ/issues/963)) ([#1011](https://github.com/SchoolyB/EZ/issues/1011)) ([9f55570](https://github.com/SchoolyB/EZ/commit/9f55570b34c66af16e7eb46cc630801361fe6e87))

## [1.0.0](https://github.com/SchoolyB/EZ/compare/v0.40.5...v1.0.0) (2026-01-14)


### ⚠ BREAKING CHANGES

* Existing code using removed/renamed functions must be updated.

### Features

* **#996:** Rename stdlib functions for consistency ([#1001](https://github.com/SchoolyB/EZ/issues/1001)) ([0ad96f8](https://github.com/SchoolyB/EZ/commit/0ad96f881a24123d6fba7f414a48bdb0b819a22d))
* stdlib function renaming for consistency ([09b658b](https://github.com/SchoolyB/EZ/commit/09b658b681593511294c6fe41d762d117c2aea4f))


### Code Refactoring

* remove duplicate stdlib aliases and rename db functions ([#1003](https://github.com/SchoolyB/EZ/issues/1003)) ([4e0dc08](https://github.com/SchoolyB/EZ/commit/4e0dc081711bd70b237bd71fde809972b0db7eca)), closes [#995](https://github.com/SchoolyB/EZ/issues/995) [#997](https://github.com/SchoolyB/EZ/issues/997)

## [0.40.5](https://github.com/SchoolyB/EZ/compare/v0.40.4...v0.40.5) (2026-01-11)


### Bug Fixes

* Add `isMultiReturnCall` to typechecker ([#951](https://github.com/SchoolyB/EZ/issues/951)) ([981d068](https://github.com/SchoolyB/EZ/commit/981d068676520c1bd1a545c8f0b524ff614856eb))
* **cli:** Allow command line arguments for programs ([#983](https://github.com/SchoolyB/EZ/issues/983)) ([9f4439d](https://github.com/SchoolyB/EZ/commit/9f4439d29be136c720d62d2c3bb0f971e60a414e))
* Detect invalid string interpolation syntax at parse time ([#988](https://github.com/SchoolyB/EZ/issues/988)) ([7fcbcc1](https://github.com/SchoolyB/EZ/commit/7fcbcc152400e7b8e201eef2c92153a1d56c488d)), closes [#984](https://github.com/SchoolyB/EZ/issues/984)
* Error on bare function/type names as statements ([#989](https://github.com/SchoolyB/EZ/issues/989)) ([a84ab6b](https://github.com/SchoolyB/EZ/commit/a84ab6b2c742b5411ce84da4b54ff0785549c24a)), closes [#985](https://github.com/SchoolyB/EZ/issues/985)
* Prevent RETURN_VALUE type leak for multi-return functions ([#987](https://github.com/SchoolyB/EZ/issues/987)) ([750906f](https://github.com/SchoolyB/EZ/commit/750906fcbb445ecdf119c0a8f9204453674a2dcf)), closes [#986](https://github.com/SchoolyB/EZ/issues/986)

## [0.40.4](https://github.com/SchoolyB/EZ/compare/v0.40.3...v0.40.4) (2026-01-10)


### Bug Fixes

* resolve integration test failures on Linux ([#981](https://github.com/SchoolyB/EZ/issues/981)) ([b20d676](https://github.com/SchoolyB/EZ/commit/b20d676e28d3dff80ab408a3f6ec698f43918b0f)), closes [#978](https://github.com/SchoolyB/EZ/issues/978)

## [0.40.3](https://github.com/SchoolyB/EZ/compare/v0.40.2...v0.40.3) (2026-01-10)


### Bug Fixes

* Typechecker modification to resolve "using directive" bug ([#979](https://github.com/SchoolyB/EZ/issues/979)) ([42cf234](https://github.com/SchoolyB/EZ/commit/42cf23451c47dc40211e8d636f6abae56f4eb3a4))

## [0.40.2](https://github.com/SchoolyB/EZ/compare/v0.40.1...v0.40.2) (2026-01-10)


### Bug Fixes

* **ci:** use EZ_DOCS_SYNC token for website repo access ([#974](https://github.com/SchoolyB/EZ/issues/974)) ([f47c649](https://github.com/SchoolyB/EZ/commit/f47c649a5f6bf012e315fc7a5f414fae0180641d))

## [0.40.1](https://github.com/SchoolyB/EZ/compare/v0.40.0...v0.40.1) (2026-01-10)


### Bug Fixes

* **ci:** create wasm directory before copying files ([#972](https://github.com/SchoolyB/EZ/issues/972)) ([193ddcb](https://github.com/SchoolyB/EZ/commit/193ddcb8fd461eb8a599d8b84e4e3a2603401899))

## [0.40.0](https://github.com/SchoolyB/EZ/compare/v0.39.3...v0.40.0) (2026-01-10)


### Features

* **wasm:** add WebAssembly build for browser playground ([#970](https://github.com/SchoolyB/EZ/issues/970)) ([89e860e](https://github.com/SchoolyB/EZ/commit/89e860e4c41865fe1e0a82e9a26ea64d8913b10e))

## [0.39.3](https://github.com/SchoolyB/EZ/compare/v0.39.2...v0.39.3) (2026-01-09)


### Bug Fixes

* **interpreter,stdlib:** replace .Type() with helper functions in error messages ([#956](https://github.com/SchoolyB/EZ/issues/956)) ([61cc075](https://github.com/SchoolyB/EZ/commit/61cc07522f3d5870320a2fd9141fe504a74fe62a)), closes [#948](https://github.com/SchoolyB/EZ/issues/948)
* **interpreter:** add Map case to objectTypeToEZ function ([#955](https://github.com/SchoolyB/EZ/issues/955)) ([8ac5396](https://github.com/SchoolyB/EZ/commit/8ac53962ac63ac22a666ccc77ab4f42be59a0f8a)), closes [#945](https://github.com/SchoolyB/EZ/issues/945)
* **interpreter:** update type name functions with missing types ([#946](https://github.com/SchoolyB/EZ/issues/946)) ([#958](https://github.com/SchoolyB/EZ/issues/958)) ([0774dde](https://github.com/SchoolyB/EZ/commit/0774dde15d73cbbda2fb516915f47c648421f09f))
* **lexer,parser:** add explicit 0o octal prefix, treat leading zeros as decimal ([#954](https://github.com/SchoolyB/EZ/issues/954)) ([b8e7f41](https://github.com/SchoolyB/EZ/commit/b8e7f418b4575480a95afb46501a68c5d1136fde)), closes [#915](https://github.com/SchoolyB/EZ/issues/915)
* **repl:** prevent E4001 error when running REPL commands ([#953](https://github.com/SchoolyB/EZ/issues/953)) ([019a81b](https://github.com/SchoolyB/EZ/commit/019a81befa384bb086868b23f6f767a2eb83ca4b)), closes [#890](https://github.com/SchoolyB/EZ/issues/890)
* **typechecker,stdlib,tests:** fix integration test failures ([#952](https://github.com/SchoolyB/EZ/issues/952)) ([#960](https://github.com/SchoolyB/EZ/issues/960)) ([f528389](https://github.com/SchoolyB/EZ/commit/f528389001fb1e291417c6439dc0ecda04b56a6c))
* **typechecker:** require type argument for json.decode ([#947](https://github.com/SchoolyB/EZ/issues/947)) ([#957](https://github.com/SchoolyB/EZ/issues/957)) ([2cbb2c2](https://github.com/SchoolyB/EZ/commit/2cbb2c21375eba690b7a807685afb0f8a4dda9c8))

## [0.39.2](https://github.com/SchoolyB/EZ/compare/v0.39.1...v0.39.2) (2026-01-07)


### Bug Fixes

* **interpreter:** allow arbitrary precision arithmetic for int/uint types ([#917](https://github.com/SchoolyB/EZ/issues/917)) ([#931](https://github.com/SchoolyB/EZ/issues/931)) ([728cd19](https://github.com/SchoolyB/EZ/commit/728cd195b90ddbf3cf373814e6765e2205f0b763))
* **parser:** error on invalid operators in string interpolation ([#916](https://github.com/SchoolyB/EZ/issues/916)) ([#933](https://github.com/SchoolyB/EZ/issues/933)) ([54b03bc](https://github.com/SchoolyB/EZ/commit/54b03bcdfed35e0b54bd7dec2262f0f199cf8a04))
* **typechecker:** show correct error for invalid sized types with using directive ([#914](https://github.com/SchoolyB/EZ/issues/914)) ([#932](https://github.com/SchoolyB/EZ/issues/932)) ([9ba4bc0](https://github.com/SchoolyB/EZ/commit/9ba4bc0217edabee4504bac0feca8a8f8b76e3e6))

## [0.39.1](https://github.com/SchoolyB/EZ/compare/v0.39.0...v0.39.1) (2026-01-06)


### Bug Fixes

* **update:** exit original process after sudo completes ([#927](https://github.com/SchoolyB/EZ/issues/927)) ([266c5c3](https://github.com/SchoolyB/EZ/commit/266c5c3f759067db37e6043b62e5d60a8bfc5468))

## [0.39.0](https://github.com/SchoolyB/EZ/compare/v0.38.0...v0.39.0) (2026-01-06)


### Features

* **time:** add weekday, month and duration constants (resolves [#902](https://github.com/SchoolyB/EZ/issues/902)) ([#923](https://github.com/SchoolyB/EZ/issues/923)) ([179fe62](https://github.com/SchoolyB/EZ/commit/179fe6290e8e95b35085387a2969f911bd87067b))

## [0.38.0](https://github.com/SchoolyB/EZ/compare/v0.37.0...v0.38.0) (2026-01-06)


### Features

* **cli:** improve ez update changelog display ([#920](https://github.com/SchoolyB/EZ/issues/920)) ([7125b83](https://github.com/SchoolyB/EZ/commit/7125b83f4df95183c25795722fd95086f6f3ca3d)), closes [#919](https://github.com/SchoolyB/EZ/issues/919)

## [0.37.0](https://github.com/SchoolyB/EZ/compare/v0.36.7...v0.37.0) (2026-01-05)


### Features

* **http:** add HTTP status code constants ([#907](https://github.com/SchoolyB/EZ/issues/907)) ([5ce155d](https://github.com/SchoolyB/EZ/commit/5ce155d7163d83a3f6bd68ba5e3be618de58b4ac))

## [0.36.7](https://github.com/SchoolyB/EZ/compare/v0.36.6...v0.36.7) (2026-01-04)


### Bug Fixes

* **ci:** use file -b to avoid matching on filenames in sanity check ([f01ca70](https://github.com/SchoolyB/EZ/commit/f01ca70345368e389e7d9880e0ab4111be557e26))
* Internal type representation leaks Go type names instead of EZ types ([26dd6c1](https://github.com/SchoolyB/EZ/commit/26dd6c12921edf797e93ecc68b08be9b7d2048cd))
* **interpreter:** set Map KeyType/ValueType when maps are created ([04e164f](https://github.com/SchoolyB/EZ/commit/04e164fc6932f5929f3f877aa7a13245e0930c66))
* **stdlib:** add DeclaredType to float decode functions in binary module ([2fd5582](https://github.com/SchoolyB/EZ/commit/2fd558242534d5185e2b84a5eba5e8f8958f978a))
* **stdlib:** add ElementType to bytes.split outer array ([330cb1d](https://github.com/SchoolyB/EZ/commit/330cb1db88060fd7a358523ee5adb6cd77ec9aac))
* **stdlib:** add ElementType to empty array in crypto.random_bytes ([02293e1](https://github.com/SchoolyB/EZ/commit/02293e197ac63c8af5a42740a8fe0090b725152d))
* **stdlib:** preserve ElementType in arrays module functions ([bb0f8de](https://github.com/SchoolyB/EZ/commit/bb0f8dec4f1b084682e1334f1fad9dd6fc98c816)), closes [#900](https://github.com/SchoolyB/EZ/issues/900)
* **stdlib:** preserve type info in `[@maps](https://github.com/maps)` module and copyByDefault ([5a0259a](https://github.com/SchoolyB/EZ/commit/5a0259a430b2c5a0df65e926f78b6b2c938f42c0))
* **stdlib:** preserve type info in builtins and `[@strings](https://github.com/strings)` modules ([0dc4d44](https://github.com/SchoolyB/EZ/commit/0dc4d44efb1fa9ea29a593fa6d5829e59f075aa3))
* **stdlib:** set DeclaredType for floats in json module ([cb0d46f](https://github.com/SchoolyB/EZ/commit/cb0d46f6c1d5d1065955f66b965bd0644ecbbed8)), closes [#900](https://github.com/SchoolyB/EZ/issues/900)
* **stdlib:** set ElementType for arrays in db module ([6e5086a](https://github.com/SchoolyB/EZ/commit/6e5086a4b9e6c524267fde59ea40a150d0b6e6b5)), closes [#900](https://github.com/SchoolyB/EZ/issues/900)
* **stdlib:** set ElementType for arrays in io module ([637bbb7](https://github.com/SchoolyB/EZ/commit/637bbb7812b7eb1430824a12efb69ec8757fb7df)), closes [#900](https://github.com/SchoolyB/EZ/issues/900)
* **stdlib:** set proper types for headers map and arrays in http module ([450c5f3](https://github.com/SchoolyB/EZ/commit/450c5f3b7be43b4d3c3b84c1673d5b7942005371))
* **stdlib:** set proper types in os module ([bba1458](https://github.com/SchoolyB/EZ/commit/bba1458f8bee9531162a3704e7c0a6b977456db2)), closes [#900](https://github.com/SchoolyB/EZ/issues/900)
* **stdlib:** set proper types in random module ([4739f16](https://github.com/SchoolyB/EZ/commit/4739f161aa6872b4f0485bd7bd72ac953cc45499)), closes [#900](https://github.com/SchoolyB/EZ/issues/900)
* **stdlib:** update getEZTypeName to return proper EZ type names ([d8ee463](https://github.com/SchoolyB/EZ/commit/d8ee463a969c1043062f53ee4a0322ad9ad29b72))
* **typechecker:** reject typeof() on void function results ([ff9897d](https://github.com/SchoolyB/EZ/commit/ff9897d233e0b3f2ccefdd52412bd8b317552ff1))
* **types:** add DeclaredType support for Float and sized type conversions ([979e9fd](https://github.com/SchoolyB/EZ/commit/979e9fdd28dcea79f43539b6a1eda35d959b71fe))
* **types:** typeof() returns "Database" for database objects ([ad2ed95](https://github.com/SchoolyB/EZ/commit/ad2ed951b639438040c8a9f9711114403d3e0486)), closes [#900](https://github.com/SchoolyB/EZ/issues/900)
* **types:** typeof() returns "Range&lt;int&gt;" for range objects ([64cbb0a](https://github.com/SchoolyB/EZ/commit/64cbb0a762a032e370a41b7fb1cc6f5e952aa367)), closes [#900](https://github.com/SchoolyB/EZ/issues/900)
* **types:** typeof() returns Ref&lt;innerType&gt; for references ([8b06b9b](https://github.com/SchoolyB/EZ/commit/8b06b9b906d5c8f6589fbd95e47f2bb4bf75a991)), closes [#900](https://github.com/SchoolyB/EZ/issues/900)
* **types:** use "File" as user-facing type name for FileHandle ([c4d9a5a](https://github.com/SchoolyB/EZ/commit/c4d9a5abaac754c7c39d7a8117489cad0281f590)), closes [#900](https://github.com/SchoolyB/EZ/issues/900)

## [0.36.6](https://github.com/SchoolyB/EZ/compare/v0.36.5...v0.36.6) (2026-01-04)


### Bug Fixes

* **errors:** update E3038 hint to be more accurate ([a34b1b5](https://github.com/SchoolyB/EZ/commit/a34b1b5a42edcfd453c5562c5c2c3df9936a9fa2))
* **interpreter:** handle void return with ensure correctly ([5580b07](https://github.com/SchoolyB/EZ/commit/5580b0777818975cda78a27be53baee1ec72320f)), closes [#883](https://github.com/SchoolyB/EZ/issues/883)
* **typechecker:** prevent assignment of void function results ([a574be5](https://github.com/SchoolyB/EZ/commit/a574be5a5888e1dcd6d46bd0b043a67438b3323d))
* **typechecker:** reject void as explicit return type ([d307711](https://github.com/SchoolyB/EZ/commit/d307711471dcbd5872c6f5f2274ec337d859e89a))
* **typechecker:** reject void in compound types ([b2182bf](https://github.com/SchoolyB/EZ/commit/b2182bfaac444ef37a47c643cdcf591102a7eb85))
* void function fixes and comprehensive type rejection ([e504704](https://github.com/SchoolyB/EZ/commit/e504704238c91dd7ab4e8f65476412160aed4e10))

## [0.36.5](https://github.com/SchoolyB/EZ/compare/v0.36.4...v0.36.5) (2026-01-04)


### Bug Fixes

* **repl:** add multi-line navigation support ([33381b0](https://github.com/SchoolyB/EZ/commit/33381b0de8b973edf8cad6424f1ae86771c6b0b2))
* **repl:** add multi-line navigation support ([a4dcf3d](https://github.com/SchoolyB/EZ/commit/a4dcf3dacb11dc072574a9a7c0de3c368bdeb93c)), closes [#875](https://github.com/SchoolyB/EZ/issues/875)

## [0.36.4](https://github.com/SchoolyB/EZ/compare/v0.36.3...v0.36.4) (2026-01-03)


### Bug Fixes

* **typechecker:** runtime string relational operator failure ([#888](https://github.com/SchoolyB/EZ/issues/888)) ([bc7aee4](https://github.com/SchoolyB/EZ/commit/bc7aee4cdb23dc89e05b7ee84156cf3ee8d92137))

## [0.36.3](https://github.com/SchoolyB/EZ/compare/v0.36.2...v0.36.3) (2025-12-31)


### Features

* add`[@http](https://github.com/http)` module HTTP client for web requests ([#877](https://github.com/SchoolyB/EZ/issues/877)) ([34a874f](https://github.com/SchoolyB/EZ/commit/34a874f753f68ff8fbd52ea83a9ff179dad3b2fd))


### Bug Fixes

* Bug: Type inference fails for most stdlib module function calls ([#878](https://github.com/SchoolyB/EZ/issues/878)) ([4b6bfa7](https://github.com/SchoolyB/EZ/commit/4b6bfa79c95250241da3f127230ea33317e4f037))
* resolve merge conflict and regenerate ERRORS.md ([53cc4da](https://github.com/SchoolyB/EZ/commit/53cc4da6f34cc3f644f19d022e5e06a6279a5cf8))
* **typechecker:** infer json.decode return type from type argument ([908b026](https://github.com/SchoolyB/EZ/commit/908b0265b1df41b75c39cd75679ee13282ab7c14))

## [0.36.2](https://github.com/SchoolyB/EZ/compare/v0.36.1...v0.36.2) (2025-12-28)


### Bug Fixes

* **ensure:** add missing validations and revert workflow change ([#866](https://github.com/SchoolyB/EZ/issues/866)) ([e502fbd](https://github.com/SchoolyB/EZ/commit/e502fbd2e41abf074ddff95ef71f6b1ab91373a0))

## [0.36.1](https://github.com/SchoolyB/EZ/compare/v0.36.0...v0.36.1) (2025-12-28)


### Features

* `ensure` keyword for guaranteed cleanup on function exit [#804](https://github.com/SchoolyB/EZ/issues/804) ([#864](https://github.com/SchoolyB/EZ/issues/864)) ([cbef798](https://github.com/SchoolyB/EZ/commit/cbef7983e727a194b4453fc38e48e83ac2fb319a))

## [0.36.0](https://github.com/SchoolyB/EZ/compare/v0.35.0...v0.36.0) (2025-12-28)


### Features

* stdlib additions and bug fixes ([88791f4](https://github.com/SchoolyB/EZ/commit/88791f4a39b32b68365b5f50e977f763839d9a96))
* **stdlib:** add [@crypto](https://github.com/crypto) module for hashing and secure random ([9a38974](https://github.com/SchoolyB/EZ/commit/9a3897430d77b6561d6dee76c34e50fcdf5f317d)), closes [#458](https://github.com/SchoolyB/EZ/issues/458)
* **stdlib:** add [@encoding](https://github.com/encoding) module with base64, hex, and URL functions ([58cf8b6](https://github.com/SchoolyB/EZ/commit/58cf8b6301769e58d323816bff6fe794ff6f55b6))
* **stdlib:** add [@uuid](https://github.com/uuid) module for UUID generation ([e283602](https://github.com/SchoolyB/EZ/commit/e283602315cffdf0f46635b55baf1fcff8bdf38a)), closes [#759](https://github.com/SchoolyB/EZ/issues/759)


### Bug Fixes

* **evaluator:** allow modifying nested struct fields in array elements ([4d5de68](https://github.com/SchoolyB/EZ/commit/4d5de68500fb29a6d00d729ce141704377c22df2)), closes [#859](https://github.com/SchoolyB/EZ/issues/859)
* **evaluator:** prevent stack overflow on self-referential structs ([6f68d98](https://github.com/SchoolyB/EZ/commit/6f68d981901738e4b6a6d880e61a9650cbe971a8)), closes [#860](https://github.com/SchoolyB/EZ/issues/860)
* **parser:** allow blank identifier _ in for and for_each loops ([456e75d](https://github.com/SchoolyB/EZ/commit/456e75dcdc287b762fa505561bfe8790c1f03295)), closes [#858](https://github.com/SchoolyB/EZ/issues/858)
* **typechecker:** allow int/float comparison with == and != operators ([0a5b97d](https://github.com/SchoolyB/EZ/commit/0a5b97d90dbc0b94544f03567190aa3d8c9ce39f)), closes [#857](https://github.com/SchoolyB/EZ/issues/857)

## [0.35.0](https://github.com/SchoolyB/EZ/compare/v0.34.1...v0.35.0) (2025-12-27)


### Features

* ASCII art banner ([#855](https://github.com/SchoolyB/EZ/issues/855)) ([4ccaea8](https://github.com/SchoolyB/EZ/commit/4ccaea8989729e55beb068cc0ee0cd5aec8389f8))

## [0.34.1](https://github.com/SchoolyB/EZ/compare/v0.34.0...v0.34.1) (2025-12-27)


### Bug Fixes

* **typechecker:** resolve struct parameter member access for imported types ([#851](https://github.com/SchoolyB/EZ/issues/851)) ([#853](https://github.com/SchoolyB/EZ/issues/853)) ([dada274](https://github.com/SchoolyB/EZ/commit/dada2740cca72f88d8ed56699d7596e38e368bfe))

## [0.34.0](https://github.com/SchoolyB/EZ/compare/v0.33.4...v0.34.0) (2025-12-26)


### Features

* add warning for unused imports ([#639](https://github.com/SchoolyB/EZ/issues/639)) ([#844](https://github.com/SchoolyB/EZ/issues/844)) ([d299bcd](https://github.com/SchoolyB/EZ/commit/d299bcdcbc750037d68ceb591c94c951abcf1152))
* **examples:** add price calculator example ([#848](https://github.com/SchoolyB/EZ/issues/848)) ([70d10b7](https://github.com/SchoolyB/EZ/commit/70d10b721970f2fb266c892128b974ad9d069f15))


### Bug Fixes

* **examples:** resolve W2009 warning in json.ez ([#357](https://github.com/SchoolyB/EZ/issues/357)) ([#847](https://github.com/SchoolyB/EZ/issues/847)) ([20815bb](https://github.com/SchoolyB/EZ/commit/20815bb81d24c2c4803d89e0c65daf162941f032))

## [0.33.4](https://github.com/SchoolyB/EZ/compare/v0.33.3...v0.33.4) (2025-12-26)


### Bug Fixes

* UTF-8 characters corrupted in interpolated strings ([#839](https://github.com/SchoolyB/EZ/issues/839)) ([ebb34dc](https://github.com/SchoolyB/EZ/commit/ebb34dc145235f85d7fbb3dc748132fd10a0e4bf))

## [0.33.3](https://github.com/SchoolyB/EZ/compare/v0.33.2...v0.33.3) (2025-12-25)


### Bug Fixes

* type inference for user module functions and void type validation ([#837](https://github.com/SchoolyB/EZ/issues/837)) ([c63c48f](https://github.com/SchoolyB/EZ/commit/c63c48f5d9e1cfad52c914e1f018e3e75e0621e6))

## [0.33.2](https://github.com/SchoolyB/EZ/compare/v0.33.1...v0.33.2) (2025-12-25)


### Bug Fixes

* #suppress before function now correctly applies to function only ([#830](https://github.com/SchoolyB/EZ/issues/830), [#831](https://github.com/SchoolyB/EZ/issues/831)) ([#832](https://github.com/SchoolyB/EZ/issues/832)) ([6837668](https://github.com/SchoolyB/EZ/commit/68376685eab9e4aeec51b5b3454aac704a90b34c))
* add cleanup for database test files ([#800](https://github.com/SchoolyB/EZ/issues/800)) ([#829](https://github.com/SchoolyB/EZ/issues/829)) ([35cfec8](https://github.com/SchoolyB/EZ/commit/35cfec8bcadb9bae1b7d1bc14f551a903473804f))
* add overflow and division-by-zero checks for float arithmetic ([#798](https://github.com/SchoolyB/EZ/issues/798)) ([#826](https://github.com/SchoolyB/EZ/issues/826)) ([476c18d](https://github.com/SchoolyB/EZ/commit/476c18d284d5841972a248701c6f3e22236d9196))
* add overflow/underflow checks for byte arithmetic ([#817](https://github.com/SchoolyB/EZ/issues/817)) ([#822](https://github.com/SchoolyB/EZ/issues/822)) ([5bd37ba](https://github.com/SchoolyB/EZ/commit/5bd37ba8ab7e2d538d27cf163e9f7822c2e38170))
* disallow loop variable shadowing ([#114](https://github.com/SchoolyB/EZ/issues/114)) ([#835](https://github.com/SchoolyB/EZ/issues/835)) ([1cd45c2](https://github.com/SchoolyB/EZ/commit/1cd45c272113f1473b27e4d5c3557c1892817eee))
* make arrays.remove_all() consistent with other remove functions ([#820](https://github.com/SchoolyB/EZ/issues/820)) ([#825](https://github.com/SchoolyB/EZ/issues/825)) ([725d34d](https://github.com/SchoolyB/EZ/commit/725d34dd10dc86c7d2f1bdac44af712e90c1b3fb))
* os.exec now returns error on non-zero exit code ([#799](https://github.com/SchoolyB/EZ/issues/799)) ([#827](https://github.com/SchoolyB/EZ/issues/827)) ([06ee450](https://github.com/SchoolyB/EZ/commit/06ee450ed261fad68b87d0b8ee0d420eeb5b6220))
* prevent json.encode() from overwriting EncodeAsString conversion ([#818](https://github.com/SchoolyB/EZ/issues/818)) ([#823](https://github.com/SchoolyB/EZ/issues/823)) ([85544e7](https://github.com/SchoolyB/EZ/commit/85544e7e70499baba4814579564d8690d1f89f94))
* prevent panic on multi-return type mismatch with fewer names than types ([#816](https://github.com/SchoolyB/EZ/issues/816)) ([#821](https://github.com/SchoolyB/EZ/issues/821)) ([d983679](https://github.com/SchoolyB/EZ/commit/d98367975d9be03db6b95e5e8ab4171f164be36c))
* properly extract first rune in strings.from_chars() ([#819](https://github.com/SchoolyB/EZ/issues/819)) ([#824](https://github.com/SchoolyB/EZ/issues/824)) ([060cfa6](https://github.com/SchoolyB/EZ/commit/060cfa67b746b3ff22944a584fde367c26ce4fc2))

## [0.33.1](https://github.com/SchoolyB/EZ/compare/v0.33.0...v0.33.1) (2025-12-24)


### Bug Fixes

* mutable parameters now work with indexed/member expressions ([#813](https://github.com/SchoolyB/EZ/issues/813)) ([4b7bdf2](https://github.com/SchoolyB/EZ/commit/4b7bdf2cfd961e8623d025066d2875ddc472fcc9)), closes [#797](https://github.com/SchoolyB/EZ/issues/797)
* prevent array mutation during for_each iteration ([#812](https://github.com/SchoolyB/EZ/issues/812)) ([00e0c83](https://github.com/SchoolyB/EZ/commit/00e0c8396b97c2753c5da05713cbaa627f25f7f5)), closes [#796](https://github.com/SchoolyB/EZ/issues/796)
* type inference for user-defined module function return types ([#811](https://github.com/SchoolyB/EZ/issues/811)) ([be278e7](https://github.com/SchoolyB/EZ/commit/be278e72ac1c59bf6675e688f1c13c9324ad6b56)), closes [#807](https://github.com/SchoolyB/EZ/issues/807)

## [0.33.0](https://github.com/SchoolyB/EZ/compare/v0.32.0...v0.33.0) (2025-12-23)


### Features

* add PowerShell install script for Windows ([e5ca88b](https://github.com/SchoolyB/EZ/commit/e5ca88bc7595be9beea852117c88d6c704cf3644))
* add PowerShell install script for Windows ([2244edb](https://github.com/SchoolyB/EZ/commit/2244edbb6c0b74f32e697710273ca459c1fc0a00)), closes [#806](https://github.com/SchoolyB/EZ/issues/806)

## [0.32.0](https://github.com/SchoolyB/EZ/compare/v0.31.0...v0.32.0) (2025-12-23)


### Features

* added workflow for checking PR validity based on filetype ([632d8bd](https://github.com/SchoolyB/EZ/commit/632d8bdf1b58fd9316ca5af5e2f13aed143168ff))


### Bug Fixes

* **ci:** add more file extension checks ([c6223fd](https://github.com/SchoolyB/EZ/commit/c6223fd8483db67bb6336dd6b84bf6391791d765))
* **ci:** correct workflow issues in PR sanity check ([9adafdd](https://github.com/SchoolyB/EZ/commit/9adafdd322c0b194c9fc6b20928d91f6a4280676))

## [0.31.0](https://github.com/SchoolyB/EZ/compare/v0.30.2...v0.31.0) (2025-12-22)


### Features

* **stdlib:** add db.exists() and db.update_key_name() ([f89aac1](https://github.com/SchoolyB/EZ/commit/f89aac142eb5344e7bc86a4500353a1d9cd2c74b))
* **stdlib:** add db.sort() with sort order constants ([8a26dab](https://github.com/SchoolyB/EZ/commit/8a26dab2c040d2372b004c97d71b9e15363794ed)), closes [#782](https://github.com/SchoolyB/EZ/issues/782)


### Bug Fixes

* **cli:** always fetch fresh version info for `ez version` command ([ffe4979](https://github.com/SchoolyB/EZ/commit/ffe49794dae0b9e5abd6189d5392c3ef9a45a5ec))
* **stdlib:** show overflow error instead of invalid format for large integers ([e4ff4bb](https://github.com/SchoolyB/EZ/commit/e4ff4bbbf01e9164aeb3199eb63dee919b78160a)), closes [#783](https://github.com/SchoolyB/EZ/issues/783)
* **tests:** strengthen db.ez assertions to actually verify behavior ([5e2bd65](https://github.com/SchoolyB/EZ/commit/5e2bd652f620db758e20f1ec3c69193bb3b32480))

## [0.30.2](https://github.com/SchoolyB/EZ/compare/v0.30.1...v0.30.2) (2025-12-22)


### Bug Fixes

* **typechecker:** require Database type for db module functions ([2d63dc6](https://github.com/SchoolyB/EZ/commit/2d63dc6b9b10582e26102023f9a31cd7497af976))
* **typechecker:** require Database type for db module functions ([851a6c2](https://github.com/SchoolyB/EZ/commit/851a6c2da1c824ef48c90f4af6c4a790695a2c87)), closes [#781](https://github.com/SchoolyB/EZ/issues/781)

## [0.30.1](https://github.com/SchoolyB/EZ/compare/v0.30.0...v0.30.1) (2025-12-22)


### Bug Fixes

* **stdlib:** write valid JSON when creating new database file ([ecb396a](https://github.com/SchoolyB/EZ/commit/ecb396a07e99e93519fcb3677e89b26fa66b9956))
* **stdlib:** write valid JSON when creating new database file ([46e0da2](https://github.com/SchoolyB/EZ/commit/46e0da26f15f7978bb05324a60d7adc3346237b3)), closes [#780](https://github.com/SchoolyB/EZ/issues/780)

## [0.30.0](https://github.com/SchoolyB/EZ/compare/v0.29.2...v0.30.0) (2025-12-21)


### Features

* **stdlib:** add [@db](https://github.com/db) module for simple key-value database ([342a4f9](https://github.com/SchoolyB/EZ/commit/342a4f922911cebf1a56eda176a83874b907cdf4))
* **stdlib:** add [@db](https://github.com/db) module for simple key-value database ([23e112a](https://github.com/SchoolyB/EZ/commit/23e112a3d9086ce9baf9ce66f6e8b936ed6c3932)), closes [#460](https://github.com/SchoolyB/EZ/issues/460)

## [0.29.2](https://github.com/SchoolyB/EZ/compare/v0.29.1...v0.29.2) (2025-12-21)


### Bug Fixes

* **cli:** consistent arg handling and remove stale `ez run` references ([8fcb687](https://github.com/SchoolyB/EZ/commit/8fcb687f7ef990aab376df653df3d14c29a0dc66))
* **cli:** consistent arg handling and remove stale `ez run` references ([0b7ba8c](https://github.com/SchoolyB/EZ/commit/0b7ba8c2dbba5968c557c7bc46b3f1113be72e59)), closes [#765](https://github.com/SchoolyB/EZ/issues/765)
* **language:** remove broken `private:module` and add typechecker validation ([3fcb311](https://github.com/SchoolyB/EZ/commit/3fcb31107049e1143186d3f468e7b9a75fa9f7b8))
* use existing E12001 instead of undefined E9020 ([09734b0](https://github.com/SchoolyB/EZ/commit/09734b0960a5383d5bfd37d2d4c9bb9663ea951f))
* use existing E12001 instead of undefined E9020 ([f959f49](https://github.com/SchoolyB/EZ/commit/f959f49b6f92fc65e21e7a87c87e02f9566f9af6)), closes [#769](https://github.com/SchoolyB/EZ/issues/769)
* **visibility:** remove broken private:module and add typechecker validation ([bca6f4c](https://github.com/SchoolyB/EZ/commit/bca6f4cdb5058bf60729fc72a3bf375cf27e9149)), closes [#767](https://github.com/SchoolyB/EZ/issues/767)

## [0.29.1](https://github.com/SchoolyB/EZ/compare/v0.29.0...v0.29.1) (2025-12-20)


### Bug Fixes

* **interpreter:** add bounds check for empty ReturnValue.Values ([69c1c39](https://github.com/SchoolyB/EZ/commit/69c1c39b14ad7ffc728fcb17e4f3979b8a7d043d))
* **interpreter:** add bounds check for empty ReturnValue.Values ([66859ca](https://github.com/SchoolyB/EZ/commit/66859cad7eca7b3f58bcc76e28064c32956b9d75)), closes [#740](https://github.com/SchoolyB/EZ/issues/740)
* **interpreter:** check Deref() error before compound assignment ([645af52](https://github.com/SchoolyB/EZ/commit/645af527f6e29700094504c890c38e2e0dd0d9a3))
* **interpreter:** check Deref() error before compound assignment ([836b7f7](https://github.com/SchoolyB/EZ/commit/836b7f71bcde48738ea16f7f67528bbc197bb958)), closes [#741](https://github.com/SchoolyB/EZ/issues/741)
* **parser:** prevent panic on comma-separated struct fields ([7886e20](https://github.com/SchoolyB/EZ/commit/7886e206e0b309821e4d3254b935d7aa123b9d66))
* **parser:** prevent panic on comma-separated struct fields ([aa68024](https://github.com/SchoolyB/EZ/commit/aa680242c9eb6b0317acd29ddb663054c38bac21)), closes [#750](https://github.com/SchoolyB/EZ/issues/750)
* **stdlib:** add bounds check for integer-to-byte conversion ([191ec8d](https://github.com/SchoolyB/EZ/commit/191ec8d7cc76739995ab150a937eee9f69e8d0e7))
* **stdlib:** add bounds check for integer-to-byte conversion ([26e214f](https://github.com/SchoolyB/EZ/commit/26e214fd72504dbeca12c470b4f3a7c6a1729470)), closes [#748](https://github.com/SchoolyB/EZ/issues/748)
* **stdlib:** make ignored file Close errors explicit in io.go ([c4048a9](https://github.com/SchoolyB/EZ/commit/c4048a9119e5e6fc51acd9a80b22bb5233cbbf0a))
* **stdlib:** make ignored file Close errors explicit in io.go ([52dd245](https://github.com/SchoolyB/EZ/commit/52dd245abae18f20a569d8dbae7296034218816e)), closes [#742](https://github.com/SchoolyB/EZ/issues/742)
* **stdlib:** remove redundant arrays.range() function ([f957fdd](https://github.com/SchoolyB/EZ/commit/f957fdd594212261739e1f9ce96dfb193f842c53))
* **stdlib:** remove redundant arrays.range() function ([6a2a7ec](https://github.com/SchoolyB/EZ/commit/6a2a7ec428f136deec22e6bd088f5109a0cc13e4)), closes [#746](https://github.com/SchoolyB/EZ/issues/746)
* **typechecker:** handle ignored error returns properly ([15d48a8](https://github.com/SchoolyB/EZ/commit/15d48a8528bd282fa4c896414d27c4b43dff3442))
* **typechecker:** handle ignored error returns properly ([5f18740](https://github.com/SchoolyB/EZ/commit/5f18740fd6a1c658e0bd4692106869db12712590)), closes [#752](https://github.com/SchoolyB/EZ/issues/752)

## [0.29.0](https://github.com/SchoolyB/EZ/compare/v0.28.3...v0.29.0) (2025-12-19)


### Features

* Struct field tags JSON customization ([ae64d0a](https://github.com/SchoolyB/EZ/commit/ae64d0aeb925d572b57c9a5898ed2753bcec0866))

## [0.28.3](https://github.com/SchoolyB/EZ/compare/v0.28.2...v0.28.3) (2025-12-19)


### Bug Fixes

* module symbol sharing and using statement resolution ([94eea8d](https://github.com/SchoolyB/EZ/commit/94eea8dac49e6c60a121d22a0d3f1316d7d231f0))
* module symbol sharing and using statement resolution ([2b7a0d0](https://github.com/SchoolyB/EZ/commit/2b7a0d021271e4a71f027b7aac3d86279f5eab91))

## [0.28.2](https://github.com/SchoolyB/EZ/compare/v0.28.1...v0.28.2) (2025-12-19)


### Bug Fixes

* show parser errors in imported modules instead of misleading undefined function errors ([e515f2c](https://github.com/SchoolyB/EZ/commit/e515f2c8bdc67bc84aef2a050500d83e3cb317d3))
* show parser errors in imported modules instead of misleading undefined function errors ([2592b1f](https://github.com/SchoolyB/EZ/commit/2592b1f6a51464f5aada3498a57c56db2c540172)), closes [#726](https://github.com/SchoolyB/EZ/issues/726)

## [0.28.1](https://github.com/SchoolyB/EZ/compare/v0.28.0...v0.28.1) (2025-12-19)


### Bug Fixes

* sort type checker errors by file and line number ([9ca37f6](https://github.com/SchoolyB/EZ/commit/9ca37f661b4d52121f42c27ddd6c14ac87b37f2a))
* sort type checker errors by file and line number ([c8e7f6b](https://github.com/SchoolyB/EZ/commit/c8e7f6bc67e2b119beadd6f37f2b90dffa4fa456)), closes [#727](https://github.com/SchoolyB/EZ/issues/727)

## [0.28.0](https://github.com/SchoolyB/EZ/compare/v0.27.0...v0.28.0) (2025-12-18)


### Features

* **lang:** add `cast()` keyword for type conversion ([0cc34f2](https://github.com/SchoolyB/EZ/commit/0cc34f24c469efaac6602765958c356eff36a4d7))
* **lang:** add cast() keyword for type conversion ([ab814f8](https://github.com/SchoolyB/EZ/commit/ab814f8ca127598625d37c0a1092ed9ef4a1054f)), closes [#717](https://github.com/SchoolyB/EZ/issues/717)
* **stdlib:** add [@binary](https://github.com/binary) module and sized type conversions ([496f874](https://github.com/SchoolyB/EZ/commit/496f87433c5f5f6a84f80af4b39f66194f309a6b))
* **stdlib:** add [@binary](https://github.com/binary) module and sized type conversions ([9487a65](https://github.com/SchoolyB/EZ/commit/9487a65fd48c8e9bb6a8ebcf9440fae5e3d14625)), closes [#716](https://github.com/SchoolyB/EZ/issues/716)


### Bug Fixes

* **modules:** add type checking for multi-file modules ([#722](https://github.com/SchoolyB/EZ/issues/722)) ([19b5006](https://github.com/SchoolyB/EZ/commit/19b50061bcc9054d653d94bc41e63e2d0cacaeda))
* **modules:** add type checking for multi-file modules ([#722](https://github.com/SchoolyB/EZ/issues/722)) ([d806280](https://github.com/SchoolyB/EZ/commit/d8062804910fe308d4a8886ac481b9bf45798009))
* **modules:** report type errors in single-file modules at check time ([#720](https://github.com/SchoolyB/EZ/issues/720)) ([72ec320](https://github.com/SchoolyB/EZ/commit/72ec320f48bc5ec1cb9b13fd0ab88a8f08de0e15))
* **modules:** report type errors in single-file modules at check time ([#720](https://github.com/SchoolyB/EZ/issues/720)) ([efcf797](https://github.com/SchoolyB/EZ/commit/efcf79757eb22c792cce539211c5a842751d756d))

## [0.27.0](https://github.com/SchoolyB/EZ/compare/v0.26.0...v0.27.0) (2025-12-18)


### Features

* **typechecker:** add type checking for all stdlib modules ([8842961](https://github.com/SchoolyB/EZ/commit/88429612f75394597e66db37d99180ea9e90d76d))
* **typechecker:** add type checking for all stdlib modules ([fc1f4e8](https://github.com/SchoolyB/EZ/commit/fc1f4e8364aeef6e43a2451629d010d542224dd6))


### Bug Fixes

* **typechecker:** comprehensive validation of imported types ([#709](https://github.com/SchoolyB/EZ/issues/709)) ([0aedff9](https://github.com/SchoolyB/EZ/commit/0aedff9e65b5d0919574f48bdb0dd5c650784f7e))
* **typechecker:** comprehensive validation of imported types ([#709](https://github.com/SchoolyB/EZ/issues/709)) ([88c32a7](https://github.com/SchoolyB/EZ/commit/88c32a7254e89a52e0a891eb7683266efb30a683))
* **typechecker:** for_each loop variables inherit mutability from collection ([fc7dd06](https://github.com/SchoolyB/EZ/commit/fc7dd0659c3cc63a864aa17c66a701157aa2520e))
* **typechecker:** resolve unqualified imported types in member access ([#706](https://github.com/SchoolyB/EZ/issues/706)) ([72f8bfc](https://github.com/SchoolyB/EZ/commit/72f8bfcde68df0de3bcf796cff8202d7c7b54c10))
* **typechecker:** resolve unqualified imported types in member access ([#706](https://github.com/SchoolyB/EZ/issues/706)) ([e6935f8](https://github.com/SchoolyB/EZ/commit/e6935f80b45949215101fa0300f2c367c940a87c))
* **typechecker:** validate field names in imported struct literals ([#708](https://github.com/SchoolyB/EZ/issues/708)) ([9ecc16b](https://github.com/SchoolyB/EZ/commit/9ecc16bb1d104ca2b00a3f3ed82c2860bb07b545))
* **typechecker:** validate field names in imported struct literals ([#708](https://github.com/SchoolyB/EZ/issues/708)) ([a14162b](https://github.com/SchoolyB/EZ/commit/a14162b9419700bace7d5c373fe47ad4faad01dd))

## [0.26.0](https://github.com/SchoolyB/EZ/compare/v0.25.0...v0.26.0) (2025-12-18)


### Features

* **parser:** support tuple unpacking in assignment statements ([#699](https://github.com/SchoolyB/EZ/issues/699)) ([#704](https://github.com/SchoolyB/EZ/issues/704)) ([1c01533](https://github.com/SchoolyB/EZ/commit/1c015336253968ce0d1655580103116de6aff52c))

## [0.25.0](https://github.com/SchoolyB/EZ/compare/v0.24.0...v0.25.0) (2025-12-18)


### Features

* multi-value handling improvements ([5b533d1](https://github.com/SchoolyB/EZ/commit/5b533d1a40ea3e3f3477f9a0805acd3a17113d39))


### Bug Fixes

* **interpreter:** display actual return type instead of RETURN_VALUE in E5012 errors ([e702d39](https://github.com/SchoolyB/EZ/commit/e702d396e2f17db6c6e9025fd5a473f4b7ba8729))
* **interpreter:** display actual return type instead of RETURN_VALUE in E5012 errors ([#696](https://github.com/SchoolyB/EZ/issues/696)) ([c84928e](https://github.com/SchoolyB/EZ/commit/c84928e2a3b632833738fdf98cf257322de2c8d5))
* **interpreter:** handle multi-value assignment errors correctly ([2f08c12](https://github.com/SchoolyB/EZ/commit/2f08c12ca9ca0c4852a249964fac49a917337b7e))
* **interpreter:** handle multi-value assignment errors correctly ([#698](https://github.com/SchoolyB/EZ/issues/698)) ([6270ad2](https://github.com/SchoolyB/EZ/commit/6270ad2a1d7786d0522f58c0a4371dee2a501aee))

## [0.24.0](https://github.com/SchoolyB/EZ/compare/v0.23.1...v0.24.0) (2025-12-17)


### Features

* add W2010 warning for chained member access on nullable structs ([#689](https://github.com/SchoolyB/EZ/issues/689)) ([cd57753](https://github.com/SchoolyB/EZ/commit/cd57753bd3858e2e8b33ca230661c9c23b416934))
* add W2010 warning for chained member access on nullable structs ([#689](https://github.com/SchoolyB/EZ/issues/689)) ([3cd1ede](https://github.com/SchoolyB/EZ/commit/3cd1ede848574aa97f0b697905ffce283a7d713b))
* implement ref() builtin and copy-by-default semantics ([#661](https://github.com/SchoolyB/EZ/issues/661)) ([743153c](https://github.com/SchoolyB/EZ/commit/743153cee33eb181622433e489dc0fa94d62a701))
* implement ref() builtin and copy-by-default semantics ([#661](https://github.com/SchoolyB/EZ/issues/661)) ([d064e56](https://github.com/SchoolyB/EZ/commit/d064e56e894206ba2a2074a03f75bca15949de14))
* **typechecker:** comprehensive typechecker overhaul with ref() builtin ([e05a459](https://github.com/SchoolyB/EZ/commit/e05a4591c8be5a5a0f99eb04fe833fd3542e7021))
* **typechecker:** detect literal division/modulo by zero ([8ce1a11](https://github.com/SchoolyB/EZ/commit/8ce1a1124f8de946cd6f283b427eb242a0e97faf))
* **typechecker:** detect literal division/modulo by zero ([#667](https://github.com/SchoolyB/EZ/issues/667)) ([76b4f1e](https://github.com/SchoolyB/EZ/commit/76b4f1e2da6d955787e394fb1fab07eb872b8e0a))
* **typechecker:** detect undefined variables and functions at check time ([#663](https://github.com/SchoolyB/EZ/issues/663)) ([4ca2c68](https://github.com/SchoolyB/EZ/commit/4ca2c68a56b82fc2917f4121373ff862bf715fdd))
* **typechecker:** detect undefined variables and functions at check time ([#663](https://github.com/SchoolyB/EZ/issues/663)) ([5334c4b](https://github.com/SchoolyB/EZ/commit/5334c4bb39363c1ec27994991d01ebc4792f6591))
* **typechecker:** reject executable statements at file scope ([c2b372f](https://github.com/SchoolyB/EZ/commit/c2b372f0ec6ecbd0868a8a75b81d8fa2561abe1e))
* **typechecker:** reject executable statements at file scope ([#662](https://github.com/SchoolyB/EZ/issues/662)) ([9ec3cd0](https://github.com/SchoolyB/EZ/commit/9ec3cd0c4155840dc1fa21f17a838c10fe470ce7))
* **typechecker:** validate integer literal ranges for sized types ([c315995](https://github.com/SchoolyB/EZ/commit/c3159952bd246079c6671e0883b2c4ae8867b03b))
* **typechecker:** validate integer literal ranges for sized types ([#666](https://github.com/SchoolyB/EZ/issues/666)) ([0d7cdfe](https://github.com/SchoolyB/EZ/commit/0d7cdfea655141e0a31359f17e66f95418072b91))


### Bug Fixes

* add type inference for stdlib module multi-return functions ([c2be2c1](https://github.com/SchoolyB/EZ/commit/c2be2c14d2cc8040e4368a7e94a912c16464782d))
* add type inference for stdlib module multi-return functions ([f5120f8](https://github.com/SchoolyB/EZ/commit/f5120f8c5594e64fb0856dd4bd01bb3d1c4ed1cb))
* detect missing return on all code paths at check time ([860be2a](https://github.com/SchoolyB/EZ/commit/860be2a8843fd5f6a1a7821f64241b41e43f4b64))
* detect missing return on all code paths at check time ([81667b9](https://github.com/SchoolyB/EZ/commit/81667b9707f2cad25c5802da7be9a74cd15d26d1)), closes [#660](https://github.com/SchoolyB/EZ/issues/660)
* properly type multi-return builtin functions like read_int() ([70519bc](https://github.com/SchoolyB/EZ/commit/70519bc5717aedd6cfc1135bc25790c0feb354b1))
* properly type multi-return builtin functions like read_int() ([a88577e](https://github.com/SchoolyB/EZ/commit/a88577e2cac2e8233df18b25345b53fdc3f9a4b2))
* support module constants via using directive ([#677](https://github.com/SchoolyB/EZ/issues/677)) ([a493e46](https://github.com/SchoolyB/EZ/commit/a493e461198baad719b48c9fe3109dfad183c23d))
* support module constants via using directive ([#677](https://github.com/SchoolyB/EZ/issues/677)) ([70dbfc3](https://github.com/SchoolyB/EZ/commit/70dbfc362b9948cd10efc08fdcc567b59aa20819))
* **typechecker:** add check-time array bounds checking ([#685](https://github.com/SchoolyB/EZ/issues/685)) ([9fb99de](https://github.com/SchoolyB/EZ/commit/9fb99ded85c0e18e6539deccd5cea722e603e3fb))
* **typechecker:** add check-time array bounds checking ([#685](https://github.com/SchoolyB/EZ/issues/685)) ([c150246](https://github.com/SchoolyB/EZ/commit/c1502462ea431098d9c6afee0e52dfc5b8a51f26))
* **typechecker:** add warning for member access on error type ([#687](https://github.com/SchoolyB/EZ/issues/687)) ([052ead9](https://github.com/SchoolyB/EZ/commit/052ead9c9159f73d485600ed4cb6e4368800e126))
* **typechecker:** add warning for member access on error type ([#687](https://github.com/SchoolyB/EZ/issues/687)) ([189a14c](https://github.com/SchoolyB/EZ/commit/189a14c8fb669fb89bb4952ce83eb02e3b153779))
* **typechecker:** detect undefined variables in assignment targets ([f467bcf](https://github.com/SchoolyB/EZ/commit/f467bcfcf834b3f0c89506decd42d3532b7aa9df))
* **typechecker:** detect undefined variables in assignment targets ([#665](https://github.com/SchoolyB/EZ/issues/665)) ([24cf9b1](https://github.com/SchoolyB/EZ/commit/24cf9b11b46ede1acf6e8e3f090300da9e377546))
* **typechecker:** extend overflow detection to all integer types ([#686](https://github.com/SchoolyB/EZ/issues/686)) ([c56aa3d](https://github.com/SchoolyB/EZ/commit/c56aa3d22302f93c5a3f8e60a17f382a94e20f36))
* **typechecker:** extend overflow detection to all integer types ([#686](https://github.com/SchoolyB/EZ/issues/686)) ([6e0b31a](https://github.com/SchoolyB/EZ/commit/6e0b31a1f5d1bbf3a332e53324391a9da3e4f84c))
* **typechecker:** infer types for module variables via using directive ([#677](https://github.com/SchoolyB/EZ/issues/677)) ([584d42e](https://github.com/SchoolyB/EZ/commit/584d42eedafcd97fefb2c731ee7108d2d3c3ea4b))
* **typechecker:** resolve user module functions via 'using' directive ([b90830e](https://github.com/SchoolyB/EZ/commit/b90830e4bea45b86cabe3aef4d0d021bad617729))
* **typechecker:** resolve user module functions via 'using' directive ([#671](https://github.com/SchoolyB/EZ/issues/671)) ([1285737](https://github.com/SchoolyB/EZ/commit/1285737733f4f2c9ae5bcfec0cee70fe0ec9a864))
* **typechecker:** validate expressions in string interpolations ([#684](https://github.com/SchoolyB/EZ/issues/684)) ([a4d8990](https://github.com/SchoolyB/EZ/commit/a4d8990b53e5a689bc06f40815265f0230518a5d))
* **typechecker:** validate expressions in string interpolations ([#684](https://github.com/SchoolyB/EZ/issues/684)) ([4da234d](https://github.com/SchoolyB/EZ/commit/4da234de0f6214635e766ae6da8dfd4a3e351d05))

## [0.23.1](https://github.com/SchoolyB/EZ/compare/v0.23.0...v0.23.1) (2025-12-17)


### Bug Fixes

* allow nil return for error type in user-defined functions ([4d78d05](https://github.com/SchoolyB/EZ/commit/4d78d050ba9d314303f239c6f8f96e520bd55a51))
* allow nil return for error type in user-defined functions ([f0fd24a](https://github.com/SchoolyB/EZ/commit/f0fd24a8bd821d782427ba30dfd42fc69ec129b3)), closes [#657](https://github.com/SchoolyB/EZ/issues/657)

## [0.23.0](https://github.com/SchoolyB/EZ/compare/v0.22.6...v0.23.0) (2025-12-17)


### Features

* **stdlib:** add `strings.to_int()`, `strings.to_float()`, `strings.to_bool()` ([846f90a](https://github.com/SchoolyB/EZ/commit/846f90af5f781c96bc13e983d4e2e183e30d6dd4))
* **stdlib:** add strings.to_int(), strings.to_float(), strings.to_bool() ([e496f23](https://github.com/SchoolyB/EZ/commit/e496f231bafb51979f7127a7c8a26516df365afb)), closes [#651](https://github.com/SchoolyB/EZ/issues/651)

## [0.22.6](https://github.com/SchoolyB/EZ/compare/v0.22.5...v0.22.6) (2025-12-16)


### Bug Fixes

* **stdlib:** `arrays.set()` now modifies array in place ([411c17f](https://github.com/SchoolyB/EZ/commit/411c17f085cb3ffe342a62d46bd318e5c898680c))
* **stdlib:** arrays.set() now modifies array in-place ([2cdb73a](https://github.com/SchoolyB/EZ/commit/2cdb73a58be0a24dd92f41b26f9ba655cdbbb8fd)), closes [#652](https://github.com/SchoolyB/EZ/issues/652)

## [0.22.5](https://github.com/SchoolyB/EZ/compare/v0.22.4...v0.22.5) (2025-12-16)


### Bug Fixes

* cross-module nested struct initialization ([a22d7e3](https://github.com/SchoolyB/EZ/commit/a22d7e36822d957f741a578ee1f5df694480e503))
* cross-module nested struct initialization ([13cf3e0](https://github.com/SchoolyB/EZ/commit/13cf3e0d46325920a59696df767a736cc473f5c5))

## [0.22.4](https://github.com/SchoolyB/EZ/compare/v0.22.3...v0.22.4) (2025-12-16)


### Bug Fixes

* workflow rebase, add README badges, improve test coverage ([d356bce](https://github.com/SchoolyB/EZ/commit/d356bcef43c84c48ddabcf09e501dd50c82fffa8))

## [0.22.3](https://github.com/SchoolyB/EZ/compare/v0.22.2...v0.22.3) (2025-12-16)


### Bug Fixes

* version update detection and error sync workflow ([fbdb7bd](https://github.com/SchoolyB/EZ/commit/fbdb7bd421d6419452fa901940d5c6a950cacf2e))
* version update detection and error sync workflow ([f773ddb](https://github.com/SchoolyB/EZ/commit/f773ddb0af517295dadf3dc3f747ebe26970b53d))

## [0.22.2](https://github.com/SchoolyB/EZ/compare/v0.22.1...v0.22.2) (2025-12-16)


### Bug Fixes

* [@strict](https://github.com/strict) when enforces enum exhaustiveness at check time ([#629](https://github.com/SchoolyB/EZ/issues/629)) ([f549a3b](https://github.com/SchoolyB/EZ/commit/f549a3bd63c013f13cdb3bb4987f061ff794a0d1))
* [@strict](https://github.com/strict) when rejects non-enum expressions ([#628](https://github.com/SchoolyB/EZ/issues/628)) ([c734b18](https://github.com/SchoolyB/EZ/commit/c734b1845b109643f8aa1c5d7acb91fab7e3c232))
* [@strict](https://github.com/strict) when rejects non-enum expressions in cases ([#628](https://github.com/SchoolyB/EZ/issues/628)) ([799b7a7](https://github.com/SchoolyB/EZ/commit/799b7a7fd3035eae677fb9388d25eef06d09fa1a))
* `[@strict](https://github.com/strict)` when enforces enum exhaustiveness at check time ([#629](https://github.com/SchoolyB/EZ/issues/629)) ([be87d59](https://github.com/SchoolyB/EZ/commit/be87d591d8c4357f87f6700196b107976df19e91))
* duplicate map keys in literal now produce check-time error ([#641](https://github.com/SchoolyB/EZ/issues/641)) ([06b3f91](https://github.com/SchoolyB/EZ/commit/06b3f91df8e8add8e7a020a50d9bd60cea691482))
* duplicate map keys in literal now produce check-time error ([#641](https://github.com/SchoolyB/EZ/issues/641)) ([f011903](https://github.com/SchoolyB/EZ/commit/f0119037c23adb2d7ebd610b347bf10453d7bb88))
* float display now shows actual precision for debugging ([#640](https://github.com/SchoolyB/EZ/issues/640)) ([d2051fb](https://github.com/SchoolyB/EZ/commit/d2051fbce7ab28af8e3016c527766ec0b75bc186))
* float display now shows actual precision for debugging ([#640](https://github.com/SchoolyB/EZ/issues/640)) ([3ef9314](https://github.com/SchoolyB/EZ/commit/3ef93147f3104321000e65e39ebdaf7f55b7318d))
* recursively initialize nested struct fields ([#621](https://github.com/SchoolyB/EZ/issues/621)) ([ad4f911](https://github.com/SchoolyB/EZ/commit/ad4f911fbd6308544fcf67970280839baad41089))
* recursively initialize nested struct fields ([#621](https://github.com/SchoolyB/EZ/issues/621)) ([3b7c653](https://github.com/SchoolyB/EZ/commit/3b7c65385280bd48f58b505b42f6c0850e863cce))
* validate type assignments for imported module struct fields ([#620](https://github.com/SchoolyB/EZ/issues/620)) ([194d74f](https://github.com/SchoolyB/EZ/commit/194d74fa917c8690bb0baf68392447d1b6cf8089))
* validate type assignments for imported module struct fields ([#620](https://github.com/SchoolyB/EZ/issues/620)) ([a965fb3](https://github.com/SchoolyB/EZ/commit/a965fb3a239bf82385c1752494ba844fa33b5b80))

## [0.22.1](https://github.com/SchoolyB/EZ/compare/v0.22.0...v0.22.1) (2025-12-15)


### Bug Fixes

* detect variable shadowing of functions from used modules ([db505b4](https://github.com/SchoolyB/EZ/commit/db505b40bd022a64e31752f08f1b59ad02e9bfe2))
* detect variable shadowing of functions from used modules ([#616](https://github.com/SchoolyB/EZ/issues/616)) ([cf40e75](https://github.com/SchoolyB/EZ/commit/cf40e75abffaee9d9a9553bc652014bda149f07e))

## [0.22.0](https://github.com/SchoolyB/EZ/compare/v0.21.2...v0.22.0) (2025-12-15)


### Features

* add `@JSON` module, raw strings, and restrict `any` type ([8e566e5](https://github.com/SchoolyB/EZ/commit/8e566e5c73e9cd14c595f7b63dbed93104745f1f))
* add JSON module, raw strings, and restrict 'any' type ([f66b734](https://github.com/SchoolyB/EZ/commit/f66b734823d38dc9c40312044e25905cda096374))

## [0.21.2](https://github.com/SchoolyB/EZ/compare/v0.21.1...v0.21.2) (2025-12-15)


### Bug Fixes

* December 15, 2025 bug fixes ([#580](https://github.com/SchoolyB/EZ/issues/580)) ([dd6253c](https://github.com/SchoolyB/EZ/commit/dd6253c1a8ccdeed95386b5c2a2418d7b8be0aad))

## [0.21.1](https://github.com/SchoolyB/EZ/compare/v0.21.0...v0.21.1) (2025-12-14)


### Bug Fixes

* December 14, 2025 bug fixes ([#544](https://github.com/SchoolyB/EZ/issues/544)) ([3169059](https://github.com/SchoolyB/EZ/commit/31690596cf470115b4c4269d1ac6d24519c15296))

## [0.21.0](https://github.com/SchoolyB/EZ/compare/v0.20.1...v0.21.0) (2025-12-12)


### Features

* global builtins, [@std](https://github.com/std) expansion, and bug fixes ([#525](https://github.com/SchoolyB/EZ/issues/525), [#526](https://github.com/SchoolyB/EZ/issues/526), [#522](https://github.com/SchoolyB/EZ/issues/522), [#523](https://github.com/SchoolyB/EZ/issues/523), [#524](https://github.com/SchoolyB/EZ/issues/524), [#527](https://github.com/SchoolyB/EZ/issues/527)) ([#532](https://github.com/SchoolyB/EZ/issues/532)) ([accd080](https://github.com/SchoolyB/EZ/commit/accd0803e711b1327d2f4b0ee6c334f04c600846))

## [0.20.1](https://github.com/SchoolyB/EZ/compare/v0.20.0...v0.20.1) (2025-12-12)


### Bug Fixes

* december 12 patch release ([#520](https://github.com/SchoolyB/EZ/issues/520)) ([9e75aa0](https://github.com/SchoolyB/EZ/commit/9e75aa07bb6687d01ce471efa5bbaafd11f9c5c3))

## [0.20.0](https://github.com/SchoolyB/EZ/compare/v0.19.3...v0.20.0) (2025-12-11)


### Features

* allow range() in if/in expressions and when/is statements ([#501](https://github.com/SchoolyB/EZ/issues/501)) ([#505](https://github.com/SchoolyB/EZ/issues/505)) ([de2890f](https://github.com/SchoolyB/EZ/commit/de2890f16bcd738adca6de3803d93c8e1dabddef))

## [0.19.3](https://github.com/SchoolyB/EZ/compare/v0.19.2...v0.19.3) (2025-12-11)


### Bug Fixes

* remove [@string](https://github.com/string) alias and add module suggestion hints ([#502](https://github.com/SchoolyB/EZ/issues/502)) ([#503](https://github.com/SchoolyB/EZ/issues/503)) ([53122a1](https://github.com/SchoolyB/EZ/commit/53122a1350104efae0bed292167851a34e04d4a6))

## [0.19.2](https://github.com/SchoolyB/EZ/compare/v0.19.1...v0.19.2) (2025-12-11)


### Bug Fixes

* show update notification immediately on all commands ([#499](https://github.com/SchoolyB/EZ/issues/499)) ([c436b2f](https://github.com/SchoolyB/EZ/commit/c436b2fe91d723317f54c5021499f45f94934ac7)), closes [#498](https://github.com/SchoolyB/EZ/issues/498)

## [0.19.1](https://github.com/SchoolyB/EZ/compare/v0.19.0...v0.19.1) (2025-12-11)


### Bug Fixes

* add path validation to archive extraction functions ([#487](https://github.com/SchoolyB/EZ/issues/487)) ([#495](https://github.com/SchoolyB/EZ/issues/495)) ([974250a](https://github.com/SchoolyB/EZ/commit/974250a0f45c8cbee29039c8b5e8ed002283f736))

## [0.19.0](https://github.com/SchoolyB/EZ/compare/v0.18.1...v0.19.0) (2025-12-11)


### Features

* add when/is switch statements ([#179](https://github.com/SchoolyB/EZ/issues/179)) ([#491](https://github.com/SchoolyB/EZ/issues/491)) ([4f0c068](https://github.com/SchoolyB/EZ/commit/4f0c0689c8efcf3b2dbf8bd08764fb5b0fa510eb))

## [0.18.1](https://github.com/SchoolyB/EZ/compare/v0.18.0...v0.18.1) (2025-12-11)


### Bug Fixes

* correct error codes for immutable variable/map errors ([#488](https://github.com/SchoolyB/EZ/issues/488)) ([727ce1b](https://github.com/SchoolyB/EZ/commit/727ce1bce576f0628aef67c1e7dd500a4c24e7d1))

## [0.18.0](https://github.com/SchoolyB/EZ/compare/v0.17.2...v0.18.0) (2025-12-10)


### Features

* add default parameter values for functions ([#312](https://github.com/SchoolyB/EZ/issues/312)) ([#485](https://github.com/SchoolyB/EZ/issues/485)) ([933ca6f](https://github.com/SchoolyB/EZ/commit/933ca6fe278654cfea9a1172e4a3eb0419f05f58))

## [0.17.2](https://github.com/SchoolyB/EZ/compare/v0.17.1...v0.17.2) (2025-12-10)


### Bug Fixes

* auto-elevate to sudo when update needs root permissions ([#483](https://github.com/SchoolyB/EZ/issues/483)) ([8cfa5c3](https://github.com/SchoolyB/EZ/commit/8cfa5c3d2814d8ec2fa7c2593ab405829e80bdb9))

## [0.17.1](https://github.com/SchoolyB/EZ/compare/v0.17.0...v0.17.1) (2025-12-10)


### Bug Fixes

* handle .tar.gz/.zip archives in ez update ([#481](https://github.com/SchoolyB/EZ/issues/481)) ([b7d4314](https://github.com/SchoolyB/EZ/commit/b7d43143472a1436037fa79e63efb396e4e3126f))

## [0.17.0](https://github.com/SchoolyB/EZ/compare/v0.16.15...v0.17.0) (2025-12-10)


### Features

* add update checker and `ez update` command ([#478](https://github.com/SchoolyB/EZ/issues/478)) ([#479](https://github.com/SchoolyB/EZ/issues/479)) ([a33c92b](https://github.com/SchoolyB/EZ/commit/a33c92be20dc111bf0d8a1529b7a06b7f3b7522b))

## [0.16.15](https://github.com/SchoolyB/EZ/compare/v0.16.14...v0.16.15) (2025-12-10)


### Bug Fixes

* show correct file location for multi-file module errors ([#466](https://github.com/SchoolyB/EZ/issues/466)) ([#476](https://github.com/SchoolyB/EZ/issues/476)) ([b187e1a](https://github.com/SchoolyB/EZ/commit/b187e1a6b8e98347acc541775eb27d6d59c98869))

## [0.16.14](https://github.com/SchoolyB/EZ/compare/v0.16.13...v0.16.14) (2025-12-10)


### Bug Fixes

* show file location in module not found error ([#461](https://github.com/SchoolyB/EZ/issues/461)) ([#474](https://github.com/SchoolyB/EZ/issues/474)) ([a6eec23](https://github.com/SchoolyB/EZ/commit/a6eec2353111ce07adfca2733e93d58afe352c3d))

## [0.16.13](https://github.com/SchoolyB/EZ/compare/v0.16.12...v0.16.13) (2025-12-10)


### Bug Fixes

* suppress module name warning for files in matching directories ([#464](https://github.com/SchoolyB/EZ/issues/464)) ([#472](https://github.com/SchoolyB/EZ/issues/472)) ([6be1062](https://github.com/SchoolyB/EZ/commit/6be106280f0cd375a9a93dac8197b6ab4fe34c2e))

## [0.16.12](https://github.com/SchoolyB/EZ/compare/v0.16.11...v0.16.12) (2025-12-10)


### Bug Fixes

* handle module-prefixed types in type compatibility checks ([#463](https://github.com/SchoolyB/EZ/issues/463)) ([#469](https://github.com/SchoolyB/EZ/issues/469)) ([3c597de](https://github.com/SchoolyB/EZ/commit/3c597dec48fafc0a2cdb9c2fadee796f04bb73ac))

## [0.16.11](https://github.com/SchoolyB/EZ/compare/v0.16.10...v0.16.11) (2025-12-10)


### Bug Fixes

* resolve uppercase constants being parsed as struct literals ([#462](https://github.com/SchoolyB/EZ/issues/462)) ([a406de3](https://github.com/SchoolyB/EZ/commit/a406de3d496cf89c4f405182d5392b16405c6fc4))

## [0.16.10](https://github.com/SchoolyB/EZ/compare/v0.16.9...v0.16.10) (2025-12-09)


### Bug Fixes

* bug fixes batch - parser, stdlib, interpreter, and enum map keys ([#454](https://github.com/SchoolyB/EZ/issues/454)) ([f90e10f](https://github.com/SchoolyB/EZ/commit/f90e10fd02b3946ac37341956b453db744198017)), closes [#452](https://github.com/SchoolyB/EZ/issues/452)

## [0.16.9](https://github.com/SchoolyB/EZ/compare/v0.16.8...v0.16.9) (2025-12-08)


### Bug Fixes

* implement arbitrary precision integers for i128/u128 support ([#437](https://github.com/SchoolyB/EZ/issues/437)) ([#440](https://github.com/SchoolyB/EZ/issues/440)) ([af4cdbf](https://github.com/SchoolyB/EZ/commit/af4cdbfd42edcd87d5982032d06cc913cc01e75f))

## [0.16.8](https://github.com/SchoolyB/EZ/compare/v0.16.7...v0.16.8) (2025-12-08)


### Bug Fixes

* integration test overhaul and bug fixes ([#435](https://github.com/SchoolyB/EZ/issues/435)) ([a4ad4e8](https://github.com/SchoolyB/EZ/commit/a4ad4e8549490a5cf03b8d54bd9b48511a3af264))

## [0.16.7](https://github.com/SchoolyB/EZ/compare/v0.16.6...v0.16.7) (2025-12-08)


### Bug Fixes

* detect integer overflow in negation and division ([#424](https://github.com/SchoolyB/EZ/issues/424)) ([e83bdfe](https://github.com/SchoolyB/EZ/commit/e83bdfef81ab73cad1d2b5d9062b1a8be071f2d8))

## [0.16.6](https://github.com/SchoolyB/EZ/compare/v0.16.5...v0.16.6) (2025-12-08)


### Bug Fixes

* stdlib functions preserve input type ([#422](https://github.com/SchoolyB/EZ/issues/422)) ([1e91801](https://github.com/SchoolyB/EZ/commit/1e91801ff35219dbdee45e87ae6a4af4be9453eb)), closes [#404](https://github.com/SchoolyB/EZ/issues/404)

## [0.16.5](https://github.com/SchoolyB/EZ/compare/v0.16.4...v0.16.5) (2025-12-08)


### Bug Fixes

* float division by zero returns INF per IEEE 754 ([#420](https://github.com/SchoolyB/EZ/issues/420)) ([744cf75](https://github.com/SchoolyB/EZ/commit/744cf75b2f9188103cfd4e4734900b364c3a4781)), closes [#402](https://github.com/SchoolyB/EZ/issues/402)

## [0.16.4](https://github.com/SchoolyB/EZ/compare/v0.16.3...v0.16.4) (2025-12-08)


### Bug Fixes

* allow byte to int conversion with int() ([#418](https://github.com/SchoolyB/EZ/issues/418)) ([33dd463](https://github.com/SchoolyB/EZ/commit/33dd463d19ed16fe36f03fa3b9aba641c56edffb)), closes [#403](https://github.com/SchoolyB/EZ/issues/403)

## [0.16.3](https://github.com/SchoolyB/EZ/compare/v0.16.2...v0.16.3) (2025-12-08)


### Bug Fixes

* reject nil assignment to non-nullable types ([#416](https://github.com/SchoolyB/EZ/issues/416)) ([40e82e3](https://github.com/SchoolyB/EZ/commit/40e82e3f7addc2410ffa0a3ec062c4f5184a34f0)), closes [#407](https://github.com/SchoolyB/EZ/issues/407)

## [0.16.2](https://github.com/SchoolyB/EZ/compare/v0.16.1...v0.16.2) (2025-12-08)


### Bug Fixes

* detect mixed-type enums at check time ([#414](https://github.com/SchoolyB/EZ/issues/414)) ([e1b8c63](https://github.com/SchoolyB/EZ/commit/e1b8c63fd7c841d8d3aea2ad48b0d2d31483e6ca)), closes [#410](https://github.com/SchoolyB/EZ/issues/410)

## [0.16.1](https://github.com/SchoolyB/EZ/compare/v0.16.0...v0.16.1) (2025-12-08)


### Bug Fixes

* make array functions modify in-place and fix remove() API ([#412](https://github.com/SchoolyB/EZ/issues/412)) ([f6890b8](https://github.com/SchoolyB/EZ/commit/f6890b82553549702b3d52186843ca7c11e2860d))

## [0.16.0](https://github.com/SchoolyB/EZ/compare/v0.15.2...v0.16.0) (2025-12-07)


### Features

* add line editor for REPL with arrow key navigation and history ([#400](https://github.com/SchoolyB/EZ/issues/400)) ([bce7343](https://github.com/SchoolyB/EZ/commit/bce7343118564ec2f8b7b2e79b2eec3cd0b49711))

## [0.15.2](https://github.com/SchoolyB/EZ/compare/v0.15.1...v0.15.2) (2025-12-07)


### Bug Fixes

* resolve stdlib bugs for arrays, strings, and math modules ([#398](https://github.com/SchoolyB/EZ/issues/398)) ([b278ce6](https://github.com/SchoolyB/EZ/commit/b278ce6744cba9c3e8bf22237038f8cce398bda8))

## [0.15.1](https://github.com/SchoolyB/EZ/compare/v0.15.0...v0.15.1) (2025-12-07)


### Bug Fixes

* resolve several interpreter bugs ([5c083d3](https://github.com/SchoolyB/EZ/commit/5c083d33e3d0f18cef30049f3f57e184ce131d78))

## [0.15.0](https://github.com/SchoolyB/EZ/compare/v0.14.10...v0.15.0) (2025-12-07)


### Features

* replace [@ignore](https://github.com/ignore) with _ (underscore) blank identifier ([#376](https://github.com/SchoolyB/EZ/issues/376)) ([66111be](https://github.com/SchoolyB/EZ/commit/66111beaf80ca72dab092a9b0e62b1fa5e4da58a))

## [0.14.10](https://github.com/SchoolyB/EZ/compare/v0.14.9...v0.14.10) (2025-12-07)


### Bug Fixes

* resolve multiple bugs in stdlib, lexer, and parser ([#374](https://github.com/SchoolyB/EZ/issues/374)) ([c4be966](https://github.com/SchoolyB/EZ/commit/c4be9660ed9ced0f6a812d5a98ef5eea9389a813))

## [0.14.9](https://github.com/SchoolyB/EZ/compare/v0.14.8...v0.14.9) (2025-12-05)


### Bug Fixes

* **interpreter:** nested mutable parameter forwarding now works correctly ([#367](https://github.com/SchoolyB/EZ/issues/367)) ([5762939](https://github.com/SchoolyB/EZ/commit/57629390fb7bd4a9c53b55d9a259f853841de901)), closes [#338](https://github.com/SchoolyB/EZ/issues/338)

## [0.14.8](https://github.com/SchoolyB/EZ/compare/v0.14.7...v0.14.8) (2025-12-05)


### Bug Fixes

* **typechecker:** type errors now report correct source location ([#362](https://github.com/SchoolyB/EZ/issues/362)) ([13cdb4e](https://github.com/SchoolyB/EZ/commit/13cdb4ecd40f282c01749bf0f0ff65a22b6ea011))

## [0.14.7](https://github.com/SchoolyB/EZ/compare/v0.14.6...v0.14.7) (2025-12-05)


### Bug Fixes

* **parser:** handle minimum int64 literal (-9223372036854775808) ([#360](https://github.com/SchoolyB/EZ/issues/360)) ([8c75b43](https://github.com/SchoolyB/EZ/commit/8c75b434ff40f3e11066edbeb13a563e03a43b76))

## [0.14.6](https://github.com/SchoolyB/EZ/compare/v0.14.5...v0.14.6) (2025-12-05)


### Bug Fixes

* **parser:** enum member access in if condition no longer causes parser error ([#358](https://github.com/SchoolyB/EZ/issues/358)) ([6e0c5de](https://github.com/SchoolyB/EZ/commit/6e0c5de28bc3ba4e287bd1bbbb92ac5abd126fc8))

## [0.14.5](https://github.com/SchoolyB/EZ/compare/v0.14.4...v0.14.5) (2025-12-05)


### Bug Fixes

* **evaluator:** isTruthy now checks Boolean.Value instead of pointer identity ([#354](https://github.com/SchoolyB/EZ/issues/354)) ([18edd78](https://github.com/SchoolyB/EZ/commit/18edd78036eb4c57637303283729496f2d3c03b1))

## [0.14.4](https://github.com/SchoolyB/EZ/compare/v0.14.3...v0.14.4) (2025-12-05)


### Bug Fixes

* **ci:** auto-merge release PRs even when a release is created in same run ([eabee09](https://github.com/SchoolyB/EZ/commit/eabee09ea4800cc6618924636b5ed1511b2f25e8))
* **stdlib:** arrays.insert() and arrays.remove() now modify array in-place ([3ad569a](https://github.com/SchoolyB/EZ/commit/3ad569a9a0e3b3b39664335551d7d0e2cfbf89e2))
* **stdlib:** arrays.insert() and arrays.remove() now modify array in-place ([f26797d](https://github.com/SchoolyB/EZ/commit/f26797ddcd2a2be3ba23f06384b3b1e3ceb4330a))

## [0.14.3](https://github.com/SchoolyB/EZ/compare/v0.14.2...v0.14.3) (2025-12-05)


### Bug Fixes

* **evaluator:** implement short-circuit evaluation for && and || ([a9e4fe1](https://github.com/SchoolyB/EZ/commit/a9e4fe13877775af1a2f7859cc218484a31909b2))
* **evaluator:** implement short-circuit evaluation for && and || ([2f6f0a6](https://github.com/SchoolyB/EZ/commit/2f6f0a6bdc10484e58ade9b54c2a45ca4acd3a25))

## [0.14.2](https://github.com/SchoolyB/EZ/compare/v0.14.1...v0.14.2) (2025-12-05)


### Bug Fixes

* **typechecker:** handle array and map types in struct fields ([0455540](https://github.com/SchoolyB/EZ/commit/04555409b90e3bcc90825f2915348eecc84e6b40))
* **typechecker:** handle array and map types in struct fields ([0d1cd53](https://github.com/SchoolyB/EZ/commit/0d1cd53cd6d738c969c4beccfad56ba49f18ddd1))

## [0.14.1](https://github.com/SchoolyB/EZ/compare/v0.14.0...v0.14.1) (2025-12-04)


### Bug Fixes

* **interpreter:** add recursion depth limit to prevent stack overflow ([ccda7b7](https://github.com/SchoolyB/EZ/commit/ccda7b779bc8534b00f3e6a5f8396b4a7c574231)), closes [#327](https://github.com/SchoolyB/EZ/issues/327)
* **lexer:** preserve line numbers in string interpolation expressions ([5a236b7](https://github.com/SchoolyB/EZ/commit/5a236b72949fb37a54a4acdcb65cbe8069978f07)), closes [#323](https://github.com/SchoolyB/EZ/issues/323)
* **parser:** block reserved names as function parameter names ([1a52eb0](https://github.com/SchoolyB/EZ/commit/1a52eb00148ccf97b2e80684183d541335a12985))
* **parser:** disallow import statements inside blocks and after declarations ([19118f6](https://github.com/SchoolyB/EZ/commit/19118f6eddcecd4632c9f1d893fe78ab90251d51)), closes [#324](https://github.com/SchoolyB/EZ/issues/324)
* **parser:** prevent nil pointer crash when using 'const' as identifier ([e2177b3](https://github.com/SchoolyB/EZ/commit/e2177b389aed3acd022e8214387146609d1b6e17)), closes [#317](https://github.com/SchoolyB/EZ/issues/317)
* **parser:** use helpful error when keywords used as identifiers ([614d2bd](https://github.com/SchoolyB/EZ/commit/614d2bda79524de9348734bbaa42c9be2ca86f0b))
* **parser:** use helpful error when keywords used as identifiers ([5ffb6e3](https://github.com/SchoolyB/EZ/commit/5ffb6e393445ae14bf806bbaa1f3baa39174746a)), closes [#326](https://github.com/SchoolyB/EZ/issues/326)
* **parser:** use specific error codes for struct/enum reserved names ([c86b903](https://github.com/SchoolyB/EZ/commit/c86b9038ca1e85e46e77b93d90a8a1d56236ec4a))
* **parser:** validate enum value names against reserved keywords ([1a3f9b1](https://github.com/SchoolyB/EZ/commit/1a3f9b18df2107bd2806fdd9b525461ffd3f2c10)), closes [#322](https://github.com/SchoolyB/EZ/issues/322)
* **parser:** validate struct field names against reserved keywords ([a077535](https://github.com/SchoolyB/EZ/commit/a077535e905202e19dc34fd696f02e4bc51f59d9)), closes [#321](https://github.com/SchoolyB/EZ/issues/321)
* **typechecker:** block user-defined types/functions as parameter names ([2bea2dc](https://github.com/SchoolyB/EZ/commit/2bea2dc6377f246adb34601f1b0e0ab058b4868a))
* **typechecker:** enforce static typing for type-inferred variables ([e1a4437](https://github.com/SchoolyB/EZ/commit/e1a443768b3f0afce1aab733f868fa5ff83fedb7)), closes [#329](https://github.com/SchoolyB/EZ/issues/329)
* **typechecker:** make missing return statement an error instead of warning ([1338860](https://github.com/SchoolyB/EZ/commit/1338860d0ebabe392f05f3b311dc2d6981960a69)), closes [#318](https://github.com/SchoolyB/EZ/issues/318)
* **typechecker:** use correct error code for missing main function ([92b3463](https://github.com/SchoolyB/EZ/commit/92b3463ad86ecb87b809a23ae6ee3cefe66495c3)), closes [#328](https://github.com/SchoolyB/EZ/issues/328)
* **typechecker:** validate member access on non-struct types ([df0daee](https://github.com/SchoolyB/EZ/commit/df0daee12fe43410abce26e41b0811adaeac3936)), closes [#313](https://github.com/SchoolyB/EZ/issues/313)

## [0.14.0](https://github.com/SchoolyB/EZ/compare/v0.13.0...v0.14.0) (2025-12-04)


### Features

* **stdlib:** add [@random](https://github.com/random) module with comprehensive random functions ([0f2f981](https://github.com/SchoolyB/EZ/commit/0f2f981b0ef78b90e653190100d521f7b91d6a3f)), closes [#309](https://github.com/SchoolyB/EZ/issues/309)
* **stdlib:** add command execution functions to [@os](https://github.com/os) ([b8b2e04](https://github.com/SchoolyB/EZ/commit/b8b2e04475d8a2c31b751fa8a2cbe673d574deb2))
* **stdlib:** add filesystem utilities to [@io](https://github.com/io) ([bf0e035](https://github.com/SchoolyB/EZ/commit/bf0e0359f1c01236e73f706e497329e5506284c5)), closes [#287](https://github.com/SchoolyB/EZ/issues/287)
* **stdlib:** add math.log_base() for arbitrary base logarithms ([7b304f6](https://github.com/SchoolyB/EZ/commit/7b304f6fed3fd54247daaa74288a4f7eb22ef404))
* **stdlib:** add new string utility functions to [@strings](https://github.com/strings) ([7cce6fd](https://github.com/SchoolyB/EZ/commit/7cce6fd96551c8ebdc91c055a607ed439e35e7cf)), closes [#285](https://github.com/SchoolyB/EZ/issues/285)
* **stdlib:** expand standard library with new modules and functions ([401203e](https://github.com/SchoolyB/EZ/commit/401203eb730037dfc3579b810b18097d77617fc7))

## [0.13.0](https://github.com/SchoolyB/EZ/compare/v0.12.0...v0.13.0) (2025-12-04)


### Features

* add error() constructor for user-defined errors ([95a85fe](https://github.com/SchoolyB/EZ/commit/95a85fed06360177d671cb18dedd1bf55e465be2))
* add error() constructor for user-defined errors ([#292](https://github.com/SchoolyB/EZ/issues/292)) ([4f2bc5e](https://github.com/SchoolyB/EZ/commit/4f2bc5eed24b63d31d9b3bc76c44035260a74434))

## [0.12.0](https://github.com/SchoolyB/EZ/compare/v0.11.3...v0.12.0) (2025-12-04)


### Features

* allow type inference for const declarations ([279afc8](https://github.com/SchoolyB/EZ/commit/279afc80d91d072a3b35aa85372bd3ea9ae6c893))
* allow type inference for const declarations ([#302](https://github.com/SchoolyB/EZ/issues/302)) ([ff94be9](https://github.com/SchoolyB/EZ/commit/ff94be94f855368dc08f67efe0cee3d398a0b8df))

## [0.11.3](https://github.com/SchoolyB/EZ/compare/v0.11.2...v0.11.3) (2025-12-04)


### Bug Fixes

* struct mutability now respects temp/const declaration ([e91d7d9](https://github.com/SchoolyB/EZ/commit/e91d7d998ced22e2faaea8d4fcdd56d27d73d75c))
* struct mutability now respects temp/const declaration ([#298](https://github.com/SchoolyB/EZ/issues/298)) ([66be5c0](https://github.com/SchoolyB/EZ/commit/66be5c04da3c65a11abcf9bf62ddffbd2cd22cc8))

## [0.11.2](https://github.com/SchoolyB/EZ/compare/v0.11.1...v0.11.2) (2025-12-04)


### Bug Fixes

* CI REPL test failing on macOS ([42cb93c](https://github.com/SchoolyB/EZ/commit/42cb93cccd995d927606e05a26fdf02e145afba2))
* CI REPL test failing on macOS ([#299](https://github.com/SchoolyB/EZ/issues/299)) ([8b14f75](https://github.com/SchoolyB/EZ/commit/8b14f754baf770a2bca091fa9748fa342d8b7de7))

## [0.11.1](https://github.com/SchoolyB/EZ/compare/v0.11.0...v0.11.1) (2025-12-03)


### Bug Fixes

* allow optional parentheses in for loops ([#293](https://github.com/SchoolyB/EZ/issues/293)) ([d86b71c](https://github.com/SchoolyB/EZ/commit/d86b71cb48281f221f19bf0aa40e98e3f67ac9c2))
* allow optional parentheses in for loops ([#293](https://github.com/SchoolyB/EZ/issues/293)) ([16c968d](https://github.com/SchoolyB/EZ/commit/16c968d5b03fc08de82a4589207f8852b0ad8841))

## [0.11.0](https://github.com/SchoolyB/EZ/compare/v0.10.0...v0.11.0) (2025-12-03)


### Features

* implement char() type conversion function ([d7362bf](https://github.com/SchoolyB/EZ/commit/d7362bf42bb6539a9592e95dcf8710b3965d9aad))
* implement char() type conversion function ([#100](https://github.com/SchoolyB/EZ/issues/100)) ([e23c6d3](https://github.com/SchoolyB/EZ/commit/e23c6d35a67488fc42c4355f98136b9223e32e61))

## [0.10.0](https://github.com/SchoolyB/EZ/compare/v0.9.0...v0.10.0) (2025-12-03)


### Features

* remove arrays.copy() and maps.copy() in favor of copy() builtin ([#269](https://github.com/SchoolyB/EZ/issues/269)) ([aa2de1d](https://github.com/SchoolyB/EZ/commit/aa2de1dbe8304eaea2950aa60c2dd8d02e537acd))

## [0.9.0](https://github.com/SchoolyB/EZ/compare/v0.8.3...v0.9.0) (2025-12-03)


### Features

* add `copy()` builtin for explicit value semantics ([051a752](https://github.com/SchoolyB/EZ/commit/051a7529922b92768cb26ecf5bfc08cb4b04b3bd))
* add copy() builtin for explicit value semantics ([#265](https://github.com/SchoolyB/EZ/issues/265)) ([4681cdd](https://github.com/SchoolyB/EZ/commit/4681cddcd4009f7157fddfe53df9be013bcde503))

## [0.8.3](https://github.com/SchoolyB/EZ/compare/v0.8.2...v0.8.3) (2025-12-03)


### Bug Fixes

* fixed-size array indexing returns correct element type ([4c7211f](https://github.com/SchoolyB/EZ/commit/4c7211fe3edfb4ef4f3188cb9c5b65a12bb7a368))
* fixed-size array indexing returns correct element type ([#267](https://github.com/SchoolyB/EZ/issues/267)) ([92eeb28](https://github.com/SchoolyB/EZ/commit/92eeb2809e33d9a39eed3ad6f00cd6e43fa72634))
* Handle comma in array type when extracting element type in inferIndexType. ([92eeb28](https://github.com/SchoolyB/EZ/commit/92eeb2809e33d9a39eed3ad6f00cd6e43fa72634))

## [0.8.2](https://github.com/SchoolyB/EZ/compare/v0.8.1...v0.8.2) (2025-12-03)


### Bug Fixes

* prevent modification of const struct fields ([20074af](https://github.com/SchoolyB/EZ/commit/20074af830d6828088817465a6e5b1410e39cca2))
* prevent modification of const struct fields ([#266](https://github.com/SchoolyB/EZ/issues/266)) ([17b033a](https://github.com/SchoolyB/EZ/commit/17b033a466f6591c211b64bc86be051c96201fd5))

## [0.8.1](https://github.com/SchoolyB/EZ/compare/v0.8.0...v0.8.1) (2025-12-03)


### Bug Fixes

* remove deprecated lowercase math constants ([9b7b238](https://github.com/SchoolyB/EZ/commit/9b7b2384fe189c6b4f1d1dd941ac8b5d2f7ed155))
* remove deprecated lowercase math constants ([#260](https://github.com/SchoolyB/EZ/issues/260)) ([7e182fd](https://github.com/SchoolyB/EZ/commit/7e182fda260c9bd49a274939ac1656b1d3cafc41))

## [0.8.0](https://github.com/SchoolyB/EZ/compare/v0.7.1...v0.8.0) (2025-12-03)


### Features

* add mutable parameters with & syntax ([7fdeb24](https://github.com/SchoolyB/EZ/commit/7fdeb24241a643a050beaeceb8cd1b9f600be9ca))
* add mutable parameters with & syntax ([#268](https://github.com/SchoolyB/EZ/issues/268)) ([5075f6f](https://github.com/SchoolyB/EZ/commit/5075f6fbdc888dbf3cf3dbded0dda7928f884b32))

## [0.7.1](https://github.com/SchoolyB/EZ/compare/v0.7.0...v0.7.1) (2025-12-02)


### Bug Fixes

* **ci:** sync release-please manifest to v0.7.0 ([d908c55](https://github.com/SchoolyB/EZ/commit/d908c55a0aee63d8e9644bd03fbb73aeed901fb4))
* **ci:** sync release-please manifest to v0.7.0 ([dd05c6e](https://github.com/SchoolyB/EZ/commit/dd05c6e13a6cb68479b800e972a40e87fa460229))

## [0.7.0](https://github.com/SchoolyB/EZ/compare/v0.6.1...v0.7.0) (2025-12-02)


### Features

* **io:** add byte I/O and atomic writes ([ab37220](https://github.com/SchoolyB/EZ/commit/ab37220ea5343df91b40856fe565858fa73149cf))
* **io:** add file handles, constants, and convenience functions ([870df20](https://github.com/SchoolyB/EZ/commit/870df203ac6cac574a25b49b6bac71168abcf2d4))
* **io:** byte operations, file handles & enhancements ([7832642](https://github.com/SchoolyB/EZ/commit/783264228085fe2467954fdb8f0b140578777c16))


### Bug Fixes

* **io:** handle close errors on writable file handles ([aebdd96](https://github.com/SchoolyB/EZ/commit/aebdd9679296b87152c229aae5a0cc05ede728ef))

## [0.6.1](https://github.com/SchoolyB/EZ/compare/v0.6.0...v0.6.1) (2025-12-02)


### Bug Fixes

* **io:** add path security validation ([3f82018](https://github.com/SchoolyB/EZ/commit/3f82018747ce98752772faa1304e117009185c48))
* **io:** add path security validation ([a520f30](https://github.com/SchoolyB/EZ/commit/a520f30c9f4f49d9bb3cd07b3953272dd22277d6)), closes [#254](https://github.com/SchoolyB/EZ/issues/254)

## [0.6.0](https://github.com/SchoolyB/EZ/compare/v0.5.0...v0.6.0) (2025-12-02)


### Features

* **lang:** add hex/binary literals and byte warnings ([172028a](https://github.com/SchoolyB/EZ/commit/172028a98ff60b27111b9c87548e599e797e1b35))
* **lang:** implement byte and [byte] data types ([d1f98be](https://github.com/SchoolyB/EZ/commit/d1f98be06acefca64562411f67366232235b40c5))
* **lang:** implement byte and [byte] data types ([4499882](https://github.com/SchoolyB/EZ/commit/4499882917a030a02e842df37ab05c9f563878a8)), closes [#248](https://github.com/SchoolyB/EZ/issues/248)
* **stdlib:** implement [@bytes](https://github.com/bytes) module for binary data operations ([48a05b6](https://github.com/SchoolyB/EZ/commit/48a05b6d3d63e416694abeac198e0008566893a5))
* **stdlib:** implement [@io](https://github.com/io) module for file system operations ([99115a1](https://github.com/SchoolyB/EZ/commit/99115a15f661f200450c7a34a90dc08ffe1d4acd)), closes [#243](https://github.com/SchoolyB/EZ/issues/243)
* **stdlib:** implement [@os](https://github.com/os) module ([f2feac2](https://github.com/SchoolyB/EZ/commit/f2feac2761e028bd1d2178b795e75c7b996231ba))
* **stdlib:** implement [@os](https://github.com/os) module for operating system operations ([7595236](https://github.com/SchoolyB/EZ/commit/75952363d82098c2faad05058985227a7254f348))
* **stdlib:** implement `[@bytes](https://github.com/bytes)` module for binary data operations ([5533a97](https://github.com/SchoolyB/EZ/commit/5533a972a16ad14168cd5187e653d95914324867))
* **stdlib:** implement `[@io](https://github.com/io)` module for file system operations ([4b195d4](https://github.com/SchoolyB/EZ/commit/4b195d45596dea2e2431c4b5c481484cedf27646))


### Bug Fixes

* **stdlib:** resolve CodeQL warnings ([0a7b971](https://github.com/SchoolyB/EZ/commit/0a7b9714fbbb46ebfc23d5b0ae1cc0ce1cba265b))

## [0.5.0](https://github.com/SchoolyB/EZ/compare/v0.4.0...v0.5.0) (2025-12-02)


### Features

* rename std.print to std.printf and fix escape sequences ([d5a03a0](https://github.com/SchoolyB/EZ/commit/d5a03a0f819a566c0d47a2616a11c19e2660838b))
* rename std.print to std.printf and fix escape sequences ([2a17192](https://github.com/SchoolyB/EZ/commit/2a171922b963ff2a37bbc92b0c3cdf50c5c9d6fc)), closes [#237](https://github.com/SchoolyB/EZ/issues/237)


### Bug Fixes

* correct CodeQL workflow order and permissions ([d93affa](https://github.com/SchoolyB/EZ/commit/d93affa9ba06d493044359f7eb48295bcab1d84d))
* correct CodeQL workflow order and permissions ([#221](https://github.com/SchoolyB/EZ/issues/221)) ([29a4c21](https://github.com/SchoolyB/EZ/commit/29a4c21c67055de62938b22bd815b89ba68c4ec3))

## [0.4.0](https://github.com/SchoolyB/EZ/compare/v0.3.0...v0.4.0) (2025-12-01)


### Features

* **stdlib:** add arrays.chunk function ([7bb900b](https://github.com/SchoolyB/EZ/commit/7bb900b4ed0bfe00eebc6e48e138748eaba6b406))
* **stdlib:** add arrays.chunk function ([3964f73](https://github.com/SchoolyB/EZ/commit/3964f7369daa2cb7c344ad1d948be07cb3722256)), closes [#228](https://github.com/SchoolyB/EZ/issues/228)
* **stdlib:** add math constants and infinity check ([b9c4a59](https://github.com/SchoolyB/EZ/commit/b9c4a59dc6b008d4d70bf18f7aa2b094bddc0f3c))
* **stdlib:** add math constants and infinity check ([bdf7c2a](https://github.com/SchoolyB/EZ/commit/bdf7c2a09c3874faef2a389e3a491703710c8585))
* **stdlib:** expand strings module with 12 new functions ([9add23b](https://github.com/SchoolyB/EZ/commit/9add23b10c8bb1dd8e2eca02bee242d412e88c22))
* **stdlib:** expand strings module with 12 new functions ([be16cfe](https://github.com/SchoolyB/EZ/commit/be16cfe857056ea423a723c49dc341002f15ad7e)), closes [#228](https://github.com/SchoolyB/EZ/issues/228)
* **stdlib:** expand time module with arithmetic and utilities ([c7f2be1](https://github.com/SchoolyB/EZ/commit/c7f2be1dc1e8c43fb20d4d4a404cb50f5e2f7cc8))
* **stdlib:** expand time module with arithmetic and utilities ([f7edf78](https://github.com/SchoolyB/EZ/commit/f7edf78f21e7e4a98516d72dc5f46f225a79de73))


### Bug Fixes

* **errors:** standardize error codes across codebase ([fdf91d2](https://github.com/SchoolyB/EZ/commit/fdf91d27a4ace86d15f6037b31c47de159886331))
* **errors:** standardize error codes across codebase ([8cf3bfd](https://github.com/SchoolyB/EZ/commit/8cf3bfd752b016468130c09cb8dd1f98cff23eeb)), closes [#233](https://github.com/SchoolyB/EZ/issues/233)

## [0.3.0](https://github.com/SchoolyB/EZ/compare/v0.2.2...v0.3.0) (2025-12-01)


### Features

* **stdlib:** add strings.repeat() function ([e13972a](https://github.com/SchoolyB/EZ/commit/e13972ad8cc2ec02bba52e88bf0be781dd914c55))
* **stdlib:** add strings.repeat() function ([c50253e](https://github.com/SchoolyB/EZ/commit/c50253e0a56af1c7da6e0349ba8f8d084327934b)), closes [#198](https://github.com/SchoolyB/EZ/issues/198)


### Bug Fixes

* **interpreter:** add helpful suggestions for common function mistakes ([3c56d14](https://github.com/SchoolyB/EZ/commit/3c56d142c59c052d87968740145c468223d1c8b3))
* **interpreter:** add helpful suggestions for common function mistakes ([7a64a64](https://github.com/SchoolyB/EZ/commit/7a64a64aa7f3a207c0c1f9004fc5f03e1fa0eaf4)), closes [#199](https://github.com/SchoolyB/EZ/issues/199)
* **interpreter:** allow empty {} literal for map types ([6f22b9f](https://github.com/SchoolyB/EZ/commit/6f22b9f27ba50e24fc679fa2314651880c7494eb))
* **interpreter:** allow empty {} literal for map types ([f59819e](https://github.com/SchoolyB/EZ/commit/f59819e4ecb35e5640739c5e0394d5cb3449453e)), closes [#194](https://github.com/SchoolyB/EZ/issues/194)
* **interpreter:** auto-detect descending range when start &gt; end ([e21d282](https://github.com/SchoolyB/EZ/commit/e21d2820cac91164fc2095102f3feee18a2de31b))
* **interpreter:** auto-detect descending range when start &gt; end ([057df6d](https://github.com/SchoolyB/EZ/commit/057df6dd4f6ce083dfb617e0804f17e588ab3267)), closes [#197](https://github.com/SchoolyB/EZ/issues/197)
* **lexer:** handle nested quotes in string interpolation ([8506973](https://github.com/SchoolyB/EZ/commit/8506973bb7c7e2ee14cd8b72ebb8071bf311f90a))
* **lexer:** handle nested quotes in string interpolation ([adebd2c](https://github.com/SchoolyB/EZ/commit/adebd2cd25dd2868e88fcc458e5ee0b6d8c2a115)), closes [#193](https://github.com/SchoolyB/EZ/issues/193)
* **loader:** properly format parse errors from imported modules ([30c13fd](https://github.com/SchoolyB/EZ/commit/30c13fd83890a89002a65b7b31f009ea6f239774))
* **loader:** properly format parse errors from imported modules ([bd39c18](https://github.com/SchoolyB/EZ/commit/bd39c18f48c5536a653ed79ed20d407a743f3fcf)), closes [#203](https://github.com/SchoolyB/EZ/issues/203)
* **stdlib:** add arrays.index() function ([868223e](https://github.com/SchoolyB/EZ/commit/868223e676d41b4bb1f884e4cd491a89172e7a35))
* **stdlib:** add arrays.index() function ([da25e94](https://github.com/SchoolyB/EZ/commit/da25e9455e561bb848b149b6be757dcfeb480c52)), closes [#200](https://github.com/SchoolyB/EZ/issues/200)
* **stdlib:** add strings.slice() function ([faeecaa](https://github.com/SchoolyB/EZ/commit/faeecaac9f9ae43bdacba65974dea3e2bb2689b8))
* **stdlib:** add strings.slice() function ([5fd5d9e](https://github.com/SchoolyB/EZ/commit/5fd5d9ee0af2c4fa5339d7f698cb00cee3069225)), closes [#201](https://github.com/SchoolyB/EZ/issues/201)
* **stdlib:** fix time.format() argument order and format conversion ([470c0c7](https://github.com/SchoolyB/EZ/commit/470c0c7cd900631d726a8e8685b8cd69c6ef89e8))
* **stdlib:** fix time.format() argument order and format conversion ([a62b2cb](https://github.com/SchoolyB/EZ/commit/a62b2cb7f5bb03364f18e5d82b232d83e2af1875)), closes [#195](https://github.com/SchoolyB/EZ/issues/195)

## [0.2.2](https://github.com/SchoolyB/EZ/compare/v0.2.1...v0.2.2) (2025-12-01)


### Bug Fixes

* **stdlib:** add uppercase math constants, deprecate lowercase ([f81bfcf](https://github.com/SchoolyB/EZ/commit/f81bfcfc34787337808dbc8e0553658cc2c9c5d0))
* **stdlib:** add uppercase math constants, deprecate lowercase ([4fab99d](https://github.com/SchoolyB/EZ/commit/4fab99dde3e2c2254fea11411f76e08fdb2a6a1f)), closes [#196](https://github.com/SchoolyB/EZ/issues/196)

## [0.2.1](https://github.com/SchoolyB/EZ/compare/v0.2.0...v0.2.1) (2025-12-01)


### Bug Fixes

* **stdlib:** strings.join no longer includes quotes around elements ([1e8c55a](https://github.com/SchoolyB/EZ/commit/1e8c55a99d1fbbd61bbaf7b4d2b851f6dc538220))
* **stdlib:** strings.join no longer includes quotes around elements ([1a4b531](https://github.com/SchoolyB/EZ/commit/1a4b531fb689e04a9767efaba6e6f034010d086f)), closes [#205](https://github.com/SchoolyB/EZ/issues/205)

## [0.2.0](https://github.com/SchoolyB/EZ/compare/v0.1.0...v0.2.0) (2025-12-01)


### Features

* add error tests and simplify README ([8fe0851](https://github.com/SchoolyB/EZ/commit/8fe0851f05f5ee967e3b925554312bd6aa9f093b))
* restructure EZ tests with pass/fail counting ([665588a](https://github.com/SchoolyB/EZ/commit/665588a786cf301de40f13caa95d7988f5968045))
* restructure EZ tests with pass/fail counting ([34893a0](https://github.com/SchoolyB/EZ/commit/34893a0da8e2b4a21bb6efb6a659e7f418446138))

## 0.1.0 (2025-12-01)


### Features

* add [@suppress](https://github.com/suppress)() attribute functionality ([6386409](https://github.com/SchoolyB/EZ/commit/6386409d9d9e009f64218406ce8b2a05a5da368b))
* add automatic versioning with release-please ([ebc27d7](https://github.com/SchoolyB/EZ/commit/ebc27d78562ac54506885557e309a6e31e96937c))
* add basic warning system ([990ffe9](https://github.com/SchoolyB/EZ/commit/990ffe9f37035e0dd8fdbf7337205f044093bb61))
* add installation and distribution system ([230ab5f](https://github.com/SchoolyB/EZ/commit/230ab5f3731524be839c60a4ed41cee8e492fb4c))
* add installation and distribution system ([9839456](https://github.com/SchoolyB/EZ/commit/983945650c2ec8c7d194d7d2795b860a132bc268))
* add project-wide build support to ez build ([ccf189e](https://github.com/SchoolyB/EZ/commit/ccf189e9a3ac82c067fb576844ac85d8beffeb98))
* add support for multi return values/update Parser, AST, Evaluator ([6801f89](https://github.com/SchoolyB/EZ/commit/6801f89a33c34f2ed3f36ece5e0d8de6deaa5d5f))
* add support for multiple inline imports ([3a040e6](https://github.com/SchoolyB/EZ/commit/3a040e6ee45d8263c265584d0e4ba99aed5e31ab))
* add support for multiple inline imports ([eb7d411](https://github.com/SchoolyB/EZ/commit/eb7d411c6c048b68df64fd78baea3d61f061ecb7)), closes [#72](https://github.com/SchoolyB/EZ/issues/72)
* Complete module system implementation ([a9734bb](https://github.com/SchoolyB/EZ/commit/a9734bb7d314e9dfeeb65487b31d7da94de94449))
* enhance CI with comprehensive tests and REPL validation ([222ff12](https://github.com/SchoolyB/EZ/commit/222ff1259bc6145c473e2eb69cb52585429af5da))
* enhance CI with comprehensive tests and REPL validation ([bfaf414](https://github.com/SchoolyB/EZ/commit/bfaf414162a5a3aae6814228feeb65bf0bdba772))
* implement full enum support with attributes ([98c90ce](https://github.com/SchoolyB/EZ/commit/98c90cefff3a80c3499091d8db322b45d1966f38))
* implement full enum support with attributes ([a8f6e06](https://github.com/SchoolyB/EZ/commit/a8f6e065d9a3d65a2a43042b718bc9f1c57bbd9a)), closes [#9](https://github.com/SchoolyB/EZ/issues/9)
* implement interactive REPL mode ([fcca9f8](https://github.com/SchoolyB/EZ/commit/fcca9f8a049db91f91dcb2b161979a43ab2f2438))
* implement interactive REPL mode ([fae00d9](https://github.com/SchoolyB/EZ/commit/fae00d9dcef0f4abb3298eac206d9024593d427e)), closes [#45](https://github.com/SchoolyB/EZ/issues/45)
* implement type sharing for function parameters ([1991480](https://github.com/SchoolyB/EZ/commit/19914808a5a3dbcba799063b1cdb6b08dd25dd91))
* implement type sharing for function parameters ([5fc89e5](https://github.com/SchoolyB/EZ/commit/5fc89e5a73b339ee8afdf48325a30a03b4a29e0f)), closes [#10](https://github.com/SchoolyB/EZ/issues/10)
* improve release artifacts with archives and checksums ([5d217c5](https://github.com/SchoolyB/EZ/commit/5d217c5e388176ff5a167470727c68bdb231acaf))
* improve release artifacts with archives and checksums ([f51725d](https://github.com/SchoolyB/EZ/commit/f51725dde475760259c284c27d2f16bf9e26c36f))
* intergrate new error handling system into parser, evaluator, & lexer ([6613db9](https://github.com/SchoolyB/EZ/commit/6613db9cee3d0072565115bc5adf4f4a718522fa))


### Bug Fixes

* add error E3007 for missing main() function ([b5f6a90](https://github.com/SchoolyB/EZ/commit/b5f6a90d9f5975db1449d83c331ce11604314a50))
* add error E3007 for missing main() function ([fb995eb](https://github.com/SchoolyB/EZ/commit/fb995eba05f15261547ea9a8a492f247293ba496))
* add support for array and string index assignment ([e46004b](https://github.com/SchoolyB/EZ/commit/e46004b624f0fd1bf007add4cb90f8a00cf3ee15))
* add support for array and string index assignment (issue [#4](https://github.com/SchoolyB/EZ/issues/4), [#12](https://github.com/SchoolyB/EZ/issues/12)) ([f92d63e](https://github.com/SchoolyB/EZ/commit/f92d63e965efd74a033226c16e999b37ee774852))
* add warning W2003 for missing return statement ([09f73d0](https://github.com/SchoolyB/EZ/commit/09f73d0df3fa0ed0faf4d36c5b15dd6ac8e9472d))
* add warning W2003 for missing return statement ([0ca1056](https://github.com/SchoolyB/EZ/commit/0ca105620ef8c81a9294509b6829b0a5d83ceca3))
* allow otherwise/or keywords after return/break/continue in if blocks ([2523722](https://github.com/SchoolyB/EZ/commit/25237225c58040544fff511b208fdc46440cfed0))
* allow otherwise/or keywords after return/break/continue in if blocks ([0719e54](https://github.com/SchoolyB/EZ/commit/0719e5442ec762ee252192aeb816ef33d5362bc7)), closes [#14](https://github.com/SchoolyB/EZ/issues/14)
* allow variable shadowing in nested blocks (parser only) ([27babab](https://github.com/SchoolyB/EZ/commit/27babab6c5062eb1c535c9299117d997a5b983ee))
* allow variable shadowing in nested blocks (parser) ([7f8b40d](https://github.com/SchoolyB/EZ/commit/7f8b40d1627c56df94c747eae06baf0b7f5d39d2))
* Boolean negation and comparison for stdlib returns ([1f274ff](https://github.com/SchoolyB/EZ/commit/1f274ffb14d40ee92e4b8d4f64dd5a384f8bda32)), closes [#146](https://github.com/SchoolyB/EZ/issues/146)
* const declarations now support qualified type names (module.Type) ([250c944](https://github.com/SchoolyB/EZ/commit/250c94431ce1fc310d565e6b20cbe67b3ef75340)), closes [#157](https://github.com/SchoolyB/EZ/issues/157)
* const variable reassignment bug ([44f41d0](https://github.com/SchoolyB/EZ/commit/44f41d046fee129cc2fd353e1955ab020d7f8a66))
* deduplicate modules in using scope to prevent ambiguity ([34fc8c7](https://github.com/SchoolyB/EZ/commit/34fc8c7aff1ef8dd3ee787b5e2698d357ca78df1))
* deduplicate modules in using scope to prevent ambiguity ([b48200e](https://github.com/SchoolyB/EZ/commit/b48200ed0b8e52320b09fd0d5791d4526dcb2ef3))
* directory modules now properly share namespace across files ([ec0d84b](https://github.com/SchoolyB/EZ/commit/ec0d84b2897694490cf9c9f4d21f49c50868f0c6))
* Display strings with quotes to distinguish from numbers (issue [#99](https://github.com/SchoolyB/EZ/issues/99)) ([6fa2f77](https://github.com/SchoolyB/EZ/commit/6fa2f7704ed98b5d990565c1fb86e943f2ef41d4))
* duplicate struct member names/add: loop depth tracking ([fbcbb90](https://github.com/SchoolyB/EZ/commit/fbcbb908afa5ff0cc1fdf70fbf75e7f763b55826))
* enable logical operators (&&, ||, !) in if conditions ([907f2c1](https://github.com/SchoolyB/EZ/commit/907f2c1c3324acc3bce5e346bdb6b603373a40db))
* enable logical operators (&&, ||, !) in if conditions (issue [#8](https://github.com/SchoolyB/EZ/issues/8), [#17](https://github.com/SchoolyB/EZ/issues/17)) ([ebf46da](https://github.com/SchoolyB/EZ/commit/ebf46da46ca4f1edf6c4077e9ec3a883f70337c6))
* error bug, module importing error bugs ([e11c8de](https://github.com/SchoolyB/EZ/commit/e11c8deb4bf896f5e116f7025c234c7249329249))
* extend for_each loop to support string iteration ([89cd5f1](https://github.com/SchoolyB/EZ/commit/89cd5f1015deb8eb12cbfde5b2cfe5fbcb9cc0a7))
* extend for_each loop to support string iteration ([f8e1ae7](https://github.com/SchoolyB/EZ/commit/f8e1ae76d49afb71194552622a50cdcee3bac732)), closes [#7](https://github.com/SchoolyB/EZ/issues/7)
* Float values ending in .0 now display decimal point ([5eaa329](https://github.com/SchoolyB/EZ/commit/5eaa329043951382f656087cbac854ea3dd75754))
* function parameter parsing bug ([dc49ff8](https://github.com/SchoolyB/EZ/commit/dc49ff808264e9e41d31e183c8edc17b4438c49c))
* implement char comparison operators ([50f1420](https://github.com/SchoolyB/EZ/commit/50f1420b38f6256d1030828d20d692abed601143))
* implement char comparison operators ([2d68f4e](https://github.com/SchoolyB/EZ/commit/2d68f4e860e31cfffe557c8830850756737217e5))
* initialize dynamic arrays to empty array instead of NIL ([87a5f3d](https://github.com/SchoolyB/EZ/commit/87a5f3d0c95de0f32b507606e7ee53831fe5fbbb))
* initialize dynamic arrays to empty array instead of NIL ([68e767f](https://github.com/SchoolyB/EZ/commit/68e767f4f2747a1b35d206c571e07a51e585783b))
* int() builtin now supports enum value conversion ([857e9dc](https://github.com/SchoolyB/EZ/commit/857e9dc7d0da94251c19792679e5ed8c8d6e4a85)), closes [#154](https://github.com/SchoolyB/EZ/issues/154)
* invalid import statments not throwing errors ([3f8ea40](https://github.com/SchoolyB/EZ/commit/3f8ea40a04309dd3143e970f40f4c70730eb61d5))
* len() builtin now supports maps ([8550f56](https://github.com/SchoolyB/EZ/commit/8550f56655a7f8fc88f5219a7ed7ce0a902c3301)), closes [#147](https://github.com/SchoolyB/EZ/issues/147)
* lexer bug when handling @ in imports and attributes ([75786d6](https://github.com/SchoolyB/EZ/commit/75786d6f6557280d2f20789fb1187a1815bffef2))
* logic that allowed nested function declarations ([e2a60e9](https://github.com/SchoolyB/EZ/commit/e2a60e9e89b2c3d00a30d2899367efe9c578f60e))
* make arrays.push() and arrays.pop() modify arrays in-place (issu… ([34f2b35](https://github.com/SchoolyB/EZ/commit/34f2b35f461ac2d55dbbcd351d4f044205329e2a))
* make arrays.push() and arrays.pop() modify arrays in-place (issue [#1](https://github.com/SchoolyB/EZ/issues/1), [#18](https://github.com/SchoolyB/EZ/issues/18)) ([c751492](https://github.com/SchoolyB/EZ/commit/c7514928a12534e92b290e1343917f2c5a6d76c0))
* Make range() end value exclusive (issue [#112](https://github.com/SchoolyB/EZ/issues/112)) ([430abd5](https://github.com/SchoolyB/EZ/commit/430abd532cda2bd20da7c452366e367e292138bf))
* modulo by zero bug/ arg count mismatch bug ([630a716](https://github.com/SchoolyB/EZ/commit/630a7164a736b979d6f8b6254ac2a1d30bc8d962))
* parser no longer misidentifies uppercase enum members as struct literals ([1a08edf](https://github.com/SchoolyB/EZ/commit/1a08edf284e5abd5d40864043b46a6e6f388912d))
* parser no longer misidentifies uppercase enum members as struct literals ([fe481c6](https://github.com/SchoolyB/EZ/commit/fe481c6ca9a5ade43b61e53cbc999e8b8d44501a)), closes [#162](https://github.com/SchoolyB/EZ/issues/162)
* preserve array mutability when passed to functions ([4a71667](https://github.com/SchoolyB/EZ/commit/4a716678a855b8b23a67cc94cde664825af549f3))
* preserve array mutability when passed to functions ([1ef204f](https://github.com/SchoolyB/EZ/commit/1ef204f5f7d7ce6a6a40a9a73a620cc17f137a3d)), closes [#155](https://github.com/SchoolyB/EZ/issues/155)
* preserve enum type information with EnumValue wrapper ([a5c9e5f](https://github.com/SchoolyB/EZ/commit/a5c9e5fb97505a5312745afae42ce14b62735cfe))
* preserve enum type information with EnumValue wrapper ([5b4890b](https://github.com/SchoolyB/EZ/commit/5b4890b996426c9f84bf0ff2f8eda5194a2a658b))
* Prevent const variable mutation through arrays and index assignment (issue [#98](https://github.com/SchoolyB/EZ/issues/98)) ([9fac9b6](https://github.com/SchoolyB/EZ/commit/9fac9b669fbf59e01d66490ba332ae3a10fb6b35))
* Prevent using reserved keywords/types as identifiers ([fb88466](https://github.com/SchoolyB/EZ/commit/fb88466ff9ac89e260978eba195f1df57e10652e))
* provide default values for uninitialized primitive types ([0584478](https://github.com/SchoolyB/EZ/commit/058447852b16b1e1852f139bed36a72f75372380))
* provide default values for uninitialized primitive types ([550a331](https://github.com/SchoolyB/EZ/commit/550a331e18521171f203035c189a87e7f3e15e43))
* range() now supports 1, 2, or 3 arguments ([cb27fef](https://github.com/SchoolyB/EZ/commit/cb27fef976ffc3ad88a6025d7bc7dcd7cb7b407d))
* resolve golangci-lint errors ([8316faf](https://github.com/SchoolyB/EZ/commit/8316faf23a41c7c7d43b8dbeb98f6f64c2c4a9b2))
* resolve module system bugs for type resolution, arrays, enums, and using directive ([045bfca](https://github.com/SchoolyB/EZ/commit/045bfcadb9ac09876b7d2199811b52428bf48b82))
* resolve types from modules via using directive ([26801df](https://github.com/SchoolyB/EZ/commit/26801df8cd5d60f027da797708be097820eb178a))
* resolve types from modules via using directive ([69c433d](https://github.com/SchoolyB/EZ/commit/69c433d43524f75eadcafddf7a9c51cc43c18975)), closes [#153](https://github.com/SchoolyB/EZ/issues/153)
* Return statements now correctly parse struct/array/map literals ([f91bf8c](https://github.com/SchoolyB/EZ/commit/f91bf8c35336d67904b168ddb060f5b320b43c02)), closes [#142](https://github.com/SchoolyB/EZ/issues/142)
* struct evaluation bug & updatec core struct logic ([7660298](https://github.com/SchoolyB/EZ/commit/76602980c97ddd1a99d7dd7867714bc01f8df8bc))
* support array types in function return declarations ([5ef0207](https://github.com/SchoolyB/EZ/commit/5ef0207d327f0a69c9ee71f0c76e0d7db2910ee9))
* support array types in function return declarations ([3c42e1c](https://github.com/SchoolyB/EZ/commit/3c42e1c71778c0615fc7a8a58638823ae3dadd64))
* support enum type annotation syntax and prevent nil pointer crash ([66ef481](https://github.com/SchoolyB/EZ/commit/66ef481c7ac325cd2da737b4888edebfe426ad52))
* support enum type annotation syntax and prevent nil pointer crash ([464ed95](https://github.com/SchoolyB/EZ/commit/464ed956ede4c4cf91ee4d5f7d608fd430548ce4)), closes [#149](https://github.com/SchoolyB/EZ/issues/149)
* support file-level using directive in type resolution ([843f912](https://github.com/SchoolyB/EZ/commit/843f9127eb9aa067b702acf2aaa8149354981bde))
* support fixed-size array declarations ([8313847](https://github.com/SchoolyB/EZ/commit/83138475dd85c9e97c1ab2dd7a0f2441128f5d52))
* support fixed-size array declarations ([92edd88](https://github.com/SchoolyB/EZ/commit/92edd882e7acc66f8ff788217148c6693cb5254e))
* Support underscore syntax in int() and float() conversions (issue [#103](https://github.com/SchoolyB/EZ/issues/103)) ([58352ce](https://github.com/SchoolyB/EZ/commit/58352cebb38e6053926a5938ce704eddc0909fc5))
* track imports by alias to support aliased using statements ([421d0d5](https://github.com/SchoolyB/EZ/commit/421d0d515acf0044c35320e4765d1a8974ac0a15))
* track imports by alias to support aliased using statements ([7396473](https://github.com/SchoolyB/EZ/commit/7396473a98432bfde408c61bd5217c9496ea6cf4))
* Typed tuple unpacking now parses correctly ([8a5e599](https://github.com/SchoolyB/EZ/commit/8a5e5993e171cdbe1647d8e92c4d4922a58e3724)), closes [#145](https://github.com/SchoolyB/EZ/issues/145)
* uncaptured return value bug ([4cbc30b](https://github.com/SchoolyB/EZ/commit/4cbc30b9093cad96a34f942c925b7c30d08baa9c))
* unwrap enum values before comparison operations ([51338dd](https://github.com/SchoolyB/EZ/commit/51338dde2344ef8f19f555557153b3a0b7d077f6))
* unwrap enum values before comparison operations ([fca6931](https://github.com/SchoolyB/EZ/commit/fca6931c15ae9c066de268f4d1b2d034cdeed9ed)), closes [#163](https://github.com/SchoolyB/EZ/issues/163)
* use global stdin reader to fix input() buffering issues ([a18aa23](https://github.com/SchoolyB/EZ/commit/a18aa23fff18fda4c00b65d8d16ffd16e3f896cc))
* use global stdin reader to fix input() buffering issues ([cf20c81](https://github.com/SchoolyB/EZ/commit/cf20c81b02579c6cdc8a654b4487c3adcb78aad8))
* Validate enum type attributes to only allow primitives (issue [#119](https://github.com/SchoolyB/EZ/issues/119)) ([135058d](https://github.com/SchoolyB/EZ/commit/135058dfcfc1b01a59ff8bf5caef23c0f6254f7a))
* Validate module import before using statement ([5aabb28](https://github.com/SchoolyB/EZ/commit/5aabb2869270a325632dbadb41ba7b50941277d6))
* Validate unknown attributes and prevent parser hang ([3a41c6b](https://github.com/SchoolyB/EZ/commit/3a41c6b8e8ced0c802432e372e6b217084803603)), closes [#165](https://github.com/SchoolyB/EZ/issues/165)


### Miscellaneous Chores

* prepare initial release ([fefa831](https://github.com/SchoolyB/EZ/commit/fefa831dacb27e7931d6e25b0d38169a9eec861a))
