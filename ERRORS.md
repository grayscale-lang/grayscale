# Grayscale Error Code Reference

> Auto-generated from `grayc/src/util/error_codes.h`. Do not edit manually.
> Run `./scripts/generate_errors.gray` to regenerate.

**Total: 382 codes** (261 errors, 17 warnings, 104 panics)

---

## Errors

| Code | Category | Description |
|------|----------|-------------|
| `E1003` | syntax | unclosed multi-line comment; add */ |
| `E1005` | syntax | unclosed character; add a closing single quote |
| `E1006` | syntax | invalid escape sequence in string; valid escapes are \\n \\t \\\\ \\\" and \\x |
| `E1007` | syntax | invalid escape sequence in character |
| `E1010` | syntax | invalid number format; hex (0x), octal (0o), or binary (0b) prefix must be followed by digits |
| `E1011` | syntax | number cannot have consecutive underscores |
| `E1012` | syntax | numeric literals cannot start with an underscore; did you mean '%s'? |
| `E1013` | syntax | number cannot end with an underscore |
| `E1014` | syntax | number cannot have an underscore before the decimal point |
| `E1015` | syntax | number cannot have an underscore after the decimal point |
| `E1016` | syntax | number cannot end with a trailing decimal point; add a digit after the dot |
| `E1017` | syntax | unclosed raw string; add a closing backtick |
| `E1018` | syntax | a char holds exactly one character; use a string for multiple |
| `E1019` | syntax | unexpected '#' character; use '//' for comments |
| `E1020` | syntax | unexpected '|' character; use '||' for logical OR |
| `E1021` | syntax | unclosed string; add a closing double quote |
| `E1022` | syntax | unexpected character |
| `E1023` | syntax | string literals cannot span multiple lines; use a raw string with backticks for multi-line text |
| `E1024` | syntax | identifier exceeds the maximum length of 255 characters |
| `E2001` | syntax | unexpected symbol |
| `E2002` | syntax | missing symbol; expected a bracket, parenthesis, or keyword |
| `E2010` | syntax | cannot use module '%s' before importing it; add 'import @%s' before the using statement |
| `E2011` | syntax | constant '%s' must have a value; add = followed by a value |
| `E2012` | syntax | duplicate parameter name '%s' |
| `E2013` | syntax | duplicate field name '%s' in struct '%s' |
| `E2014` | syntax | duplicate variant name '%s' in enum '%s' |
| `E2015` | syntax | duplicate field '%s' in struct literal; field can only be initialized once |
| `E2016` | syntax | enum '%s' has no values; an enum must have at least one value |
| `E2017` | syntax | stray comma; remove the extra ',' |
| `E2025` | syntax | expected integer or constant for array size; the second value in [type, size] must be a positive integer or a const integer identifier |
| `E2036` | syntax | imports must be at the top of the file, not inside a function |
| `E2037` | syntax | duplicate function name in struct; each function must have a unique name |
| `E2038` | syntax | reserved name for struct or enum; this name is used by the language |
| `E2039` | syntax | required parameter '%s' cannot come after a parameter with a default value |
| `E2043` | syntax | duplicate case value in when statement |
| `E2050` | syntax | break and continue can only be used inside a loop |
| `E2051` | syntax | nested function declarations are not allowed; define '%s' at the top level |
| `E2053` | syntax | %s '%s' must be defined at the file scope, not inside a function |
| `E2056` | syntax | statements cannot sit at file scope; move this into do main() |
| `E2057` | syntax | invalid interpolation syntax; use ${variable} instead of $variable |
| `E2058` | syntax | cannot declare a struct or enum inside %s '%s'; define it at the file scope |
| `E2059` | syntax | empty when block; add at least one 'is' branch |
| `E2060` | syntax | too many return values; a function can return at most %d values |
| `E2061` | syntax | 'module' declarations are not supported; imported files are identified by their file path |
| `E2062` | syntax | too many variables in multi-variable declaration; maximum is %d |
| `E2063` | syntax | duplicate or conflicting named return value; each name must be unique and not collide with parameters |
| `E2064` | syntax | function '%s' conflicts with field '%s' in struct '%s' |
| `E2065` | syntax | enum variant '%s' cannot have the same name as its enum type '%s' |
| `E2066` | syntax | struct field '%s' cannot have the same name as its struct type '%s' |
| `E2067` | syntax | struct '%s' has no fields; a struct must have at least one field |
| `E2068` | syntax | %ss must be declared with 'const', not 'mut'; change 'mut' to 'const' |
| `E2069` | syntax | unexpected semicolon; statements and declarations are separated by newlines, not semicolons |
| `E2070` | syntax | wildcard type '?' is only allowed in function parameter and return types; not in variable declarations, struct fields, or enum types |
| `E2071` | syntax | empty string interpolation '${}'; interpolation requires an expression between the braces |
| `E2072` | syntax | '&' is not a valid operator; use 'addr(x)' to take the address of a variable |
| `E2073` | syntax | function calls cannot have whitespace between the name and the opening parenthesis; write 'name(...)' with no space or newline |
| `E2074` | syntax | member access cannot have whitespace before the dot; write 'obj.field' or 'Enum.VARIANT' with no space or newline |
| `E2075` | syntax | index expressions cannot have whitespace before the opening bracket; write 'arr[i]' with no space or newline |
| `E2076` | syntax | postfix operators ('++', '--', '^') cannot have whitespace before them; write 'x++', 'x--', or 'p^' with no space or newline |
| `E2079` | syntax | 'nil' is a value, not a type; for a function that returns nothing, omit the '-> ...' clause |
| `E2080` | syntax | invalid character in C header path; only [A-Za-z0-9./_+-] are permitted |
| `E2081` | syntax | '^' is a dereference operator, not a type modifier; for a pointer return type write '^%s', not '%s^' |
| `E2082` | syntax | arrays of typed func signatures are not supported; use '[func]' or '[func, N]' with '()func_name' elements instead |
| `E2083` | syntax | enum variant '%s' cannot have both a payload and an explicit value |
| `E2084` | syntax | blank identifier '_' requires '='; use '%s _ = <expr>' to discard a result |
| `E2085` | syntax | when statement already has a default branch; only one default is allowed |
| `E2086` | syntax | '%s' requires a value on the left side; '%s' checks whether a value belongs to a collection or range |
| `E2087` | syntax | type parameters (<?>) cannot be mixed with value parameters in the same function |
| `E2088` | syntax | mixed keyword aliases in the same file; '%s' used here, but '%s' was used on line %d |
| `E2089` | syntax | #discard attribute can only be applied to function declarations, not struct fields |
| `E3001` | types | type mismatch; a value of one type is used where a different type is expected |
| `E3002` | types | this operator does not work on this type; for example, strings cannot be subtracted |
| `E3003` | types | invalid array index type; array indices must be integers |
| `E3004` | types | strings are not element-assignable; individual string characters cannot be modified by index |
| `E3005` | types | cannot modify constant '%s'; declare with 'mut' to make it mutable |
| `E3006` | types | too many variables; the function returns %d value(s) but variable %d was requested |
| `E3007` | types | cannot negate type '%s'; only numeric types support negation |
| `E3008` | types | type '%s' does not support indexing; only arrays, maps, and strings can be indexed |
| `E3009` | types | cannot iterate over type '%s'; for_each requires an array, map, or string |
| `E3010` | types | struct '%s' has no field '%s' |
| `E3011` | types | '%s' is a type, not a value; did you mean to declare a type? (e.g., 'mut x %s = ...') |
| `E3012` | types | 'addr()' needs a variable, field, or array element; the address of a value like 42 cannot be taken |
| `E3013` | types | type does not support access via dot notation |
| `E3015` | types | '%s' is a %s, not a function; it cannot be called |
| `E3016` | types | cannot dereference non-pointer type '%s'; only ^T types can use ^ |
| `E3017` | types | 'fmt.%s()' cannot format value of type '%s'; use 'println()' for composite types, or access individual fields |
| `E3018` | types | type mismatch in 'when'; comparing '%s' with '%s' |
| `E3019` | types | cannot assign signed type '%s' to unsigned type '%s'; value may be negative |
| `E3024` | types | function '%s' must return a value but has no return statement |
| `E3027` | types | cannot pass a constant to a mutable parameter; the function wants to modify this value |
| `E3031` | types | function '%s' cannot be used as a value; did you mean '%s()' or '()%s'? |
| `E3032` | types | cannot compare enum '%s' with enum '%s'; different enum types are never equal |
| `E3033` | types | duplicate value in enum '%s': '%s' and '%s' both have the same value |
| `E3034` | types | 'any' type is reserved for internal use and cannot be used in declarations |
| `E3035` | types | not all code paths in '%s' return a value |
| `E3036` | types | value %lld is out of range for type '%s' (valid range: %lld to %lld) |
| `E3038` | types | 'void' cannot be used as a variable type or in expressions like type_of() |
| `E3039` | types | 'ensure' expects a function call; for example: 'ensure close(file)' |
| `E3040` | types | '%s' returns %d values; use 'mut a, b = %s()' to capture all of them |
| `E3041` | types | cannot interpolate expression; interpolation supports primitives, strings, arrays, and maps |
| `E3043` | types | cannot cast between incompatible types; only numeric, enum, and string conversions are allowed |
| `E3044` | types | cannot access field '%s' on type '%s'; use an instance variable instead |
| `E3045` | types | 'or_return' requires a function that returns (T, Error); '%s()' does not return an error |
| `E3046` | types | integer too large for 64 bits; max is 9223372036854775807 |
| `E3047` | types | enum '%s' has no member '%s' |
| `E3048` | types | operator '+' is not defined for strings; use string interpolation or 'fmt.format()' instead |
| `E3049` | types | cannot use '%s' on enum values; enums only support == and != comparisons |
| `E3050` | types | array needs a type annotation; declare as [T] (e.g., mut x [int] = {1, 2, 3}) |
| `E3051` | types | map needs a type annotation; declare as [K:V] or map[K:V] (e.g., mut x [string:int] = {\"a\": 1}) |
| `E3052` | types | too many elements in array initializer; declared size is %d, got %d |
| `E3053` | types | type mismatch in array initializer; expected '%s', got '%s' |
| `E3054` | types | mutable arrays cannot have a fixed size; remove the size or use 'const' (e.g., mut %s %.*s] = ...) |
| `E3055` | types | const arrays must have a fixed size; declare as [T, N] (e.g., const %s [%.*s, %d] = ...) |
| `E3056` | types | #strict when is not exhaustive; missing variant '%s.%s' |
| `E3057` | types | type '%s' cannot be used as a map key; only primitive types (int, string, bool, char, byte, float) and enums are hashable |
| `E3058` | types | in instantiation of generic function '%s' with '?' = %s |
| `E3059` | types | maps cannot be declared const; use 'mut' for maps or a struct for fixed data |
| `E3060` | types | wildcard '?' in return type cannot be resolved; at least one parameter must also use '?' to bind the concrete type |
| `E3061` | types | struct '%s' cannot contain itself by value through '%s'; break the cycle with a pointer field '^%s' |
| `E3062` | types | '%s' cannot be declared const; use 'mut' (every operation on a '%s' mutates its state) |
| `E3063` | types | cannot return 'addr(%s)'; '%s' is a local variable whose memory is freed when this function returns |
| `E3064` | types | '%s(%s)' called again; '%s' was already destroyed |
| `E3066` | types | function reference signature mismatch; expected and actual function types differ |
| `E3068` | types | 'void' is not a user-facing type; omit the '-> R' clause to declare a function with no return value |
| `E3069` | types | '&' on a parameter must come before the name, not the type; write '&%s %s' to mark this parameter mutable |
| `E3070` | types | 'ensure' may only appear at the top level of a function body; lift it out of the enclosing block |
| `E3071` | types | cannot 'return nil' from a function whose return type contains '?'; 'nil' is not a valid value for every binding (e.g. int, string) |
| `E3072` | types | cannot return 'nil' from a function that returns '%s'; nil is only valid for pointer and error types |
| `E3073` | types | 'return' is not allowed in 'main()'; 'main()' exits when control reaches the closing brace |
| `E3074` | types | arrays cannot be compared with comparison operators; use 'arrays.is_equal(a, b)' for equality, or compare elements individually for ordering |
| `E3075` | types | chained struct function calls are not supported; assign the intermediate result to a variable, then call the next struct function on it |
| `E3076` | types | maps cannot be compared with comparison operators; use 'maps.is_equal(a, b)' for equality (maps have no defined ordering) |
| `E3077` | types | structs cannot be compared with comparison operators; compare individual fields instead (e.g., 'a.x == b.x', 'a.x < b.x') |
| `E3078` | types | pointer arithmetic is not supported; '^T' is the address of one value, not a buffer |
| `E3079` | types | cannot take a mutable reference to a const variable; declare the reference as 'const', or 'copy()' the value to get an independent mutable instance |
| `E3080` | types | function must return named variable '%s', not a different expression |
| `E3081` | types | function '%s' used as a statement without being called; did you mean '%s()'? |
| `E3082` | types | wildcard type '?' cannot be used in named return positions; use an unnamed return instead |
| `E3083` | types | 'c_string()' requires a raw C pointer; cannot convert a non-pointer type |
| `E3084` | types | 'type_of()' expects a value, not a type name; use 'type_of(instance)' instead |
| `E3085` | types | 'in' operator type mismatch: cannot check if '%s' is in '%s' |
| `E3086` | types | 'fmt.%s' format string must be a string literal; use string interpolation for dynamic values |
| `E3087` | types | '%%n' is not permitted in fmt format strings |
| `E3088` | types | 'fmt.%s' format directive '%%%s' expects %s but argument %d has type '%s' |
| `E3089` | usage | '%s()' can fail; use 'mut val, err = %s()' to handle the error, or 'mut val, _ = %s()' to discard it |
| `E3090` | types | '!' only works on bool; got '%s' |
| `E3091` | types | '%s' cannot be used as a condition |
| `E3092` | types | cannot compare '%s' to nil; only Error types and pointers can be nil |
| `E3093` | types | cannot use '%s' on '%s' |
| `E3094` | types | cannot assign '%s' to element of '%s' |
| `E3095` | types | 'in' only works with arrays, maps, and strings; got '%s' |
| `E3096` | types | cannot negate unsigned type '%s'; negation of unsigned types is not defined |
| `E3097` | safety | pointer '%s' assigned address of inner-scope variable '%s'; the variable's memory is freed when the scope exits |
| `E3098` | types | type mismatch: cannot assign '%s' to '%s' through pointer dereference |
| `E3099` | types | '%s' is a reserved stdlib type name and cannot be used as a struct name |
| `E3100` | types | type name '%s' cannot be used as a value |
| `E3101` | types | func reference variables must be declared with 'const', not 'mut'; func references are compile-time aliases |
| `E3102` | types | function '%s' returns a func type; func references cannot be assigned from function return values. Use '()func_name' or 'ref(func_name)' to create a func reference |
| `E3103` | types | #json struct '%s' cannot have func-typed field '%s'; func references have no JSON representation |
| `E3104` | types | #json struct '%s' cannot declare functions; #json structs are data-only — move '%s' to a standalone function |
| `E3105` | types | 'fmt.%s': unknown format directive '%%%c' |
| `E3106` | types | 'fmt.%s': dangling '%%' at end of format string |
| `E3107` | types | 'fmt.%s': format string has %d directive(s) but %d argument(s) were passed (too few) |
| `E3108` | types | 'fmt.%s': format string has %d directive(s) but %d argument(s) were passed (too many) |
| `E3109` | types | #json struct '%s' cannot have default field values; field '%s' has a default |
| `E3110` | types | implicit enum selector '.%s' requires type context; use the full form 'EnumName.%s' or add a type annotation |
| `E3111` | types | payload types are not allowed on string enum variants |
| `E3112` | types | payload types are not allowed on #flags enum variants |
| `E3113` | types | variant '%s' of enum '%s' expects %d payload value(s), got %d |
| `E3114` | types | variant '%s' of enum '%s' has no payload; remove the arguments |
| `E3115` | types | enum '%s' is not a tagged enum; variant '%s' cannot be called |
| `E3116` | types | wrong number of bindings for variant '%s'; expected %d, got %d |
| `E3117` | types | cannot compare enum '%s' with '%s'; use an enum variant like '%s.VARIANT', or cast to int with 'cast(value, int)' |
| `E3118` | types | cannot assign '%s' to enum '%s'; use an enum variant like '%s.VARIANT' |
| `E3119` | types | fixed-size arrays are not allowed in function parameters; use '[%s]' instead of '%s' for parameter '%s' |
| `E3120` | types | pointer ordering comparisons are not supported; only == and != are allowed on pointers |
| `E3121` | types | cannot use '%s' as a condition in a when statement; allowed types are int, uint, string, char, byte, bool, float, and enum |
| `E3122` | safety | cannot modify value through pointer '%s'; the pointee is a const-declared variable |
| `E3123` | iteration | 'for_each' with both positions discarded accesses nothing; use 'for _ in range(0, len(collection))' to iterate by count |
| `E3124` | types | operator '%s' is not defined for tagged enum '%s'; tagged enums carry payloads and cannot be compared with == or != |
| `E3125` | types | '%s' is not a compile-time integer constant; array size must be a const int/uint value |
| `E3126` | types | array size must be greater than zero; '%s' resolves to %d |
| `E3127` | types | type parameter expects a struct type name, but '%s' is not a struct; only struct types can be passed as type arguments |
| `E3128` | types | type parameter expects a struct type name, but got a non-type expression; pass a struct type name like 'MyStruct' |
| `E3129` | safety | empty loop body; this will loop forever at runtime |
| `E3130` | types | bare 'func' is not allowed as a struct field type |
| `E3131` | types | file-scope 'const' requires an explicit type annotation; write 'const %s %s = ...' instead |
| `E3132` | types | alias target type '%s' is not defined |
| `E3133` | types | alias '%s' creates a circular reference |
| `E3134` | types | alias '%s' cannot target a module-qualified type; only local types can be aliased |
| `E3135` | types | alias '%s' cannot target the wildcard type '?' |
| `E3136` | types | empty array literal has no elements to infer a type from; add at least one element or use a typed declaration |
| `E4001` | names | this variable does not exist; check the spelling or make sure it is declared above this line |
| `E4002` | names | this function does not exist; check the spelling or make sure it is defined |
| `E4003` | names | variable '%s' already declared in this scope (line %d) |
| `E4004` | names | function '%s' already declared |
| `E4005` | names | module '%s' has no function named '%s' |
| `E4006` | names | name '%s' uses reserved prefix ('gray_', '_gray_', 'Gray'); these are reserved for the compiler |
| `E4007` | names | a type with this name already exists; each struct and enum must have a unique name |
| `E4008` | names | 'main()' cannot have parameters or a return type; it must be declared as 'do main() { }' |
| `E4012` | names | variable '%s' shadows a type definition with the same name |
| `E4013` | names | variable '%s' shadows a function with the same name |
| `E4014` | names | variable '%s' shadows an imported module with the same name |
| `E4015` | names | '%s' is private and cannot be accessed from outside its file |
| `E4016` | names | undefined type '%s'; check the spelling or import the module that defines it |
| `E4017` | names | function '%s.%s' is private and cannot be called from outside the struct |
| `E4018` | names | struct '%s' has no function named '%s' |
| `E4019` | names | cannot take a function reference to '%s'; builtin and stdlib functions are not first-class values |
| `E4020` | names | type alias '%s' is already declared |
| `E4021` | names | type alias '%s' is private and cannot be accessed from outside its file |
| `E5007` | usage | cannot modify immutable %s '%s'; declare with 'mut' to allow modification |
| `E5008` | arguments | wrong number of arguments; the function expects a different count than was provided |
| `E5009` | arguments | invalid base for integer conversion; base must be between 2 and 36 |
| `E5011` | usage | return value of '%s' is not used; assign it to a variable or use '_' to discard |
| `E5012` | usage | the throwaway '_' is only meaningful when discarding the result of a function call; the right-hand side has no return value to discard |
| `E5013` | usage | function calls are not allowed in file-scope initializers; move this declaration into a function body |
| `E5014` | usage | 'here()' takes no arguments; the call site's file, line, and column are substituted at compile time |
| `E5015` | usage | postfix '++' and '--' require a variable, not a value or expression |
| `E5016` | naming | this name is reserved by a builtin function and cannot be redeclared |
| `E5017` | usage | 'embed()' argument must be a string literal file path, not an expression |
| `E5018` | usage | 'embed()' cannot open '%s': file not found or unreadable |
| `E5023` | usage | cannot use '%s' on type '%s'; only integer types support increment/decrement |
| `E5024` | usage | return type mismatch: cannot return signed '%s' as unsigned '%s' |
| `E5025` | usage | invalid assignment target; left side of '=' must be a variable, field, or index expression |
| `E5026` | arguments | argument type mismatch; the function expects a different type than what was provided |
| `E5027` | usage | 'embed()' path must not escape the source file's directory tree |
| `E5028` | usage | func references are not printable values; func references cannot be passed to print functions |
| `E5029` | usage | 'copy()' cannot be used on a func reference; func references are compile-time aliases, not copyable values |
| `E5030` | usage | cannot call the return value of '%s' directly; func references must be created with '()func_name' or 'ref(func_name)' before calling |
| `E5031` | usage | unknown parameter name '%s' in call to '%s' |
| `E5032` | usage | parameter '%s' is already provided positionally (argument %d) in call to '%s' |
| `E5033` | usage | positional argument after named argument in call to '%s' |
| `E5034` | usage | named arguments are not supported for builtin function '%s' |
| `E5035` | naming | this name is reserved by a standard library module and cannot be redeclared |
| `E5036` | usage | '%s' is a type, not a function; use 'cast(value, %s)' to convert |
| `E5037` | usage | 'copy()' cannot be applied to a pointer; dereference first with 'copy(p^)' |
| `E5038` | usage | tagged enum '%s' cannot be passed to '%s()'; use when/is to destructure the payload first |
| `E5039` | usage | constant expression overflows type '%s' |
| `E5040` | usage | constant requires a compile-time value; function calls are evaluated at runtime |
| `E5042` | usage | '#discard' attribute is not allowed on void function '%s'; only functions that return a value can use '#discard' |
| `E5043` | usage | 'fields()' requires a struct instance, got '%s' |
| `E5044` | usage | 'error()' argument must be a string, got '%s' |
| `E6001` | imports | unknown module '@%s' |
| `E6002` | imports | cannot find file or directory '%s' |
| `E6003` | imports | directory '%s' contains no .gray files |
| `E6004` | imports | cannot import own module directory |
| `E6008` | imports | '%s.%s' is a module constant and cannot be assigned to |
| `E7004` | stdlib | function argument must be an integer, not a float |
| `E7006` | stdlib | 'threads.spawn()' needs a function reference; use '()function_name' to pass a function |
| `E7014` | stdlib | cannot convert %lld to char; value must be a valid Unicode code point (0 or greater) |
| `E7015` | stdlib | 'len()' is not supported for type '%s'; 'len()' works on string, array, and map types |
| `E9002` | stdlib | 'arrays.%s()' requires a numeric array, got array of '%s' |
| `E9003` | stdlib | 'arrays.%s()' requires a function reference; use '()func_name' to pass a function |
| `E9004` | stdlib | 'arrays.%s()' callback signature mismatch; %s |
| `E9005` | stdlib | invalid range: start (%lld) must be less than end (%lld) |
| `E9006` | stdlib | 'arrays.contains()' does not support arrays of '%s'; only primitive and string element types are supported |
| `E12001` | stdlib | 'maps.%s()' requires a map argument, got an array |
| `E12006` | stdlib | duplicate key in map literal |
| `E12007` | stdlib | 'maps.contains_value()' does not support maps with '%s' values; only primitive and string value types are supported |
| `E8001` | bitwise | '%s' can only be used with integers or #flags enums; got '%s' and '%s' |
| `E8002` | bitwise | 'bit_not' can only be used with integers; got '%s' |

---

## Warnings

| Code | Category | Description |
|------|----------|-------------|
| `W1001` | cleanup | variable is declared but never used; remove it or use it |
| `W1003` | cleanup | function is declared but never called; remove it or call it |
| `W1005` | cleanup | typed blank identifier; '_' doesn't need a type annotation, use 'mut _ = <expr>' instead |
| `W1002` | cleanup | this import is never used; remove it or use a function from the module |
| `W2002` | safety | this variable shadows a variable with the same name in an outer scope |
| `W2003` | safety | unreachable code; this statement will never execute because it comes after a return |
| `W2007` | safety | this variable shadows a global constant or variable |
| `W2008` | safety | parameter shadows an enum variant name |
| `W2011` | safety | named return value is declared in the signature but no matching variable exists in the function body |
| `W2012` | safety | 'when' condition is a float; equality checks on floats are imprecise; prefer 'math.abs(x - y) < epsilon' |
| `W2013` | imports | duplicate import of already-imported module |
| `W2014` | imports | intra-directory import already included by directory import |
| `W2015` | imports | file already imported as part of a directory import; redundant import |
| `W3003` | safety | fixed-size array is not fully initialized; remaining elements will be zero-valued |
| `W3004` | safety | pointer may reference memory from a scope that has ended; assigning addr() of an inner-scope variable to an outer-scope pointer |
| `W3005` | safety | when statement matches on enum values without #strict and no default; exhaustiveness is not checked |
| `W3006` | safety | empty default branch in when statement; unmatched values are silently ignored |

---

## Panics

Runtime panics are fatal errors that terminate the program immediately. They are not compiler errors — they happen at runtime when the program encounters an unrecoverable condition.

| Code | Category | Description |
|------|----------|-------------|
| `P0001` | memory | cannot allocate from a destroyed arena; mem.destroy() was already called on this arena |
| `P0002` | memory | mem.destroy() called on an arena that was already destroyed; each arena can only be destroyed once |
| `P0003` | runtime | maximum recursion depth exceeded (%d calls deep); your function is calling itself too many times |
| `P0004` | arithmetic | addition result is too large; value exceeds the range of int |
| `P0005` | arithmetic | subtraction result is too large; value exceeds the range of int |
| `P0006` | arithmetic | multiplication result is too large; value exceeds the range of int |
| `P0007` | arithmetic | negation result is too large; value exceeds the range of int |
| `P0008` | arithmetic | addition result is too large; value exceeds the range of uint |
| `P0009` | arithmetic | subtraction result is negative, but uint cannot hold negative values |
| `P0010` | arithmetic | multiplication result is too large; value exceeds the range of uint |
| `P0011` | arithmetic | %s addition result is too large; value exceeds the range of this type |
| `P0012` | arithmetic | %s subtraction result is too large; value exceeds the range of this type |
| `P0013` | arithmetic | %s multiplication result is too large; value exceeds the range of this type |
| `P0014` | arithmetic | %s negation result is too large; value exceeds the range of this type |
| `P0015` | arithmetic | %s addition result is too large; value exceeds the range of this unsigned type |
| `P0016` | arithmetic | %s subtraction result is negative, but this unsigned type cannot hold negative values |
| `P0017` | arithmetic | %s multiplication result is too large; value exceeds the range of this unsigned type |
| `P0018` | arithmetic | cast to %s failed; value %lld is outside the valid range (%lld to %lld) |
| `P0019` | arithmetic | cast to %s failed; value %lld is outside the valid range (0 to %llu) |
| `P0020` | arithmetic | cannot convert float to int; the value is too large, too small, or NaN |
| `P0021` | arithmetic | i128 addition result is too large; value exceeds the range of i128 |
| `P0022` | arithmetic | i128 subtraction result is too large; value exceeds the range of i128 |
| `P0023` | arithmetic | i128 multiplication result is too large; value exceeds the range of i128 |
| `P0024` | arithmetic | u128 addition result is too large; value exceeds the range of u128 |
| `P0025` | arithmetic | u128 subtraction result is negative, but u128 cannot hold negative values |
| `P0026` | arithmetic | u128 multiplication result is too large; value exceeds the range of u128 |
| `P0027` | arithmetic | i256 addition result is too large; value exceeds the range of i256 |
| `P0028` | arithmetic | i256 subtraction result is too large; value exceeds the range of i256 |
| `P0029` | arithmetic | i256 multiplication result is too large; value exceeds the range of i256 |
| `P0030` | arithmetic | u256 addition result is too large; value exceeds the range of u256 |
| `P0031` | arithmetic | u256 subtraction result is negative, but u256 cannot hold negative values |
| `P0032` | arithmetic | u256 multiplication result is too large; value exceeds the range of u256 |
| `P0033` | bounds | index out of bounds; tried to access index %d but the length is %d |
| `P0034` | iteration | cannot modify array during for_each iteration |
| `P0035` | iteration | cannot modify map during for_each iteration |
| `P0036` | encoding | encoding.base64_decode: input length %d is not a multiple of 4 |
| `P0037` | encoding | encoding.base64_decode: padding character '=' before end of input |
| `P0038` | encoding | encoding.base64_decode: invalid padding |
| `P0039` | encoding | encoding.base64_decode: invalid character in input |
| `P0040` | encoding | encoding.hex_decode: input length %d is not even |
| `P0041` | encoding | encoding.hex_decode: invalid hex character at position %d |
| `P0042` | encoding | encoding.url_decode: invalid percent-escape at position %d |
| `P0043` | bounds | arrays.insert_at: index %d is out of bounds for an array of length %d |
| `P0044` | bounds | arrays.remove_at: index %d is out of bounds for an array of length %d |
| `P0045` | bounds | arrays.get_first called on an empty array |
| `P0046` | bounds | arrays.get_last called on an empty array |
| `P0047` | bounds | arrays.remove_first called on an empty array |
| `P0048` | bounds | arrays.remove_last called on an empty array |
| `P0049` | bounds | to_char() index out of bounds; index %lld is negative |
| `P0050` | bounds | to_char() index out of bounds; index %lld but string has %lld characters |
| `P0051` | crypto | crypto.random_hex: length must be non-negative (got %lld) |
| `P0052` | crypto | crypto.random_hex: failed to read from /dev/urandom |
| `P0053` | io | io.read_file: input exceeds maximum string length |
| `P0054` | strconv | strconv.to_int: invalid base %d; must be between 2 and 36 |
| `P0055` | strconv | strconv.to_int: cannot convert '%s' to int (base %d) |
| `P0056` | strconv | strconv.to_uint: invalid base %d; must be between 2 and 36 |
| `P0057` | strconv | strconv.to_uint: cannot convert '%s' to uint (base %d) |
| `P0058` | strconv | strconv.to_uint: cannot convert '%s' to uint; value is negative |
| `P0059` | strconv | strconv.to_float: cannot convert '%s' to float |
| `P0060` | strconv | strconv.to_bool: cannot convert '%s' to bool |
| `P0061` | memory | mem.arena() size %lld bytes exceeds the maximum allowed size of 1 GB |
| `P0062` | random | random.sample() count %d exceeds array length %d |
| `P0063` | random | random.sample() count cannot be negative (%d) |
| `P0064` | math | math.sqrt() requires a non-negative number, got %g |
| `P0065` | math | math.log() requires a positive number, got %g |
| `P0066` | math | math.log2() requires a positive number, got %g |
| `P0067` | math | math.log10() requires a positive number, got %g |
| `P0068` | math | math.asin() requires value in [-1, 1], got %g |
| `P0069` | math | math.acos() requires value in [-1, 1], got %g |
| `P0070` | math | math.factorial() requires a non-negative integer, got %lld |
| `P0071` | strings | strings.replace() result exceeds maximum string length |
| `P0072` | strings | strings.repeat() count cannot be negative (%lld) |
| `P0073` | strings | strings.repeat() result exceeds maximum string length |
| `P0074` | uuid | uuid.parse: invalid UUID string |
| `P0075` | runtime | assertion failed |
| `P0076` | runtime | panic |
| `P0077` | io | io.delete_file() cannot delete a directory; use io.remove_dir() for directories |
| `P0078` | arithmetic | division by zero |
| `P0079` | arithmetic | %s result is too large; value exceeds the range of this type |
| `P0080` | runtime | nil pointer dereference |
| `P0081` | runtime | key not found in map |
| `P0082` | bounds | string index %d out of bounds (length %d) |
| `P0083` | runtime | sleep duration cannot be negative (%lld) |
| `P0084` | runtime | cannot convert '%s' to int |
| `P0085` | runtime | cannot convert '%s' to float |
| `P0086` | io | io.read_file() cannot read a directory; use io.list_dir() or io.walk() to list directory contents |
| `P0087` | io | io.write_file() cannot write to a directory |
| `P0088` | io | io.append_file() cannot append to a directory |
| `P0089` | io | io.copy_file() cannot copy a directory; use io.walk() to enumerate files and copy them individually |
| `P0090` | runtime | range step cannot be zero |
| `P0091` | arithmetic | cannot convert float to uint; the value is negative, too large, or NaN |
| `P0092` | arithmetic | shift amount %lld is out of range; must be in [0, 63] |
| `P0093` | arithmetic | cast from i128 failed; value is outside the representable range of int64 |
| `P0094` | arithmetic | cast from i128 failed; value is negative or outside the representable range of uint64 |
| `P0095` | arithmetic | cast from u128 failed; value exceeds the representable range of int64 |
| `P0096` | arithmetic | cast from u128 failed; value exceeds the representable range of uint64 |
| `P0097` | arithmetic | cast from i256 failed; value is outside the representable range of int64 |
| `P0098` | arithmetic | cast from i256 failed; value is negative or outside the representable range of uint64 |
| `P0099` | arithmetic | cast from u256 failed; value exceeds the representable range of int64 |
| `P0100` | arithmetic | cast from u256 failed; value exceeds the representable range of uint64 |
| `P0101` | server | server.cors: origin contains CR or LF — HTTP header injection is not allowed |
| `P0102` | arithmetic | invalid digit in integer literal |
| `P0103` | io | file path contains an embedded null byte |
| `P0104` | memory | arena memory limit exceeded: attempted to grow beyond the maximum of %zu bytes |

---

## Code Ranges

| Range | What It Means |
|-------|---------------|
| E1xxx | Problems reading your code (invalid characters, malformed literals) |
| E2xxx | Problems understanding your code (missing brackets, unexpected symbols) |
| E3xxx | Type problems (wrong types, invalid operations) |
| E4xxx | Name problems (undefined variables, duplicate names) |
| E5xxx | Usage problems (wrong number of arguments, invalid assignment targets) |
| E6xxx | Import problems (unknown modules, missing files) |
| E7xxx | Standard library problems (wrong usage of built-in functions) |
| E8xxx | Bitwise operator errors (non-integer operands) |
| E9xxx | Array errors (empty array ops, invalid ranges) |
| E12xxx | Map errors (unhashable keys, duplicate keys) |
| W1xxx | Cleanup suggestions (unused variables, functions) |
| W2xxx | Safety warnings (shadowing, unused imports) |
| W3xxx | Quality warnings (uninitialized elements, pointer lifetime) |
| P0xxx | Runtime panics (overflow, out-of-bounds, nil dereference) |

---

*Generated on 2026-08-21 02:06:07 UTC*
