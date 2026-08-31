/*
 * error_codes.h — Single source of truth for all compiler error, warning,
 * and panic codes, used by both the compiler and ERRORS.md generation.
 *
 * Author:  Marshall A Burns (@SchoolyB)
 * Copyright (c) 2025-Present Marshall A Burns
 * Licensed under the MIT License. See LICENSE for details.
 */

#ifndef GRAYC_ERROR_CODES_H
#define GRAYC_ERROR_CODES_H

/* --- E1xxx: Reading Your Code (Lexer) ---
 *
 * NOTE: Lexer errors are NOT emitted via diagnostic_error() inside lexer.c.
 * Instead, when the lexer encounters an invalid token it sets two fields
 * on the Lexer struct: error_code (e.g. "E1010") and error_msg, and
 * returns a TOK_ILLEGAL token. The parser's next_token() helper in
 * parser.c detects TOK_ILLEGAL and surfaces the error via diagnostic_error_message()
 * at that point. This keeps the lexer free of diagnostic dependencies.
 * All E1xxx codes defined here are emitted through that single path.
 */
#define GRAY_LEXER_ERRORS \
    GRAY_ERROR("E1003", "syntax", "unclosed multi-line comment; add */") \
    GRAY_ERROR("E1005", "syntax", "unclosed character; add a closing single quote") \
    GRAY_ERROR("E1006", "syntax", "invalid escape sequence in string; valid escapes are \\n \\t \\\\ \\\" and \\x") \
    GRAY_ERROR("E1007", "syntax", "invalid escape sequence in character") \
    GRAY_ERROR("E1010", "syntax", "invalid number format; hex (0x), octal (0o), or binary (0b) prefix must be followed by digits") \
    GRAY_ERROR("E1011", "syntax", "number cannot have consecutive underscores") \
    GRAY_ERROR("E1012", "syntax", "numeric literals cannot start with an underscore; did you mean '%s'?") \
    GRAY_ERROR("E1013", "syntax", "number cannot end with an underscore") \
    GRAY_ERROR("E1014", "syntax", "number cannot have an underscore before the decimal point") \
    GRAY_ERROR("E1015", "syntax", "number cannot have an underscore after the decimal point") \
    GRAY_ERROR("E1016", "syntax", "number cannot end with a trailing decimal point; add a digit after the dot") \
    GRAY_ERROR("E1017", "syntax", "unclosed raw string; add a closing backtick") \
    GRAY_ERROR("E1018", "syntax", "a char holds exactly one character; use a string for multiple") \
    GRAY_ERROR("E1019", "syntax", "unexpected '#' character; use '//' for comments") \
    GRAY_ERROR("E1020", "syntax", "unexpected '|' character; use '||' for logical OR") \
    GRAY_ERROR("E1021", "syntax", "unclosed string; add a closing double quote") \
    GRAY_ERROR("E1022", "syntax", "unexpected character") \
    GRAY_ERROR("E1023", "syntax", "string literals cannot span multiple lines; use a raw string with backticks for multi-line text") \
    GRAY_ERROR("E1024", "syntax", "identifier exceeds the maximum length of 255 characters")

