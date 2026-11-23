# Commit 12.12: The Spec, the Reference and the Book

## The Feature

- **Commits numbered by concept.** Each commit is `N.M`, where `N` is the concept it changes most and `M` counts within that concept. The concepts run from least to most advanced: Foundations 1, Numbers 2, Text 3, Arrays and pointers 4, Collections 5, Types 6, Functions 7, Contracts 8, Modules and stdcheri 9, Concurrency 10, Interop 11, Tooling 12. When several concepts apply, the commit goes under the one it affects most. A commit that only settles array equality goes under equality, and one doing a lot of array work, equality included, goes under arrays.
- **The implementation spec.** `design/README.md` describes the pipeline and the architecture, and lists every concept with its commits, both the built ones and the planned ones. It's also the roadmap. Every commit doc is written to one shape: the feature, why it was needed, what implementing it took in each part of the compiler, its tests, and its limitations.
- **The reference.** The language definition, formerly `design/spec.md`, is now `docs/reference.md`, without the compiler's architecture, which moved to `design/README.md`.
- **The book.** `docs/book/` teaches the language from the start. Its chapters, outlines for now, go from what a computer is, through the inside and outside worlds and states and actions, to ownership and testing.
- **stdcheri's docs.** `docs/std/` is where the standard library's functions will be explained.

## Why

People come to a language for two different reasons. Contributors, and anyone curious how cherimoya works, want to see how the compiler is built. People learning the language want to know what it does and how to use it. One spec that tried to serve both did neither well: the design docs mixed how features behave with how they're built, and the spec mixed the language with its implementation.

- **Concept numbers instead of build order** keep related work together, so a reader can follow arrays from fixed arrays to typed addresses without skipping over tooling. Commits can still be built out of order.
- **The index doubles as the roadmap.** Future work then lives in one place, next to what it extends, and git history shows how far each concept has got.
- **Feature, why, implementation** is the order a reviewer asks in, and it puts the reasons next to the code they shaped.
- **The book starts from first principles** because the inside and outside split is the idea the rest of the language rests on: what can be checked before the program runs, and what can fail while it does.

## Implementation

### Design Docs

Every commit doc is rewritten from its commit's diff and renamed to its new number. Each one names the real files, functions, types and error messages its commit added. Future work is named by feature, never by number. `design/README.md` adds the pipeline table, the architecture notes that came out of the spec, and the concept index.

### Docs

`design/spec.md` moves to `docs/reference.md`. It loses its architecture section, and its reference examples become section 15. `docs/README.md` links the book, the reference and stdcheri's docs. `docs/book/README.md` lays out the chapters, and each chapter file lists what it covers, the example it builds on, and the chapter it leads to.

### README

The README points at the book, the reference and `design/`, and its layout table lists `docs/`.

### History

Every commit message in git is rewritten to its new number and a short description, such as `4.1: fixed-width arrays`.

## Tests

Nothing in the compiler changes. Every link in `design/README.md` resolves. Each doc's numbered references point at earlier commits only, and they were checked against the new numbering.

## Current Limitations

- **The book is outlines.** Its chapters get written by hand.
- **The reference keeps its own section numbers,** which follow the language rather than the concepts.
- **stdcheri's docs** wait for modules and stdcheri.
