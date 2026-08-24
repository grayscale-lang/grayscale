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
    module_table_map_file(table, "main.gray", "main", true);
    module_table_map_file(table, "lib.gray", "lib", false);
    return table;
}

static DeclEntry *define(ModuleTable *table, const char *module,
                         const char *name, Visibility vis) {
    ModuleScope *scope = module_table_find(table, module);
    return module_scope_define(table, scope, DECL_FUNC, name, NULL, NULL,
                               "test.gray", 1, vis);
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
    module_table_map_file(table, "main.gray", "main", true);
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
    ASSERT_STR_EQ(module_table_module_for_file(table, "unknown.gray"), "main");
    ASSERT_STR_EQ(module_table_module_for_file(table, NULL), "main");
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

/* --- Qualified resolution --- */

static void test_resolve_qualified_ok(void) {
    ModuleTable *table = table_with_modules();
    DeclEntry *declared = define(table, "lib", "helper", VIS_PUBLIC);

    ResolveStatus status;
    DeclEntry *found = module_resolve_qualified(table, "main", "lib", "helper", &status);
    ASSERT_EQ(status, RESOLVE_OK);
    ASSERT(found == declared);
}

static void test_resolve_qualified_unknown_module(void) {
    ModuleTable *table = table_with_modules();
    ResolveStatus status;
    DeclEntry *found = module_resolve_qualified(table, "main", "nosuch", "helper", &status);
    ASSERT_EQ(status, RESOLVE_NO_MODULE);
    ASSERT(found == NULL);
}

static void test_resolve_qualified_unknown_member(void) {
    ModuleTable *table = table_with_modules();
    define(table, "lib", "helper", VIS_PUBLIC);
    ResolveStatus status;
    DeclEntry *found = module_resolve_qualified(table, "main", "lib", "absent", &status);
    ASSERT_EQ(status, RESOLVE_NO_DECL);
    ASSERT(found == NULL);
}

/* Private declarations still come back, so the diagnostic can point at where
 * they were declared. Only the status says access was refused. */
static void test_resolve_qualified_private_from_outside(void) {
    ModuleTable *table = table_with_modules();
    DeclEntry *declared = define(table, "lib", "secret", VIS_PRIVATE);
    ResolveStatus status;
    DeclEntry *found = module_resolve_qualified(table, "main", "lib", "secret", &status);
    ASSERT_EQ(status, RESOLVE_PRIVATE);
    ASSERT(found == declared);
}

static void test_resolve_qualified_private_from_own_module(void) {
    ModuleTable *table = table_with_modules();
    define(table, "lib", "secret", VIS_PRIVATE);
    ResolveStatus status;
    DeclEntry *found = module_resolve_qualified(table, "lib", "lib", "secret", &status);
    ASSERT_EQ(status, RESOLVE_OK);
    ASSERT_NOT_NULL(found);
}

static void test_resolve_qualified_null_status_allowed(void) {
    ModuleTable *table = table_with_modules();
    define(table, "lib", "helper", VIS_PUBLIC);
    ASSERT_NOT_NULL(module_resolve_qualified(table, "main", "lib", "helper", NULL));
}

/* --- Aliases --- */

static void test_alias_resolves_to_module(void) {
    ModuleTable *table = table_with_modules();
    module_table_add_alias(table, "l", "lib");
    define(table, "lib", "helper", VIS_PUBLIC);

    ASSERT_STR_EQ(module_table_resolve_alias(table, "l"), "lib");
    ResolveStatus status;
    DeclEntry *found = module_resolve_qualified(table, "main", "l", "helper", &status);
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
    DeclEntry *own = define(table, "main", "name", VIS_PUBLIC);
    define(table, "lib", "name", VIS_PUBLIC);

    const char *using_list[] = {"lib"};
    const char *ambiguous = NULL;
    DeclEntry *found = module_resolve_unqualified(table, "main", using_list, 1,
                                                  "name", &ambiguous);
    ASSERT(found == own);
    ASSERT(ambiguous == NULL);
}

static void test_unqualified_finds_using_module(void) {
    ModuleTable *table = table_with_modules();
    DeclEntry *declared = define(table, "lib", "helper", VIS_PUBLIC);
    const char *using_list[] = {"lib"};
    DeclEntry *found = module_resolve_unqualified(table, "main", using_list, 1,
                                                  "helper", NULL);
    ASSERT(found == declared);
}

static void test_unqualified_skips_private(void) {
    ModuleTable *table = table_with_modules();
    define(table, "lib", "secret", VIS_PRIVATE);
    const char *using_list[] = {"lib"};
    ASSERT(module_resolve_unqualified(table, "main", using_list, 1, "secret", NULL) == NULL);
}

/* Two using'd modules exporting the same name is an error, not a silent pick
 * of whichever was written first. */
static void test_unqualified_ambiguity_reported(void) {
    ModuleTable *table = table_with_modules();
    module_table_map_file(table, "other.gray", "other", false);
    define(table, "lib", "shared", VIS_PUBLIC);
    define(table, "other", "shared", VIS_PUBLIC);

    const char *using_list[] = {"lib", "other"};
    const char *ambiguous = NULL;
    DeclEntry *found = module_resolve_unqualified(table, "main", using_list, 2,
                                                  "shared", &ambiguous);
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
    ASSERT(module_resolve_unqualified(table, "main", using_list, 2, "shared", NULL) == first);
}

static void test_unqualified_miss(void) {
    ModuleTable *table = table_with_modules();
    const char *using_list[] = {"lib"};
    ASSERT(module_resolve_unqualified(table, "main", using_list, 1, "absent", NULL) == NULL);
}

/* --- Mangling and splitting --- */

static void test_mangle_imported_and_entry(void) {
    ModuleTable *table = table_with_modules();
    DeclEntry *imported = define(table, "lib", "Point", VIS_PUBLIC);
    DeclEntry *local = define(table, "main", "main", VIS_PUBLIC);

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
    RUN_TEST(test_resolve_qualified_ok);
    RUN_TEST(test_resolve_qualified_unknown_module);
    RUN_TEST(test_resolve_qualified_unknown_member);
    RUN_TEST(test_resolve_qualified_private_from_outside);
    RUN_TEST(test_resolve_qualified_private_from_own_module);
    RUN_TEST(test_resolve_qualified_null_status_allowed);
    RUN_TEST(test_alias_resolves_to_module);
    RUN_TEST(test_non_alias_passes_through);
    RUN_TEST(test_self_alias_ignored);
    RUN_TEST(test_unqualified_prefers_current_module);
    RUN_TEST(test_unqualified_finds_using_module);
    RUN_TEST(test_unqualified_skips_private);
    RUN_TEST(test_unqualified_ambiguity_reported);
    RUN_TEST(test_unqualified_first_match_without_slot);
    RUN_TEST(test_unqualified_miss);
    RUN_TEST(test_mangle_imported_and_entry);
    RUN_TEST(test_split_qualified);
    RUN_TEST(test_split_ignores_underscores);

    PRINT_RESULTS();
    arena_destroy(arena);
    return _test_fail > 0 ? 1 : 0;
}