/* --- E2xxx: Understanding Your Code (Parser) --- */
#define GRAY_PARSER_ERRORS \
    GRAY_ERROR("E2001", "syntax", "unexpected symbol") \
    GRAY_ERROR("E2002", "syntax", "missing symbol; expected a bracket, parenthesis, or keyword") \
    GRAY_ERROR("E2010", "syntax", "cannot use module '%s' before importing it; add 'import @%s' before the using statement") \
    GRAY_ERROR("E2011", "syntax", "constant '%s' must have a value; add = followed by a value") \
    GRAY_ERROR("E2012", "syntax", "duplicate parameter name '%s'") \
    GRAY_ERROR("E2013", "syntax", "duplicate field name '%s' in struct '%s'") \
    GRAY_ERROR("E2014", "syntax", "duplicate variant name '%s' in enum '%s'") \
    GRAY_ERROR("E2015", "syntax", "duplicate field '%s' in struct literal; field can only be initialized once") \
    GRAY_ERROR("E2016", "syntax", "enum '%s' has no values; an enum must have at least one value") \
    GRAY_ERROR("E2017", "syntax", "stray comma; remove the extra ','") \
    GRAY_ERROR("E2025", "syntax", "expected integer or constant for array size; the second value in [type, size] must be a positive integer or a const integer identifier") \
    GRAY_ERROR("E2036", "syntax", "imports must be at the top of the file, not inside a function") \
    GRAY_ERROR("E2037", "syntax", "duplicate function name in struct; each function must have a unique name") \
    GRAY_ERROR("E2038", "syntax", "reserved name; this name is used by the language") \
    GRAY_ERROR("E2039", "syntax", "required parameter '%s' cannot come after a parameter with a default value") \
    GRAY_ERROR("E2043", "syntax", "duplicate case value in when statement") \
    GRAY_ERROR("E2050", "syntax", "break and continue can only be used inside a loop") \
    GRAY_ERROR("E2051", "syntax", "nested function declarations are not allowed; define '%s' at the top level") \
    GRAY_ERROR("E2053", "syntax", "%s '%s' must be defined at the file scope, not inside a function") \
    GRAY_ERROR("E2056", "syntax", "statements cannot sit at file scope; move this into do main()") \
    GRAY_ERROR("E2057", "syntax", "invalid interpolation syntax; use ${variable} instead of $variable") \
    GRAY_ERROR("E2058", "syntax", "cannot declare a struct or enum inside %s '%s'; define it at the file scope") \
    GRAY_ERROR("E2059", "syntax", "empty when block; add at least one 'is' branch") \
    GRAY_ERROR("E2060", "syntax", "too many return values; a function can return at most %d values") \
    GRAY_ERROR("E2061", "syntax", "'module' declarations are not supported; imported files are identified by their file path") \
    GRAY_ERROR("E2062", "syntax", "too many variables in multi-variable declaration; maximum is %d") \
    GRAY_ERROR("E2063", "syntax", "duplicate or conflicting named return value; each name must be unique and not collide with parameters") \
    GRAY_ERROR("E2064", "syntax", "function '%s' conflicts with field '%s' in struct '%s'") \
    GRAY_ERROR("E2065", "syntax", "enum variant '%s' cannot have the same name as its enum type '%s'") \
    GRAY_ERROR("E2066", "syntax", "struct field '%s' cannot have the same name as its struct type '%s'") \
    GRAY_ERROR("E2067", "syntax", "struct '%s' has no fields; a struct must have at least one field") \
    GRAY_ERROR("E2068", "syntax", "%ss must be declared with 'const', not 'mut'; change 'mut' to 'const'") \
    GRAY_ERROR("E2069", "syntax", "unexpected semicolon; statements and declarations are separated by newlines, not semicolons") \
    GRAY_ERROR("E2070", "syntax", "wildcard type '?' is only allowed in function parameter and return types; not in variable declarations, struct fields, or enum types") \
    GRAY_ERROR("E2071", "syntax", "empty string interpolation '${}'; interpolation requires an expression between the braces") \
    GRAY_ERROR("E2072", "syntax", "'&' is not a valid operator; use 'addr(x)' to take the address of a variable") \
    GRAY_ERROR("E2073", "syntax", "function calls cannot have whitespace between the name and the opening parenthesis; write 'name(...)' with no space or newline") \
    GRAY_ERROR("E2074", "syntax", "member access cannot have whitespace before the dot; write 'obj.field' or 'Enum.VARIANT' with no space or newline") \
    GRAY_ERROR("E2075", "syntax", "index expressions cannot have whitespace before the opening bracket; write 'arr[i]' with no space or newline") \
    GRAY_ERROR("E2076", "syntax", "postfix operators ('++', '--', '^') cannot have whitespace before them; write 'x++', 'x--', or 'p^' with no space or newline") \
    GRAY_ERROR("E2079", "syntax", "'nil' is a value, not a type; for a function that returns nothing, omit the '-> ...' clause") \
    GRAY_ERROR("E2080", "syntax", "invalid character in C header path; only [A-Za-z0-9./_+-] are permitted") \
    GRAY_ERROR("E2081", "syntax", "'^' is a dereference operator, not a type modifier; for a pointer return type write '^%s', not '%s^'") \
    GRAY_ERROR("E2082", "syntax", "arrays of typed func signatures are not supported; use '[func]' or '[func, N]' with '()func_name' elements instead") \
    GRAY_ERROR("E2083", "syntax", "enum variant '%s' cannot have both a payload and an explicit value") \
    GRAY_ERROR("E2084", "syntax", "blank identifier '_' requires '='; use '%s _ = <expr>' to discard a result") \
    GRAY_ERROR("E2085", "syntax", "when statement already has a default branch; only one default is allowed") \
    GRAY_ERROR("E2086", "syntax", "'%s' requires a value on the left side; '%s' checks whether a value belongs to a collection or range") \
    GRAY_ERROR("E2087", "syntax", "type parameters (<?>) cannot be mixed with value parameters in the same function") \
    GRAY_ERROR("E2088", "syntax", "mixed keyword aliases in the same file; '%s' used here, but '%s' was used on line %d") \
    GRAY_ERROR("E2089", "syntax", "#discard attribute can only be applied to function declarations, not struct fields")

