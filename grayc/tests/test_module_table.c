/*
 * test_module_table.c — Unit tests for the per-module symbol table: scope
 * creation, file-to-module attribution, qualified/unqualified resolution,
 * visibility, aliases, and mangling.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "test.h"
#include "../src/util/arena.h"
#include "../src/typechecker/module_table.h"

static Arena *arena;

static ModuleTable *table_with_modules(void) {
    ModuleTable *table = module_table_create(arena);
    module_table_map_file(table, "main.gray", NULL, true);
    module_table_map_file(table, "lib.gray", "lib", false);
    return table;
}

/* A scope resolving from `module`, with the given using list. `file` matters
 * only for visibility; tests that care pass a declaration's origin file. */
static ResolveScope scope_of(const char *module, const char *file,
                             const char **using_modules, int using_count) {
    ResolveScope s;
    s.module = module;
    s.file = file;
    s.using_modules = using_modules;
    s.using_count = using_count;
    return s;
}

/* The file a module's declarations live in. Privacy is file-scoped, so tests
 * need declarations and callers to sit in distinct files. */
static const char *module_file(const char *module) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s.gray", *module ? module : "main");
    return arena_copy_string(arena, buf);
}

static DeclEntry *define(ModuleTable *table, const char *module,
                         const char *name, Visibility vis) {
    ModuleScope *scope = module_table_find(table, module);
    return module_scope_define(table, scope, DECL_FUNC, name, NULL, NULL,
                               module_file(module), 1, vis);
}

/* --- Scopes and definition --- */

static void test_define_and_lookup(void) {
    ModuleTable *table = table_with_modules();
    define(table, "lib", "helper", VIS_PUBLIC);

    DeclEntry *entry = module_scope_lookup(module_table_find(table, "lib"), "helper");
    ASSERT_NOT_NULL(entry);
    ASSERT_STR_EQ(entry->name, "helper");
    ASSERT_STR_EQ(entry->module_name, "lib");
    ASSERT_EQ(entry->kind, DECL_FUNC);
    ASSERT(!entry->module_is_entry);
}

static void test_lookup_miss_returns_null(void) {
    ModuleTable *table = table_with_modules();
    ASSERT(module_scope_lookup(module_table_find(table, "lib"), "absent") == NULL);
}

static void test_redefinition_keeps_first(void) {
    ModuleTable *table = table_with_modules();
    DeclEntry *first = define(table, "lib", "dup", VIS_PUBLIC);
    DeclEntry *second = define(table, "lib", "dup", VIS_PRIVATE);
    ASSERT(first == second);
    ASSERT_EQ(first->visibility, VIS_PUBLIC);
    ASSERT_EQ(module_table_find(table, "lib")->count, 1);
}

static void test_unknown_module_has_no_scope(void) {
    ModuleTable *table = table_with_modules();
    ASSERT(module_table_find(table, "nosuch") == NULL);
}

/* A directory import maps several files to one module name; their
 * declarations must land in the same scope, which is what makes a sibling
 * reference an ordinary same-module lookup. */
static void test_directory_module_merges_files(void) {
    ModuleTable *table = module_table_create(arena);
    module_table_map_file(table, "main.gray", NULL, true);
    module_table_map_file(table, "pkg/types.gray", "pkg", false);
    module_table_map_file(table, "pkg/logic.gray", "pkg", false);

    ASSERT_STR_EQ(module_table_module_for_file(table, "pkg/types.gray"), "pkg");
    ASSERT_STR_EQ(module_table_module_for_file(table, "pkg/logic.gray"), "pkg");

    define(table, "pkg", "Item", VIS_PUBLIC);
    define(table, "pkg", "process", VIS_PUBLIC);
    ASSERT_EQ(module_table_find(table, "pkg")->count, 2);
}

static void test_unmapped_file_falls_back_to_entry(void) {
    ModuleTable *table = table_with_modules();
    ASSERT_STR_EQ(module_table_module_for_file(table, "unknown.gray"), MODULE_ENTRY_NAME);
    ASSERT_STR_EQ(module_table_module_for_file(table, NULL), MODULE_ENTRY_NAME);
}

