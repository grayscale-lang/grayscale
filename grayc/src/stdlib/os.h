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

/*@man lookup_env
 *@module os
 *@group Environment
 *@sig lookup_env(name string) -> (string, bool)
 *@desc Returns the value of the environment variable name and whether it is actually set. Unlike get_env, this distinguishes an unset variable from one explicitly set to "".
 *@example
 *   import @os
 *   mut val, ok = os.lookup_env("MY_VAR")
 *   if ok { println(val) } otherwise { println("not set") }
 *@end
 */
/* os.lookup_env(name) — get environment variable and whether it is set.
 * Layout must match the {GrayString v0; bool v1;} tuple codegen emits for
 * a multi-return stdlib call. */
typedef struct { GrayString v0; bool v1; } GrayOsLookupEnvResult;
GrayOsLookupEnvResult gray_os_lookup_env(GrayArena *arena, GrayString name);

/*@man find_executable
 *@module os
 *@group System
 *@sig find_executable(name string) -> (string, bool)
 *@desc Returns the full path of the first executable regular file called name on PATH, and whether one was found. A name containing a path separator is checked directly instead of searched. On Windows the PATHEXT extensions are tried and PATH is split on ";".
 *@example
 *   import @os
 *   mut path, found = os.find_executable("cc")
 *   if found { println(path) } otherwise { println("cc not found") }
 *@end
 */
/* os.find_executable(name) — search PATH for an executable regular file.
 * Layout must match the {GrayString v0; bool v1;} tuple codegen emits for
 * a multi-return stdlib call. */
typedef struct { GrayString v0; bool v1; } GrayOsFindExecutableResult;
GrayOsFindExecutableResult gray_os_find_executable(GrayArena *arena, GrayString name);

/*@man environ
 *@module os
 *@group Environment
 *@sig environ() -> [string]
 *@desc Returns every environment variable of the current process as an array of "KEY=VALUE" strings.
 *@example
 *   import @os
 *   for_each kv in os.environ() {
 *       println(kv)
 *   }
 *@end
 */
/* os.environ() — all environment variables as "KEY=VALUE" strings */
GrayArray gray_os_environ(GrayArena *arena);

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

/*@man home_dir
 *@module os
 *@group System
 *@sig home_dir() -> string
 *@desc Returns the current user's home directory. Reads $HOME on Unix and %USERPROFILE% on Windows.
 *@example
 *   import @os
 *   println(os.home_dir())
 *@end
 */
/* os.home_dir() — current user's home directory */
GrayString gray_os_home_dir(GrayArena *arena);

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
 *@sig pid() -> i64
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
 *@sig cpu_count() -> i64
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

/*@man ExecResult
 *@module os
 *@group Types
 *@kind type
 *@field exit_code i64
 *@field stdout string
 *@field stderr string
 *@field signal i64
 *@desc The result returned by exec: the process's exit code, the standard output and standard error it produced, and the signal that terminated it (0 when it exited normally, and always 0 on Windows). ExecResult is reserved as a type name while os is imported.
 *@example
 *   import @os
 *   mut result, err = os.exec("ls", {"-l"})
 *   println(result.exit_code)
 *   println(result.stdout)
 *   println(result.signal)
 *@end
 */
/* os.exec result: exit code, captured stdout, captured stderr. The C member
 * names for stdout/stderr carry the codegen sanitize_name prefix, because
 * those identifiers are libc macros. */
typedef struct { int64_t exit_code; GrayString _gray_stdout; GrayString _gray_stderr; int64_t signal; } GrayOsExecResult;
typedef struct { GrayOsExecResult v0; GrayError *v1; } GrayResult_os_exec;