/* --- E3xxx: Type Problems (Typechecker) --- */
#define GRAY_TYPE_ERRORS \
    GRAY_ERROR("E3001", "types", "type mismatch; a value of one type is used where a different type is expected") \
    GRAY_ERROR("E3002", "types", "this operator does not work on this type; for example, strings cannot be subtracted") \
    GRAY_ERROR("E3003", "types", "invalid array index type; array indices must be integers") \
    GRAY_ERROR("E3004", "types", "strings are not element-assignable; individual string characters cannot be modified by index") \
    GRAY_ERROR("E3005", "types", "cannot modify constant '%s'; declare with 'mut' to make it mutable") \
    GRAY_ERROR("E3006", "types", "too many variables; the function returns %d value(s) but variable %d was requested") \
    GRAY_ERROR("E3007", "types", "cannot negate type '%s'; only numeric types support negation") \
    GRAY_ERROR("E3008", "types", "type '%s' does not support indexing; only arrays, maps, and strings can be indexed") \
    GRAY_ERROR("E3009", "types", "cannot iterate over type '%s'; for_each requires an array, map, or string") \
    GRAY_ERROR("E3010", "types", "struct '%s' has no field '%s'") \
    GRAY_ERROR("E3011", "types", "'%s' is a type, not a value; did you mean to declare a type? (e.g., 'mut x %s = ...')") \
    GRAY_ERROR("E3012", "types", "'addr()' needs a variable, field, or array element; the address of a value like 42 cannot be taken") \
    GRAY_ERROR("E3013", "types", "type does not support access via dot notation") \
    GRAY_ERROR("E3015", "types", "'%s' is a %s, not a function; it cannot be called") \
    GRAY_ERROR("E3016", "types", "cannot dereference non-pointer type '%s'; only ^T types can use ^") \
    GRAY_ERROR("E3017", "types", "'fmt.%s()' cannot format value of type '%s'; use 'println()' for composite types, or access individual fields") \
    GRAY_ERROR("E3018", "types", "type mismatch in 'when'; comparing '%s' with '%s'") \
    GRAY_ERROR("E3019", "types", "cannot assign signed type '%s' to unsigned type '%s'; value may be negative") \
    GRAY_ERROR("E3024", "types", "function '%s' must return a value but has no return statement") \
    GRAY_ERROR("E3027", "types", "cannot pass a constant to a mutable parameter; the function wants to modify this value") \
    GRAY_ERROR("E3031", "types", "function '%s' cannot be used as a value; did you mean '%s()' or '()%s'?") \
    GRAY_ERROR("E3032", "types", "cannot compare enum '%s' with enum '%s'; different enum types are never equal") \
    GRAY_ERROR("E3033", "types", "duplicate value in enum '%s': '%s' and '%s' both have the same value") \
    GRAY_ERROR("E3034", "types", "'any' type is reserved for internal use and cannot be used in declarations") \
    GRAY_ERROR("E3035", "types", "not all code paths in '%s' return a value") \
    GRAY_ERROR("E3036", "types", "value %lld is out of range for type '%s' (valid range: %lld to %lld)") \
    GRAY_ERROR("E3038", "types", "'void' cannot be used as a variable type or in expressions like type_of()") \
    GRAY_ERROR("E3039", "types", "'ensure' expects a function call; for example: 'ensure close(file)'") \
    GRAY_ERROR("E3040", "types", "'%s' returns %d values; use 'mut a, b = %s()' to capture all of them") \
    GRAY_ERROR("E3041", "types", "cannot interpolate expression; interpolation supports primitives, strings, arrays, and maps") \
    GRAY_ERROR("E3043", "types", "cannot cast between incompatible types; only numeric, enum, and string conversions are allowed") \
    GRAY_ERROR("E3044", "types", "cannot access field '%s' on type '%s'; use an instance variable instead") \
    GRAY_ERROR("E3045", "types", "'or_return' requires a function that returns (T, Error); '%s()' does not return an error") \
    GRAY_ERROR("E3046", "types", "integer too large for 64 bits; max is 9223372036854775807") \
    GRAY_ERROR("E3047", "types", "enum '%s' has no member '%s'") \
    GRAY_ERROR("E3048", "types", "operator '+' is not defined for strings; use string interpolation or 'fmt.format()' instead") \
    GRAY_ERROR("E3049", "types", "cannot use '%s' on enum values; enums only support == and != comparisons") \
    GRAY_ERROR("E3050", "types", "array needs a type annotation; declare as [T] (e.g., mut x [int] = {1, 2, 3})") \
    GRAY_ERROR("E3051", "types", "map needs a type annotation; declare as [K:V] or map[K:V] (e.g., mut x [string:int] = {\"a\": 1})") \
    GRAY_ERROR("E3052", "types", "too many elements in array initializer; declared size is %d, got %d") \
    GRAY_ERROR("E3053", "types", "type mismatch in array initializer; expected '%s', got '%s'") \
    GRAY_ERROR("E3054", "types", "mutable arrays cannot have a fixed size; remove the size or use 'const' (e.g., mut %s %.*s] = ...)") \
    GRAY_ERROR("E3055", "types", "const arrays must have a fixed size; declare as [T, N] (e.g., const %s [%.*s, %d] = ...)") \
    GRAY_ERROR("E3056", "types", "#strict when is not exhaustive; missing variant '%s.%s'") \
    GRAY_ERROR("E3057", "types", "type '%s' cannot be used as a map key; only primitive types (int, string, bool, char, byte, float) and enums are hashable") \
    GRAY_ERROR("E3058", "types", "in instantiation of generic function '%s' with '?' = %s") \
    GRAY_ERROR("E3059", "types", "maps cannot be declared const; use 'mut' for maps or a struct for fixed data") \
    GRAY_ERROR("E3060", "types", "wildcard '?' in return type cannot be resolved; at least one parameter must also use '?' to bind the concrete type") \
    GRAY_ERROR("E3061", "types", "struct '%s' cannot contain itself by value through '%s'; break the cycle with a pointer field '^%s'") \
    GRAY_ERROR("E3062", "types", "'%s' cannot be declared const; use 'mut' (every operation on a '%s' mutates its state)") \
    GRAY_ERROR("E3063", "types", "cannot return 'addr(%s)'; '%s' is a local variable whose memory is freed when this function returns") \
    GRAY_ERROR("E3064", "types", "'%s(%s)' called again; '%s' was already destroyed") \
    GRAY_ERROR("E3066", "types", "function reference signature mismatch; expected and actual function types differ") \
    GRAY_ERROR("E3068", "types", "'void' is not a user-facing type; omit the '-> R' clause to declare a function with no return value") \
    GRAY_ERROR("E3069", "types", "'&' on a parameter must come before the name, not the type; write '&%s %s' to mark this parameter mutable") \
    GRAY_ERROR("E3070", "types", "'ensure' may only appear at the top level of a function body; lift it out of the enclosing block") \
    GRAY_ERROR("E3071", "types", "cannot 'return nil' from a function whose return type contains '?'; 'nil' is not a valid value for every binding (e.g. int, string)") \
    GRAY_ERROR("E3072", "types", "cannot return 'nil' from a function that returns '%s'; nil is only valid for pointer and error types") \
    GRAY_ERROR("E3073", "types", "'return' is not allowed in 'main()'; 'main()' exits when control reaches the closing brace") \
    GRAY_ERROR("E3074", "types", "arrays cannot be compared with comparison operators; use 'arrays.is_equal(a, b)' for equality, or compare elements individually for ordering") \
    GRAY_ERROR("E3075", "types", "chained struct function calls are not supported; assign the intermediate result to a variable, then call the next struct function on it") \
    GRAY_ERROR("E3076", "types", "maps cannot be compared with comparison operators; use 'maps.is_equal(a, b)' for equality (maps have no defined ordering)") \
    GRAY_ERROR("E3077", "types", "structs cannot be compared with comparison operators; compare individual fields instead (e.g., 'a.x == b.x', 'a.x < b.x')") \
    GRAY_ERROR("E3078", "types", "pointer arithmetic is not supported; '^T' is the address of one value, not a buffer") \
    GRAY_ERROR("E3079", "types", "cannot take a mutable reference to a const variable; declare the reference as 'const', or 'copy()' the value to get an independent mutable instance") \
    GRAY_ERROR("E3080", "types", "function must return named variable '%s', not a different expression") \
    GRAY_ERROR("E3081", "types", "function '%s' used as a statement without being called; did you mean '%s()'?") \
    GRAY_ERROR("E3082", "types", "wildcard type '?' cannot be used in named return positions; use an unnamed return instead") \
    GRAY_ERROR("E3083", "types", "'c_string()' requires a raw C pointer; cannot convert a non-pointer type") \
    GRAY_ERROR("E3084", "types", "'type_of()' expects a value, not a type name; use 'type_of(instance)' instead") \
    GRAY_ERROR("E3085", "types", "'in' operator type mismatch: cannot check if '%s' is in '%s'") \
    GRAY_ERROR("E3086", "types", "'fmt.%s' format string must be a string literal; use string interpolation for dynamic values") \
    GRAY_ERROR("E3087", "types", "'%%n' is not permitted in fmt format strings") \
    GRAY_ERROR("E3088", "types", "'fmt.%s' format directive '%%%s' expects %s but argument %d has type '%s'") \
    GRAY_ERROR("E3089", "usage", "'%s()' can fail; use 'mut val, err = %s()' to handle the error, or 'mut val, _ = %s()' to discard it") \
    GRAY_ERROR("E3090", "types", "'!' only works on bool; got '%s'") \
    GRAY_ERROR("E3091", "types", "'%s' cannot be used as a condition") \
    GRAY_ERROR("E3092", "types", "cannot compare '%s' to nil; only Error types and pointers can be nil") \
    GRAY_ERROR("E3093", "types", "cannot use '%s' on '%s'") \
    GRAY_ERROR("E3094", "types", "cannot assign '%s' to element of '%s'") \
    GRAY_ERROR("E3095", "types", "'in' only works with arrays, maps, and strings; got '%s'") \
    GRAY_ERROR("E3096", "types", "cannot negate unsigned type '%s'; negation of unsigned types is not defined") \
    GRAY_ERROR("E3097", "safety", "pointer '%s' assigned address of inner-scope variable '%s'; the variable's memory is freed when the scope exits") \
    GRAY_ERROR("E3098", "types", "type mismatch: cannot assign '%s' to '%s' through pointer dereference") \
    GRAY_ERROR("E3099", "types", "'%s' is a stdlib type name and the module that provides it is imported; rename this type or drop the import") \
    GRAY_ERROR("E3100", "types", "type name '%s' cannot be used as a value") \
    GRAY_ERROR("E3101", "types", "func reference variables must be declared with 'const', not 'mut'; func references are compile-time aliases") \
    GRAY_ERROR("E3102", "types", "function '%s' returns a func type; func references cannot be assigned from function return values. Use '()func_name' or 'ref(func_name)' to create a func reference") \
    GRAY_ERROR("E3103", "types", "#json struct '%s' cannot have func-typed field '%s'; func references have no JSON representation") \
    GRAY_ERROR("E3104", "types", "#json struct '%s' cannot declare functions; #json structs are data-only — move '%s' to a standalone function") \
    GRAY_ERROR("E3105", "types", "'fmt.%s': unknown format directive '%%%c'") \
    GRAY_ERROR("E3106", "types", "'fmt.%s': dangling '%%' at end of format string") \
    GRAY_ERROR("E3107", "types", "'fmt.%s': format string has %d directive(s) but %d argument(s) were passed (too few)") \
    GRAY_ERROR("E3108", "types", "'fmt.%s': format string has %d directive(s) but %d argument(s) were passed (too many)") \
    GRAY_ERROR("E3109", "types", "#json struct '%s' cannot have default field values; field '%s' has a default") \
    GRAY_ERROR("E3110", "types", "implicit enum selector '.%s' requires type context; use the full form 'EnumName.%s' or add a type annotation") \
    GRAY_ERROR("E3111", "types", "payload types are not allowed on string enum variants") \
    GRAY_ERROR("E3112", "types", "payload types are not allowed on #flags enum variants") \
    GRAY_ERROR("E3113", "types", "variant '%s' of enum '%s' expects %d payload value(s), got %d") \
    GRAY_ERROR("E3114", "types", "variant '%s' of enum '%s' has no payload; remove the arguments") \
    GRAY_ERROR("E3115", "types", "enum '%s' is not a tagged enum; variant '%s' cannot be called") \
    GRAY_ERROR("E3116", "types", "wrong number of bindings for variant '%s'; expected %d, got %d") \
    GRAY_ERROR("E3117", "types", "cannot compare enum '%s' with '%s'; use an enum variant like '%s.VARIANT', or cast to int with 'cast(value, int)'") \
    GRAY_ERROR("E3118", "types", "cannot assign '%s' to enum '%s'; use an enum variant like '%s.VARIANT'") \
    GRAY_ERROR("E3119", "types", "fixed-size arrays are not allowed in function parameters; use '[%s]' instead of '%s' for parameter '%s'") \
    GRAY_ERROR("E3120", "types", "pointer ordering comparisons are not supported; only == and != are allowed on pointers") \
    GRAY_ERROR("E3121", "types", "cannot use '%s' as a condition in a when statement; allowed types are int, uint, string, char, byte, bool, float, and enum") \
    GRAY_ERROR("E3122", "safety", "cannot modify value through pointer '%s'; the pointee is a const-declared variable") \
    GRAY_ERROR("E3123", "iteration", "'for_each' with both positions discarded accesses nothing; use 'for _ in range(0, len(collection))' to iterate by count") \
    GRAY_ERROR("E3124", "types", "operator '%s' is not defined for tagged enum '%s'; tagged enums carry payloads and cannot be compared with == or !=") \
    GRAY_ERROR("E3125", "types", "'%s' is not a compile-time integer constant; array size must be a const int/uint value") \
    GRAY_ERROR("E3126", "types", "array size must be greater than zero; '%s' resolves to %d") \
    GRAY_ERROR("E3127", "types", "type parameter '%s' is used as a struct literal, so it accepts only struct types, but '%s' is not a struct") \
    GRAY_ERROR("E3128", "types", "type parameter expects a type name, but got a non-type expression; pass a type name like 'MyStruct' or 'int'") \
    GRAY_ERROR("E3129", "safety", "empty loop body; this will loop forever at runtime") \
    GRAY_ERROR("E3130", "types", "bare 'func' is not allowed as a struct field type") \
    GRAY_ERROR("E3131", "types", "file-scope 'const' requires an explicit type annotation; write 'const %s %s = ...' instead") \
    GRAY_ERROR("E3132", "types", "alias target type '%s' is not defined") \
    GRAY_ERROR("E3133", "types", "alias '%s' creates a circular reference") \
    GRAY_ERROR("E3134", "types", "alias '%s' cannot target a module-qualified type; only local types can be aliased") \
    GRAY_ERROR("E3135", "types", "alias '%s' cannot target the wildcard type '?'") \
    GRAY_ERROR("E3136", "types", "empty array literal has no elements to infer a type from; add at least one element or use a typed declaration") \
    GRAY_ERROR("E3137", "types", "constant division overflows; %lld / %lld cannot be represented in type '%s'") \
    GRAY_ERROR("E3138", "types", "float literal overflows 64-bit float; max magnitude is 1.7976931348623157e308") \
    GRAY_ERROR("E3139", "types", "returns %s '%s', but declares the concrete return type '%s'; the value's type is whatever the caller passes, so it is not always '%s'") \
    GRAY_ERROR("E3140", "types", "#json struct '%s' field '%s' has type '%s', which has no JSON representation; #json fields must be int, uint, float, string, or bool") \
    GRAY_ERROR("E3141", "types", "'%s' is a struct, not an enum; it has no variant or member '%s'") \
    GRAY_ERROR("E3142", "types", "function '%s' cannot have a func return type; a returned func value cannot be called, assigned, or stored")

