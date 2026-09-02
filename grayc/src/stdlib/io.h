/*
 * io.h — Public interface for the io stdlib module.
 * Declares file read/write, path manipulation, directory operations,
 * globbing, and result types shared across other stdlib modules.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_IO_H
#define GRAY_IO_H

#include "../runtime/runtime.h"
#include "../runtime/array.h"
#include <stdio.h>

/* File reading */

/*@man read_file
 *@module io
 *@group File Reading
 *@sig read_file(path string) -> (string, Error)
 *@desc Reads the entire file at path and returns its contents as a string. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut content, err = io.read_file("data.txt")
 *   if err != nil { println("failed: ${err}") }
 *   mut content, _ = io.read_file("data.txt")
 *@end
 */
GrayString gray_io_read_file(GrayArena *arena, GrayString path);

/*@man read_bytes
 *@module io
 *@group File Reading
 *@sig read_bytes(path string) -> ([byte], Error)
 *@desc Reads the entire file at path and returns its contents as a byte array. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut data, err = io.read_bytes("image.png")
 *@end
 */
GrayArray  gray_io_read_bytes(GrayArena *arena, GrayString path);

/*@man read_lines
 *@module io
 *@group File Reading
 *@sig read_lines(path string) -> ([string], Error)
 *@desc Reads the file at path and returns its contents split into lines. CR/LF and LF line endings are stripped. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut lines, err = io.read_lines("data.txt")
 *   for_each line in lines {
 *       println(line)
 *   }
 *@end
 */
GrayArray  gray_io_read_lines(GrayArena *arena, GrayString path);

/*@man file_exists
 *@module io
 *@group File Operations
 *@sig file_exists(path string) -> bool
 *@desc Returns true if a file or directory exists at path. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   if io.file_exists("config.txt") {
 *       println("found")
 *   }
 *@end
 */
bool gray_io_file_exists(GrayString path);

/*@man is_file
 *@module io
 *@group File Operations
 *@sig is_file(path string) -> bool
 *@desc Returns true if path exists and is a regular file (not a directory or special file). Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   println(io.is_file("data.txt"))
 *@end
 */
bool gray_io_is_file(GrayString path);

/*@man is_directory
 *@module io
 *@group File Operations
 *@sig is_directory(path string) -> bool
 *@desc Returns true if path exists and is a directory. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   println(io.is_directory("/tmp"))
 *@end
 */
bool gray_io_is_directory(GrayString path);

/*@man file_size
 *@module io
 *@group File Operations
 *@sig file_size(path string) -> (int, Error)
 *@desc Returns the size of the file at path in bytes. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut size, _ = io.file_size("data.txt")
 *   println(size)
 *@end
 */
int64_t gray_io_file_size(GrayString path);

/* Not a builtin: reads an already-opened file into a GrayString, sizing
 * safely (guards fseek/ftell, caps at INT32_MAX, streams non-seekable
 * input). Returns {NULL, -1} if the file is too large. Shared with
 * csv.c so csv.read_file doesn't re-derive this on its own. */
GrayString gray_io_read_file_impl(GrayArena *arena, FILE *f);

/* File writing */

/*@man write_file
 *@module io
 *@group File Writing
 *@sig write_file(path string, content string) -> (bool, Error)
 *@desc Writes content to the file at path, creating it if it does not exist or overwriting it if it does. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut ok, _ = io.write_file("out.txt", "hello\n")
 *@end
 */
bool gray_io_write_file(GrayString path, GrayString content);

/*@man append_file
 *@module io
 *@group File Writing
 *@sig append_file(path string, content string) -> (bool, Error)
 *@desc Appends content to the end of the file at path. Creates the file if it does not exist. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut ok, _ = io.append_file("log.txt", "new entry\n")
 *@end
 */
bool gray_io_append_file(GrayString path, GrayString content);

/*@man write_bytes
 *@module io
 *@group File Writing
 *@sig write_bytes(path string, data [byte]) -> (bool, Error)
 *@desc Writes a byte array to the file at path, creating it if it does not exist or overwriting it if it does. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut data [byte] = {1, 2, 3}
 *   mut ok, _ = io.write_bytes("out.bin", data)
 *@end
 */
bool gray_io_write_bytes(GrayString path, GrayArray data);

/*@man append_bytes
 *@module io
 *@group File Writing
 *@sig append_bytes(path string, data [byte]) -> (bool, Error)
 *@desc Appends a byte array to the end of the file at path. Creates the file if it does not exist. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut data [byte] = {1, 2, 3}
 *   mut ok, _ = io.append_bytes("log.bin", data)
 *@end
 */
bool gray_io_append_bytes(GrayString path, GrayArray data);

/* File operations */

/*@man delete_file
 *@module io
 *@group File Operations
 *@sig delete_file(path string) -> (bool, Error)
 *@desc Deletes the file at path. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut ok, _ = io.delete_file("tmp.txt")
 *@end
 */