/* The hash starts at 16 slots and is held at a load factor of one half, so
 * this crosses several rebuilds. Every name must survive them. */
static void test_growth_preserves_all_entries(void) {
    ModuleTable *table = table_with_modules();
    char name[32];
    for (int i = 0; i < 200; i++) {
        snprintf(name, sizeof(name), "sym%d", i);
        define(table, "lib", name, VIS_PUBLIC);
    }
    ASSERT_EQ(module_table_find(table, "lib")->count, 200);
    for (int i = 0; i < 200; i++) {
        snprintf(name, sizeof(name), "sym%d", i);
        ASSERT_NOT_NULL(module_scope_lookup(module_table_find(table, "lib"), name));
    }
}

static void test_many_modules_preserved(void) {
    ModuleTable *table = module_table_create(arena);
    char mod[32];
    for (int i = 0; i < 100; i++) {
        snprintf(mod, sizeof(mod), "mod%d", i);
        module_table_map_file(table, mod, mod, false);
    }
    ASSERT_EQ(table->count, 100);
    for (int i = 0; i < 100; i++) {
        snprintf(mod, sizeof(mod), "mod%d", i);
        ASSERT_NOT_NULL(module_table_find(table, mod));
    }
}

/* An imported module whose basename matches the entry file's is still a
 * separate module. Keying the entry scope by its basename would have merged
 * the two. */
static void test_imported_module_may_share_entry_basename(void) {
    ModuleTable *table = module_table_create(arena);
    module_table_map_file(table, "main.gray", NULL, true);
    module_table_map_file(table, "sub/main.gray", "main", false);

    define(table, MODULE_ENTRY_NAME, "shared", VIS_PUBLIC);
    define(table, "main", "shared", VIS_PUBLIC);

    ASSERT_EQ(module_table_find(table, MODULE_ENTRY_NAME)->count, 1);
    ASSERT_EQ(module_table_find(table, "main")->count, 1);
    ASSERT_STR_EQ(module_mangle(table, module_scope_lookup(
        module_table_find(table, "main"), "shared")), "main_shared");
    ASSERT_STR_EQ(module_mangle(table, module_scope_lookup(
        module_table_find(table, MODULE_ENTRY_NAME), "shared")), "shared");
}

/* Grayscale has no syntax for qualifying by the entry module, so an empty
 * qualifier must not reach into its declarations. */
static void test_entry_module_is_not_a_qualifier(void) {
    ModuleTable *table = table_with_modules();
    define(table, MODULE_ENTRY_NAME, "helper", VIS_PUBLIC);
    ResolveStatus status;
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), NULL, 0);
    DeclEntry *found = module_resolve_qualified(table, &sc,
                                                MODULE_ENTRY_NAME, "helper", &status);
    ASSERT_EQ(status, RESOLVE_NO_MODULE);
    ASSERT(found == NULL);
}

/* The node index recovers a declaration's module from the node alone, which
 * is how a later phase gets the mangled name without a file or a name to look
 * up. */
static void test_node_index(void) {
    ModuleTable *table = table_with_modules();
    AstNode fake_a;
    AstNode fake_b;
    ModuleScope *lib = module_table_find(table, "lib");
    DeclEntry *a = module_scope_define(table, lib, DECL_FUNC, "a", &fake_a, NULL,
                                       "lib.gray", 1, VIS_PUBLIC);
    DeclEntry *b = module_scope_define(table, lib, DECL_FUNC, "b", &fake_b, NULL,
                                       "lib.gray", 2, VIS_PUBLIC);

    ASSERT(module_table_entry_for_node(table, &fake_a) == a);
    ASSERT(module_table_entry_for_node(table, &fake_b) == b);
    ASSERT_STR_EQ(module_mangle(table, module_table_entry_for_node(table, &fake_a)), "lib_a");

    AstNode unknown;
    ASSERT(module_table_entry_for_node(table, &unknown) == NULL);
    ASSERT(module_table_entry_for_node(table, NULL) == NULL);
}