/* --- E4xxx: Name Problems (References) --- */
#define GRAY_REFERENCE_ERRORS \
    GRAY_ERROR("E4001", "names", "this variable does not exist; check the spelling or make sure it is declared above this line") \
    GRAY_ERROR("E4002", "names", "this function does not exist; check the spelling or make sure it is defined") \
    GRAY_ERROR("E4003", "names", "variable '%s' already declared in this scope (line %d)") \
    GRAY_ERROR("E4004", "names", "function '%s' already declared") \
    GRAY_ERROR("E4005", "names", "module '%s' has no function named '%s'") \
    GRAY_ERROR("E4006", "names", "name '%s' uses reserved prefix ('gray_', '_gray_', 'Gray'); these are reserved for the compiler") \
    GRAY_ERROR("E4007", "names", "a type with this name already exists; each struct and enum must have a unique name") \
    GRAY_ERROR("E4008", "names", "'main()' cannot have parameters or a return type; it must be declared as 'do main() { }'") \
    GRAY_ERROR("E4012", "names", "variable '%s' shadows a type definition with the same name") \
    GRAY_ERROR("E4013", "names", "variable '%s' shadows a function with the same name") \
    GRAY_ERROR("E4014", "names", "variable '%s' shadows an imported module with the same name") \
    GRAY_ERROR("E4015", "names", "'%s' is private and cannot be accessed from outside its file") \
    GRAY_ERROR("E4016", "names", "undefined type '%s'; check the spelling or import the module that defines it") \
    GRAY_ERROR("E4017", "names", "function '%s.%s' is private and cannot be called from outside the struct") \
    GRAY_ERROR("E4018", "names", "struct '%s' has no function named '%s'") \
    GRAY_ERROR("E4019", "names", "cannot take a function reference to '%s'; builtin and stdlib functions are not first-class values") \
    GRAY_ERROR("E4020", "names", "type alias '%s' is already declared") \
    GRAY_ERROR("E4021", "names", "type alias '%s' is private and cannot be accessed from outside its file") \
    GRAY_ERROR("E4022", "names", "struct function '%s.%s' conflicts with the top-level function '%s'; a bare call inside the struct resolves to the top-level one, so rename one of them") \
    GRAY_ERROR("E4023", "names", "module '%s' has no function named '%s'") \
    GRAY_ERROR("E4024", "names", "module '%s' has no member named '%s'") \
    GRAY_ERROR("E4025", "names", "public alias '%s' cannot target private type '%s'; the alias would re-export it, so mark the alias 'private' or make the type public")

