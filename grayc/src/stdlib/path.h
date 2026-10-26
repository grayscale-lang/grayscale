/*
 * path.h — Public interface for the path stdlib module.
 * Separator-aware path manipulation. Every function treats both '/' and '\'
 * as separators on every platform and recognizes Windows drive letters
 * (C:) and UNC prefixes (\\server\share). Results use the platform's
 * native separator, except to_forward_slashes. Only absolute_path touches
 * the filesystem environment (the working directory).
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_PATH_H
#define GRAY_PATH_H

#include "../runtime/runtime.h"
#include "../runtime/array.h"

typedef struct { GrayString v0; GrayString v1; } GrayPathSplitResult;
typedef struct { GrayString v0; GrayError *v1; } GrayResult_path_string;

/*@man join_parts
 *@module path
 *@group Building
 *@sig join_parts(parts [string]) -> string
 *@desc Joins the non-empty parts with the platform separator and normalizes the result. Returns an empty string when every part is empty.
 *@example
 *   import @path
 *   println(path.join_parts({"src", "stdlib", "../runtime", "io.c"}))   // src/runtime/io.c
 *@end
 */
GrayString gray_path_join_parts(GrayArena *arena, GrayArray parts);

/*@man normalize_path
 *@module path
 *@group Building
 *@sig normalize_path(p string) -> string
 *@desc Returns the shortest equivalent path: collapses repeated separators, removes "." elements, resolves ".." elements lexically, and converts separators to the platform separator. An empty result becomes ".".
 *@example
 *   import @path
 *   println(path.normalize_path("a//b/./c/../d"))   // a/b/d
 *@end
 */
GrayString gray_path_normalize_path(GrayArena *arena, GrayString path);

/*@man split_dir_file
 *@module path
 *@group Parts
 *@sig split_dir_file(p string) -> (string, string)
 *@desc Splits p after its final separator into a directory (including the trailing separator) and a file name. The directory is empty when p has no separator. Joining the two results gives p back.
 *@example
 *   import @path
 *   mut dir, file = path.split_dir_file("src/main.gray")
 *   println(dir)    // src/
 *   println(file)   // main.gray
 *@end
 */
GrayPathSplitResult gray_path_split_dir_file(GrayString path);

/*@man file_extension
 *@module path
 *@group Parts
 *@sig file_extension(p string) -> string
 *@desc Returns the suffix of the final path element starting at its last dot, including the dot. Returns an empty string when the final element has no dot.
 *@example
 *   import @path
 *   println(path.file_extension("src/main.gray"))   // .gray
 *@end
 */
GrayString gray_path_file_extension(GrayString path);

/*@man is_absolute
 *@module path
 *@group Queries
 *@sig is_absolute(p string) -> bool
 *@desc True when p starts with a separator, a UNC prefix, or a drive letter followed by a separator (such as C: then a slash). A drive letter alone (C: or C:file) is not absolute.
 *@example
 *   import @path
 *   println(path.is_absolute("/usr/bin"))     // true
 *   println(path.is_absolute("C:\\Users"))    // true
 *   println(path.is_absolute("src/main"))     // false
 *@end
 */
bool gray_path_is_absolute(GrayString path);

/*@man relative_path
 *@module path
 *@group Resolving
 *@sig relative_path(base string, target string) -> (string, Error)
 *@desc Returns a path that, joined to base, leads to target. The work is purely lexical. Returns an Error when only one of the paths is absolute, when they have different drive or UNC roots, or when base contains ".." elements that cannot be resolved.
 *@example
 *   import @path
 *   mut rel, err = path.relative_path("/a/b", "/a/c/d")
 *   println(rel)   // ../c/d
 *@end
 */
GrayResult_path_string gray_path_relative_path(GrayArena *arena, GrayString base, GrayString target);

/*@man absolute_path
 *@module path
 *@group Resolving
 *@sig absolute_path(p string) -> (string, Error)
 *@desc Returns p as a normalized absolute path, resolving a relative p against the current working directory. Returns an Error when the working directory cannot be read or when p is drive-relative (C:file).
 *@example
 *   import @path
 *   mut full, err = path.absolute_path("src/main.gray")
 *@end
 */
GrayResult_path_string gray_path_absolute_path(GrayArena *arena, GrayString path);

/*@man to_forward_slashes
 *@module path
 *@group Building
 *@sig to_forward_slashes(p string) -> string
 *@desc Returns p with every backslash replaced by a forward slash, on every platform.
 *@example
 *   import @path
 *   println(path.to_forward_slashes("src\\stdlib\\io.c"))   // src/stdlib/io.c
 *@end
 */
GrayString gray_path_to_forward_slashes(GrayArena *arena, GrayString path);

#endif /* GRAY_PATH_H */
