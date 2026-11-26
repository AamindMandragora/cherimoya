# Commit 12.13: Error Codes

## The Feature

- **Every rule has a code.** Each diagnostic carries a stable code: a letter for where it's reported and a number.
  - `L` the lexer, `P` the parser, `S` sema, `W` a warning;
  - `N` a feature that isn't built yet.

  Codes are given out in order and never reused, and a retired code stays documented as retired. There are 187: 8 `L`, 54 `P`, 86 `S`, 4 `W` and 35 `N`.
- **The code shows in the terminal.**

  ```
  error[S001] at line 1:
  let (xs :: arr[3, int], i :: int) -> pick -> (x :: int) { xs[i] -| }
                                                               ^
  cherimoya requires an index to be unsigned, please convert it, like 'i64u(i)'

  run 'cheri guide S001' for more about this error
  ```

  The last line is printed once a run, about the first error, so a run with many errors doesn't repeat it. Notes keep `note at line N:`.
- **`cheri guide <code>`** explains a code. It gives the rule, why cherimoya has it, a program that breaks it, and the same program fixed. `cheri guide` alone lists every code.
- **Five forms.** Two forms join the three existing ones:
  - `cherimoya doesn't support <X> yet[, which <what will bring it>][, please <workaround>]` for a feature that's planned but not built. Each such feature has its own `N` code, retired once the feature is built.
  - `cherimoya never <does X>, since <why>[, please <fix>]` for a warning, which is about code that runs but does nothing useful, so it doesn't claim a rule was broken. A `.toya` file with no tests now says `cherimoya never runs a test in this file, since none is marked '@test', please mark one`.
- **Editors get the code.** The language server sends it as the diagnostic's `code`.

## Why

An agent, or a person searching the web, needs a stable handle on an error. A message's wording changes as it's improved, but its code doesn't. A code also gives each rule one place to be explained properly, with examples, without making the message itself longer.

- **Letter and number, not concept numbers,** because concepts can be renumbered, and a code must never change meaning.
- **One code per rule, not per call site.** Places that report the same rule share a code, and a message whose wording follows what was written still states one rule. The parser's `expect` gets one code per construct it's parsing, such as a signature, a match arm or a type, since what explains that error is the construct's shape.
- **One code per feature that isn't built yet,** so each one's explanation can say what the feature will be and how to work around it today, and can be retired when it's built.
- **A form of their own for warnings and for missing features.** "Requires" says the program broke a rule. A warning is about code that runs fine, and a missing feature is the compiler's gap, not the program's.
- **Tested explanations.** A wrong example has to report exactly its code, and the fixed one has to check clean, so an explanation can't drift from what the compiler does.

## Implementation

### Diagnostics

- **`Code`.** `src/diagnostics/diagnostics.hpp` adds `struct Code { std::string_view id; std::string_view topic; }`, where `topic` is the reference section that explains the rule.
- **The engine.** `Diagnostic` gains `code`. `printError` and `printWarning` take a `Code` first. `getErrorN` labels a diagnostic `error[S001]`, and `firstErrorCode` gives the driver the code of the first error.
- **The registry.** Every code is a named constant, one per rule, with a one-line comment saying the rule. A report names the rule, `codes::indexUnsigned`, instead of writing an id. `src/diagnostics/codes.hpp` includes one file per stage under `src/diagnostics/codes/`: `lexer.hpp`, `parser.hpp` and `sema.hpp`.

### Lexer

`Decoded` in `src/lexer/escape.hpp` carries the `Code` of what's wrong with a character, beside its message, so the lexer reports a bad escape or a bad UTF-8 byte under its own code.

### Parser

`expect` and `expected` take the construct's `Code` first. A `.` followed by something that isn't a name gives an `ErrorExpr` instead of a member named by the bad token, so sema doesn't report the same mistake a second time as an unknown method or field. One message is reworded to fit its form: `addr[size, T]` is `cherimoya expected only the type an address names in 'addr[T]', got a size or an allocator before it instead`.

### Sema

`unsupported` takes the feature's `Code`, and every kind of expression that isn't built yet has its own case in `typeOf`, so it reports its own feature. `expectBool` and `mismatch` keep one code each, since they always state one rule.

### Driver

- **`cheri guide`.** `tools/cheri/main.cpp` adds `cheri guide [code]`, which prints the explanation, or the list for no code.
- **The hint.** `pointAtGuide` prints the last line of the terminal output after the front end fails, once a run, including across the files of `cheri test`.
- **Embedding.** `CMakeLists.txt` reads every `docs/errors/*.md` into `explanations.hpp`, generated from `src/diagnostics/explanations.hpp.in`, as raw strings keyed by code, the way the gdb printers are embedded. The guide always matches the compiler's version, and works offline.

### Language Server

`diagnosticOf` in `src/lsp/features.hpp` sends `code`.

### Docs

Each `docs/errors/<code>.md` holds:
- a title with the code and the rule;
- the topic;
- a short explanation;
- a `## Wrong` and a `## Fixed` code block.

A block written ```` ```cherimoya toya ```` is a test file. One written ```` ```cherimoya whole ```` is checked as it is, without the `start` the test otherwise adds to a program that has none. A rule no program can show has `## No Example` with the reason: invalid UTF-8 can't be written in a doc, and a closure's missing `->` only ever follows a parameter list's error. `docs/errors/README.md` lists every code. The reference's diagnostics section and the README describe codes and the five forms.

## Tests

- **`scripts/codes_test.sh`** checks the registry against the docs: every constant has a file, every file has a constant, no id repeats, and `docs/errors/README.md` lists them all.
- **`scripts/error_test.sh`** runs once per code:
  - the wrong example has to report exactly its code;
  - the fixed example has to check clean;
  - for an `L` or `P` code only lexer and parser diagnostics count, since the rule is about how a program is written, so its fixed example can use syntax for a feature sema doesn't support yet.
- **`tests/unit/lsp/`** checks that a diagnostic's `code` is its rule's.

14802 unit assertions, 186 explanations checked, 9 programs, the run of `tests/test/`, 200 feature tests, 9 debug programs, all under valgrind, and the grammar's corpus, 0 failures.

## Current Limitations

- **Text only.** JSON and SARIF output, test results as JSON, and `cheri guide` by topic or section, with each topic's common errors, come in 12.14.
- **Draft explanations.** They're written from the code that reports each rule, and are to be edited by hand.
- **Codes for features being dropped.** Some `N` codes cover features that are being dropped, `wit` modes and `old`. They'll be retired when contracts replace them.