/* The index rehashes; every node must survive it. */
static void test_node_index_growth(void) {
    ModuleTable *table = table_with_modules();
    static AstNode nodes[300];
    ModuleScope *lib = module_table_find(table, "lib");
    char name[32];
    for (int i = 0; i < 300; i++) {
        snprintf(name, sizeof(name), "n%d", i);
        module_scope_define(table, lib, DECL_FUNC, name, &nodes[i], NULL, "lib.gray", i, VIS_PUBLIC);
    }
    for (int i = 0; i < 300; i++) {
        snprintf(name, sizeof(name), "n%d", i);
        DeclEntry *e = module_table_entry_for_node(table, &nodes[i]);
        ASSERT_NOT_NULL(e);
        ASSERT_STR_EQ(e->name, name);
    }
}

/* --- Qualified resolution --- */

static void test_resolve_qualified_ok(void) {
    ModuleTable *table = table_with_modules();
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), NULL, 0);
    DeclEntry *declared = define(table, "lib", "helper", VIS_PUBLIC);

    ResolveStatus status;
    DeclEntry *found = module_resolve_qualified(table, &sc, "lib", "helper", &status);
    ASSERT_EQ(status, RESOLVE_OK);
    ASSERT(found == declared);
}

static void test_resolve_qualified_unknown_module(void) {
    ModuleTable *table = table_with_modules();
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), NULL, 0);
    ResolveStatus status;
    DeclEntry *found = module_resolve_qualified(table, &sc, "nosuch", "helper", &status);
    ASSERT_EQ(status, RESOLVE_NO_MODULE);
    ASSERT(found == NULL);
}

static void test_resolve_qualified_unknown_member(void) {
    ModuleTable *table = table_with_modules();
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), NULL, 0);
    define(table, "lib", "helper", VIS_PUBLIC);
    ResolveStatus status;
    DeclEntry *found = module_resolve_qualified(table, &sc, "lib", "absent", &status);
    ASSERT_EQ(status, RESOLVE_NO_DECL);
    ASSERT(found == NULL);
}

/* Private declarations still come back, so the diagnostic can point at where
 * they were declared. Only the status says access was refused. */
static void test_resolve_qualified_private_from_outside(void) {
    ModuleTable *table = table_with_modules();
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), NULL, 0);
    DeclEntry *declared = define(table, "lib", "secret", VIS_PRIVATE);
    ResolveStatus status;
    DeclEntry *found = module_resolve_qualified(table, &sc, "lib", "secret", &status);
    ASSERT_EQ(status, RESOLVE_PRIVATE);
    ASSERT(found == declared);
}

static void test_resolve_qualified_private_from_own_module(void) {
    ModuleTable *table = table_with_modules();
    ResolveScope sc = scope_of("lib", module_file("lib"), NULL, 0);
    define(table, "lib", "secret", VIS_PRIVATE);
    ResolveStatus status;
    DeclEntry *found = module_resolve_qualified(table, &sc, "lib", "secret", &status);
    ASSERT_EQ(status, RESOLVE_OK);
    ASSERT_NOT_NULL(found);
}

static void test_resolve_qualified_null_status_allowed(void) {
    ModuleTable *table = table_with_modules();
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), NULL, 0);
    define(table, "lib", "helper", VIS_PUBLIC);
    ASSERT_NOT_NULL(module_resolve_qualified(table, &sc, "lib", "helper", NULL));
}

/* Private is scoped to the declaring file, not the module: two files merged
 * into one directory module are as much "outside" each other as two modules. */
static void test_private_is_file_scoped_within_a_module(void) {
    ModuleTable *table = module_table_create(arena);
    module_table_map_file(table, "main.gray", NULL, true);
    module_table_map_file(table, "pkg/a.gray", "pkg", false);
    module_table_map_file(table, "pkg/b.gray", "pkg", false);
    ModuleScope *pkg = module_table_find(table, "pkg");
    module_scope_define(table, pkg, DECL_FUNC, "secret", NULL, NULL,
                        "pkg/a.gray", 1, VIS_PRIVATE);

    ResolveScope same = scope_of("pkg", "pkg/a.gray", NULL, 0);
    ResolveScope sibling = scope_of("pkg", "pkg/b.gray", NULL, 0);
    ResolveStatus st;

    module_resolve_qualified(table, &same, "pkg", "secret", &st);
    ASSERT_EQ(st, RESOLVE_OK);
    module_resolve_qualified(table, &sibling, "pkg", "secret", &st);
    ASSERT_EQ(st, RESOLVE_PRIVATE);
}