/* --- E5xxx: Usage Problems --- */
#define GRAY_USAGE_ERRORS \
    GRAY_ERROR("E5007", "usage", "cannot modify immutable %s '%s'; declare with 'mut' to allow modification") \
    GRAY_ERROR("E5008", "arguments", "wrong number of arguments; the function expects a different count than was provided") \
    GRAY_ERROR("E5009", "arguments", "invalid base for integer conversion; base must be between 2 and 36") \
    GRAY_ERROR("E5011", "usage", "return value of '%s' is not used; assign it to a variable or use '_' to discard") \
    GRAY_ERROR("E5012", "usage", "the throwaway '_' is only meaningful when discarding the result of a function call; the right-hand side has no return value to discard") \
    GRAY_ERROR("E5013", "usage", "function calls are not allowed in file-scope initializers; move this declaration into a function body") \
    GRAY_ERROR("E5014", "usage", "'here()' takes no arguments; the call site's file, line, and column are substituted at compile time") \
    GRAY_ERROR("E5015", "usage", "postfix '++' and '--' require a variable, not a value or expression") \
    GRAY_ERROR("E5016", "naming", "this name is reserved by a builtin function and cannot be redeclared") \
    GRAY_ERROR("E5017", "usage", "'embed()' argument must be a string literal file path, not an expression") \
    GRAY_ERROR("E5018", "usage", "'embed()' cannot open '%s': file not found or unreadable") \
    GRAY_ERROR("E5023", "usage", "cannot use '%s' on type '%s'; only integer types support increment/decrement") \
    GRAY_ERROR("E5024", "usage", "return type mismatch: cannot return signed '%s' as unsigned '%s'") \
    GRAY_ERROR("E5025", "usage", "invalid assignment target; left side of '=' must be a variable, field, or index expression") \
    GRAY_ERROR("E5026", "arguments", "argument type mismatch; the function expects a different type than what was provided") \
    GRAY_ERROR("E5027", "usage", "'embed()' path must not escape the source file's directory tree") \
    GRAY_ERROR("E5028", "usage", "func references are not printable values; func references cannot be passed to print functions") \
    GRAY_ERROR("E5029", "usage", "'copy()' cannot be used on a func reference; func references are compile-time aliases, not copyable values") \
    GRAY_ERROR("E5030", "usage", "cannot call the return value of '%s' directly; func references must be created with '()func_name' or 'ref(func_name)' before calling") \
    GRAY_ERROR("E5031", "usage", "unknown parameter name '%s' in call to '%s'") \
    GRAY_ERROR("E5032", "usage", "parameter '%s' is already provided positionally (argument %d) in call to '%s'") \
    GRAY_ERROR("E5033", "usage", "positional argument after named argument in call to '%s'") \
    GRAY_ERROR("E5034", "usage", "named arguments are not supported for builtin function '%s'") \
    GRAY_ERROR("E5035", "naming", "this name is reserved by a standard library module and cannot be redeclared") \
    GRAY_ERROR("E5036", "usage", "'%s' is a type, not a function; use 'cast(value, %s)' to convert") \
    GRAY_ERROR("E5037", "usage", "'copy()' cannot be applied to a pointer; dereference first with 'copy(p^)'") \
    GRAY_ERROR("E5038", "usage", "tagged enum '%s' cannot be passed to '%s()'; use when/is to destructure the payload first") \
    GRAY_ERROR("E5039", "usage", "constant expression overflows type '%s'") \
    GRAY_ERROR("E5040", "usage", "constant requires a compile-time value; function calls are evaluated at runtime") \
    GRAY_ERROR("E5042", "usage", "'#discard' attribute is not allowed on void function '%s'; only functions that return a value can use '#discard'") \
    GRAY_ERROR("E5043", "usage", "'fields()' requires a struct instance, got '%s'") \
    GRAY_ERROR("E5044", "usage", "'error()' argument must be a string, got '%s'")

