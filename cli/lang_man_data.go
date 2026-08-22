package main

import "strings"

// LangManEntry holds documentation for a single language construct.
type LangManEntry struct {
	Kind    string // "keyword", "type", "symbol", "attribute"
	Syntax  string
	Desc    string
	Example string
}

// langManDocs is the lookup table used by gray man for language reference entries.
var langManDocs = map[string]LangManEntry{
	// ── Keywords: Control Flow ──────────────────────────────────────────
	"if": {
		Kind:    "keyword",
		Syntax:  "if <condition> { ... }",
		Desc:    "Executes the block if the condition is true.",
		Example: "if x > 0 {\n    println(\"positive\")\n}",
	},
	"else": {
		Kind:    "keyword",
		Syntax:  "} else {",
		Desc:    "Alias for 'otherwise'. Default branch of an if/or chain.",
		Example: "if ok {\n    println(\"yes\")\n} else {\n    println(\"no\")\n}",
	},
	"otherwise": {
		Kind:    "keyword",
		Syntax:  "} otherwise {",
		Desc:    "Default branch of an if/or chain. Alias: 'else'.",
		Example: "if x > 0 {\n    println(\"pos\")\n} otherwise {\n    println(\"non-pos\")\n}",
	},
	"or": {
		Kind:    "keyword",
		Syntax:  "} or <condition> {",
		Desc:    "Else-if branch following an 'if'; introduces another condition. Equivalent to 'else if' in other languages. Alias: 'elif'.",
		Example: "if x < 0 {\n    println(\"neg\")\n} or x == 0 {\n    println(\"zero\")\n}",
	},
	"elif": {
		Kind:    "keyword",
		Syntax:  "} elif <condition> {",
		Desc:    "Alias for 'or'. Else-if branch following an 'if'. Pairs with 'else': an 'elif' chain must close with 'else', not 'otherwise'.",
		Example: "if x < 0 {\n    println(\"neg\")\n} elif x == 0 {\n    println(\"zero\")\n} else {\n    println(\"pos\")\n}",
	},
	"for": {
		Kind:    "keyword",
		Syntax:  "for <var> in range(<start>, <end>) { ... }",
		Desc:    "Iterates over a range of integers. The loop variable is scoped to the block.",
		Example: "for i in range(0, 5) {\n    println(i)\n}",
	},
	"for_each": {
		Kind:    "keyword",
		Syntax:  "for_each [index,] item in <collection> { ... }",
		Desc:    "Iterates over every element in an array, map, or string. Optional index variable precedes the value.",
		Example: "for_each i, v in items {\n    println(\"${i}: ${v}\")\n}",
	},
	"while": {
		Kind:    "keyword",
		Syntax:  "while <condition> { ... }",
		Desc:    "Condition-based loop; executes until condition is false. Alias for 'as_long_as'.",
		Example: "while count < 10 {\n    count++\n}",
	},
	"as_long_as": {
		Kind:    "keyword",
		Syntax:  "as_long_as <condition> { ... }",
		Desc:    "Condition-based loop; executes until condition is false. Alias: 'while'.",
		Example: "as_long_as x > 0 {\n    x--\n}",
	},
	"loop": {
		Kind:    "keyword",
		Syntax:  "loop { ... }",
		Desc:    "Creates an infinite loop that runs until 'break' or 'return' exits it.",
		Example: "loop {\n    mut s = input()\n    if s == \"quit\" { break }\n}",
	},
	"when": {
		Kind:    "keyword",
		Syntax:  "when <expr> { is <val> { ... } default { ... } }",
		Desc:    "Pattern-matching statement. Matches the expression against literal values, ranges, or enum variants. Alias: 'switch'.",
		Example: "when x {\n    is 1 { println(\"one\") }\n    is 2, 3 { println(\"two or three\") }\n    default { println(\"other\") }\n}",
	},
	"is": {
		Kind:    "keyword",
		Syntax:  "is <value> { ... }",
		Desc:    "A branch inside a 'when' block that matches a specific value, list of values, range, or enum variant. Alias: 'case'.",
		Example: "when dir {\n    is Direction.NORTH { println(\"north\") }\n    default { println(\"other\") }\n}",
	},
	"switch": {
		Kind:    "keyword",
		Syntax:  "switch <expr> { case <val> { ... } default { ... } }",
		Desc:    "Alias for 'when'. Pattern-matching statement. Pairs with 'case': a 'switch' must use 'case' branches, not 'is'.",
		Example: "switch x {\n    case 1 { println(\"one\") }\n    case 2, 3 { println(\"two or three\") }\n    default { println(\"other\") }\n}",
	},
	"case": {
		Kind:    "keyword",
		Syntax:  "case <value> { ... }",
		Desc:    "Alias for 'is'. A branch inside a 'switch' block that matches a specific value, list of values, range, or enum variant.",
		Example: "switch dir {\n    case Direction.NORTH { println(\"north\") }\n    default { println(\"other\") }\n}",
	},
	"break": {
		Kind:    "keyword",
		Syntax:  "break",
		Desc:    "Terminates the innermost enclosing loop immediately.",
		Example: "for i in range(0, 100) {\n    if i == 5 { break }\n}",
	},
	"continue": {
		Kind:    "keyword",
		Syntax:  "continue",
		Desc:    "Skips the remainder of the current loop iteration and begins the next.",
		Example: "for i in range(0, 10) {\n    if i % 2 == 0 { continue }\n    println(i)\n}",
	},
	"default": {
		Kind:    "keyword",
		Syntax:  "default { ... }",
		Desc:    "The fallback branch inside a 'when' block, executed when no 'is' branch matches.",
		Example: "when status {\n    is 0 { println(\"ok\") }\n    default { println(\"fail\") }\n}",
	},
	"return": {
		Kind:    "keyword",
		Syntax:  "return [value]",
		Desc:    "Exits the current function, optionally returning a value.",
		Example: "do add(a int, b int) -> int {\n    return a + b\n}",
	},

	// ── Keywords: Error Handling ────────────────────────────────────────
	"ensure": {
		Kind:    "keyword",
		Syntax:  "ensure <function_call>()",
		Desc:    "Registers a function to be called when the enclosing function exits, whether via normal return or early return. Alias: 'defer'.",
		Example: "do process() {\n    ensure cleanup()\n    // ... do work ...\n}",
	},
	"defer": {
		Kind:    "keyword",
		Syntax:  "defer <function_call>()",
		Desc:    "Alias for 'ensure'. Registers a function to be called when the enclosing function exits, whether via normal return or early return.",
		Example: "do process() {\n    defer cleanup()\n    // ... do work ...\n}",
	},
	"or_return": {
		Kind:    "keyword",
		Syntax:  "<expr> or_return [fallback_values]",
		Desc:    "Error propagation shorthand: if the expression returns a non-nil Error, immediately returns from the enclosing function.",
		Example: "do load() -> (string, Error) {\n    mut content = read_file(\"f\") or_return\n    return content, nil\n}",
	},

	// ── Keywords: Declarations ──────────────────────────────────────────
	"mut": {
		Kind:    "keyword",
		Syntax:  "mut <name> [Type] = <value>",
		Desc:    "Declares a mutable variable. Must be initialized at declaration. Can be reassigned.",
		Example: "mut count int = 0\nmut name = \"Alice\"",
	},
	"const": {
		Kind:    "keyword",
		Syntax:  "const <name> [Type] = <value>",
		Desc:    "Declares an immutable constant or defines a struct/enum type. Cannot be reassigned.",
		Example: "const PI float = 3.14159\nconst MAX int = 100",
	},
	"do": {
		Kind:    "keyword",
		Syntax:  "do <name>(<params>) [-> <ReturnType>] { ... }",
		Desc:    "Declares a function. Introduces both top-level functions and struct-namespaced functions. Alias: 'fn'.",
		Example: "do add(a int, b int) -> int {\n    return a + b\n}",
	},
	"fn": {
		Kind:    "keyword",
		Syntax:  "fn <name>(<params>) [-> <ReturnType>] { ... }",
		Desc:    "Alias for 'do'. Declares a function, at the top level or inside a struct body.",
		Example: "fn add(a int, b int) -> int {\n    return a + b\n}",
	},
	"struct": {
		Kind:    "keyword",
		Syntax:  "const <Name> struct { <field> <Type> ... }",
		Desc:    "Defines a composite type with named fields. Must be declared at the top level using 'const'.",
		Example: "const Point struct {\n    x int\n    y int\n}",
	},
	"enum": {
		Kind:    "keyword",
		Syntax:  "const <Name> enum { VARIANT ... }",
		Desc:    "Defines a type with a fixed set of named values. Variants auto-increment from 0 by default.",
		Example: "const Direction enum {\n    NORTH\n    EAST\n    SOUTH\n    WEST\n}",
	},
	"import": {
		Kind:    "keyword",
		Syntax:  "import @<module> | import \"<path>\"",
		Desc:    "Brings a standard library module or a local file/directory into scope.",
		Example: "import @math, @strings\nimport \"./helpers\"",
	},
	"using": {
		Kind:    "keyword",
		Syntax:  "using <module>",
		Desc:    "Brings module members into scope for unqualified access. Can appear at file scope or function scope.",
		Example: "import @strings\nusing strings\nprintln(to_upper(\"hello\"))",
	},
	// "new" is a builtin; reachable via gray man keywords listing
	"new": {
		Kind:    "keyword",
		Syntax:  "new(<Type>) -> ^<Type>",
		Desc:    "Allocates a zero-initialized value of any type on the heap and returns a pointer to it. Works with primitives, structs, arrays, maps, and all other types.",
		Example: "mut p = new(Point)\np.x = 10\nmut n = new(int)\nn^ = 42",
	},
	"private": {
		Kind:    "keyword",
		Syntax:  "private do <name>(...) | private const <name>",
		Desc:    "Restricts a function or constant to the declaring module; not accessible from other modules.",
		Example: "private do validate(n int) -> bool {\n    return n > 0\n}",
	},
	"alias": {
		Kind:    "keyword",
		Syntax:  "alias <Name> = <Type>",
		Desc:    "Creates an interchangeable name for an existing type. Aliases are erased at compile time; type_of() returns the underlying type. Can alias primitives, structs, enums, and collections. File-scope only. Use 'private alias' to restrict to the declaring file.",
		Example: "alias Meters = float\nalias Vec2 = Point\nalias Names = [string]\n\ndo main() {\n    mut d Meters = 10.5\n    println(type_of(d))  // float\n}",
	},

	// ── Keywords: Operators ─────────────────────────────────────────────
	"in": {
		Kind:    "keyword",
		Syntax:  "<value> in <collection_or_range>",
		Desc:    "Membership test operator. Returns true if the value is in the array, map (by key), or range.",
		Example: "if 3 in numbers { println(\"found\") }\nif x in range(0, 10) { println(\"ok\") }",
	},
	"not_in": {
		Kind:    "keyword",
		Syntax:  "<value> not_in <collection>",
		Desc:    "Non-membership test. Returns true if value is not in the collection or range. Alias: '!in'.",
		Example: "if \"key\" not_in m { println(\"missing\") }",
	},

	// ── Keywords: Literals ──────────────────────────────────────────────
	"true": {
		Kind:    "keyword",
		Syntax:  "true",
		Desc:    "Boolean literal representing the true value.",
		Example: "mut flag bool = true",
	},
	"false": {
		Kind:    "keyword",
		Syntax:  "false",
		Desc:    "Boolean literal representing the false value.",
		Example: "mut flag bool = false",
	},
	"nil": {
		Kind:    "keyword",
		Syntax:  "nil",
		Desc:    "Represents the absence of a value. Used with pointers and Error returns.",
		Example: "mut p ^int = nil\nif err != nil { panic(err) }",
	},

	// ── Types ───────────────────────────────────────────────────────────
	"int_type": {
		Kind:    "type",
		Syntax:  "int",
		Desc:    "64-bit signed integer. Arithmetic is overflow-checked; overflow causes a runtime panic.",
		Example: "mut x int = 42\nmut big int = 9223372036854775807",
	},
	"uint_type": {
		Kind:    "type",
		Syntax:  "uint",
		Desc:    "64-bit unsigned integer. Overflow-checked; assigning a negative literal is a compile-time error.",
		Example: "mut count uint = 100",
	},
	"float_type": {
		Kind:    "type",
		Syntax:  "float",
		Desc:    "64-bit IEEE 754 double-precision float. Division by zero panics at runtime.",
		Example: "mut pi float = 3.14159\nmut x float = 5  // promoted to 5.0",
	},
	"string_type": {
		Kind:    "type",
		Syntax:  "string",
		Desc:    "UTF-8 encoded byte sequence. len() returns byte length; use char_count() for codepoints. Supports ${expr} interpolation.",
		Example: "mut s string = \"Hello, ${name}!\"\nmut raw string = `C:\\path\\file`",
	},
	"bool_type": {
		Kind:    "type",
		Syntax:  "bool",
		Desc:    "Boolean type with exactly two values: true and false.",
		Example: "mut flag bool = true\nmut ok bool = 10 > 5",
	},
	"char_type": {
		Kind:    "type",
		Syntax:  "char",
		Desc:    "Single character type. Literals use single quotes. Convertible to/from int (Unicode codepoint).",
		Example: "mut letter char = 'A'\nmut code int = int(letter)  // 65",
	},
	"byte_type": {
		Kind:    "type",
		Syntax:  "byte",
		Desc:    "8-bit unsigned integer, range 0-255. Used for raw binary data.",
		Example: "mut b byte = 0xFF\nmut c byte = 128",
	},
	"i8_type": {
		Kind:    "type",
		Syntax:  "i8",
		Desc:    "8-bit signed integer, range -128 to 127.",
		Example: "mut x i8 = -100",
	},
	"i16_type": {
		Kind:    "type",
		Syntax:  "i16",
		Desc:    "16-bit signed integer, range -32,768 to 32,767.",
		Example: "mut x i16 = 1000",
	},
	"i32_type": {
		Kind:    "type",
		Syntax:  "i32",
		Desc:    "32-bit signed integer, range -2^31 to 2^31-1.",
		Example: "mut x i32 = cast(n, i32)",
	},
	"i64_type": {
		Kind:    "type",
		Syntax:  "i64",
		Desc:    "64-bit signed integer, range -2^63 to 2^63-1.",
		Example: "mut x i64 = cast(n, i64)",
	},
	"u8_type": {
		Kind:    "type",
		Syntax:  "u8",
		Desc:    "8-bit unsigned integer, range 0 to 255.",
		Example: "mut x u8 = cast(42, u8)",
	},
	"u16_type": {
		Kind:    "type",
		Syntax:  "u16",
		Desc:    "16-bit unsigned integer, range 0 to 65,535.",
		Example: "mut x u16 = cast(n, u16)",
	},
	"u32_type": {
		Kind:    "type",
		Syntax:  "u32",
		Desc:    "32-bit unsigned integer, range 0 to 2^32-1.",
		Example: "mut x u32 = cast(n, u32)",
	},
	"u64_type": {
		Kind:    "type",
		Syntax:  "u64",
		Desc:    "64-bit unsigned integer, range 0 to 2^64-1.",
		Example: "mut x u64 = cast(n, u64)",
	},
	"i128_type": {
		Kind:    "type",
		Syntax:  "i128",
		Desc:    "128-bit signed integer. Struct-based portable implementation. Supports all arithmetic and comparison operators.",
		Example: "mut a i128 = i128(42)\nmut b i128 = a + i128(100)",
	},
	"u128_type": {
		Kind:    "type",
		Syntax:  "u128",
		Desc:    "128-bit unsigned integer. Struct-based portable implementation.",
		Example: "mut a u128 = u128(18446744073709551616)",
	},
	"i256_type": {
		Kind:    "type",
		Syntax:  "i256",
		Desc:    "256-bit signed integer. Struct-based portable implementation.",
		Example: "mut a i256 = i256(42)\nprintln(size_of(i256))  // 32",
	},
	"u256_type": {
		Kind:    "type",
		Syntax:  "u256",
		Desc:    "256-bit unsigned integer. Struct-based portable implementation.",
		Example: "mut a u256 = u256(0)",
	},
	"f32_type": {
		Kind:    "type",
		Syntax:  "f32",
		Desc:    "32-bit IEEE 754 single-precision float. Maps to C float.",
		Example: "mut x f32 = 1  // 1.0\nmut y f32 = cast(pi, f32)",
	},
	"f64_type": {
		Kind:    "type",
		Syntax:  "f64",
		Desc:    "64-bit IEEE 754 double-precision float. Identical to 'float'. Maps to C double.",
		Example: "mut x f64 = 3.14",
	},
	"Error_type": {
		Kind:    "type",
		Syntax:  "Error",
		Desc:    "Error type returned from fallible functions. Created with error(). Compared to nil to check for success.",
		Example: "do parse(s string) -> (int, Error) {\n    if s == \"\" { return 0, error(\"empty\") }\n    return 42, nil\n}",
	},

	// ── Symbols ─────────────────────────────────────────────────────────
	"^": {
		Kind:    "symbol",
		Syntax:  "^<Type>  /  <ptr>^",
		Desc:    "Pointer sigil. As a type prefix (^int) it denotes a pointer type; as a postfix on a variable (p^) it dereferences the pointer.",
		Example: "mut x int = 42\nmut p ^int = addr(x)\np^ = 100\nprintln(x)  // 100",
	},
	"&": {
		Kind:    "symbol",
		Syntax:  "&<param>",
		Desc:    "Mutable parameter prefix in function signatures. Makes the parameter a mutable alias so writes propagate back to the caller.",
		Example: "do bump(&v Vec) {\n    v.x = v.x + 1\n}",
	},
	"->": {
		Kind:    "symbol",
		Syntax:  "do <name>(...) -> <ReturnType>",
		Desc:    "Return type arrow. Separates the parameter list from the return type in a function declaration.",
		Example: "do square(x int) -> int {\n    return x * x\n}",
	},
	"@": {
		Kind:    "symbol",
		Syntax:  "import @<module>",
		Desc:    "Standard library module prefix in import statements. Distinguishes stdlib modules from local file paths.",
		Example: "import @math, @strings\nmath.sqrt(16.0)",
	},
	"#": {
		Kind:    "symbol",
		Syntax:  "#<attribute>",
		Desc:    "Attribute prefix. Placed before a declaration to attach metadata or modify compiler behavior.",
		Example: "#doc(\"A 2D point\")\nconst Point struct {\n    x int\n    y int\n}",
	},
	"?": {
		Kind:    "symbol",
		Syntax:  "do <name>(x ?) -> ?",
		Desc:    "Wildcard type parameter. Binds to the concrete type of the argument at each call site, enabling generic-style functions.",
		Example: "do identity(x ?) -> ? {\n    return x\n}",
	},
	"<?>": {
		Kind:    "symbol",
		Syntax:  "do <name>(T <?>) -> ^?",
		Desc:    "Type-level generic parameter. Accepts a struct type name (not a value) at the call site, enabling type-aware constructors.",
		Example: "do make(T <?>) -> ^? {\n    return new(T)\n}\nmut p = make(Point)",
	},

	// ── Attributes ──────────────────────────────────────────────────────
	"#doc": {
		Kind:    "attribute",
		Syntax:  "#doc(\"<description>\")",
		Desc:    "Attaches documentation metadata to a function, struct, or enum. Used by 'gray doc' to generate documentation.",
		Example: "#doc(\"Adds two integers\")\ndo add(a int, b int) -> int {\n    return a + b\n}",
	},
	"#json": {
		Kind:    "attribute",
		Syntax:  "#json",
		Desc:    "Marks a struct for JSON serialization/deserialization. The compiler generates marshal/unmarshal code automatically.",
		Example: "#json\nconst User struct {\n    name string\n    age int\n}",
	},
	"#flags": {
		Kind:    "attribute",
		Syntax:  "#flags",
		Desc:    "Marks an enum as a bitflag set. Variant values are powers of 2 (1, 2, 4, 8, ...) instead of sequential.",
		Example: "#flags\nconst Permissions enum {\n    READ\n    WRITE\n    EXECUTE\n}",
	},
	"#strict": {
		Kind:    "attribute",
		Syntax:  "#strict",
		Desc:    "Requires exhaustive coverage of all enum variants in a 'when' block. Errors if any variant is missing without a 'default'.",
		Example: "#strict\nwhen direction {\n    is Direction.NORTH { println(\"N\") }\n    is Direction.EAST  { println(\"E\") }\n    is Direction.SOUTH { println(\"S\") }\n    is Direction.WEST  { println(\"W\") }\n}",
	},
	"#discard": {
		Kind:    "attribute",
		Syntax:  "#discard",
		Desc:    "Allows callers to ignore the return value of a function without triggering E5011. Cannot be applied to void functions (E5042).",
		Example: "#discard\ndo tryInsert(value int) -> bool {\n    return true\n}\n\ndo main() {\n    tryInsert(42)  // OK — no E5011\n}",
	},
}