bool gray_io_delete_file(GrayString path);

/*@man rename_file
 *@module io
 *@group File Operations
 *@sig rename_file(old_path string, new_path string) -> (bool, Error)
 *@desc Renames (or moves) the file at old_path to new_path. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut ok, _ = io.rename_file("old.txt", "new.txt")
 *@end
 */
bool gray_io_rename_file(GrayString old_path, GrayString new_path);

/*@man copy_file
 *@module io
 *@group File Operations
 *@sig copy_file(src string, dst string) -> (bool, Error)
 *@desc Copies the file at src to dst. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut ok, _ = io.copy_file("src.txt", "dst.txt")
 *@end
 */
bool gray_io_copy_file(GrayString src, GrayString dst);

/*@man move_file
 *@module io
 *@group File Operations
 *@sig move_file(src string, dst string) -> (bool, Error)
 *@desc Moves the file at src to dst. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut ok, _ = io.move_file("tmp/file.txt", "final/file.txt")
 *@end
 */
bool gray_io_move_file(GrayString src, GrayString dst);

/* Directory operations */

/*@man list_dir
 *@module io
 *@group Directory Operations
 *@sig list_dir(path string) -> ([string], Error)
 *@desc Returns the names of all entries in the directory at path. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut entries, _ = io.list_dir(".")
 *   for_each e in entries {
 *       println(e)
 *   }
 *@end
 */
GrayArray gray_io_list_dir(GrayArena *arena, GrayString path);

/*@man make_dir
 *@module io
 *@group Directory Operations
 *@sig make_dir(path string) -> (bool, Error)
 *@desc Creates the directory at path. Fails if the parent directory does not exist. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut ok, _ = io.make_dir("output")
 *@end
 */
bool gray_io_make_dir(GrayString path);

/*@man make_dir_all
 *@module io
 *@group Directory Operations
 *@sig make_dir_all(path string) -> (bool, Error)
 *@desc Creates the directory at path, including all missing parent directories. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut ok, _ = io.make_dir_all("a/b/c")
 *@end
 */
bool gray_io_make_dir_all(GrayString path);

/*@man remove_dir
 *@module io
 *@group Directory Operations
 *@sig remove_dir(path string) -> (bool, Error)
 *@desc Removes the empty directory at path. Fails if the directory is not empty. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut ok, _ = io.remove_dir("empty_dir")
 *@end
 */
bool gray_io_remove_dir(GrayString path);

/*@man remove_dir_all
 *@module io
 *@group Directory Operations
 *@sig remove_dir_all(path string) -> (bool, Error)
 *@desc Removes the directory at path and all of its contents recursively. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut ok, _ = io.remove_dir_all("build")
 *@end
 */
bool gray_io_remove_dir_all(GrayString path);

/*@man walk
 *@module io
 *@group Directory Operations
 *@sig walk(path string) -> ([string], Error)
 *@desc Recursively lists all files under path. Returns full paths relative to path. Always use destructuring — single-variable assignment is a compile error. Relative paths resolve from the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut files, _ = io.walk("src")
 *   for_each f in files {
 *       println(f)
 *   }
 *@end
 */
GrayArray gray_io_walk(GrayArena *arena, GrayString path);

/*@man glob
 *@module io
 *@group Directory Operations
 *@sig glob(pattern string) -> ([string], Error)
 *@desc Returns all file paths matching the glob pattern. Returns an empty array if there are no matches. Always use destructuring — single-variable assignment is a compile error. Glob patterns are matched relative to the working directory where the binary is executed, not the source file location.
 *@example
 *   import @io
 *   mut files, _ = io.glob("src/\*.gray")
 *@end
 */
GrayArray gray_io_glob(GrayArena *arena, GrayString pattern);

/* Temporary files */

/*@man temp_file
 *@module io
 *@group Temporary Files
 *@sig temp_file() -> (string, Error)
 *@desc Creates a temporary file and returns its path. The file is automatically deleted when the program exits. Always use destructuring — single-variable assignment is a compile error.
 *@example
 *   import @io
 *   mut path, _ = io.temp_file()
 *   mut _, _ = io.write_file(path, "scratch data")
 *@end
 */
GrayString gray_io_temp_file(GrayArena *arena);

/*@man temp_dir
 *@module io
 *@group Temporary Files
 *@sig temp_dir() -> (string, Error)
 *@desc Creates a temporary directory and returns its path. The directory and all its contents are automatically deleted when the program exits. Always use destructuring — single-variable assignment is a compile error.
 *@example
 *   import @io
 *   mut path, _ = io.temp_dir()
 *   mut _, _ = io.write_file(io.path_join({path, "data.txt"}), "hello")
 *@end
 */
GrayString gray_io_temp_dir(GrayArena *arena);

/* Path manipulation */

/*@man path_join
 *@module io
 *@group Path
 *@sig path_join(parts [string]) -> string
 *@desc Joins path segments into a single path. An absolute segment replaces everything accumulated so far.
 *@example
 *   import @io
 *   println(io.path_join({"/home", "user", "docs"}))
 *@end
 */
