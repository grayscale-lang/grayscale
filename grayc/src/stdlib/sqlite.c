/*
 * sqlite.c — Implementation of the sqlite stdlib module.
 * Provides database open/close, query execution, and row retrieval
 * backed by the embedded SQLite3 amalgamation.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#include "sqlite.h"
#include "../vendor/sqlite3.h"
#include <string.h>
#include <stdio.h>

GraySqlite *gray_sqlite_open(GrayArena *arena, GrayString path) {
    GraySqlite *db = (GraySqlite *)gray_arena_alloc(arena, sizeof(GraySqlite));
    sqlite3 *handle = NULL;
    int return_code =sqlite3_open(path.data, &handle);
    if (return_code !=SQLITE_OK) {
        if (handle) sqlite3_close(handle);
        db->handle = NULL;
        return db;
    }
    db->handle = handle;
    return db;
}

void gray_sqlite_close(GraySqlite *db) {
    if (db && db->handle) {
        sqlite3_close((sqlite3 *)db->handle);
        db->handle = NULL;
    }
}

bool gray_sqlite_exec(GraySqlite *db, GrayString sql) {
    if (!db || !db->handle) return false;
    char *err = NULL;
    int return_code =sqlite3_exec((sqlite3 *)db->handle, sql.data, NULL, NULL, &err);
    if (err) sqlite3_free(err);
    return return_code == SQLITE_OK;
}

GrayArray gray_sqlite_query(GrayArena *arena, GraySqlite *db, GrayString sql) {
    GrayArray rows = gray_array_new(arena, sizeof(GrayMap), 8);
    if (!db || !db->handle) return rows;

    sqlite3_stmt *stmt = NULL;
    int return_code =sqlite3_prepare_v2((sqlite3 *)db->handle, sql.data, sql.len, &stmt, NULL);
    if (return_code !=SQLITE_OK || !stmt) return rows;

    int col_count = sqlite3_column_count(stmt);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GrayMap row = gray_map_new(arena, sizeof(GrayString), sizeof(GrayString), col_count * 2);
        for (int i = 0; i < col_count; i++) {
            const char *col_name = sqlite3_column_name(stmt, i);
            GrayString key = gray_string_new(arena, col_name, (int32_t)strlen(col_name));

            const char *val_text = (const char *)sqlite3_column_text(stmt, i);
            GrayString val;
            if (val_text) {
                val = gray_string_new(arena, val_text, (int32_t)strlen(val_text));
            } else {
                val = gray_string_lit("");
            }
            GRAY_MAP_SET(arena, &row, &key, &val);
        }
        GRAY_ARRAY_PUSH(arena, &rows, &row);
    }

    sqlite3_finalize(stmt);
    return rows;
}

/* _result variants */

GrayResult_sqlite gray_sqlite_open_result(GrayArena *arena, GrayString path) {
    GrayResult_sqlite r;
    r.v0 = gray_sqlite_open(arena, path);
    if (!r.v0 || !r.v0->handle) {
        if (!r.v0) r.v0 = (GraySqlite *)gray_arena_alloc(arena, sizeof(GraySqlite));
        r.v0->handle = NULL;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "cannot open database '%s'", path.data));
    } else {
        r.v1 = NULL;
    }
    return r;
}

GrayResult_bool gray_sqlite_exec_result(GrayArena *arena, GraySqlite *db, GrayString sql) {
    GrayResult_bool r;
    if (!db || !db->handle) {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "database handle is nil"));
        return r;
    }
    char *err = NULL;
    int return_code =sqlite3_exec((sqlite3 *)db->handle, sql.data, NULL, NULL, &err);
    if (return_code !=SQLITE_OK) {
        GrayString msg = err ? gray_string_format(arena, "exec failed: %s", err)
                           : gray_string_format(arena, "exec failed (code %d)", return_code);
        if (err) sqlite3_free(err);
        r.v0 = false;
        r.v1 = gray_error_new(arena, msg);
    } else {
        if (err) sqlite3_free(err);
        r.v0 = true;
        r.v1 = NULL;
    }
    return r;
}

GrayResult_array gray_sqlite_query_result(GrayArena *arena, GraySqlite *db, GrayString sql) {
    GrayResult_array r;
    if (!db || !db->handle) {
        r.v0 = gray_array_new(arena, sizeof(GrayMap), 0);
        r.v1 = gray_error_new(arena, gray_string_format(arena, "database handle is nil"));
        return r;
    }
    sqlite3_stmt *stmt = NULL;
    int return_code =sqlite3_prepare_v2((sqlite3 *)db->handle, sql.data, sql.len, &stmt, NULL);
    if (return_code !=SQLITE_OK || !stmt) {
        r.v0 = gray_array_new(arena, sizeof(GrayMap), 0);
        const char *errmsg = sqlite3_errmsg((sqlite3 *)db->handle);
        r.v1 = gray_error_new(arena, gray_string_format(arena, "query failed: %s", errmsg ? errmsg : "unknown error"));
        return r;
    }
    sqlite3_finalize(stmt);
    r.v0 = gray_sqlite_query(arena, db, sql);
    r.v1 = NULL;
    return r;
}