/* --- E6xxx: Import Problems --- */
#define GRAY_IMPORT_ERRORS \
    GRAY_ERROR("E6001", "imports", "unknown module '@%s'") \
    GRAY_ERROR("E6002", "imports", "cannot find file or directory '%s'") \
    GRAY_ERROR("E6003", "imports", "directory '%s' contains no .gray files") \
    GRAY_ERROR("E6004", "imports", "cannot import own module directory") \
    GRAY_ERROR("E6005", "imports", "cannot import the program's entry point") \
    GRAY_ERROR("E6006", "imports", "module name derived from the import path is not a usable identifier") \
    GRAY_ERROR("E6007", "imports", "standard library imports cannot be aliased") \
    GRAY_ERROR("E6008", "imports", "cannot assign to '%s.%s'; a module %s is read-only from outside the module that declares it") \
    GRAY_ERROR("E6009", "imports", "'%s' and '%s' produce the same compiled name; one of them would be lost") \
    GRAY_ERROR("E6010", "imports", "unknown module '%s'; no import, struct, or variable by that name is in scope") \
    GRAY_ERROR("E6011", "imports", "module '%s' is already imported in this file")

/* --- E7xxx+: Standard Library --- */
#define GRAY_STDLIB_ERRORS \
    GRAY_ERROR("E7004", "stdlib", "function argument must be an integer, not a float") \
    GRAY_ERROR("E7006", "stdlib", "'threads.spawn()' needs a function reference; use '()function_name' to pass a function") \
    GRAY_ERROR("E7014", "stdlib", "cannot convert %lld to char; value must be a valid Unicode code point (0 or greater)") \
    GRAY_ERROR("E7015", "stdlib", "'len()' is not supported for type '%s'; 'len()' works on string, array, and map types") \
    GRAY_ERROR("E9002", "stdlib", "'arrays.%s()' requires a numeric array, got array of '%s'") \
    GRAY_ERROR("E9003", "stdlib", "'arrays.%s()' requires a function reference; use '()func_name' to pass a function") \
    GRAY_ERROR("E9004", "stdlib", "'arrays.%s()' callback signature mismatch; %s") \
    GRAY_ERROR("E9005", "stdlib", "invalid range: start (%lld) must be less than end (%lld)") \
    GRAY_ERROR("E9006", "stdlib", "'arrays.contains()' does not support arrays of '%s'; only primitive and string element types are supported") \
    GRAY_ERROR("E12001", "stdlib", "'maps.%s()' requires a map argument, got an array") \
    GRAY_ERROR("E12006", "stdlib", "duplicate key in map literal") \
    GRAY_ERROR("E12007", "stdlib", "'maps.contains_value()' does not support maps with '%s' values; only primitive and string value types are supported")

/* --- E8xxx: Bitwise Operators --- */
#define GRAY_BITWISE_ERRORS \
    GRAY_ERROR("E8001", "bitwise", "'%s' can only be used with integers or #flags enums; got '%s' and '%s'") \
    GRAY_ERROR("E8002", "bitwise", "'bit_not' can only be used with integers; got '%s'")

