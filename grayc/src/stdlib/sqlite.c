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
    GraySqlite *database = (GraySqlite *)gray_arena_alloc(arena, sizeof(GraySqlite));
    sqlite3 *handle = NULL;
    int result_code =sqlite3_open(path.data, &handle);
    if (result_code !=SQLITE_OK) {
        if (handle) sqlite3_close(handle);
        database->handle = NULL;
        return database;
    }
    database->handle = handle;
    return database;
}

void gray_sqlite_close(GraySqlite *database) {
    if (database && database->handle) {
        sqlite3_close((sqlite3 *)database->handle);
        database->handle = NULL;
    }
}

bool gray_sqlite_exec(GraySqlite *database, GrayString sql_text) {
    if (!database || !database->handle) return false;
    char *error_text = NULL;
    int result_code =sqlite3_exec((sqlite3 *)database->handle, sql_text.data, NULL, NULL, &error_text);
    if (error_text) sqlite3_free(error_text);
    return result_code == SQLITE_OK;
}

/* Bind all parameters from a [string] array to a prepared statement. */
static int bind_string_parameters(sqlite3_stmt *statement, GrayArray parameters) {
    for (int32_t i = 0; i < parameters.len; i++) {
        GrayString *parameter_string = (GrayString *)((char *)parameters.data + i * parameters.elem_size);
        int result_code = sqlite3_bind_text(statement, i + 1, parameter_string->data, parameter_string->len, SQLITE_STATIC);
        if (result_code != SQLITE_OK) return result_code;
    }
    return SQLITE_OK;
}

bool gray_sqlite_exec_params(GraySqlite *database, GrayString sql_text, GrayArray parameters) {
    if (!database || !database->handle) return false;
    sqlite3_stmt *statement = NULL;
    int result_code = sqlite3_prepare_v2((sqlite3 *)database->handle, sql_text.data, sql_text.len, &statement, NULL);
    if (result_code != SQLITE_OK || !statement) return false;
    result_code = bind_string_parameters(statement, parameters);
    if (result_code != SQLITE_OK) { sqlite3_finalize(statement); return false; }
    result_code = sqlite3_step(statement);
    sqlite3_finalize(statement);
    return result_code == SQLITE_DONE;
}

/* Step through a prepared statement and collect all result rows into a GrayArray
 * of GrayMap. Caller is responsible for sqlite3_finalize. */
static GrayArray sqlite_collect_rows(GrayArena *arena, sqlite3_stmt *statement) {
    int column_count = sqlite3_column_count(statement);
    GrayArray rows = gray_array_new(arena, sizeof(GrayMap), 8, GRAY_ELEM_MAP);

    while (sqlite3_step(statement) == SQLITE_ROW) {
        GrayMap row_map = gray_map_new_kind(arena, sizeof(GrayString), sizeof(GrayString), column_count * 2, GRAY_ELEM_STRING, GRAY_ELEM_STRING);
        for (int i = 0; i < column_count; i++) {
            const char *column_name = sqlite3_column_name(statement, i);
            GrayString key = gray_string_new(arena, column_name, (int32_t)strlen(column_name));

            const char *value_text = (const char *)sqlite3_column_text(statement, i);
            GrayString value;
            if (value_text) {
                value = gray_string_new(arena, value_text, (int32_t)strlen(value_text));
            } else {
                value = gray_string_lit("");
            }
            GRAY_MAP_SET(arena, &row_map, &key, &value);
        }
        GRAY_ARRAY_PUSH(arena, &rows, &row_map);
    }

    return rows;
}

GrayArray gray_sqlite_query(GrayArena *arena, GraySqlite *database, GrayString sql_text) {
    if (!database || !database->handle) return gray_array_new(arena, sizeof(GrayMap), 8, GRAY_ELEM_MAP);

    sqlite3_stmt *statement = NULL;
    int result_code = sqlite3_prepare_v2((sqlite3 *)database->handle, sql_text.data, sql_text.len, &statement, NULL);
    if (result_code != SQLITE_OK || !statement) return gray_array_new(arena, sizeof(GrayMap), 8, GRAY_ELEM_MAP);

    GrayArray rows = sqlite_collect_rows(arena, statement);
    sqlite3_finalize(statement);
    return rows;
}

GrayArray gray_sqlite_query_params(GrayArena *arena, GraySqlite *database, GrayString sql_text, GrayArray parameters) {
    if (!database || !database->handle) return gray_array_new(arena, sizeof(GrayMap), 8, GRAY_ELEM_MAP);

    sqlite3_stmt *statement = NULL;
    int result_code = sqlite3_prepare_v2((sqlite3 *)database->handle, sql_text.data, sql_text.len, &statement, NULL);
    if (result_code != SQLITE_OK || !statement) return gray_array_new(arena, sizeof(GrayMap), 8, GRAY_ELEM_MAP);

    result_code = bind_string_parameters(statement, parameters);
    if (result_code != SQLITE_OK) { sqlite3_finalize(statement); return gray_array_new(arena, sizeof(GrayMap), 8, GRAY_ELEM_MAP); }

    GrayArray rows = sqlite_collect_rows(arena, statement);
    sqlite3_finalize(statement);
    return rows;
}

/* _result variants */

