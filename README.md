# tree-sitter-astro

A standalone Astro grammar for source analysis. It parses TypeScript frontmatter
and template expressions into one structured tree; client scripts and styles
remain separate raw-text bodies. It does not depend on the old Astro Rust
crate, the Astro compiler, or a browser at runtime.

`grammar.js` extends the upstream TypeScript/TSX grammar instead of implementing a
second JavaScript parser. The handwritten C scanner handles Astro fences, HTML
names and closing rules, fragments, comments, entities, and raw-text modes.
Generated C sources, ABI 15 metadata, Rust `LanguageFn` bindings, and a
highlighting query are included. No Node installation is needed to consume the generated parser.

## Build and test

For a Rust consumer, use this directory as a path dependency, or publish it to a
Git repository and pin a commit:

```toml
[dependencies]
tree-sitter = "0.27"
tree-sitter-astro = { path = "../tree-sitter-astro" }
```

```rust
let mut parser = tree_sitter::Parser::new();
parser.set_language(&tree_sitter_astro::LANGUAGE.into())?;
let tree = parser.parse("<p>{Astro.props.title}</p>", None).unwrap();
assert!(!tree.root_node().has_error());
```

The grammar crate itself depends on `tree-sitter-language`, not a particular
Tree-sitter runtime. Cargo compiles the checked-in C files. `tree-sitter.json`
includes the grammar sources, highlighting query path, file extension, scope,
and scanner helper path. Entanglement owns its Astro analysis and injection
queries in `src/queries/astro.scm` and `src/queries/astro-injections.scm` in the
Entanglement repository.

Run the tests with Cargo. Python and the Tree-sitter CLI are not required:

```sh
cargo test
```

Cargo checks the corpus snapshots, fixture structure, UTF-8 byte positions,
invalid input, scanner limits, and the highlighting query.

To check all Astro source files in a local website checkout:

```sh
ASTRO_TEST_REPO=../inctdd.org cargo test local_repository_parses -- --ignored --nocapture
```

This optional test skips dependency and build directories. It does not build or
modify the website. Use the Tree-sitter CLI for optional incremental fuzzing:

```sh
tree-sitter fuzz --iterations 50 --edits 3
```

To build a native shared library directly on Linux:

```sh
cc -std=c11 -fPIC -shared -Isrc src/parser.c src/scanner.c -o libtree-sitter-astro.so
```

## Regenerating

The generated C parser can be regenerated from the included grammar JSON without
Node or npm dependencies:

```sh
tree-sitter generate --abi 15 src/grammar.json
```

To change the grammar DSL, install its development dependencies, then regenerate:

```sh
bun install --ignore-scripts
tree-sitter generate --abi 15 --js-runtime bun
```

Use a CLI supporting ABI 15 and the JavaScript 0.25 grammar DSL, such as
Tree-sitter 0.27 or newer. The package manifest deliberately overrides TypeScript
0.23.2's older JavaScript dependency with JavaScript 0.25.0. Do not remove that
override: external-token ordering and the `using` declaration rules depend on it.
The npm package is development tooling only; Node bindings are not provided.