/* --- P0xxx: Runtime Panics --- */
#define GRAY_PANIC_CODES \
    GRAY_PANIC("P0001", "memory",     "cannot allocate from a destroyed arena; mem.destroy() was already called on this arena") \
    GRAY_PANIC("P0002", "memory",     "mem.destroy() called on an arena that was already destroyed; each arena can only be destroyed once") \
    GRAY_PANIC("P0003", "runtime",    "maximum recursion depth exceeded (%d calls deep); your function is calling itself too many times") \
    GRAY_PANIC("P0004", "arithmetic", "addition result is too large; value exceeds the range of int") \
    GRAY_PANIC("P0005", "arithmetic", "subtraction result is too large; value exceeds the range of int") \
    GRAY_PANIC("P0006", "arithmetic", "multiplication result is too large; value exceeds the range of int") \
    GRAY_PANIC("P0007", "arithmetic", "negation result is too large; value exceeds the range of int") \
    GRAY_PANIC("P0008", "arithmetic", "addition result is too large; value exceeds the range of uint") \
    GRAY_PANIC("P0009", "arithmetic", "subtraction result is negative, but uint cannot hold negative values") \
    GRAY_PANIC("P0010", "arithmetic", "multiplication result is too large; value exceeds the range of uint") \
    GRAY_PANIC("P0011", "arithmetic", "%s addition result is too large; value exceeds the range of this type") \
    GRAY_PANIC("P0012", "arithmetic", "%s subtraction result is too large; value exceeds the range of this type") \
    GRAY_PANIC("P0013", "arithmetic", "%s multiplication result is too large; value exceeds the range of this type") \
    GRAY_PANIC("P0014", "arithmetic", "%s negation result is too large; value exceeds the range of this type") \
    GRAY_PANIC("P0015", "arithmetic", "%s addition result is too large; value exceeds the range of this unsigned type") \
    GRAY_PANIC("P0016", "arithmetic", "%s subtraction result is negative, but this unsigned type cannot hold negative values") \
    GRAY_PANIC("P0017", "arithmetic", "%s multiplication result is too large; value exceeds the range of this unsigned type") \
    GRAY_PANIC("P0018", "arithmetic", "cast to %s failed; value %lld is outside the valid range (%lld to %lld)") \
    GRAY_PANIC("P0019", "arithmetic", "cast to %s failed; value %lld is outside the valid range (0 to %llu)") \
    GRAY_PANIC("P0020", "arithmetic", "cannot convert float to int; the value is too large, too small, or NaN") \
    GRAY_PANIC("P0021", "arithmetic", "i128 addition result is too large; value exceeds the range of i128") \
    GRAY_PANIC("P0022", "arithmetic", "i128 subtraction result is too large; value exceeds the range of i128") \
    GRAY_PANIC("P0023", "arithmetic", "i128 multiplication result is too large; value exceeds the range of i128") \
    GRAY_PANIC("P0024", "arithmetic", "u128 addition result is too large; value exceeds the range of u128") \
    GRAY_PANIC("P0025", "arithmetic", "u128 subtraction result is negative, but u128 cannot hold negative values") \
    GRAY_PANIC("P0026", "arithmetic", "u128 multiplication result is too large; value exceeds the range of u128") \
    GRAY_PANIC("P0027", "arithmetic", "i256 addition result is too large; value exceeds the range of i256") \
    GRAY_PANIC("P0028", "arithmetic", "i256 subtraction result is too large; value exceeds the range of i256") \
    GRAY_PANIC("P0029", "arithmetic", "i256 multiplication result is too large; value exceeds the range of i256") \
    GRAY_PANIC("P0030", "arithmetic", "u256 addition result is too large; value exceeds the range of u256") \
    GRAY_PANIC("P0031", "arithmetic", "u256 subtraction result is negative, but u256 cannot hold negative values") \
    GRAY_PANIC("P0032", "arithmetic", "u256 multiplication result is too large; value exceeds the range of u256") \
    GRAY_PANIC("P0033", "bounds",     "index out of bounds; tried to access index %d but the length is %d") \
    GRAY_PANIC("P0034", "iteration",  "cannot modify array during for_each iteration") \
    GRAY_PANIC("P0035", "iteration",  "cannot modify map during for_each iteration") \
    GRAY_PANIC("P0036", "encoding",   "encoding.base64_decode: input length %d is not a multiple of 4") \
    GRAY_PANIC("P0037", "encoding",   "encoding.base64_decode: padding character '=' before end of input") \
    GRAY_PANIC("P0038", "encoding",   "encoding.base64_decode: invalid padding") \
    GRAY_PANIC("P0039", "encoding",   "encoding.base64_decode: invalid character in input") \
    GRAY_PANIC("P0040", "encoding",   "encoding.hex_decode: input length %d is not even") \
    GRAY_PANIC("P0041", "encoding",   "encoding.hex_decode: invalid hex character at position %d") \
    GRAY_PANIC("P0042", "encoding",   "encoding.url_decode: invalid percent-escape at position %d") \
    GRAY_PANIC("P0043", "bounds",     "arrays.insert_at: index %d is out of bounds for an array of length %d") \
    GRAY_PANIC("P0044", "bounds",     "arrays.remove_at: index %d is out of bounds for an array of length %d") \
    GRAY_PANIC("P0045", "bounds",     "arrays.get_first called on an empty array") \
    GRAY_PANIC("P0046", "bounds",     "arrays.get_last called on an empty array") \
    GRAY_PANIC("P0047", "bounds",     "arrays.remove_first called on an empty array") \
    GRAY_PANIC("P0048", "bounds",     "arrays.remove_last called on an empty array") \
    GRAY_PANIC("P0049", "bounds",     "to_char() index out of bounds; index %lld is negative") \
    GRAY_PANIC("P0050", "bounds",     "to_char() index out of bounds; index %lld but string has %lld characters") \
    GRAY_PANIC("P0051", "crypto",     "crypto.random_hex: length must be non-negative (got %lld)") \
    GRAY_PANIC("P0052", "crypto",     "crypto.random_hex: failed to read from /dev/urandom") \
    GRAY_PANIC("P0053", "io",         "io.read_file: input exceeds maximum string length") \
    GRAY_PANIC("P0054", "strconv",    "strconv.to_int: invalid base %d; must be between 2 and 36") \
    GRAY_PANIC("P0055", "strconv",    "strconv.to_int: cannot convert '%s' to int (base %d)") \
    GRAY_PANIC("P0056", "strconv",    "strconv.to_uint: invalid base %d; must be between 2 and 36") \
    GRAY_PANIC("P0057", "strconv",    "strconv.to_uint: cannot convert '%s' to uint (base %d)") \
    GRAY_PANIC("P0058", "strconv",    "strconv.to_uint: cannot convert '%s' to uint; value is negative") \
    GRAY_PANIC("P0059", "strconv",    "strconv.to_float: cannot convert '%s' to float") \
    GRAY_PANIC("P0060", "strconv",    "strconv.to_bool: cannot convert '%s' to bool") \
    GRAY_PANIC("P0061", "memory",     "mem.arena() size %lld bytes exceeds the maximum allowed size of 1 GB") \
    GRAY_PANIC("P0062", "random",     "random.sample() count %d exceeds array length %d") \
    GRAY_PANIC("P0063", "random",     "random.sample() count cannot be negative (%d)") \
    GRAY_PANIC("P0064", "math",       "math.sqrt() requires a non-negative number, got %g") \
    GRAY_PANIC("P0065", "math",       "math.log() requires a positive number, got %g") \
    GRAY_PANIC("P0066", "math",       "math.log2() requires a positive number, got %g") \
    GRAY_PANIC("P0067", "math",       "math.log10() requires a positive number, got %g") \
    GRAY_PANIC("P0068", "math",       "math.asin() requires value in [-1, 1], got %g") \
    GRAY_PANIC("P0069", "math",       "math.acos() requires value in [-1, 1], got %g") \
    GRAY_PANIC("P0070", "math",       "math.factorial() requires a non-negative integer, got %lld") \
    GRAY_PANIC("P0071", "strings",    "strings.replace() result exceeds maximum string length") \
    GRAY_PANIC("P0072", "strings",    "strings.repeat() count cannot be negative (%lld)") \
    GRAY_PANIC("P0073", "strings",    "strings.repeat() result exceeds maximum string length") \
    GRAY_PANIC("P0074", "uuid",       "uuid.parse: invalid UUID string") \
    GRAY_PANIC("P0075", "runtime",    "assertion failed") \
    GRAY_PANIC("P0076", "runtime",    "panic") \
    GRAY_PANIC("P0077", "io",         "io.delete_file() cannot delete a directory; use io.remove_dir() for directories") \
    GRAY_PANIC("P0078", "arithmetic", "division by zero") \
    GRAY_PANIC("P0079", "arithmetic", "%s result is too large; value exceeds the range of this type") \
    GRAY_PANIC("P0080", "runtime",    "nil pointer dereference") \
    GRAY_PANIC("P0081", "runtime",    "key not found in map") \
    GRAY_PANIC("P0082", "bounds",     "string index %d out of bounds (length %d)") \
    GRAY_PANIC("P0083", "runtime",    "sleep duration cannot be negative (%lld)") \
    GRAY_PANIC("P0084", "runtime",    "cannot convert '%s' to int") \
    GRAY_PANIC("P0085", "runtime",    "cannot convert '%s' to float") \
    GRAY_PANIC("P0086", "io",         "io.read_file() cannot read a directory; use io.list_dir() or io.walk() to list directory contents") \
    GRAY_PANIC("P0087", "io",         "io.write_file() cannot write to a directory") \
    GRAY_PANIC("P0088", "io",         "io.append_file() cannot append to a directory") \
    GRAY_PANIC("P0089", "io",         "io.copy_file() cannot copy a directory; use io.walk() to enumerate files and copy them individually") \
    GRAY_PANIC("P0090", "runtime",    "range step cannot be zero") \
    GRAY_PANIC("P0091", "arithmetic", "cannot convert float to uint; the value is negative, too large, or NaN") \
    GRAY_PANIC("P0092", "arithmetic", "shift amount %lld is out of range; must be in [0, 63]") \
    GRAY_PANIC("P0093", "arithmetic", "cast from i128 failed; value is outside the representable range of int64") \
    GRAY_PANIC("P0094", "arithmetic", "cast from i128 failed; value is negative or outside the representable range of uint64") \
    GRAY_PANIC("P0095", "arithmetic", "cast from u128 failed; value exceeds the representable range of int64") \
    GRAY_PANIC("P0096", "arithmetic", "cast from u128 failed; value exceeds the representable range of uint64") \
    GRAY_PANIC("P0097", "arithmetic", "cast from i256 failed; value is outside the representable range of int64") \
    GRAY_PANIC("P0098", "arithmetic", "cast from i256 failed; value is negative or outside the representable range of uint64") \
    GRAY_PANIC("P0099", "arithmetic", "cast from u256 failed; value exceeds the representable range of int64") \
    GRAY_PANIC("P0100", "arithmetic", "cast from u256 failed; value exceeds the representable range of uint64") \
    GRAY_PANIC("P0101", "server",     "server.cors: origin contains CR or LF — HTTP header injection is not allowed") \
    GRAY_PANIC("P0102", "arithmetic", "invalid digit in integer literal") \
    GRAY_PANIC("P0103", "io",         "file path contains an embedded null byte") \
    GRAY_PANIC("P0104", "memory",     "arena memory limit exceeded: attempted to grow beyond the maximum of %zu bytes") \
    GRAY_PANIC("P0105", "time",       "time.parse: cannot parse '%s' with layout '%s'") \
    GRAY_PANIC("P0106", "math",       "math.next_power_of_two() result is too large for int, got %lld") \
    GRAY_PANIC("P0107", "arithmetic", "cast to %s failed; value %lld does not match any variant of %s") \
    GRAY_PANIC("P0108", "threads",    "threads.spawn: failed to create OS thread (%s); the process thread limit was likely reached") \
    GRAY_PANIC("P0109", "threads",    "threads.spawn: out of memory allocating thread state")

