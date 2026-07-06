# C++ Code Style Guide

This document defines the coding style for this C++ project. The goal is **readability, consistency, and safety**.

---

## Table of Contents

- [Background](#background)
  - [Goals of the Style Guide](#goals-of-the-style-guide)
- [C++ Version](#c-version)
- [Header Files](#header-files)
  - [Self-contained Headers](#self-contained-headers)
  - [The #define Guard](#the-define-guard)
  - [Include What You Use](#include-what-you-use)
  - [Forward Declarations](#forward-declarations)
  - [Defining Functions in Header Files](#defining-functions-in-header-files)
  - [Names and Order of Includes](#names-and-order-of-includes)
- [Scoping](#scoping)
  - [Namespaces](#namespaces)
  - [Internal Linkage](#internal-linkage)
  - [Local Variables](#local-variables)
  - [Static and Global Variables](#static-and-global-variables)
  - [thread_local Variables](#thread_local-variables)
- [Classes](#classes)
  - [Doing Work in Constructors](#doing-work-in-constructors)
  - [Implicit Conversions](#implicit-conversions)
  - [Copyable and Movable Types](#copyable-and-movable-types)
  - [Structs vs. Classes](#structs-vs-classes)
  - [Structs vs. Pairs and Tuples](#structs-vs-pairs-and-tuples)
  - [Inheritance](#inheritance)
  - [Operator Overloading](#operator-overloading)
  - [Access Control](#access-control)
  - [Declaration Order](#declaration-order)
- [Functions](#functions)
  - [Inputs and Outputs](#inputs-and-outputs)
  - [Write Short Functions](#write-short-functions)
  - [Function Overloading](#function-overloading)
  - [Default Arguments](#default-arguments)
  - [Trailing Return Type Syntax](#trailing-return-type-syntax)
- [Other C++ Features](#other-c-features)
  - [Ownership and Smart Pointers](#ownership-and-smart-pointers)
  - [Rvalue References](#rvalue-references)
  - [Friends](#friends)
  - [Exceptions](#exceptions)
  - [noexcept](#noexcept)
  - [Run-Time Type Information (RTTI)](#run-time-type-information-rtti)
  - [Casting](#casting)
  - [Streams](#streams)
  - [Preincrement and Predecrement](#preincrement-and-predecrement)
  - [Use of const](#use-of-const)
  - [Use of constexpr, constinit, and consteval](#use-of-constexpr-constinit-and-consteval)
  - [Integer Types](#integer-types)
  - [Floating-Point Types](#floating-point-types)
  - [Architecture Portability](#architecture-portability)
  - [Preprocessor Macros](#preprocessor-macros)
  - [0 and nullptr/NULL](#0-and-nullptrnull)
  - [sizeof](#sizeof)
  - [Type Deduction (including auto)](#type-deduction-including-auto)
    - [Function template argument deduction](#function-template-argument-deduction)
    - [Local variable type deduction](#local-variable-type-deduction)
    - [Return type deduction](#return-type-deduction)
    - [Function parameter type deduction](#function-parameter-type-deduction)
    - [Structured bindings](#structured-bindings)
  - [Class Template Argument Deduction](#class-template-argument-deduction)
  - [Designated Initializers](#designated-initializers)
  - [Lambda Expressions](#lambda-expressions)
  - [Template Metaprogramming](#template-metaprogramming)
  - [Concepts and Constraints](#concepts-and-constraints)
  - [C++20 modules](#c20-modules)
  - [Coroutines](#coroutines)
  - [Disallowed standard library features](#disallowed-standard-library-features)
  - [Third-party Libraries](#third-party-libraries)
  - [Nonstandard Extensions](#nonstandard-extensions)
  - [Aliases](#aliases)
  - [Switch Statements](#switch-statements)
- [Naming](#naming)
  - [Choosing Names](#choosing-names)
  - [File Names](#file-names)
  - [Type Names](#type-names)
  - [Concept Names](#concept-names)
  - [Variable Names](#variable-names)
    - [Class Data Members](#class-data-members)
    - [Struct Data Members](#struct-data-members)
  - [Constant Names](#constant-names)
  - [Function Names](#function-names)
  - [Namespace Names](#namespace-names)
  - [Enumerator Names](#enumerator-names)
  - [Template Parameter Names](#template-parameter-names)
  - [Macro Names](#macro-names)
  - [Aliases](#aliases-1)
- [Comments](#comments)
  - [Legal Notice and Author Line](#legal-notice-and-author-line)
- [Formatting](#formatting)
  - [Line Length](#line-length)
  - [Non-ASCII Characters](#non-ascii-characters)
  - [Indentation](#indentation)
  - [Function Declarations and Definitions](#function-declarations-and-definitions)
  - [Lambda Expressions](#lambda-expressions-1)
  - [String Literals](#string-literals)
  - [Floating-point Literals](#floating-point-literals)
  - [Function Calls](#function-calls)
  - [Braced Initializer List Format](#braced-initializer-list-format)
  - [Looping and branching statements](#looping-and-branching-statements)
  - [Parentheses](#parentheses)
  - [Switch statements format](#switch-statements-format)
  - [Pointer and Reference Expressions and Types](#pointer-and-reference-expressions-and-types)
  - [Boolean Expressions](#boolean-expressions)
  - [Return Values](#return-values)
  - [Variable and Array Initialization](#variable-and-array-initialization)
  - [Preprocessor Directives](#preprocessor-directives)
  - [Class Format](#class-format)
  - [Constructor Initializer Lists](#constructor-initializer-lists)
  - [Namespace Formatting](#namespace-formatting)
  - [Horizontal Whitespace](#horizontal-whitespace)
    - [General](#general)
    - [Loops and Conditionals](#loops-and-conditionals)
    - [Operators](#operators)
    - [Templates and Casts](#templates-and-casts)
  - [Vertical Whitespace](#vertical-whitespace)
- [Exceptions to the Rules](#exceptions-to-the-rules)
  - [Existing Non-conformant Code](#existing-non-conformant-code)
- [Tooling](#tooling)
- [References](#references)

---

## Background

- Style covers more than formatting: naming, scoping, ownership, and API design.
- This guide assumes familiarity with C++; it is not a C++ tutorial.

### Goals of the Style Guide

- Optimize for the **reader**, not the writer.
- Be consistent within this codebase and with `.clang-format`.
- Avoid surprising or hard-to-maintain constructs.

---

## C++ Version

- Target **C++20**.
- Avoid non-standard language extensions unless approved; see [Nonstandard Extensions](#nonstandard-extensions).

---

## Header Files

- Every `.cpp` file should have a matching `.h` file when it defines reusable APIs (tests and tiny `main()` files are exceptions).
- Use `*.h` and `*.cpp` extensions.
- Use `snake_case` for file names.
- Every file must include the project license header.

### Self-contained Headers

- Use `#include <...>` for C/C++ standard library headers; `#include "..."` for project headers.
- Headers must compile on their own — include only what they need; put other includes in `.cpp` files.
- Expose only the module's public variables, types, and functions; keep implementation details in `.cpp` files.
- Use the same license header as the rest of the project.
- Do not use `.inl.h` split headers; put template/inline definitions in the header when required.

```cpp
// License comes here
#ifndef PATH_TO_FILE_H
#define PATH_TO_FILE_H

// Include headers

// File content here

#endif // PATH_TO_FILE_H
```

```cpp
// License comes here
#ifndef MY_PROJECT_NET_H
#define MY_PROJECT_NET_H

#include <cstdint>
#include <string>

namespace my_project::net {

class Socket {
public:
    explicit Socket(int fd);
    int send(const std::string &data);

private:
    int fd;
};

} // namespace my_project::net

#endif // MY_PROJECT_NET_H
```

### The #define Guard

- All header files use `#ifndef` / `#define` / `#endif` guards to prevent multiple inclusion.
- Guard name is the file path in uppercase: `PATH_TO_FILE_H`.

```cpp
#ifndef FOO_BAR_BAZ_H
#define FOO_BAR_BAZ_H

...

#endif // FOO_BAR_BAZ_H
```

### Include What You Use

- Include every header whose symbols you directly use.
- Do not rely on a symbol being available through another header's includes.

```cpp
// OK
#include <algorithm>
#include <vector>
std::vector<int> v;

// Wrong
#include <algorithm>
std::vector<int> v;
```

### Forward Declarations

- Prefer `#include` over forward declarations when practical.
- Do not forward-declare symbols you do not own.
- Avoid forward declarations that hide dependencies or break when APIs evolve.

```cpp
// Prefer
#include "other_module.h"

// Only when include cost is high and dependency is stable
class ExpensiveType;
void register_type(const ExpensiveType &value);
```

### Defining Functions in Header Files

- Define functions in headers only when `inline`, `constexpr`, or templates require it.
- Avoid non-trivial function bodies in headers; keep public inline bodies short (roughly ≤10 lines) and put larger bodies in `.cpp` files.

```cpp
// OK in header
inline int square(int x)
{
    return x * x;
}

template<typename T>
T clamp(T val, T lo, T hi)
{
    return std::max(lo, std::min(val, hi));
}
```

### Names and Order of Includes

**Project order**:

1. Own header (in `.cpp` files)
2. Application/project headers
3. Third-party headers
4. C++ standard library headers
5. C/system headers

Separate groups with a blank line; sort alphabetically within each group.

```cpp
// foo.cpp

#include "foo.h"        // 1. own header

#include "bar.h"        // 2. application headers

#include <boost/system/error_code.hpp>  // 3. third-party

#include <iostream>     // 4. standard library

#include <sys/ioctl.h>  // 5. system / OS headers
```

---

## Scoping

### Namespaces

- Place project code in named namespaces.
- Use unique names based on the project name and possibly its path.
- Do not use `using namespace` in headers.
- Do not use inline namespaces.
- Close namespaces with a comment.
- Wrap the entire source file after includes, forward declarations of classes from other namespaces and defines.
- Prefer single-line nested namespaces. 

```cpp
// my_component.h
#include <iostream>

class my_project::other_component;

namespace my_project::my_component {

// All declarations are within the namespace scope.
class MyComponent {
public:
    void foo();
};

} // namespace my_project::my_component

// my_component.cpp
#include "my_component.h"

#include <vector>

namespace my_project::my_component {

// Definition of functions is within scope of the namespace.
void MyComponent::foo()
{
    ...
}

} // namespace my_project::my_component
```

### Internal Linkage

- Use anonymous namespaces for functions, variables, and custom types that are not referenced outside the file.
- Prefer anonymous namespaces over `static`.
- Do not use anonymous namespaces in headers.

```cpp
// OK
namespace {
struct A {
    int a;
};
int var;
void helper()
{
}
} // namespace

// Wrong
static int var;
static void helper()
{
}
```

### Local Variables

- Declare variables in the narrowest scope, as close to first use as possible.
- Initialize at declaration.

```cpp
// OK
int jobs = num_jobs();
func(jobs);

for (int i = 0; i < jobs; ++i) {
}

// Wrong
int jobs;
int i;
...
func(jobs);
for (i = 0; i < jobs; ++i) {
}
```

### Static and Global Variables

- Avoid any dynamic initialization of a global/static object that depends on another non-trivial global/static object in a different translation unit.

```cpp
// OK - trivial
// config.cpp
constexpr int g_config = 1;

// logger.cpp
extern int g_config;
constexpr int g_logger_config = g_config;

// OK - initialization on first use
// config.cpp
const std::string &get_config()
{
    static const std::string config = "project";
    return config;
}

// logger.cpp
std::string get_prefix()
{
    return get_config() + ":";
}

// Wrong - initialization order undefined
// config.cpp
const std::string g_config = "project";

// logger.cpp
const std::string g_prefix = g_config + ":";
```

- Avoid global/file-local static objects whose destructors depend on other non-trivially destructible global/file-local static objects.

```cpp
// OK - no external dependencies
// logger.cpp
struct Logger {
    ~Logger() {
        std::cout << "shutdown\n";
    }
};

Logger g_logger;

// Wrong - destruction order undefined
// config.cpp
std::string g_config = "project";

// logger.cpp
extern std::string g_config;

struct Logger {
    ~Logger() {
        std::cout << "closing logs for " << g_config;
    }
};

Logger g_logger;
```

### thread_local Variables

- Prefer `thread_local` over other ways of defining thread-local data.

```cpp
// OK
thread_local Parser parser;

// Wrong
std::unordered_map<std::thread::id, Parser> data;
```

- Avoid any dynamic initialization of a `thread_local` object that depends on another non-trivial `thread_local` object in a different translation unit.

- Avoid `thread_local` objects whose destructors depend on other non-trivially destructible `thread_local` objects in different translation units.

---

## Classes

### Doing Work in Constructors

- Avoid virtual calls in constructors.
- Keep constructors lightweight.
- Use factories or `init()` for work that can fail (this project does not use exceptions as primary error flow).

```cpp
// OK
Foo::Foo()
{
}

std::error_code Foo::init()
{
    return connect_to_database();
}

// Wrong
Foo::Foo()
{
    connect_to_database();
}
```

### Implicit Conversions

- Mark single-argument constructors and conversion operators `explicit`.
- Copy/move constructors should stay non-`explicit`.
- Constructor with single std::initializer_list parameter should omit `explicit`, in order to support copy-initialization (e.g., `MyType m = {1, 2};`).

```cpp
class Buffer {
public:
    explicit Buffer(size_t size);
    Buffer(const Buffer &buffer);
    Buffer(Buffer &&buffer) noexcept;
    Buffer(std::initializer_list<char> values);
    explicit operator bool() const noexcept;
};
```

### Copyable and Movable Types

- Make copy/move intent explicit (Rule of Five or Rule of Zero).
- Move operations should be `noexcept` when defined.

```cpp
class Socket {
public:
    Socket() = default;
    ~Socket();
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    Socket(Socket &&) noexcept;
    Socket &operator=(Socket &&) noexcept;
};
```

### Structs vs. Classes

- Use `struct` for passive data with public fields and no invariants.
- Use `class` when methods enforce invariants.

```cpp
// OK
struct Point {
    double x, y;
};

// OK
class Circle {
public:
    Circle(const Point &center, double radius);
    int set_radius(double radius);

private:
    Point center;
    double radius;
};
```

### Structs vs. Pairs and Tuples

- Prefer named `struct` over `std::pair` / `std::tuple` when fields have meaning.

```cpp
struct Range {
    double low, high;
};
```

### Inheritance

- Use `override` on all virtual function overrides.
- Do not use `virtual` when declaring an override.
- Polymorphic base classes must have a virtual destructor.

```cpp
// OK
class Base {
public:
    virtual ~Base() = default;
    virtual void process() = 0;
};

class Derived : public Base {
public:
    void process() override;
};
```

### Operator Overloading

- Overload only when meaning matches built-in operators (`==`, `<<`, etc.).
- Do not overload `&&`, `||`, `,` because evaluation order does not match semantics of the built-in operators.
- Do not overload unary `&` because the meaning depends on overload declaration visibility.

### Access Control

- Make class data members `private` unless they are constants.

### Declaration Order

- Keep declaration order: `public` → `protected` → `private`.
- Within each section, declare in this order:

1. Types and type aliases (typedef, using, enum, nested structs and classes, and friend types).
2. Static constants.
3. Factory functions.
4. Constructors and assignment operators.
5. Destructor.
6. All other functions (static and non-static member functions, and friend functions).
7. All other data members (static and non-static).

- Omit empty sections (protected: if unused).

```cpp
class Foo {
public:
    using Id = int;
    static constexpr int max_id = 100;
    explicit Foo(Id id);
    Id get_id() const noexcept;

private:
    Id id;
};
```

---

## Functions

### Inputs and Outputs

- Prefer output parameters over return values. Return `std::error_code` for success/failure status, or return data when the operation cannot fail.
- Inputs: pass by value (cheap copies) or `const &` (read-only).
- Outputs: non-optional outputs use references; optional outputs use pointers.
- Place input arguments before output arguments.

```cpp
// OK
std::error_code get_id(const std::string &name, Id &id);

// Wrong — output first
void get_id(Id &id, const std::string &name);
```

### Write Short Functions

- Keep functions focused; consider splitting around 40 lines.
- Extract helpers instead of deep nesting.

### Function Overloading

- Overload only when call sites are clear without knowing the exact overload.
- Prefer `std::string_view` over parallel `std::string` / `const char*` overloads.

### Default Arguments

- Do not use default arguments for virtual functions.

### Trailing Return Type Syntax

- Use only for lambdas, template-heavy returns, or when it clearly improves readability.

```cpp
template<typename T, typename U>
auto add(T a, U b) -> decltype(a + b);
```

---

## Other C++ Features

### Ownership and Smart Pointers

- Prefer single, clear owners; express transfers with `std::unique_ptr`.
- Use `std::shared_ptr` only for shared ownership that is truly needed.

```cpp
std::unique_ptr<Foo> make_foo();
void consume(std::unique_ptr<Foo> foo);
```

### Rvalue References

- Do not use rvalue references casually in non-move APIs.

### Friends

- Define friends in the same file when possible.

### Exceptions

- Do not use C++ exceptions.
- Use `std::error_code`, `std::optional`, or `init()`/factories for expected failures.
- Prefer third-party APIs that do not throw, or isolate throwing code at boundaries.

### noexcept

- Use `noexcept` when it is useful for performance, especially move operations.

```cpp
Socket(Socket &&) noexcept;
Socket &operator=(Socket &&) noexcept;
```

### Run-Time Type Information (RTTI)

- Avoid `typeid` in production code; prefer virtual dispatch or visitors.
- RTTI is acceptable in tests when necessary.

### Casting

- Use C++ named casts; never C-style casts.
- Use `static_cast` for safe conversions; `reinterpret_cast` only for low-level code.

```cpp
// OK
auto x = static_cast<double>(value);
// Wrong
auto x = (double)value;
```

### Streams

- Prefer alternative facilities over streams for formatting, conversions, and logging due to performance overhead and limited control over formatting.

```cpp
// OK
log_info("value={}", value);

// Wrong
std::cout << "value=" << value << "\n";

// OK
auto s = std::format("value={}", value);

// Wrong
std::stringstream ss;
ss << "value=" << value;
```

### Preincrement and Predecrement

- Prefer `++i` / `--i` unless postfix semantics are required.

```cpp
// OK
for (auto it = v.begin(); it != v.end(); ++it) {}
```

### Use of const

- Use `const` on parameters, locals (when helpful), and member functions that do not mutate logical state.
- Do not use `const` for by-value parameters.

```cpp
// OK
const int limit = 100;
const std::string &name() const;
void process(const Request &req);
```

### Use of constexpr, constinit, and consteval

- Use `constexpr` for compile-time constants and functions.
- Use `constinit` for static/thread-local storage requiring constant initialization.
- Use `consteval` when evaluation must happen at compile time only.
- Prefer `constexpr` over macros for constants.

```cpp
// OK
constexpr int max_buffer = 4096;
constexpr int factorial(int n)
{
    return n <= 1 ? 1 : n * factorial(n - 1);
}
constinit thread_local int tls_counter = 0;
consteval int square(int n)
{
    return n * n;
}
```

### Integer Types

- Use `int` for small integers.
- Use `<cstdint>` (`int32_t`, `uint64_t`, etc.) when size matters.
- Use `size_t` for sizes and container indices.
- Omit the `std::` prefix on standard library integer types.

```cpp
uint64_t order_id;
size_t count = vec.size();
```

### Floating-Point Types

- Use `float` and `double` only; avoid `long double`.
- Include a decimal point in literals (`1.0`, `0.5f`).

### Architecture Portability

- Use type-safe numeric formatting libraries like `std::format` instead of the `printf` family of functions.
- Use `uintptr_t` to store memory addresses as integers.
- Use portable floating point types; avoid `long double`.
- Use portable integer types; avoid `short`, `long`, and `long long`.
- Use braced-initialization as needed to create 64-bit constants.

```cpp
// OK
int64_t my_value { 0x123456789 };
uint64_t my_mask { uint64_t{ 3 } << 48 };
```

### Preprocessor Macros

- Avoid macros for constants and functions; use `constexpr` and `inline`.
- Reserve macros for include guards and conditional compilation.
- When a macro is required:
  - ALL_CAPS name with optional `_`; same spacing as functions (no space after macro name in `#define NAME(`).
  - Parenthesize every argument and the full expansion.
  - Multi-statement macros use `do { ... } while (0)`.
  - Prefer `#if defined(X)` / `#if !defined(X)` over `#ifdef` / `#ifndef`.
  - Document every `#if` / `#elif` / `#else` / `#endif` with a trailing comment.
- Macro in header should have project name prefix.

```cpp
// OK — macro formatting
#define SQUARE(x) ((x) * (x))

// Wrong
#define square(x) ((x) * (x))
#define SQUARE( x ) (( x ) * ( x ))
```

```cpp
// OK
#define MIN(x, y) ((x) < (y) ? (x) : (y))

// Wrong
#define MIN(x, y) x < y ? x : y
```

```cpp
// OK
#define SUM(x, y) ((x) + (y))

// Wrong — expansion not fully parenthesized
#define SUM(x, y) (x) + (y)
```

```cpp
// OK
#define DO_A_AND_B() \
    do {             \
        do_a();      \
        do_b();      \
    } while (0)

// Wrong
#define DO_A_AND_B() \
    {                \
        do_a();      \
        do_b();      \
    }
```

```cpp
// OK
#if defined(XYZ)
#endif // defined(XYZ)

// Wrong
#ifdef XYZ
#endif // XYZ
```

```cpp
// OK
#if defined(XYZ)
#else // defined(XYZ)
#endif // defined(XYZ)

// Wrong
#if defined(XYZ)
#else
#endif
```

```cpp
// OK
#if defined(PROJECT_FEATURE_X)
#endif // defined(PROJECT_FEATURE_X)
```

### 0 and nullptr/NULL

- Use `nullptr` for null pointers; never `0` or `NULL`.
- Use `'\0'` for the null character.

```cpp
// OK
int *p = nullptr;
char c = '\0';
```

### sizeof

- Prefer `sizeof(variable)` over `sizeof(Type)`.
- Always parenthesize the operand: `sizeof(x)`, not `sizeof x`.

```cpp
// OK
memset(&data, 0, sizeof(data));
```

### Type Deduction (including auto)

- Use type deduction only to make the code clearer or safer, and do not use it merely to avoid the inconvenience of writing an explicit type.

#### Function template argument deduction

- Let the compiler deduce template arguments when clear.

#### Local variable type deduction

- Use `auto` when type is obvious / not important.

```cpp
// OK - it type is obvious, widget type is not
auto it = map.find(key);
Widget &widget = *it->second;
```

#### Return type deduction

- Avoid `auto` return types on public APIs unless the type is obvious or template code requires it.

#### Function parameter type deduction

- Do not use auto parameters in non-lambda functions. Use named template parameters instead, because they are more explicit about the fact that the function is a template.

```cpp
// OK
template<typename T>
void f(T arg)
{
}

// Wrong
void f(auto arg)
{
}
```

#### Structured bindings

- Use structured bindings when it improves readability.

```cpp
// OK
const auto [iter, inserted] = map.insert({key, value});
```

### Class Template Argument Deduction

- Prefer explicit types over class template argument deduction.

```cpp
// OK
std::vector<std::string> names = { "a", "b" };

// Wrong
std::vector names = { "a", "b" };
```

### Designated Initializers

- Use designated initializers only in the form that is compatible with the C++20 standard: with initializers in the same order as the corresponding fields appear in the struct definition.

```cpp
// OK
Config cfg {
    .host = "localhost",
    .port = 8080,
};
```

### Lambda Expressions

- Capture by reference only when lifetime is guaranteed.

### Template Metaprogramming

- Avoid template metaprogramming.

### Concepts and Constraints

- Prefer constraints over template metaprogramming.

### C++20 modules

- Not used in this project yet.

### Coroutines

- Use only coroutine libraries that have been approved for project-wide use. Do not roll your own promise or awaitable types.

### Disallowed standard library features

- `std::auto_ptr` — use `std::unique_ptr`.
- Avoid deprecated facilities; follow compiler warnings.

### Third-party Libraries

- Prefer established libraries already in the project over ad-hoc implementations.
- Match third-party naming only at integration boundaries.

### Nonstandard Extensions

- Avoid compiler-specific extensions such as GCC's `__attribute__`, intrinsic functions `__builtin_popcount` or SIMD, `#pragma`, inline assembly, `__COUNTER__`, `__PRETTY_FUNCTION__`, compound statement expressions (e.g., `foo = ({ int x; Bar(&x); x }`), variable-length arrays, `alloca()`, the "Elvis Operator" `a ? : b`.
- Wrap unavoidable extensions in portability macros or headers.

```cpp
// OK — standard attribute
[[nodiscard]] int parse(const std::string &input);

// OK — standard library / portable code
int count = std::popcount(flags); // C++20

// Wrong — vendor builtin in application code
int count = __builtin_popcount(flags);

// Wrong — GCC/Clang-only attribute in public API
__attribute__((packed)) struct Header { int id; };

// Wrong — inline assembly without abstraction
asm("nop");

// OK — isolated platform layer (document why standard C++ is insufficient)
#if defined(__GNUC__)
#define FORCE_INLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline
#endif
```

### Aliases

- Prefer `using` over `typedef`.
- Type aliases at the appropriate scope — public API types in headers, local aliases in `.cpp`.

```cpp
// OK
using Id = std::uint64_t;
```

### Switch Statements

- Always include `default`.
- Mark intentional fall-through with `[[fallthrough]]`.

```cpp
// OK
switch (var) {
case 0:
    do_job();
    break;
default:
    break;
}

// Wrong — default is missing
switch (var) {
case 0:
    do_job();
    break;
}

// OK — intentional fall-through
switch (state) {
case State::idle:
    reset();
    break;
case State::running:
    tick();
    [[fallthrough]];
case State::stopping:
    shutdown();
    break;
default:
    break;
}
```

---

## Naming

### Choosing Names

- Clarity over cleverness.
- Names reflect meaning at their scope — short in tight loops, descriptive in public APIs.
- **Project style:** lowercase `snake_case` for functions, variables, and constants.
- Do not use `_` or `__` prefixes reserved for the implementation.

### File Names

- Use `snake_case.h` / `snake_case.cpp`.

### Type Names

- Type names (classes, structs, type aliases, enum types, and type template parameters) start with a capital letter and have a capital letter for each new word, with no underscores: MyClass, MyEnum.

```cpp
// OK
class MyClass {
};

enum class MyEnum {
};
```

### Concept Names

- The same rule as [Type Names](#type-names)

### Variable Names

- Use `snake_case` (all lowercase, with underscores between words) for the names of variables (including function parameters) and data members.
- Do not encode type in the name (`i32_count`).

```cpp
// OK
class Session {
    int user_id;
    std::string display_name;
};
```

#### Class Data Members

- Use `snake_case`.
- Do not add trailing underscores `_` for data members.
- Do not add `m_` prefix for data members.

#### Struct Data Members

- Use `snake_case`.

### Constant Names

- Use `snake_case`.

```cpp
// OK
constexpr int max_retries = 5;
```

### Function Names

- Use `snake_case` for free functions and methods.

```cpp
// OK
void process_request();
int compute_hash();
```

### Namespace Names

- Use `snake_case`.

```cpp
// OK
namespace my_project {
} // namespace my_project
```

### Enumerator Names

- Use `snake_case`.

```cpp
// OK
enum class Color {
    color_red,
    color_green,
};
```

### Template Parameter Names

- Use [Type Names](#type-names) rules for naming type template parameters.
- Use rules [Variable Names](#variable-names) for naming non-type template parameters.

### Macro Names

- Use ALL_CAPS with project prefix.

```cpp
// OK
#define MYPROJECT_ASSERT(x) ...
```

### Aliases

- Use [Type Names](#type-names) rules.

---

## Comments

- Add comments only for non-obvious functionality.
- Explain **why**, not **what**.
- Prefer `//` for comments.
- Use single space after `//`.

### Legal Notice and Author Line

- Every file includes the project license header.
- Do not add per-author ownership lines unless required by project policy.

---

## Formatting

### Line Length

- Limit lines to **100 characters**.

```cpp
// OK
if (has_valid_session() && session().user_id() == expected_user_id &&
    session().expires_at() > Clock::now()) {
    grant_access();
}

// Wrong
if (has_valid_session() && session().user_id() == expected_user_id && session().expires_at() > Clock::now()) {
    grant_access();
}
```

### Non-ASCII Characters

- Source files are UTF-8.
- Avoid non-ASCII in identifiers; acceptable in comments when needed (e.g. units).

```cpp
// OK
constexpr double micros_per_second = 1e6; // µs
```

### Indentation

- Use **4 spaces** for indentation; never tabs.
- Use one additional indent level (4 spaces) for continuation lines.
- Do not align to the opening `(`, `"`, or parameter column.
- Do not column-align variable assignments, function arguments, or comments.

```cpp
// OK — function call continuation
log_info("parameter1: {} parameter2: {} parameter3: {} parameter4: {}", parameter1,
    parameter2, parameter3, parameter4);

// Wrong
log_info("parameter1: {} parameter2: {} parameter3: {} parameter4: {}", parameter1,
         parameter2, parameter3, parameter4);

// OK
int a = 1;
int abc = 2;

// Wrong
int a   = 1;
int abc = 2;
```

- Indentation is required for every opening bracket.

```cpp
// OK
if (a) {
    do_a();
} else {
    do_b();
    if (c) {
        do_c();
    }
}

// Wrong
if (a) {
    do_a();
} else {
    do_b();
    if (c) {
    do_c();
    }
}
```

### Function Declarations and Definitions

- Return type on the same line as the function name.
- Opening brace on its own line for functions.
- Attach `*` / `&` to the function name.
- Empty parameter lists omit `void` unless C linkage requires it.
- Do not align function prototypes or parameters.
- Add a forward declaration in a source file only if the function is used before it is defined.

```cpp
// OK
void process()
{
}

// Wrong
void
process() {
}
```

```cpp
// OK
const char *my_func();

// Wrong
const char* my_func();
```

```cpp
// OK
void set(int32_t a);
const char *get();

// Wrong
void        set(int32_t a);
const char *get();
```

```cpp
// OK
void my_function(int param1, char *param2, int param3, bool param4,
    bool param5);

// Wrong
void my_function( int param1,
                  char *param2,
                  int param3,
                  bool param4,
                  bool param5 );
```

```c
// OK
void my_func();

// OK — C linkage
extern "C" {
void my_func(void);
}
```

```cpp
// OK
void my_func();

int main()
{
    my_func();
    return 0;
}

void my_func()
{
}

// Wrong
void func();

void my_func()
{
}

int main()
{
    func();
    return 0;
}
```

### Lambda Expressions

- Short lambdas inline; multi-line lambdas indented like function bodies.

```cpp
// OK
auto f = [](int x) { return x * 2; };

auto g = [&](const Request &r) {
    validate(r);
    submit(r);
};
```

### String Literals

- Split long literals across lines using adjacent string literals, not `\` continuation.
- Align continuation literals with the opening quote of the first literal.

```cpp
// OK
log_info("This is a long string that we want to print and is more than 100 chars long so we need "
         "to split it");

// Wrong
log_info("This is a long string that we want to print and is more than 100 chars long so we need \
         to split it");
```

### Floating-point Literals

- Always include a decimal point to make the type explicit.

```cpp
// OK
double ratio = 1.0 / 3.0;
float scale = 0.5f;
```

### Function Calls

- No space between function name and opening parenthesis.
- No space between opening parenthesis and first parameter.
- Single space after every comma.

```cpp
int32_t a = sum(4, 3);              // OK

int32_t a = sum (4, 3);             // Wrong
int32_t a = sum( 4, 3 );            // Wrong
int32_t a = sum(4,3);               // Wrong
```

### Braced Initializer List Format

- Opening `{` on the same line as the initializer.
- Short lists inline; longer lists one element per line.
- Space inside braces: `{ 1, 2, 3 }`.

```cpp
// OK
std::vector<int> v = { 1, 2, 3 };

Node node = { 1, nullptr };

Node node = {
    1,
    nullptr,
};

// Wrong
Node node =
{
    1,
    nullptr,
};
```

### Looping and branching statements

- Opening `{` on the same line as the keyword (`if`, `for`, `while`, `do`, `switch`, `else`).
- Always brace `if` / `for` / `while` bodies.
- `else` / `else if` on the same line as the closing `}` of the preceding block.
- `while` of `do-while` on the same line as the closing `}` of `do`.
- The body of an `if` statement must be on its own line (not `if (fp) fclose(fp);`).
- Empty `for` / `while` bodies: use `{}` on the same line, not `;`.

```cpp
size_t i;

for (i = 0; i < 5; i++) {           // OK
}

for (i = 0; i < 5; i++){            // Wrong — space before {
}

for (i = 0; i < 5; i++)             // Wrong — brace on next line
{
}

// OK
if (a) {
    do_a();
}

if (c) {
    for (int i = 0; i < 10; i++) {
        do_a();
    }
}

if (c) {
    a = 12;
} else {
    for (int i = 0; i < 10; i++) {
        do_a();
    }
}

// Wrong
if (a)
    do_a();

if (c)
    for (int i = 0; i < 10; i++)
        do_a();

if (c)
    a = 12;
else {
    for (int i = 0; i < 10; i++)
        do_a();
}

// OK
if (a) {
} else if (b) {
} else {
}

// Wrong
if (a) {
}
else {
}

if (a) {
}
else
{
}

// OK
do {
    int32_t a;
    a = do_a();
    do_b(a);
} while (check());

// Wrong
do
{
} while (check());

do {
}
while (check());

// OK
if (fp) {
    fclose(fp);
}

// Wrong
if (fp) fclose(fp);

// OK
for (i = 0; i < *p; i++) {}

// Wrong
for (i = 0; i < *p; i++);
```

- Prefer `continue` / `return` over nested blocks.
- Do not use `else` after `return` in an `if` block.

```cpp
// OK — prefer continue over nesting
while (condition1) {
    if (!condition2) {
        continue;
    }

    if (condition3 && condition4) {
        x = 10;
    }
}

// Wrong
while (condition1) {
    if (condition2) {
        if (condition3) {
            if (condition4) {
                x = 10;
            }
        }
    }
}

// OK — no else after return
if (condition) {
    return;
}
do_work();

// Wrong
if (condition) {
    return;
} else {
    do_work();
}
```

### Parentheses

- Do not overuse parentheses.

```cpp
// OK
if ((a & b) > 0 && c > 0 && d) {
}

// Wrong
if ((a & b) > 0 && (c > 0) && (d)) {
}

// Wrong
if ((my_func(a))) {
}

// Wrong
ptr = &(p->next);
```

- Use parentheses when assigning in a condition expression of `if` / `for` / `while`.

```cpp
// OK
for (i = 0; (ret = my_func()); i++) {}

// Wrong
for (i = 0; ret = my_func(); i++) {}
```

### Switch statements format

- Do not indent `case` / `default` labels.
- Indent the body of each `case` / `default` once.
- No space before `:`; one space after `:` when the case body is on the same line.
- If local variables are required inside a `case`, wrap the body in `{ }` with `{` on the same line as `case`; put `break` inside the braces.

```cpp
// OK
switch (c) {
case 0:
    do_a();
    break;
case 1:
    do_b();
    break;
default:
    break;
}

// Wrong
switch (c) {
case 0: do_a(); break;
case 1: do_b(); break;
default: break;
}

// Wrong
switch (c) {
    case 0:
        do_a();
        break;
    case 1:
        do_b();
        break;
    default:
        break;
}

// OK — case-local variables
switch (n) {
case 0: {
    int32_t a, b;
    char c;

    a = 5;

    break;
}
}

// Wrong
switch (n) {
case 0:
    {
        int32_t a;
        break;
    }
}

// Wrong — break must be inside the braces
switch (n) {
case 0: {
    int32_t a;
}
    break;
}
```

### Pointer and Reference Expressions and Types

- Attach `*` and `&` to the **name**, not the type.

```cpp
// OK
int *ptr;
const std::string &ref;

// Wrong
int* ptr;
const std::string& ref;
```

### Boolean Expressions

- Do not compare variables against `0`, `false`, or `nullptr`; use `!` instead.
- Do not place the constant before the variable in a comparison.

```cpp
size_t length = 5;      // Counter variable
bool is_ok = false;     // Boolean variable
void *ptr = nullptr;    // Pointer variable

if (length == 5)        // OK
if (!length)            // OK
if (length)             // OK

if (5 == length)        // Wrong
if (length == 0)        // Wrong
if (length != 0)        // Wrong

if (is_ok)              // OK
if (!is_ok)             // OK

if (is_ok == true)      // Wrong
if (is_ok == false)     // Wrong

if (!ptr)               // OK
if (ptr)                // OK

if (ptr == nullptr)     // Wrong
if (ptr != nullptr)     // Wrong
```

- Place logical `&&` / `||` operators at the end when breaking line.

```cpp
// OK
if (a > b &&
    c == d &&
    e && f) {
}
```

### Return Values

- No parentheses around return values unless needed for clarity.
- Pointer functions return `nullptr` on failure.
- Functions that can fail return `std::error_code`; a default-constructed value means success.
- `bool` return types are for predicates (`is_*`, `has_*`) — return `true` / `false` directly, not `0` / `1`. Do not use `bool` for general success/failure status.
- No bare `return;` at the end of `void` functions.
- Exit early on errors and failed preconditions.

```cpp
// OK
return result;
return (a > b) ? a : b; // parens OK for ternary

// Wrong
return (result);
```

```cpp
// OK
std::error_code open_file()
{
    if (error) {
        return std::make_error_code(std::errc::io_error);
    }
    return {};
}

std::error_code ec = open_file();
if (ec) {
    log_error("open failed: {}", ec.message());
}

// OK
bool is_done()
{
    if (done) {
        return true;
    }

    return false;
}

void *my_alloc()
{
    return is_ok ? ptr : nullptr;
}

// Wrong — bool for success/fail
bool my_func()
{
    if (error) {
        return false;
    }

    return true;
}
```

```cpp
// OK
void my_func()
{
}

// Wrong
void my_func()
{
    return;
}
```

```cpp
// OK — exit early
void my_func()
{
    if (!condition) {
        return;
    }

    if (func1() < 0) {
        return;
    }

    if (func2() < 0) {
        return;
    }

    x = 10;
}

// Wrong — deep nesting
void my_func()
{
    if (condition) {
        if (func1()) {
            if (func2()) {
                x = 10;
            }
        }
    }
}
```

### Variable and Array Initialization

- Use brace initialization `{}` to prevent narrowing conversions.

```cpp
// OK — compile error
int x { 3.14 };

// Wrong — silent narrowing
int y = 3.14;
```

### Preprocessor Directives

- Do not indent code inside `#if` / `#elif` / `#else` / `#endif` blocks.

```cpp
// OK
#if defined(XYZ)
#if defined(ABC)
// do when ABC defined
#endif // defined(ABC)
#else // defined(XYZ)
// Do when XYZ not defined
#endif // defined(XYZ)

// Wrong
#if defined(XYZ)
    #if defined(ABC)
        // do when ABC defined
    #endif // defined(ABC)
#else // defined(XYZ)
    // Do when XYZ not defined
#endif // defined(XYZ)
```

### Class Format

- Access specifiers aligned with `class`; members indented 4 spaces.
- Class opening brace on same line.

```cpp
// OK
class Foo {
public:
    explicit Foo(int id);
    int get_id() const noexcept;

private:
    int id;
};
```

### Constructor Initializer Lists

- Put `:` and the initializer list on the same line as the constructor declaration when they fit within the column limit; otherwise put `:` at the end of the declaration line and wrap the initializers.

```cpp
// OK
Foo::Foo(int id, std::string name) : id(id), name(std::move(name))
{
}
```

### Namespace Formatting

- No extra indent inside namespaces; comment closing braces.

```cpp
namespace my_project::net {

class Socket {};

} // namespace my_project::net
```

### Horizontal Whitespace

#### General

- No trailing whitespace.

#### Operators

- Single space before and after assignment, binary, and ternary operators (`=`, `+`, `-`, `<`, `>`, `*`, `/`, `%`, `|`, `&`, `^`, `<=`, `>=`, `==`, `!=`, `?`, `:`).
- Do not align operators across lines.

```cpp
int32_t a;

a = 3 + 4;              // OK
for (a = 0; a < 5; a++) // OK
bits |= BIT5;           // OK
a ? b : c               // OK

a=3+4;                  // Wrong
a = 3+4;                // Wrong
for (a=0;a<5;a++)       // Wrong
bits|=BIT5;             // Wrong
a?b:c                   // Wrong
```

- No space after unary operators (`&`, `*`, `+`, `-`, `~`, `!`).
- No space around `.` and `->`.
- No space before postfix `++`/`--`; no space after prefix `++`/`--`.

```cpp
res = !x;               // OK
ptr->x;                 // OK
++i;                    // OK

res = ! x;              // Wrong
ptr -> x;               // Wrong
++ i;                   // Wrong
```

#### Loops and Conditionals

- Single space between `if` / `while` / `for` / `do` / `switch` and the opening `(`.

```cpp
// OK
if (condition)
while (condition)
for (init; condition; step)
do {} while (condition)
switch (var)

// Wrong
if(condition)
while(condition)
for(init;condition;step)
do {} while(condition)
switch(var)
```

#### Templates and Casts

- No space in `static_cast<int>(value)`.

### Vertical Whitespace

- One blank line between logical sections within a function (and between top-level definitions).
- No blank line at the start or end of a function body.

```cpp
// OK
void func()
{
    log_info("code block 1");

    log_info("code block 2");
}

// Wrong — multiple consecutive blank lines
void func()
{
    log_info("code block 1");


    log_info("code block 2");
}
```

---

## Exceptions to the Rules

### Existing Non-conformant Code

- Match surrounding style in legacy code.

---

## Tooling

- Format code with **clang-format** using the project `.clang-format` in the repository root.

---

## References

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
- [LLVM Coding Standards](https://llvm.org/docs/CodingStandards.html)

---
