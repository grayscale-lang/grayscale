## Related issue

Closes #

## Summary

<!-- 1-3 bullet points describing what this PR does and why -->

-

## Type of change

- [ ] Bug fix
- [ ] New feature
- [ ] Refactor
- [ ] Tests
- [ ] Documentation
- [ ] CI/Build

## Breaking changes

<!-- Does this PR break any existing behavior? If yes, describe what breaks and why. -->

None.

## Checklist

- [ ] `make build` compiles with zero warnings
- [ ] Tests added/updated (unit, integration, or both as appropriate)
- [ ] No `@` before module names in any text (it tags GitHub users)
- [ ] Added yourself to the `Contributors:` section of the header of each source file you changed

### If adding user-facing stdlib/builtin/type

- [ ] C implementation in `grayc/src/stdlib/<module>.h` and `.c` (with `@man` block)
- [ ] Typechecker wired: return type, `stdlib_arg_table[]`, `stdlib_arg_type_table[]`, `_using_funcs[]`
- [ ] Codegen wired: `emit_<module>_call()`
- [ ] `STANDARD.md` updated
- [ ] `./scripts/generate_stdlib_man.sh` run and output committed
