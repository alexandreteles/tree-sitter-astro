//! Astro syntax with native TypeScript nodes and separate raw script/style bodies.
//!
//! ```
//! let mut parser = tree_sitter::Parser::new();
//! parser.set_language(&tree_sitter_astro::LANGUAGE.into()).unwrap();
//! let tree = parser.parse("<p>{Astro.props.title}</p>", None).unwrap();
//! assert!(!tree.root_node().has_error());
//! ```

use tree_sitter_language::LanguageFn;

extern "C" {
    fn tree_sitter_astro() -> *const ();
}

/// Language entry point, independent of a specific tree-sitter Rust runtime.
pub const LANGUAGE: LanguageFn = unsafe { LanguageFn::from_raw(tree_sitter_astro) };
/// Named node types and fields.
pub const NODE_TYPES: &str = include_str!("../../src/node-types.json");
/// Astro and common TypeScript syntax highlighting.
pub const HIGHLIGHTS_QUERY: &str = include_str!("../../queries/highlights.scm");

#[cfg(test)]
mod tests;
