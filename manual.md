# Hop Reference Manual

Hop is a compact interpreter for a subset of the Hope functional programming
language, implemented as a single C file. It supports pattern matching, lazy
evaluation, infinite lists, higher-order functions, and a module system.

---

## Table of Contents

1. [Getting Started](#1-getting-started)
2. [Lexical Elements](#2-lexical-elements)
3. [Data Types](#3-data-types)
4. [Operators and Precedence](#4-operators-and-precedence)
5. [Expressions](#5-expressions)
6. [Function Definitions](#6-function-definitions)
7. [Pattern Matching](#7-pattern-matching)
8. [Local Bindings: `where` and `whererec`](#8-local-bindings-where-and-whererec)
9. [Formatted Output: `write`](#9-formatted-output-write)
10. [Module System: `uses`](#10-module-system-uses)
11. [Built-in Functions](#11-built-in-functions)
12. [Standard Library (`lib.hop`)](#12-standard-library-libhop)
13. [Lazy Evaluation](#13-lazy-evaluation)
14. [Graphics Functions](#14-graphics-functions)
15. [Audio Functions](#15-audio-functions)
16. [Garbage Collection](#16-garbage-collection)
17. [Educational Math Demo (`math_edu.hop`)](#17-educational-math-demo-math_eduhop)
18. [Programming Pitfalls](#18-programming-pitfalls)
19. [Unsupported Hope Features](#19-unsupported-hope-features)

---

## 1. Getting Started

Compile and run:

```sh
make                              # builds with fenster.h GUI support
./hop examples/basic.hop     # text-only program
./hop examples/g_life.hop    # Game of Life animation
./hop examples/g_ball.hop    # bouncing ball animation
```

A hop program is a sequence of **definitions** and **expressions**,
each terminated by `;`. Top-level expressions are evaluated and their
results printed immediately.

```hope
! This is a comment
42;                   ! prints 42
3 + 4 * 2;            ! prints 11

fun double x = 2 * x;
double 21;            ! prints 42
```

---

## 2. Lexical Elements

### Keywords

| Keyword     | Purpose                                          |
|-------------|--------------------------------------------------|
| `fun`       | Begin a new function definition (first clause)   |
| `---`       | Additional clause for the same function          |
| `if` `then` `else` | Conditional expression                    |
| `where`     | Non-recursive local binding                      |
| `whererec`  | Recursive local binding (for infinite structures)|
| `uses`      | Import a module                                  |
| `write`     | Formatted output statement                       |
| `nil`       | Empty list (synonym for `[]`)                    |
| `true`      | Boolean true (value `1`)                         |
| `false`     | Boolean false (value `0`)                        |
| `mod`       | Modulo operator                                  |

### Ignored Declarations

The following top-level keywords are recognized by the parser but
**silently skipped** — they produce no effect at runtime. They exist
so that source files written for the full Hope language can be loaded
without errors:

| Keyword    | Meaning in Hope                        |
|------------|----------------------------------------|
| `dec`      | Type signature declaration             |
| `typevar`  | Type variable declaration              |
| `infix`    | Left-associative infix operator        |
| `infixr`   | Right-associative infix operator       |
| `data`     | Algebraic data type definition         |
| `abstype`  | Abstract type definition               |
| `type`     | Type alias definition                  |
| `private`  | Visibility / access modifier           |

Each ignored declaration is consumed up to and including the next `;`,
then parsing continues normally.

### Comments

Everything from `!` to end of line is a comment, except that `!=` is the
not-equal operator.

```hope
! This is a comment
x = 5;   ! inline comment
a != b;  ! this is NOT a comment — it's "a not-equal b"
```

### Literals

| Form        | Example      | Value                              |
|-------------|--------------|------------------------------------|
| Integer     | `42`, `-5`   | Number                             |
| Float       | `3.14`       | Number                             |
| Character   | `'A'`        | Number (ASCII code 65)             |
| String      | `"Hello"`    | List of character codes            |
| Escape seqs | `'\n'` `'\t'` `'\\'` `'\''` `\"` | Special characters |

---

## 3. Data Types

Hop has five value types:

### Numbers

Double-precision floating point. Displayed as integers when the value is
whole.

```hope
42;       ! integer
3.14;     ! float
'A';      ! character = 65
```

### Lists

Ordered sequences built from cons cells. Empty list is `[]` or `nil`.

```hope
[1, 2, 3];           ! list literal
1 :: 2 :: 3 :: [];    ! equivalent cons form
"Hello";              ! string = [72, 101, 108, 108, 111]
```

Strings are just lists of character codes. The `%v` format auto-detects
printable ASCII lists and displays them with quotes.

### Pairs (Tuples)

Two-element tuples. Nest pairs for larger structures.

```hope
(1, 2);               ! a pair
(1, (2, 3));           ! nested pair (simulates a triple)
```

### Arrays

Immutable, O(1) random access. Created from lists or built directly.
Printed as `[|...|]`.

```hope
a = array [10, 20, 30];   ! [|10, 20, 30|]
aget 1 a;                  ! 20
alen a;                    ! 3
aset 1 99 a;               ! [|10, 99, 30|] (new array, a unchanged)
amake 5 0;                 ! [|0, 0, 0, 0, 0|]
amap double a;             ! [|20, 40, 60|]
```

Arrays are especially useful for grid-based programs (e.g., Game of Life)
where `aget` is O(1) vs. `nth` which is O(n) on lists.

### Functions

First-class values. Can be passed to higher-order functions.

```hope
fun double x = 2 * x;
map double [1, 2, 3];    ! [2, 4, 6]
```

---

## 4. Operators and Precedence

From **highest** to **lowest** precedence:

| Prec | Operators               | Assoc | Description              |
|------|-------------------------|-------|--------------------------|
| 7    | function application    | Left  | `f x y` = `(f x) y`     |
| 6    | unary `-`               | —     | Negation                 |
| 5    | `*` `/` `mod`           | Left  | Multiplicative           |
| 4    | `+` `-`                 | Left  | Additive                 |
| 3    | `..` `\|\|`             | Left  | Range, zip               |
| 2    | `<>`                    | Left  | List append              |
| 2    | `==` `!=` `<` `>` `<=` `>=` | None | Comparison          |
| 1    | `::`                    | Right | Cons (prepend to list)   |
| 0    | `where` `whererec`      | Left  | Local bindings           |

### Sections (Operator as Function)

Wrap an operator in parentheses to use it as a function that takes a pair:

```hope
map (+) ([1, 2, 3] || [10, 20, 30]);   ! [11, 22, 33]
foldr (+) 0 (1..10);                    ! 55
foldr (*) 1 (1..5);                     ! 120
```

Supported sections: `(+)` `(-)` `(*)` `(/)` `(mod)` `(::)`
`(<)` `(>)` `(==)` `(!=)` `(<=)` `(>=)`.

Not supported: `(<>)`.

---

## 5. Expressions

### Arithmetic

```hope
3 + 4;          ! 7
10 - 3;         ! 7
3 * 4;          ! 12
10 / 3;         ! 3.33333
10 mod 3;       ! 1
-x;             ! unary negation
```

### Comparison

Returns `1` (true) or `0` (false). Only works on numbers.

```hope
3 == 3;         ! 1
3 != 4;         ! 1
3 < 5;          ! 1
```

### Conditional

```hope
if n mod 2 == 0 then "even" else "odd";
```

Both branches must be present. Only the chosen branch is evaluated (lazy).

### List Operations

```hope
1 :: [2, 3];           ! [1, 2, 3]     (cons)
[1, 2] <> [3, 4];      ! [1, 2, 3, 4]  (append)
1..5;                   ! [1, 2, 3, 4, 5] (range)
[1, 2] || [3, 4];      ! [(1,3), (2,4)] (zip)
```

### Top-Level Bindings

A bare assignment at top level binds a name:

```hope
pi = 3.14159;
n = 10;
pi * n;         ! 31.4159
```

---

## 6. Function Definitions

### Basic Syntax

```hope
fun name pattern = expression;
```

First clause uses `fun`; additional clauses use `---` with the same name:

```hope
fun fact 0 = 1;
--- fact n = n * fact(n - 1);
```

### Calling Conventions

There are two distinct styles. **They must not be mixed** for the same
function.

**Pair style** — single argument that is a pair/tuple:

```hope
fun gcd(a, 0) = a;
--- gcd(a, b) = gcd(b, a mod b);

gcd(12, 8);     ! call with parentheses
```

**Curried style** — multiple space-separated arguments:

```hope
fun foldr f z [] = z;
--- foldr f z (x :: xs) = f(x, foldr f z xs);

foldr (+) 0 (1..10);    ! call with spaces
```

### Definition Order

Functions must be defined **before** they are used. Forward references
cause an "undefined function" error.

---

## 7. Pattern Matching

Patterns appear in function definitions and `where` bindings. They are
tried top-to-bottom; the first match wins.

| Pattern        | Matches                  | Binds          |
|----------------|--------------------------|----------------|
| `42`           | Exact number             | Nothing        |
| `'A'`          | Character (= 65)         | Nothing        |
| `x`            | Anything                 | `x` to value   |
| `_`            | Anything                 | Nothing        |
| `[]` or `nil`  | Empty list               | Nothing        |
| `x :: xs`      | Non-empty list           | Head and tail  |
| `(a, b)`       | Pair                     | Both elements  |
| `[1, 2, 3]`    | Exact list               | Nothing (unless vars inside) |

Patterns can be nested arbitrarily:

```hope
fun process (x :: y :: rest) = ...;    ! at least two elements
fun deep ((a, b), (c, d)) = ...;       ! nested pairs
fun tree (l, (v, r)) = ...;            ! tree node
```

Example — list recursion:

```hope
fun sum [] = 0;
--- sum (x :: xs) = x + sum xs;
```

Example — pair destructuring:

```hope
fun swap (a, b) = (b, a);
```

---

## 8. Local Bindings: `where` and `whererec`

### `where` — Non-Recursive

Binds a value in a local scope:

```hope
a + b where a = 3 where b = 4;       ! 7
```

Supports pattern destructuring:

```hope
x + y where (x, y) = (3, 4);         ! 7
```

**Chaining**: multiple `where` clauses are evaluated **bottom-to-top**
(rightmost binding first):

```hope
result
    where answer = x + y        ! 2nd: uses x and y
    where (x, y) = (3, 4);      ! 1st: binds x and y
```

**Scoping with `if`**: `where` binds to the **entire** `if-then-else`,
not just the `else` branch (consistent with the original Hope language):

```hope
if x > 0 then a else b
where a = 42
where b = 99;
```

When `where` scopes over an `if`, non-`_` bindings are **lazy** — they
are only evaluated when referenced. This avoids evaluating definitions
that are only needed in one branch.

### `whererec` — Recursive

Allows the definition to reference itself. Essential for infinite
structures:

```hope
front(10, ones) whererec ones = 1 :: ones;
! [1, 1, 1, 1, 1, 1, 1, 1, 1, 1]

front(8, rows) whererec rows = [1] :: map nextrow rows;
! Pascal's triangle
```

Restrictions:
- Only accepts a **single variable name** (no pattern destructuring).
- Cannot be chained.

---

## 9. Formatted Output: `write`

### Syntax

```hope
write "format string" arg1 arg2 ...;
```

### Format Specifiers

| Specifier | Expects         | Output                          |
|-----------|-----------------|---------------------------------|
| `%d`      | Number          | Integer format                  |
| `%f`      | Number          | Floating-point format           |
| `%c`      | Number          | Single character (ASCII)        |
| `%s`      | String (list)   | Characters (no quotes)          |
| `%v`      | Any value       | Generic (auto-detects strings)  |
| `%%`      | —               | Literal `%`                     |

### Argument Rules

Each argument is parsed as an **atom**. Function calls or compound
expressions must be wrapped in parentheses:

```hope
write "n = %d\n" 42;                ! OK — atom
write "result = %d\n" (double 5);   ! OK — parenthesized
write "result = %d\n" double 5;     ! WRONG — two separate args
```

---

## 10. Module System: `uses`

```hope
uses lib;
uses lib, utils;
```

- Searches for `<name>.hop` in the same directory as the source file.
- Relative paths are supported: `uses ../lib/helpers;`
- Definitions are loaded; top-level expressions are **not** executed.
- Duplicate loads are skipped.

The standard library `lib.hop` provides common utility functions:

```hope
uses lib;
reverse "Hello";       ! "olleH"
sum (1..10);           ! 55
filter even (1..10);   ! [2, 4, 6, 8, 10]
```

---

## 11. Built-in Functions

These are implemented in C and always available (no `uses` needed):

### General

| Function          | Description                                  |
|-------------------|----------------------------------------------|
| `map f lst`       | Apply `f` to each element (lazy)             |
| `head lst`        | First element (error on `[]`)                |
| `tail lst`        | All but first element (error on `[]`)        |
| `succ n`          | Return `n + 1`                               |
| `front(n, lst)`   | First `n` elements (lazy)                    |
| `length lst`      | Count elements (fails on infinite lists!)    |
| `nth n lst`       | Element at index `n` (0-based, C-accelerated)|
| `xor a b`         | Bitwise XOR of two integers                  |
| `timeseed`        | `time(NULL) % 1000000` — changes every second|
| `rand 0`          | Random 32-bit unsigned integer (C `arc4random()`)|
| `srand seed`      | Seed the random number generator (C `srand()`)   |

```hope
srand timeseed;
front(10, randoms 0)
    whererec randoms _ = rand 0 :: randoms 0;
```

### Pair Access

| Function      | Description                    |
|---------------|--------------------------------|
| `fst pair`    | First element of pair          |
| `snd pair`    | Second element of pair         |

### Arrays

| Function            | Description                                       |
|---------------------|---------------------------------------------------|
| `array lst`         | Create array from list                            |
| `aget i arr`        | Element at index `i` (0-based, O(1))              |
| `alen arr`          | Length of array                                    |
| `aset i val arr`    | New array with `arr[i]` replaced by `val`         |
| `amake n val`       | New array of `n` copies of `val`                  |
| `amap f arr`        | New array where each element is `f(old[i])`       |
| `tabulate n f`      | New array of `n` elements where `arr[i] = f(i)`   |

```hope
a = array [10, 20, 30];
aget 1 a;                   ! 20
b = aset 0 99 a;            ! [|99, 20, 30|]  (a is unchanged)
amap double a;              ! [|20, 40, 60|]
tabulate 5 succ;            ! [|1, 2, 3, 4, 5|]
```

### Math (C `math.h`)

All trig functions use **radians**. These take priority over any
user-defined or library functions with the same name.

| Function / Constant | Description                                    |
|---------------------|------------------------------------------------|
| `pi`                | 3.14159265358979323846                         |
| `sin x`             | Sine                                           |
| `cos x`             | Cosine                                         |
| `tan x`             | Tangent                                        |
| `asin x`            | Arcsine, result in [-π/2, π/2]                |
| `acos x`            | Arccosine, result in [0, π]                   |
| `atan x`            | Arctangent, result in (-π/2, π/2)             |
| `atan2 y x`         | Two-argument arctangent, result in (-π, π]    |
| `sqrt x`            | Square root                                    |
| `pow x y`           | `x` raised to the power `y`                   |
| `exp x`             | e^x                                            |
| `log x`             | Natural logarithm (ln)                         |
| `log10 x`           | Base-10 logarithm                              |
| `floor x`           | Round down to integer                          |
| `ceil x`            | Round up to integer                            |
| `fabs x`            | Absolute value (floating-point)                |

```hope
write "sin(pi/2) = %f\n" (sin (pi / 2));   ! 1.0
write "sqrt(2)   = %f\n" (sqrt 2);          ! 1.41421
write "atan2(1,1)*4 = %f\n" (atan2 1 1 * 4); ! 3.14159
```

---

## 12. Standard Library (`lib.hop`)

Load with `uses lib;`. All functions below become available.

### List Operations

| Function               | Description                                |
|------------------------|--------------------------------------------|
| `null lst`             | `1` if empty, `0` otherwise               |
| `last lst`             | Last element                               |
| `init lst`             | All but last element                       |
| `take n lst`           | First `n` elements                         |
| `drop n lst`           | Skip first `n` elements                    |
| `reverse lst`          | Reverse a list                             |
| `nth n lst`            | Element at index `n` (0-based)             |

### Higher-Order Functions

| Function                   | Description                                |
|----------------------------|--------------------------------------------|
| `foldr f z lst`            | Right fold                                 |
| `foldl f z lst`            | Left fold                                  |
| `filter p lst`             | Keep elements where `p` returns nonzero    |
| `zipwith f xs ys`          | Combine two lists element-wise             |
| `takewhile p lst`          | Take while predicate holds                 |
| `dropwhile p lst`          | Drop while predicate holds                 |
| `concatmap f lst`          | Map then flatten                           |
| `compose f g x`            | Function composition: `f(g(x))`            |
| `flip f a b`               | Swap arguments: `f(b, a)`                  |
| `twice f x`                | Apply twice: `f(f(x))`                     |
| `ntimes n f x`             | Apply `f` to `x`, `n` times               |

### Numeric Utilities

| Function          | Description                              |
|-------------------|------------------------------------------|
| `sum lst`         | Sum via `foldr (+) 0`                    |
| `product lst`     | Product via `foldr (*) 1`                |
| `abs x`           | Absolute value (integer-safe)            |
| `max(a, b)`       | Maximum of two numbers                   |
| `min(a, b)`       | Minimum of two numbers                   |
| `even n`          | `1` if even, `0` otherwise               |
| `odd n`           | `1` if odd, `0` otherwise                |
| `gcd(a, b)`       | Greatest common divisor                  |
| `trunc x`         | Truncate float to integer                |
| `intdiv(a, b)`    | Integer division (non-negative a, positive b) |
| `lerp a b t`      | Linear interpolation: `a + t*(b-a)`      |

### String Utilities

| Function                   | Description                                |
|----------------------------|--------------------------------------------|
| `contains(c, s)`           | Is character `c` in string `s`?            |
| `count(c, s)`              | Count occurrences of `c` in `s`            |
| `indexOf(c, s)`            | Position of `c` (or `-1`)                  |
| `streq(s1, s2)`            | String equality (use instead of `==`)      |
| `startsWith(pfx, s)`       | Does `s` start with `pfx`?                 |
| `endsWith(sfx, s)`         | Does `s` end with `sfx`?                   |
| `isSubstring(needle, s)`   | Is `needle` a substring of `s`?            |
| `split(delim, s)`          | Split string by delimiter character        |
| `join(delim, lst)`         | Join list of strings with delimiter        |

### Infinite Sequences

| Function      | Description                        |
|---------------|------------------------------------|
| `from n`      | `[n, n+1, n+2, ...]`              |
| `repeat x`    | `[x, x, x, ...]`                  |

---

## 13. Lazy Evaluation

Hop uses **lazy evaluation**: expressions are not computed until
their values are needed. This enables infinite data structures.

### Infinite Lists

```hope
fun from n = n :: from(n + 1);
front(5, from 1);         ! [1, 2, 3, 4, 5]
```

The tail of `::` is always a thunk — it is only evaluated when accessed.

### Classic Examples

**Sieve of Eratosthenes:**

```hope
fun remove(p, []) = [];
--- remove(p, x :: xs) =
    if x mod p == 0 then remove(p, xs)
    else x :: remove(p, xs);

fun sieve (p :: xs) = p :: sieve(remove(p, xs));

front(10, sieve(from 2));
! [2, 3, 5, 7, 11, 13, 17, 19, 23, 29]
```

**Fibonacci via self-referencing:**

```hope
front(10, fibs)
    whererec fibs = 0 :: 1 :: map (+) (fibs || tail fibs);
! [0, 1, 1, 2, 3, 5, 8, 13, 21, 34]
```

**Hamming numbers:**

```hope
fun double n = 2 * n;
fun triple n = 3 * n;
fun times5 n = 5 * n;

fun merge(x :: xs, y :: ys) =
    if x < y then x :: merge(xs, y :: ys)
    else if x > y then y :: merge(x :: xs, ys)
    else x :: merge(xs, ys);

front(20, h)
    whererec h = 1 :: merge(map double h,
                       merge(map triple h,
                             map times5 h));
```

---

## 14. Graphics Functions

Hop includes built-in graphics support via [fenster.h](https://github.com/zserge/fenster).
No `uses` is needed — all graphics functions are always available.

### Window Management

| Function         | Description                                        |
|------------------|----------------------------------------------------|
| `gopen (w, h)`   | Open a window of size `w` x `h` pixels             |
| `gclose 0`       | Close the window                                   |
| `gtitle "text"`  | Set the window title bar text                      |
| `gloop 0`        | Process events; returns `0` normally, nonzero on quit |
| `gsync 0`        | Sync display at ~60fps (frame rate limiter)        |

### Drawing

| Function                       | Description                                |
|--------------------------------|--------------------------------------------|
| `gclear color`                      | Fill entire window with `color`                    |
| `gplot (x, (y, color))`            | Plot a single pixel                                |
| `gblit arr`                         | Copy array of color values to framebuffer          |
| `gline x1 y1 x2 y2 color`          | Draw a line (Bresenham algorithm)                  |
| `gcircle cx cy r color`             | Draw a circle outline (Bresenham)                  |
| `gtext x y color "text"`           | Draw text using 5x7 dot matrix font                |
| `gdrawcol (w, (h, (s, colorList)))` | Draw w×h grid of cells, each with its own color   |
| `gsave 0`                           | Snapshot current framebuffer                       |
| `grestore 0`                        | Restore framebuffer from snapshot                  |

### Settings

| Function      | Description                                          |
|---------------|------------------------------------------------------|
| `gscale n`    | Set font scale factor (`1`–`8`, default `2`)         |
| `gpen n`      | Set line/circle pen width (`1`–`32`, default `1`)    |

### Input

| Function       | Description                                         |
|----------------|-----------------------------------------------------|
| `gkey keycode`  | Returns `1` if key is pressed, `0` otherwise             |
| `gkeyevent 0`   | Returns keycode of most recently pressed key (or `0`)    |
| `gmouse 0`      | Returns `(x, y)` pair of mouse position                  |
| `gclick 0`      | Returns mouse button state (`> 0` if held)               |
| `gwidth 0`      | Returns window width                                     |
| `gheight 0`     | Returns window height                                    |

### Colors

Colors are 24-bit RGB integers: `red * 65536 + green * 256 + blue`.

| Color   | Value      | Hex        |
|---------|------------|------------|
| Black   | `0`        | `0x000000` |
| White   | `16777215` | `0xFFFFFF` |
| Red     | `16711680` | `0xFF0000` |
| Green   | `65280`    | `0x00FF00` |
| Blue    | `255`      | `0x0000FF` |
| Yellow  | `16776960` | `0xFFFF00` |
| Cyan    | `65535`    | `0x00FFFF` |

### Event Loop Pattern

Graphics programs need an event loop to keep the window responsive.
The standard pattern:

```hope
fun loop _ = if gloop 0 == 0
    then if gkey 27 == 1 then 0 else loop (gsync 0)
    else 0;
loop 0;
gclose 0;
```

This calls `gloop` to process OS events, checks for ESC (keycode 27),
and syncs the display at ~60fps. The loop uses tail call optimization
so it runs indefinitely without stack overflow.

### Animation Pattern

For animations, thread state through the recursive loop:

```hope
fun frame x y dx dy =
    if closed != 0 then 0
     else if gkey 27 == 1 then 0
     else frame (x + dx) (y + dy) dx dy
    where _ = gsync 0
    where closed = gloop 0
    where _ = gcircle x y 10 65280
    where _ = gclear 0;

write "" (frame 160 120 2 3);
gclose 0;
```

Note: `where _` clauses execute eagerly bottom-to-top — `gclear` runs
first, then drawing, then `gloop`/`gsync`, then the next frame.
`where` scopes over the entire `if`, so no parentheses are needed.

### Side-Effect Sequencing

Graphics calls return `0`. Use `+` to sequence multiple draw calls,
since all operands are evaluated:

```hope
gline 0 0 100 100 16777215 +
gcircle 50 50 30 16711680;
```

Use `write "" (expr)` to force evaluation of a drawing expression
without printing its return value.

---

## 15. Audio Functions

Hop includes audio support via [fenster_audio.h](https://github.com/zserge/fenster).
Audio is initialized automatically when the graphics window opens.

| Function           | Description                                        |
|--------------------|----------------------------------------------------|
| `gbeep freq dur`   | Play a sine wave at `freq` Hz for `dur` milliseconds |

The beep is non-blocking — samples are fed to the audio system each
frame via `gsync`. Multiple beeps do not overlap; a new `gbeep` call
replaces the current tone.

### Example

```hope
gopen (320, 240);

fun loop _ =
    if gloop 0 != 0 then 0
     else if gkey 27 == 1 then 0
     else loop (gsync 0)
    where _ = if gkey 32 == 1 then gbeep 440 100 else 0;

loop 0;
gclose 0;
```

Press space to play a 440 Hz tone for 100 ms.

---

## 16. Garbage Collection

Hop uses a conservative mark-sweep garbage collector to reclaim
unused memory during long-running programs (e.g., animations).

- **Val and Env nodes** are allocated from static pools (256K nodes each)
- **GC triggers** automatically when a pool is exhausted
- **Mark phase** conservatively scans the C stack for pointers into
  the pools, then traces all reachable Val and Env nodes
- **Sweep phase** returns unmarked nodes to a free list for reuse

No explicit action is needed from the programmer — GC is fully
automatic and transparent. This enables animations and interactive
programs to run indefinitely without running out of memory.

---

## 17. Educational Math Demo (`math_edu.hop`)

`examples/math_edu.hop` is an **educational demo** — it is not a library
to `uses`. It demonstrates how math functions can be implemented in pure
Hope using Taylor series and lookup tables.

> **Note:** The C math builtins (`sin`, `cos`, `sqrt`, `pi`, etc.) described
> in [Section 11](#11-built-in-functions) are always available and should be
> used in practice. `math_edu.hop` exists to show the underlying algorithms.

### Hope Implementations Demonstrated

| Name      | Method                        | Input    |
|-----------|-------------------------------|----------|
| `sinS x`  | Taylor series                 | Radians  |
| `cosS x`  | Taylor series                 | Radians  |
| `tanS x`  | Taylor series                 | Radians  |
| `asinS x` | Bisection                     | Radians  |
| `acosS x` | Bisection                     | Radians  |
| `atanS x` | Bisection                     | Radians  |
| `sqrtS x` | Newton's method               | —        |
| `lnS x`   | Series (range reduction)      | —        |
| `log10S x`| via `lnS`                     | —        |
| `expS x`  | Taylor series                 | —        |
| `sinT deg`| Lookup table + interpolation  | Degrees  |
| `cosT deg`| Lookup table + interpolation  | Degrees  |
| `tanT deg`| Lookup table + interpolation  | Degrees  |
| `asinT v` | Reverse lookup                | Degrees  |
| `acosT v` | Reverse lookup                | Degrees  |
| `atanT v` | Reverse lookup                | Degrees  |
| `lnT x`   | Lookup table                  | —        |
| `log10T x`| Lookup table                  | —        |
| `expT x`  | Lookup table                  | —        |

Run it directly to see a comparison of results:

```sh
./hop examples/math_edu.hop
```

---

## 18. Programming Pitfalls

### 18.1 `where` Scopes over `if-then-else`

`where` has very low precedence and binds to the **entire**
`if-then-else` expression, consistent with the original Hope language:

```hope
! where binds to the whole if-then-else — a and b are in scope for both branches
if x > 0 then a else b where a = 1 where b = 2;
```

When a `where` binding is only needed inside one branch (e.g. for
branch-specific side effects), use parentheses to restrict its scope:

```hope
! where _ draws only when the condition is true
if done then 0
else (0 where _ = drawSomething x y);
```

### 18.2 `where` Chains Evaluate Bottom-to-Top

The rightmost (bottom) `where` is bound first. Each definition can only
reference bindings that appear **below** it:

```hope
! WRONG — b tries to use a, but a is not yet in scope
x where a = 1 where b = a + 1;

! CORRECT — a is defined below, so b can see it
b where b = a + 1 where a = 1;
```

### 18.3 Pair vs. Curried Style Must Match

If a function is defined with pair-style arguments, it must be called with
parentheses, and vice versa:

```hope
fun add(a, b) = a + b;     ! pair style (nargs = 1)
add(3, 4);                 ! OK
add 3 4;                   ! ERROR — wrong arity

fun add2 a b = a + b;      ! curried style (nargs = 2)
add2 3 4;                  ! OK
add2(3, 4);                ! ERROR — receives a pair as single arg
```

### 18.4 No Boolean Type — Use Arithmetic

There are no `&&` or `||` operators. Simulate with `*` (AND) and `+` (OR):

```hope
fun isdigit c = (c >= '0') * (c <= '9');     ! AND
fun isalnum c = isdigit c + isalpha c;       ! OR (works if each is 0 or 1)
```

### 18.5 Strings Cannot Use `==`

The `==` operator compares numbers. For strings (lists of chars), use
`streq` from the standard library:

```hope
uses lib;
"abc" == "abc";            ! WRONG — undefined behavior
streq("abc", "abc");       ! CORRECT — returns 1
```

### 18.6 `length` Diverges on Infinite Lists

`length` traverses the entire list. On an infinite list, it never
terminates. Use `front(n, lst)` to take a finite prefix first:

```hope
fun nats = from 1;
length nats;               ! HANGS FOREVER
length(front(10, nats));   ! OK — returns 10
```

### 18.7 Functions Must Be Defined Before Use

Unlike Haskell, hop processes definitions top-to-bottom. A function
cannot call another function that hasn't been defined yet:

```hope
fun isEven n = ...isOdd...;    ! ERROR — isOdd not yet defined
fun isOdd n = ...isEven...;
```

### 18.8 No Partial Application

Functions cannot be partially applied implicitly. Write a wrapper:

```hope
fun add a b = a + b;
map (add 3) [1, 2, 3];        ! ERROR

fun add3 x = add 3 x;         ! manual wrapper
map add3 [1, 2, 3];           ! OK — [4, 5, 6]
```

### 18.9 `::` Has Very Low Precedence

`::` binds weaker than `<>`, which can cause surprises:

```hope
0 :: row <> [0];       ! parses as: 0 :: (row <> [0])
(0 :: row) <> [0];     ! use parentheses if you mean this
```

### 18.10 `whererec` Restrictions

- Only accepts a **single variable name** — no pattern destructuring.
- Cannot be chained (`whererec ... whererec` is invalid).

```hope
! WRONG — pattern in whererec
x whererec (a, b) = ...;

! CORRECT
x whererec pair = ...;
```

### 18.11 `write` Arguments Must Be Atoms

Each `write` argument is parsed as an atom. Compound expressions require
parentheses:

```hope
write "%d\n" 42;               ! OK
write "%d\n" (fact 10);        ! OK — parenthesized call
write "%d\n" fact 10;          ! WRONG — fact and 10 are two args
```

### 18.12 List Literals Are Syntactic Sugar

`[1, 2, 3]` is sugar for `1 :: 2 :: 3 :: []`. Patterns follow the same
expansion, so `[x]` matches a one-element list, `[x, y]` matches exactly
two elements, etc.

---

## 19. Unsupported Hope Features

Hop is a **minimal** interpreter (one C file). The features below exist
in the original Hope language but are absent from hop by design — adding
them would require structural changes to the evaluator or a type system
that hop deliberately omits.

### 19.1 Lambda / Anonymous Functions

Hope supports anonymous functions (`\x -> expr`). Hop has no such
syntax. Every function must be defined at the top level with `fun`.

**Workaround:** define a named helper function.

```hope
! HOPE (not valid in hop)
map (\x -> x * 2) [1, 2, 3];

! HOP
fun double x = x * 2;
map double [1, 2, 3];
```

**Why absent:** hop stores all functions in a global name table. Supporting
closures would require a new value type that captures the definition-time
environment, significantly complicating the evaluator.

### 19.2 `let...in` Expressions

Hope has `let x = ... in ...`. Hop only has `where` / `whererec`.

**Workaround:** use `where` — it is semantically equivalent for all
practical purposes.

**Why absent:** `where` already covers the use case; adding `let...in`
would be redundant complexity.

### 19.3 List Comprehensions

Hop supports list comprehension syntax compatible with Hope:

```hope
[x * x | x <- 1..10, odd x]          ! [1, 9, 25, 49, 81]
[(x, y) | x <- 1..3, y <- 1..3, x != y]
```

**Syntax:** `[output | gen1, gen2, ..., guard]`

- **Generator:** `var <- range` — iterates `var` over a list or range.
- **Guard:** a boolean expression after the last generator (optional).
- Multiple generators produce the Cartesian product, filtered by the guard.
- Up to 7 generators are supported.

**Example — qsort:**

```hope
fun qsort [] = [];
--- qsort (pivot :: xs) = qsort lesser <> [pivot] <> qsort greater
    where greater = [x | x <- xs, x > pivot]
    where lesser  = [x | x <- xs, x <= pivot];
```

**Limitation:** a guard of the form `var < -expr` (less-than applied to a
negated value) is parsed as a generator. Rewrite as `var < (0 - expr)`.

### 19.4 Tuples Beyond Pairs

Hope supports arbitrary n-tuples: `(a, b, c)`. Hop only supports
2-element pairs; a third comma in a parenthesised expression is a
parse error.

**Workaround:** nest pairs.

```hope
(1, (2, 3));      ! simulates a triple
```

**Why absent:** the `V_PAIR` value type uses two fixed pointers (`fst`,
`snd`). Generalising to n-tuples would require a dynamic array
representation and a revised pattern matcher.

### 19.5 As-Patterns

Hope allows binding a name to a value while also destructuring it
(`xs @ (x :: rest)`). The `@` character is not meaningful in hop.

**Workaround:** bind the whole value in one clause, then extract parts
with `where`.

```hope
fun f xs = head xs + length xs
    where h = head xs;
```

**Why absent:** would require a new `Pat` node type and extra bindings in
the pattern matcher — a minor but unnecessary addition for hop's scope.

### 19.6 Negative Literal Patterns

Patterns like `fun f (-1) = ...` are not supported. The pattern parser
only recognises non-negative number literals.

**Workaround:** match a variable and guard with `if`.

```hope
fun f n = if n == -1 then ... else ...;
```

**Why absent:** the pattern parser does not handle a leading `-` sign;
adding it is a small but omitted change.

### 19.7 Algebraic Data Types and User-Defined Constructors

`data` declarations are silently skipped (see Section 2). As a result,
no user-defined constructors exist at runtime — they cannot appear in
expressions or patterns.

**Workaround:** encode variants with numbers or tagged pairs.

```hope
! data Shape = Circle | Rect  — skipped, constructors unavailable
! Encode manually: 0 = circle, 1 = rect
fun area (0, r)      = pi * r * r;
--- area (1, (w, h)) = w * h;
```

**Why absent:** supporting constructors requires registering them at
parse time, a new tagged-union value type, and constructor-aware pattern
matching — a structural extension inconsistent with the minimal design.

### 19.8 Type Classes

`class`, `instance`, and `deriving` declarations are not in the
silence list — they are unrecognised identifiers that will cause a
**parse error** if encountered.

**Why absent:** type classes require a static type system and
dictionary-passing at the call site. Hop has no type checker; at
runtime there is no "type" level at which to dispatch.

### 19.9 Qualified Module Names

`uses` merges all loaded definitions into a single flat global namespace.
There is no `Module.function` syntax for qualified access.

**Why absent:** implementing namespaces would require prefixed name
lookup or a layered scope structure in the evaluator — beyond hop's
flat function-table design.

### 19.10 Module Visibility

`private` declarations are silently skipped. Every definition loaded
via `uses` is unconditionally visible to all subsequent code.

**Why absent:** visibility control is a module-system feature; hop's
`uses` is purely additive file loading with no isolation mechanism.