/*@man exec
 *@module os
 *@group System
 *@sig exec(cmd string, args [string]) -> (ExecResult, Error)
 *@desc Executes the command cmd with the given args and captures its output. The Error is non-nil only when the process could not be launched; a process that runs and exits non-zero returns a nil Error and its exit_code.
 *@example
 *   import @os
 *   mut result, err = os.exec("ls", {"-l"})
 *   if err != nil {
 *       println("failed to launch: ${err.message}")
 *       return
 *   }
 *   if result.exit_code != 0 {
 *       println("exited with error: ${result.stderr}")
 *   }
 *@end
 */
GrayResult_os_exec gray_os_exec_result(GrayArena *arena, GrayString command, GrayArray args);

/*@man executable_path
 *@module os
 *@group System
 *@sig executable_path() -> (string, Error)
 *@desc Returns the absolute path of the running binary. Returns an Error with code IoFailure if the path cannot be determined and Unsupported on a platform without a way to ask for it.
 *@example
 *   import @os
 *   mut path, err = os.executable_path()
 *   if err == nil {
 *       println(path)
 *   }
 *@end
 */
typedef struct { GrayString v0; GrayError *v1; } GrayOsExecutablePathResult;
GrayOsExecutablePathResult gray_os_executable_path(GrayArena *arena);

/*@man cpu_usage
 *@module os
 *@group System
 *@sig cpu_usage(interval_ms i64) -> (f64, Error)
 *@desc Samples the system CPU time counters, waits interval_ms, samples again, and returns the busy fraction of that window as a percentage from 0.0 to 100.0, averaged across all logical CPUs. Blocks for interval_ms. Panics if interval_ms is less than 1. Returns an Error with code Unsupported on a platform without the counters (Windows) and IoFailure if they cannot be read.
 *@example
 *   import @os
 *   mut cpu, err = os.cpu_usage(250)
 *   if err == nil {
 *       println("cpu ${cpu}%")
 *   }
 *@end
 */
typedef struct { double v0; GrayError *v1; } GrayOsCpuUsageResult;
GrayOsCpuUsageResult gray_os_cpu_usage(GrayArena *arena, int64_t interval_ms);

/*@man memory_usage
 *@module os
 *@group System
 *@sig memory_usage() -> (i64, i64, Error)
 *@desc Returns (used, total) in bytes of physical memory. used is total minus the memory available to new allocations, so reclaimable cache does not count as used. Returns an Error with code Unsupported on a platform without the counters (Windows) and IoFailure if they cannot be read.
 *@example
 *   import @os
 *   mut used, total, err = os.memory_usage()
 *   if err == nil {
 *       println("memory ${used} / ${total} bytes")
 *   }
 *@end
 */
typedef struct { int64_t v0; int64_t v1; GrayError *v2; } GrayOsUsageResult;
GrayOsUsageResult gray_os_memory_usage(GrayArena *arena);

/*@man disk_usage
 *@module os
 *@group System
 *@sig disk_usage(path string) -> (i64, i64, Error)
 *@desc Returns (used, total) in bytes of the filesystem that contains path. A path that does not exist returns (0, 0) and an Error with code NotFound. Returns an Error with code Unsupported on a platform without the counters (Windows).
 *@example
 *   import @os
 *   mut used, total, err = os.disk_usage("/")
 *   if err == nil {
 *       println("disk ${used} / ${total} bytes")
 *   }
 *@end
 */
GrayOsUsageResult gray_os_disk_usage(GrayArena *arena, GrayString path);

/*@man load_average
 *@module os
 *@group System
 *@sig load_average() -> ([f64], Error)
 *@desc Returns the 1, 5 and 15 minute load averages as a three-element array. Returns an Error with code Unsupported on a platform without the counters (Windows) and IoFailure if they cannot be read.
 *@example
 *   import @os
 *   mut load, err = os.load_average()
 *   if err == nil {
 *       println("load ${load[0]} ${load[1]} ${load[2]}")
 *   }
 *@end
 */
typedef struct { GrayArray v0; GrayError *v1; } GrayOsLoadAverageResult;
GrayOsLoadAverageResult gray_os_load_average(GrayArena *arena);

#endif