/* --- Warnings --- */
#define GRAY_WARNINGS \
    GRAY_WARNING("W1001", "cleanup", "variable is declared but never used; remove it or use it") \
    GRAY_WARNING("W1003", "cleanup", "function is declared but never called; remove it or call it") \
    GRAY_WARNING("W1005", "cleanup", "typed blank identifier; '_' doesn't need a type annotation, use 'mut _ = <expr>' instead") \
    GRAY_WARNING("W1004", "cleanup", "variable is declared with a type but no value; it defaults to that type's zero value") \
    GRAY_WARNING("W1002", "cleanup", "this import is never used; remove it or use a function from the module") \
    GRAY_WARNING("W2002", "safety", "this variable shadows a variable with the same name in an outer scope") \
    GRAY_WARNING("W2003", "safety", "unreachable code; this statement will never execute because it comes after a return") \
    GRAY_WARNING("W2007", "safety", "this variable shadows a global constant or variable") \
    GRAY_WARNING("W2008", "safety", "parameter shadows an enum variant name") \
    GRAY_WARNING("W2011", "safety", "named return value is declared in the signature but no matching variable exists in the function body") \
    GRAY_WARNING("W2012", "safety", "'when' condition is a float; equality checks on floats are imprecise; prefer 'math.abs(x - y) < epsilon'") \
    GRAY_WARNING("W2014", "imports", "intra-directory import already included by directory import") \
    GRAY_WARNING("W3003", "safety", "fixed-size array is not fully initialized; remaining elements will be zero-valued") \
    GRAY_WARNING("W3004", "safety", "pointer may reference memory from a scope that has ended; assigning addr() of an inner-scope variable to an outer-scope pointer") \
    GRAY_WARNING("W3005", "safety", "when statement matches on enum values without #strict and no default; exhaustiveness is not checked") \
    GRAY_WARNING("W3006", "safety", "empty default branch in when statement; unmatched values are silently ignored") \
    GRAY_WARNING("W3007", "deprecation", "reference to a function, struct, or enum marked #deprecated")

/* Look up the canonical message for a code like "E3050" or "W2001".
 * Returns NULL if the code is unknown. The returned pointer is a
 * string literal and lives for the lifetime of the program. */
const char *gray_error_message(const char *code);

#endif