/* --- Aliases --- */

static void test_alias_resolves_to_module(void) {
    ModuleTable *table = table_with_modules();
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), NULL, 0);
    module_table_add_alias(table, "l", "lib");
    define(table, "lib", "helper", VIS_PUBLIC);

    ASSERT_STR_EQ(module_table_resolve_alias(table, "l"), "lib");
    ResolveStatus status;
    DeclEntry *found = module_resolve_qualified(table, &sc, "l", "helper", &status);
    ASSERT_EQ(status, RESOLVE_OK);
    ASSERT_NOT_NULL(found);
    ASSERT_STR_EQ(found->module_name, "lib");
}

static void test_non_alias_passes_through(void) {
    ModuleTable *table = table_with_modules();
    ASSERT_STR_EQ(module_table_resolve_alias(table, "lib"), "lib");
}

static void test_self_alias_ignored(void) {
    ModuleTable *table = table_with_modules();
    module_table_add_alias(table, "lib", "lib");
    ASSERT_EQ(table->alias_count, 0);
}

/* --- Unqualified resolution --- */

static void test_unqualified_prefers_current_module(void) {
    ModuleTable *table = table_with_modules();
    DeclEntry *own = define(table, MODULE_ENTRY_NAME, "name", VIS_PUBLIC);
    define(table, "lib", "name", VIS_PUBLIC);

    const char *using_list[] = {"lib"};
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), using_list, sizeof(using_list)/sizeof(using_list[0]));
    const char *ambiguous = NULL;
    DeclEntry *found = module_resolve_unqualified(table, &sc, "name", &ambiguous);
    ASSERT(found == own);
    ASSERT(ambiguous == NULL);
}

static void test_unqualified_finds_using_module(void) {
    ModuleTable *table = table_with_modules();
    DeclEntry *declared = define(table, "lib", "helper", VIS_PUBLIC);
    const char *using_list[] = {"lib"};
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), using_list, sizeof(using_list)/sizeof(using_list[0]));
    DeclEntry *found = module_resolve_unqualified(table, &sc, "helper", NULL);
    ASSERT(found == declared);
}

static void test_unqualified_skips_private(void) {
    ModuleTable *table = table_with_modules();
    define(table, "lib", "secret", VIS_PRIVATE);
    const char *using_list[] = {"lib"};
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), using_list, sizeof(using_list)/sizeof(using_list[0]));
    ASSERT(module_resolve_unqualified(table, &sc, "secret", NULL) == NULL);
}

/* Two using'd modules exporting the same name is an error, not a silent pick
 * of whichever was written first. */
static void test_unqualified_ambiguity_reported(void) {
    ModuleTable *table = table_with_modules();
    module_table_map_file(table, "other.gray", "other", false);
    define(table, "lib", "shared", VIS_PUBLIC);
    define(table, "other", "shared", VIS_PUBLIC);

    const char *using_list[] = {"lib", "other"};
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), using_list, sizeof(using_list)/sizeof(using_list[0]));
    const char *ambiguous = NULL;
    DeclEntry *found = module_resolve_unqualified(table, &sc, "shared", &ambiguous);
    ASSERT(found == NULL);
    ASSERT_NOT_NULL(ambiguous);
    ASSERT_STR_EQ(ambiguous, "other");
}

/* Callers that pass no ambiguity slot opt into first-match-wins. */
static void test_unqualified_first_match_without_slot(void) {
    ModuleTable *table = table_with_modules();
    module_table_map_file(table, "other.gray", "other", false);
    DeclEntry *first = define(table, "lib", "shared", VIS_PUBLIC);
    define(table, "other", "shared", VIS_PUBLIC);

    const char *using_list[] = {"lib", "other"};
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), using_list, sizeof(using_list)/sizeof(using_list[0]));
    ASSERT(module_resolve_unqualified(table, &sc, "shared", NULL) == first);
}