GrayString gray_io_path_join(GrayArena *arena, GrayArray parts);

/*@man dirname
 *@module io
 *@group Path
 *@sig dirname(path string) -> string
 *@desc Returns the parent directory component of path.
 *@example
 *   import @io
 *   println(io.dirname("/home/user/file.txt"))
 *@end
 */
GrayString gray_io_dirname(GrayArena *arena, GrayString path);

/*@man basename
 *@module io
 *@group Path
 *@sig basename(path string) -> string
 *@desc Returns the filename component of path, without any leading directory.
 *@example
 *   import @io
 *   println(io.basename("/home/user/file.txt"))
 *@end
 */
GrayString gray_io_basename(GrayArena *arena, GrayString path);

/*@man extension
 *@module io
 *@group Path
 *@sig extension(path string) -> string
 *@desc Returns the file extension of path including the leading dot (e.g. ".txt"). Returns an empty string if there is no extension.
 *@example
 *   import @io
 *   println(io.extension("report.pdf"))
 *@end
 */
GrayString gray_io_extension(GrayArena *arena, GrayString path);

/*@man is_absolute
 *@module io
 *@group Path
 *@sig is_absolute(path string) -> bool
 *@desc Returns true if path is an absolute path.
 *@example
 *   import @io
 *   println(io.is_absolute("/etc/hosts"))
 *   println(io.is_absolute("relative/path"))
 *@end
 */
bool gray_io_is_absolute(GrayString path);

/*@man normalize
 *@module io
 *@group Path
 *@sig normalize(path string) -> string
 *@desc Cleans and normalizes path by resolving ".", "..", and redundant separators.
 *@example
 *   import @io
 *   println(io.normalize("a/b/../c"))
 *@end
 */
GrayString gray_io_normalize(GrayArena *arena, GrayString path);

/* Tuple-returning versions for (value, Error) pattern */
typedef struct { GrayString v0; GrayError *v1; } GrayResult_string;
#ifndef GRAY_RESULT_BOOL_DEFINED
#define GRAY_RESULT_BOOL_DEFINED
typedef struct { bool v0; GrayError *v1; } GrayResult_bool;
#endif
typedef struct { GrayArray v0; GrayError *v1; } GrayResult_array;

GrayResult_string gray_io_read_file_result(GrayArena *arena, GrayString path);
GrayResult_array  gray_io_read_bytes_result(GrayArena *arena, GrayString path);
GrayResult_array  gray_io_read_lines_result(GrayArena *arena, GrayString path);
GrayResult_int    gray_io_file_size_result(GrayArena *arena, GrayString path);
GrayResult_bool gray_io_write_file_result(GrayArena *arena, GrayString path, GrayString content);
GrayResult_bool gray_io_delete_file_result(GrayArena *arena, GrayString path);
GrayResult_bool gray_io_append_file_result(GrayArena *arena, GrayString path, GrayString content);
GrayResult_bool gray_io_rename_file_result(GrayArena *arena, GrayString old_path, GrayString new_path);
GrayResult_bool gray_io_copy_file_result(GrayArena *arena, GrayString src, GrayString dst);
GrayResult_bool gray_io_move_file_result(GrayArena *arena, GrayString src, GrayString dst);
GrayResult_array gray_io_glob_result(GrayArena *arena, GrayString pattern);
GrayResult_array gray_io_list_dir_result(GrayArena *arena, GrayString path);
GrayResult_bool gray_io_make_dir_result(GrayArena *arena, GrayString path);
GrayResult_bool gray_io_make_dir_all_result(GrayArena *arena, GrayString path);
GrayResult_bool gray_io_remove_dir_result(GrayArena *arena, GrayString path);
GrayResult_bool gray_io_remove_dir_all_result(GrayArena *arena, GrayString path);
GrayResult_array gray_io_walk_result(GrayArena *arena, GrayString path);
GrayResult_bool gray_io_write_bytes_result(GrayArena *arena, GrayString path, GrayArray data);
GrayResult_bool gray_io_append_bytes_result(GrayArena *arena, GrayString path, GrayArray data);
GrayResult_string gray_io_temp_file_result(GrayArena *arena);
GrayResult_string gray_io_temp_dir_result(GrayArena *arena);

/*@man OpenFlag
 *@module io
 *@group Types
 *@kind type
 *@field O_RDONLY
 *@field O_WRONLY
 *@field O_RDWR
 *@desc Mutually-exclusive open modes. Reachable as io.O_RDONLY or OpenFlag.O_RDONLY (same value). Underlying values: O_RDONLY 0, O_WRONLY 1, O_RDWR 2.
 *@example
 *   import @io
 *   mut mode OpenFlag = io.O_RDWR
 *   when mode { is .O_RDONLY { } is .O_WRONLY { } is .O_RDWR { } default { } }
 *@end
 */

#endif
