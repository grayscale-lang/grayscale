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
    int rc =sqlite3_open(path.data, &handle);
    if (rc !=SQLITE_OK) {
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
    int rc =sqlite3_exec((sqlite3 *)db->handle, sql.data, NULL, NULL, &err);
    if (err) sqlite3_free(err);
    return rc == SQLITE_OK;
}

/* Bind all parameters from a [string] array to a prepared statement. */
static int bind_string_params(sqlite3_stmt *stmt, GrayArray params) {
    for (int32_t i = 0; i < params.len; i++) {
        GrayString *s = (GrayString *)((char *)params.data + i * params.elem_size);
        int rc = sqlite3_bind_text(stmt, i + 1, s->data, s->len, SQLITE_STATIC);
        if (rc != SQLITE_OK) return rc;
    }
    return SQLITE_OK;
}

bool gray_sqlite_exec_params(GraySqlite *db, GrayString sql, GrayArray params) {
    if (!db || !db->handle) return false;
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2((sqlite3 *)db->handle, sql.data, sql.len, &stmt, NULL);
    if (rc != SQLITE_OK || !stmt) return false;
    rc = bind_string_params(stmt, params);
    if (rc != SQLITE_OK) { sqlite3_finalize(stmt); return false; }
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

/* Step through a prepared statement and collect all result rows into a GrayArray
 * of GrayMap. Caller is responsible for sqlite3_finalize. */
static GrayArray sqlite_collect_rows(GrayArena *arena, sqlite3_stmt *stmt) {
    int col_count = sqlite3_column_count(stmt);
    GrayArray rows = gray_array_new(arena, sizeof(GrayMap), 8);

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

    return rows;
}

GrayArray gray_sqlite_query(GrayArena *arena, GraySqlite *db, GrayString sql) {
    if (!db || !db->handle) return gray_array_new(arena, sizeof(GrayMap), 8);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2((sqlite3 *)db->handle, sql.data, sql.len, &stmt, NULL);
    if (rc != SQLITE_OK || !stmt) return gray_array_new(arena, sizeof(GrayMap), 8);

    GrayArray rows = sqlite_collect_rows(arena, stmt);
    sqlite3_finalize(stmt);
    return rows;
}

GrayArray gray_sqlite_query_params(GrayArena *arena, GraySqlite *db, GrayString sql, GrayArray params) {
    if (!db || !db->handle) return gray_array_new(arena, sizeof(GrayMap), 8);

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2((sqlite3 *)db->handle, sql.data, sql.len, &stmt, NULL);
    if (rc != SQLITE_OK || !stmt) return gray_array_new(arena, sizeof(GrayMap), 8);

    rc = bind_string_params(stmt, params);
    if (rc != SQLITE_OK) { sqlite3_finalize(stmt); return gray_array_new(arena, sizeof(GrayMap), 8); }

    GrayArray rows = sqlite_collect_rows(arena, stmt);
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
    int rc =sqlite3_exec((sqlite3 *)db->handle, sql.data, NULL, NULL, &err);
    if (rc !=SQLITE_OK) {
        GrayString msg = err ? gray_string_format(arena, "exec failed: %s", err)
                           : gray_string_format(arena, "exec failed (code %d)", rc);
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

GrayResult_bool gray_sqlite_exec_params_result(GrayArena *arena, GraySqlite *db, GrayString sql, GrayArray params) {
    GrayResult_bool r;
    if (!db || !db->handle) {
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "database handle is nil"));
        return r;
    }
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2((sqlite3 *)db->handle, sql.data, sql.len, &stmt, NULL);
    if (rc != SQLITE_OK || !stmt) {
        const char *errmsg = sqlite3_errmsg((sqlite3 *)db->handle);
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "exec_params failed: %s", errmsg ? errmsg : "unknown error"));
        return r;
    }
    rc = bind_string_params(stmt, params);
    if (rc != SQLITE_OK) {
        const char *errmsg = sqlite3_errmsg((sqlite3 *)db->handle);
        sqlite3_finalize(stmt);
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "exec_params bind failed: %s", errmsg ? errmsg : "unknown error"));
        return r;
    }
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        const char *errmsg = sqlite3_errmsg((sqlite3 *)db->handle);
        r.v0 = false;
        r.v1 = gray_error_new(arena, gray_string_format(arena, "exec_params failed: %s", errmsg ? errmsg : "unknown error"));
    } else {
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
    int rc = sqlite3_prepare_v2((sqlite3 *)db->handle, sql.data, sql.len, &stmt, NULL);
    if (rc != SQLITE_OK || !stmt) {
        r.v0 = gray_array_new(arena, sizeof(GrayMap), 0);
        const char *errmsg = sqlite3_errmsg((sqlite3 *)db->handle);
        r.v1 = gray_error_new(arena, gray_string_format(arena, "query failed: %s", errmsg ? errmsg : "unknown error"));
        return r;
    }
    r.v0 = sqlite_collect_rows(arena, stmt);
    sqlite3_finalize(stmt);
    r.v1 = NULL;
    return r;
}

GrayResult_array gray_sqlite_query_params_result(GrayArena *arena, GraySqlite *db, GrayString sql, GrayArray params) {
    GrayResult_array r;
    if (!db || !db->handle) {
        r.v0 = gray_array_new(arena, sizeof(GrayMap), 0);
        r.v1 = gray_error_new(arena, gray_string_format(arena, "database handle is nil"));
        return r;
    }
    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2((sqlite3 *)db->handle, sql.data, sql.len, &stmt, NULL);
    if (rc != SQLITE_OK || !stmt) {
        r.v0 = gray_array_new(arena, sizeof(GrayMap), 0);
        const char *errmsg = sqlite3_errmsg((sqlite3 *)db->handle);
        r.v1 = gray_error_new(arena, gray_string_format(arena, "query_params failed: %s", errmsg ? errmsg : "unknown error"));
        return r;
    }
    rc = bind_string_params(stmt, params);
    if (rc != SQLITE_OK) {
        const char *errmsg = sqlite3_errmsg((sqlite3 *)db->handle);
        sqlite3_finalize(stmt);
        r.v0 = gray_array_new(arena, sizeof(GrayMap), 0);
        r.v1 = gray_error_new(arena, gray_string_format(arena, "query_params bind failed: %s", errmsg ? errmsg : "unknown error"));
        return r;
    }
    r.v0 = sqlite_collect_rows(arena, stmt);
    sqlite3_finalize(stmt);
    r.v1 = NULL;
    return r;
}