static void test_unqualified_miss(void) {
    ModuleTable *table = table_with_modules();
    const char *using_list[] = {"lib"};
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), using_list, sizeof(using_list)/sizeof(using_list[0]));
    ASSERT(module_resolve_unqualified(table, &sc, "absent", NULL) == NULL);
}

/* --- Written names --- */

static ModuleTable *typed_table(void) {
    ModuleTable *table = module_table_create(arena);
    module_table_map_file(table, "main.gray", NULL, true);
    module_table_map_file(table, "lib.gray", "lib", false);
    ModuleScope *lib = module_table_find(table, "lib");
    module_scope_define(table, lib, DECL_STRUCT, "Point", NULL, NULL, module_file("lib"), 1, VIS_PUBLIC);
    module_scope_define(table, lib, DECL_ENUM, "Color", NULL, NULL, module_file("lib"), 2, VIS_PUBLIC);
    module_scope_define(table, lib, DECL_ALIAS, "Score", NULL, NULL, module_file("lib"), 3, VIS_PUBLIC);
    module_scope_define(table, lib, DECL_FUNC, "helper", NULL, NULL, module_file("lib"), 4, VIS_PUBLIC);
    return table;
}

static void test_resolve_written_qualified(void) {
    ModuleTable *table = typed_table();
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), NULL, 0);
    DeclEntry *e = module_resolve_written(table, &sc, "lib.Point");
    ASSERT_NOT_NULL(e);
    ASSERT_STR_EQ(e->name, "Point");
    ASSERT_STR_EQ(e->module_name, "lib");
}

/* A bare name inside a module finds that module's own declarations — the
 * sibling case, which the import merge handled by textual rewriting. */
static void test_resolve_written_bare_in_own_module(void) {
    ModuleTable *table = typed_table();
    ResolveScope sc = scope_of("lib", module_file("lib"), NULL, 0);
    DeclEntry *e = module_resolve_written(table, &sc, "Point");
    ASSERT_NOT_NULL(e);
    ASSERT_STR_EQ(e->module_name, "lib");
}

static void test_resolve_written_bare_needs_using(void) {
    ModuleTable *table = typed_table();
    ResolveScope bare = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), NULL, 0);
    ASSERT(module_resolve_written(table, &bare, "Point") == NULL);
    const char *using_list[] = {"lib"};
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), using_list, 1);
    ASSERT_NOT_NULL(module_resolve_written(table, &sc, "Point"));
}

/* --- Written type names --- */

static void test_type_name_leaf(void) {
    ModuleTable *table = typed_table();
    ResolveScope sc = scope_of("lib", module_file("lib"), NULL, 0);
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "lib.Point"),
                  "lib_Point");
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "Color"), "lib_Color");
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "Score"), "lib_Score");
}

static void test_type_name_primitives_untouched(void) {
    ModuleTable *table = typed_table();
    ResolveScope sc = scope_of("lib", module_file("lib"), NULL, 0);
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "int"), "int");
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "string"), "string");
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "Unknown"), "Unknown");
}

/* A function is not a type; a type annotation must not pick one up. */
static void test_type_name_ignores_non_types(void) {
    ModuleTable *table = typed_table();
    ResolveScope sc = scope_of("lib", module_file("lib"), NULL, 0);
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "helper"), "helper");
}

static void test_type_name_composites(void) {
    ModuleTable *table = typed_table();
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), NULL, 0);
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "[lib.Point]"),
                  "[lib_Point]");
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "[lib.Point,3]"),
                  "[lib_Point,3]");
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "^lib.Point"),
                  "^lib_Point");
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "map[string:lib.Point]"),
                  "map[string:lib_Point]");
}

static void test_type_name_nested_composites(void) {
    ModuleTable *table = typed_table();
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), NULL, 0);
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "[[lib.Point]]"),
                  "[[lib_Point]]");
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "^[lib.Point]"),
                  "^[lib_Point]");
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "map[lib.Color:[lib.Point]]"),
                  "map[lib_Color:[lib_Point]]");
}

