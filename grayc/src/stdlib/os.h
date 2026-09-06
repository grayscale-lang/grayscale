/*
 * os.h — Public interface for the os stdlib module.
 * Declares access to command-line args, environment variables,
 * working directory, hostname, process execution, and signal handling.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAY_OS_H
#define GRAY_OS_H

#include "../runtime/runtime.h"
#include "../runtime/array.h"

/*@man args
 *@module os
 *@group System
 *@sig args() -> [string]
 *@desc Returns the command-line arguments passed to the program as an array of strings. The first element is the program name.
 *@example
 *   import @os
 *   mut argv = os.args()
 *   println(argv[0])
 *@end
 */
/* os.args() — return command-line arguments as [string] */
GrayArray gray_os_args(GrayArena *arena);

/*@man get_env
 *@module os
 *@group Environment
 *@sig get_env(name string) -> string
 *@desc Returns the value of the environment variable name. Returns an empty string if the variable is not set.
 *@example
 *   import @os
 *   mut home = os.get_env("HOME")
 *   println(home)
 *@end
 */
/* os.get_env(name) — get environment variable */
GrayString gray_os_get_env(GrayArena *arena, GrayString name);

/*@man set_env
 *@module os
 *@group Environment
 *@sig set_env(name string, value string)
 *@desc Sets the environment variable name to value for the current process.
 *@example
 *   import @os
 *   os.set_env("MY_VAR", "hello")
 *   println(os.get_env("MY_VAR"))
 *@end
 */
/* os.set_env(name, value) */
void gray_os_set_env(GrayString name, GrayString value);

/*@man unset_env
 *@module os
 *@group Environment
 *@sig unset_env(name string)
 *@desc Removes the environment variable with the given name from the current process. Does nothing if the variable is not set.
 *@example
 *   import @os
 *   os.set_env("MY_VAR", "hello")
 *   os.unset_env("MY_VAR")
 *   println(os.get_env("MY_VAR"))  // ""
 *@end
 */
/* os.unset_env(name) — remove environment variable */
void gray_os_unset_env(GrayString name);

/*@man current_dir
 *@module os
 *@group System
 *@sig current_dir() -> string
 *@desc Returns the current working directory of the process.
 *@example
 *   import @os
 *   mut cwd = os.current_dir()
 *   println(cwd)
 *@end
 */
/* os.current_dir() — current working directory */
GrayString gray_os_cwd(GrayArena *arena);

/*@man hostname
 *@module os
 *@group System
 *@sig hostname() -> string
 *@desc Returns the hostname of the machine.
 *@example
 *   import @os
 *   println(os.hostname())
 *@end
 */
/* os.hostname() */
GrayString gray_os_hostname(GrayArena *arena);

/*@man Platform
 *@module os
 *@group Types
 *@kind type
 *@field MAC_OS
 *@field LINUX
 *@field WINDOWS
 *@field OTHER
 *@desc Host operating system. Reachable as os.MAC_OS or Platform.MAC_OS (same value). Underlying values: MAC_OS 0, LINUX 1, WINDOWS 2, OTHER 3.
 *@end
 */

/*@man current_os
 *@module os
 *@group System
 *@sig current_os() -> Platform
 *@desc Returns the current operating system as a Platform enum value.
 *@example
 *   import @os
 *   when os.current_os() {
 *       is .LINUX { println("running on Linux") }
 *       default   { }
 *   }
 *@end
 */
/* os.current_os() — returns MAC_OS=0, LINUX=1, WINDOWS=2, OTHER=3 */
int64_t gray_os_current_os(void);

/*@man arch
 *@module os
 *@group System
 *@sig arch() -> string
 *@desc Returns the CPU architecture of the current machine, such as "arm64" or "x86_64".
 *@example
 *   import @os
 *   println(os.arch())
 *@end
 */
/* os.arch() — "arm64", "x86_64", etc. */
GrayString gray_os_arch(void);

/*@man pid
 *@module os
 *@group System
 *@sig pid() -> int
 *@desc Returns the process ID of the current process.
 *@example
 *   import @os
 *   println(os.pid())
 *@end
 */
/* os.pid() */
int64_t gray_os_pid(void);

/*@man cpu_count
 *@module os
 *@group System
 *@sig cpu_count() -> int
 *@desc Returns the number of logical CPUs available to the process. Falls back to 1 if the count cannot be determined.
 *@example
 *   import @os
 *   println(os.cpu_count())
 *@end
 */
int64_t gray_os_cpu_count(void);

/*@man is_tty
 *@module os
 *@group System
 *@sig is_tty() -> bool
 *@desc Returns true if standard output is connected to a terminal, false when it is redirected to a file or pipe.
 *@example
 *   import @os
 *   if os.is_tty() { println("interactive") }
 *@end
 */
bool gray_os_is_tty(void);

/* Store argc/argv from main for os.args() */
void gray_os_init(int argc, char **argv);

/*@man exec
 *@module os
 *@group System
 *@sig exec(cmd string, args [string]) -> (int, string, string, bool)
 *@desc Executes the command cmd with the given args. Returns (exit_code, stdout, stderr, ok). ok is false if the process could not be launched.
 *@example
 *   import @os
 *   mut code, stdout, stderr, ok = os.exec("ls", {"-l"})
 *   if ok && code == 0 {
 *       println(stdout)
 *   }
 *@end
 */
/* os.exec(cmd, args) — run a process, capture stdout and stderr, return (exit_code, stdout, stderr, ok) */
typedef struct { int64_t v0; GrayString v1; GrayString v2; bool v3; } GrayOsExecResult;
GrayOsExecResult gray_os_exec(GrayArena *arena, GrayString cmd, GrayArray args);

#endif