GrayResult_sqlite gray_sqlite_open_result(GrayArena *arena, GrayString path) {
    GrayResult_sqlite result;
    result.v0 = gray_sqlite_open(arena, path);
    if (!result.v0 || !result.v0->handle) {
        if (!result.v0) result.v0 = (GraySqlite *)gray_arena_alloc(arena, sizeof(GraySqlite));
        result.v0->handle = NULL;
        result.v1 = gray_error_new(arena, GRAY_ERR_IoFailure, gray_string_format(arena, "cannot open database '%s'", path.data));
    } else {
        result.v1 = NULL;
    }
    return result;
}

GrayResult_bool gray_sqlite_exec_result(GrayArena *arena, GraySqlite *database, GrayString sql_text) {
    GrayResult_bool result;
    if (!database || !database->handle) {
        result.v0 = false;
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena, "database handle is nil"));
        return result;
    }
    char *error_text = NULL;
    int result_code =sqlite3_exec((sqlite3 *)database->handle, sql_text.data, NULL, NULL, &error_text);
    if (result_code !=SQLITE_OK) {
        GrayString message = error_text ? gray_string_format(arena, "exec failed: %s", error_text)
                           : gray_string_format(arena, "exec failed (code %d)", result_code);
        if (error_text) sqlite3_free(error_text);
        result.v0 = false;
        result.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, message);
    } else {
        if (error_text) sqlite3_free(error_text);
        result.v0 = true;
        result.v1 = NULL;
    }
    return result;
}

GrayResult_bool gray_sqlite_exec_params_result(GrayArena *arena, GraySqlite *database, GrayString sql_text, GrayArray parameters) {
    GrayResult_bool result;
    if (!database || !database->handle) {
        result.v0 = false;
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena, "database handle is nil"));
        return result;
    }
    sqlite3_stmt *statement = NULL;
    int result_code = sqlite3_prepare_v2((sqlite3 *)database->handle, sql_text.data, sql_text.len, &statement, NULL);
    if (result_code != SQLITE_OK || !statement) {
        const char *errmsg = sqlite3_errmsg((sqlite3 *)database->handle);
        result.v0 = false;
        result.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "exec_params failed: %s", errmsg ? errmsg : "unknown error"));
        return result;
    }
    result_code = bind_string_parameters(statement, parameters);
    if (result_code != SQLITE_OK) {
        const char *errmsg = sqlite3_errmsg((sqlite3 *)database->handle);
        sqlite3_finalize(statement);
        result.v0 = false;
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena, "exec_params bind failed: %s", errmsg ? errmsg : "unknown error"));
        return result;
    }
    result_code = sqlite3_step(statement);
    sqlite3_finalize(statement);
    if (result_code != SQLITE_DONE) {
        const char *errmsg = sqlite3_errmsg((sqlite3 *)database->handle);
        result.v0 = false;
        result.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "exec_params failed: %s", errmsg ? errmsg : "unknown error"));
    } else {
        result.v0 = true;
        result.v1 = NULL;
    }
    return result;
}

GrayResult_array gray_sqlite_query_result(GrayArena *arena, GraySqlite *database, GrayString sql_text) {
    GrayResult_array result;
    if (!database || !database->handle) {
        result.v0 = gray_array_new(arena, sizeof(GrayMap), 0, GRAY_ELEM_MAP);
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena, "database handle is nil"));
        return result;
    }
    sqlite3_stmt *statement = NULL;
    int result_code = sqlite3_prepare_v2((sqlite3 *)database->handle, sql_text.data, sql_text.len, &statement, NULL);
    if (result_code != SQLITE_OK || !statement) {
        result.v0 = gray_array_new(arena, sizeof(GrayMap), 0, GRAY_ELEM_MAP);
        const char *errmsg = sqlite3_errmsg((sqlite3 *)database->handle);
        result.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "query failed: %s", errmsg ? errmsg : "unknown error"));
        return result;
    }
    result.v0 = sqlite_collect_rows(arena, statement);
    sqlite3_finalize(statement);
    result.v1 = NULL;
    return result;
}

GrayResult_array gray_sqlite_query_params_result(GrayArena *arena, GraySqlite *database, GrayString sql_text, GrayArray parameters) {
    GrayResult_array result;
    if (!database || !database->handle) {
        result.v0 = gray_array_new(arena, sizeof(GrayMap), 0, GRAY_ELEM_MAP);
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena, "database handle is nil"));
        return result;
    }
    sqlite3_stmt *statement = NULL;
    int result_code = sqlite3_prepare_v2((sqlite3 *)database->handle, sql_text.data, sql_text.len, &statement, NULL);
    if (result_code != SQLITE_OK || !statement) {
        result.v0 = gray_array_new(arena, sizeof(GrayMap), 0, GRAY_ELEM_MAP);
        const char *errmsg = sqlite3_errmsg((sqlite3 *)database->handle);
        result.v1 = gray_error_new(arena, GRAY_ERR_ParseFailure, gray_string_format(arena, "query_params failed: %s", errmsg ? errmsg : "unknown error"));
        return result;
    }
    result_code = bind_string_parameters(statement, parameters);
    if (result_code != SQLITE_OK) {
        const char *errmsg = sqlite3_errmsg((sqlite3 *)database->handle);
        sqlite3_finalize(statement);
        result.v0 = gray_array_new(arena, sizeof(GrayMap), 0, GRAY_ELEM_MAP);
        result.v1 = gray_error_new(arena, GRAY_ERR_InvalidInput, gray_string_format(arena, "query_params bind failed: %s", errmsg ? errmsg : "unknown error"));
        return result;
    }
    result.v0 = sqlite_collect_rows(arena, statement);
    sqlite3_finalize(statement);
    result.v1 = NULL;
    return result;
}