static void test_type_name_unresolvable_unchanged(void) {
    ModuleTable *table = typed_table();
    ResolveScope sc = scope_of(MODULE_ENTRY_NAME, module_file(MODULE_ENTRY_NAME), NULL, 0);
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "[Nope]"), "[Nope]");
    ASSERT_STR_EQ(module_resolve_type_name(table, &sc, "map[string:int]"),
                  "map[string:int]");
}

/* --- Mangling and splitting --- */

static void test_mangle_imported_and_entry(void) {
    ModuleTable *table = table_with_modules();
    DeclEntry *imported = define(table, "lib", "Point", VIS_PUBLIC);
    DeclEntry *local = define(table, MODULE_ENTRY_NAME, "main", VIS_PUBLIC);

    ASSERT_STR_EQ(module_mangle(table, imported), "lib_Point");
    ASSERT_STR_EQ(module_mangle(table, local), "main");
}

static void test_split_qualified(void) {
    const char *module = NULL;
    const char *name = NULL;

    ASSERT(module_split_qualified(arena, "lib.Score", &module, &name));
    ASSERT_STR_EQ(module, "lib");
    ASSERT_STR_EQ(name, "Score");

    ASSERT(!module_split_qualified(arena, "Score", &module, &name));
    ASSERT(module == NULL);
    ASSERT_STR_EQ(name, "Score");
}

/* An identifier containing an underscore is not a qualified name — this is
 * exactly what the suffix-guessing it replaces got wrong. */
static void test_split_ignores_underscores(void) {
    const char *module = NULL;
    const char *name = NULL;
    ASSERT(!module_split_qualified(arena, "my_Thing", &module, &name));
    ASSERT(module == NULL);
    ASSERT_STR_EQ(name, "my_Thing");
}

int main(void) {
    arena = arena_create(64 * 1024);

    printf("\n");
    RUN_TEST(test_define_and_lookup);
    RUN_TEST(test_lookup_miss_returns_null);
    RUN_TEST(test_redefinition_keeps_first);
    RUN_TEST(test_unknown_module_has_no_scope);
    RUN_TEST(test_directory_module_merges_files);
    RUN_TEST(test_unmapped_file_falls_back_to_entry);
    RUN_TEST(test_growth_preserves_all_entries);
    RUN_TEST(test_many_modules_preserved);
    RUN_TEST(test_node_index);
    RUN_TEST(test_node_index_growth);
    RUN_TEST(test_imported_module_may_share_entry_basename);
    RUN_TEST(test_entry_module_is_not_a_qualifier);
    RUN_TEST(test_resolve_qualified_ok);
    RUN_TEST(test_resolve_qualified_unknown_module);
    RUN_TEST(test_resolve_qualified_unknown_member);
    RUN_TEST(test_resolve_qualified_private_from_outside);
    RUN_TEST(test_resolve_qualified_private_from_own_module);
    RUN_TEST(test_resolve_qualified_null_status_allowed);
    RUN_TEST(test_private_is_file_scoped_within_a_module);
    RUN_TEST(test_alias_resolves_to_module);
    RUN_TEST(test_non_alias_passes_through);
    RUN_TEST(test_self_alias_ignored);
    RUN_TEST(test_unqualified_prefers_current_module);
    RUN_TEST(test_unqualified_finds_using_module);
    RUN_TEST(test_unqualified_skips_private);
    RUN_TEST(test_unqualified_ambiguity_reported);
    RUN_TEST(test_unqualified_first_match_without_slot);
    RUN_TEST(test_unqualified_miss);
    RUN_TEST(test_resolve_written_qualified);
    RUN_TEST(test_resolve_written_bare_in_own_module);
    RUN_TEST(test_resolve_written_bare_needs_using);
    RUN_TEST(test_type_name_leaf);
    RUN_TEST(test_type_name_primitives_untouched);
    RUN_TEST(test_type_name_ignores_non_types);
    RUN_TEST(test_type_name_composites);
    RUN_TEST(test_type_name_nested_composites);
    RUN_TEST(test_type_name_unresolvable_unchanged);
    RUN_TEST(test_mangle_imported_and_entry);
    RUN_TEST(test_split_qualified);
    RUN_TEST(test_split_ignores_underscores);

    PRINT_RESULTS();
    arena_destroy(arena);
    return _test_fail > 0 ? 1 : 0;
}