// langGroup is one labeled group of names within a language category index.
type langGroup struct {
	Label string
	Names []string
}

// langCategories maps category names to their display groups.
var langCategories = map[string][]langGroup{
	"keywords": {
		{Label: "Control flow  ", Names: []string{"if", "else", "otherwise", "or", "elif", "for", "for_each", "while", "as_long_as", "loop", "when", "switch", "is", "case", "break", "continue", "default", "return"}},
		{Label: "Error handling", Names: []string{"ensure", "defer", "or_return"}},
		{Label: "Declarations  ", Names: []string{"mut", "const", "do", "fn", "struct", "enum", "alias", "import", "using", "new", "private"}},
		{Label: "Operators     ", Names: []string{"in", "not_in"}},
		{Label: "Literals      ", Names: []string{"true", "false", "nil"}},
	},
	"types": {
		{Label: "Core   ", Names: []string{"int", "uint", "float", "string", "bool", "char", "byte"}},
		{Label: "Signed ", Names: []string{"i8", "i16", "i32", "i64"}},
		{Label: "Unsigned", Names: []string{"u8", "u16", "u32", "u64"}},
		{Label: "Wide   ", Names: []string{"i128", "u128", "i256", "u256"}},
		{Label: "Float  ", Names: []string{"f32", "f64"}},
		{Label: "Error  ", Names: []string{"Error"}},
	},
	"symbols": {
		{Label: "Symbols", Names: []string{"^", "&", "->", "@", "#", "?", "<?>"}},
	},
	"attributes": {
		{Label: "Attributes", Names: []string{"#doc", "#json", "#flags", "#strict", "#discard"}},
	},
}

// langSymbolAliases maps shell-friendly names to their symbol/attribute keys.
var langSymbolAliases = map[string]string{
	"pointer":   "^",
	"caret":     "^",
	"arrow":     "->",
	"ampersand": "&",
	"at":        "@",
	"hash":      "#",
	"wildcard":  "?",
	"question":  "?",
	// Attribute aliases without # prefix (shell eats #)
	"doc":     "#doc",
	"json":    "#json",
	"flags":   "#flags",
	"strict":  "#strict",
	"discard": "#discard",
}

// langDisplayName strips the _type suffix used to avoid builtin key collisions.
func langDisplayName(key string) string {
	if strings.HasSuffix(key, "_type") {
		return strings.TrimSuffix(key, "_type")
	}
	return key
}
