use std::path::{Path, PathBuf};

use tree_sitter::{Node, Parser};

use super::{HIGHLIGHTS_QUERY, LANGUAGE};

/// Collect named nodes in source order, including the root.
fn named_nodes(node: Node<'_>) -> Vec<Node<'_>> {
    let mut cursor = node.walk();
    std::iter::once(node)
        .chain(node.named_children(&mut cursor).flat_map(named_nodes))
        .collect()
}

/// Remove field labels and whitespace from a named-node tree snapshot.
fn normalize_tree(tree: &str) -> String {
    tree.replace('(', " ( ")
        .replace(')', " ) ")
        .split_whitespace()
        .filter(|word| !word.ends_with(':'))
        .collect::<Vec<_>>()
        .join(" ")
}

#[test]
fn highlighting_query_compiles() {
    tree_sitter::Query::new(&LANGUAGE.into(), HIGHLIGHTS_QUERY).expect("query must match grammar");
}

/// Compare all corpus cases with their expected named-node trees.
#[test]
fn corpus_snapshots() {
    let mut parser = Parser::new();
    parser.set_language(&LANGUAGE.into()).unwrap();
    let sections: Vec<_> = include_str!("../../test/corpus/astro.txt")
        .split("========================================================================\n")
        .skip(1)
        .collect();
    assert!(!sections.is_empty());
    assert_eq!(sections.len() % 2, 0, "each case needs a name and a body");
    for case in sections.chunks_exact(2) {
        let name = case[0].trim();
        let (source, expected) = case[1].split_once("\n---\n\n").expect("snapshot separator");
        let tree = parser.parse(source, None).unwrap();
        assert!(!tree.root_node().has_error(), "{name}");
        assert_eq!(
            normalize_tree(&tree.root_node().to_sexp()),
            normalize_tree(expected),
            "{name}",
        );
    }
}

/// Check fixture structure and UTF-8 byte positions through the Rust binding.
#[test]
fn fixture_structure() {
    let mut parser = Parser::new();
    parser.set_language(&LANGUAGE.into()).unwrap();
    for (name, source) in [
        (
            "frontmatter_fences",
            include_str!("../../test/fixtures/frontmatter_fences.astro"),
        ),
        (
            "scanner_boundaries",
            include_str!("../../test/fixtures/scanner_boundaries.astro"),
        ),
        (
            "callback_scope",
            include_str!("../../test/fixtures/callback_scope.astro"),
        ),
        (
            "unquoted_urls",
            include_str!("../../test/fixtures/unquoted_urls.astro"),
        ),
        (
            "utf8_offsets",
            include_str!("../../test/fixtures/utf8_offsets.astro"),
        ),
        (
            "script_languages",
            include_str!("../../test/fixtures/script_languages.astro"),
        ),
        (
            "dynamic_language",
            include_str!("../../test/fixtures/dynamic_language.astro"),
        ),
    ] {
        let tree = parser.parse(source, None).unwrap();
        assert!(!tree.root_node().has_error(), "{name}");
        let nodes = named_nodes(tree.root_node());
        let count = |kind| nodes.iter().filter(|node| node.kind() == kind).count();
        match name {
            "frontmatter_fences" => {
                assert_eq!(count("frontmatter"), 1);
                assert_eq!(count("type_assertion"), 1);
            }
            "scanner_boundaries" => {
                let mut raw = 0;
                let mut lookalikes = 0;
                for element in nodes.iter().filter(|node| node.kind() == "jsx_element") {
                    let opening = element.child_by_field_name("open_tag").unwrap();
                    let attributes = named_nodes(opening);
                    let attribute_names: Vec<_> = attributes
                        .iter()
                        .filter(|node| node.kind() == "attribute_name")
                        .map(|node| node.utf8_text(source.as_bytes()).unwrap())
                        .collect();
                    let contents = named_nodes(*element);
                    if attribute_names.contains(&"is:raw") {
                        raw += 1;
                        assert!(contents.iter().any(|node| node.kind() == "raw_text"));
                    }
                    if attribute_names.iter().any(|name| {
                        matches!(
                            *name,
                            "is:rawness" | "is:raw-value" | "is:raw@not-a-directive"
                        )
                    }) {
                        lookalikes += 1;
                        assert!(!contents.iter().any(|node| node.kind() == "raw_text"));
                    }
                    let tag = opening
                        .child_by_field_name("name")
                        .unwrap()
                        .utf8_text(source.as_bytes())
                        .unwrap();
                    if matches!(tag, "title" | "textarea" | "math") {
                        let expressions = contents
                            .iter()
                            .filter(|node| node.kind() == "jsx_expression")
                            .count();
                        assert_eq!(expressions, usize::from(tag != "math"), "{tag}");
                    }
                }
                assert_eq!(raw, 2);
                assert_eq!(lookalikes, 3);
                assert!(nodes
                    .iter()
                    .filter(|node| node.kind() == "raw_text")
                    .all(|node| node.named_child_count() == 0));
            }
            "callback_scope" => {
                let callbacks: Vec<_> = nodes
                    .iter()
                    .filter(|node| node.kind() == "arrow_function")
                    .map(|node| named_nodes(*node))
                    .collect();
                assert_eq!(callbacks.len(), 2);
                assert!(callbacks
                    .iter()
                    .all(|nodes| nodes.iter().any(|node| matches!(
                        node.kind(),
                        "jsx_element" | "jsx_self_closing_element" | "fragment"
                    ))));
                assert_eq!(count("shorthand_attribute"), 1);
                assert_eq!(count("spread_attribute"), 1);
                assert!(callbacks.iter().any(|nodes| nodes
                    .iter()
                    .any(|node| node.kind() == "shorthand_attribute")
                    && nodes.iter().any(|node| node.kind() == "spread_attribute")));
            }
            "unquoted_urls" => assert_eq!(count("unquoted_attribute_value"), 3),
            "utf8_offsets" => {
                let offset = source.find("value=").unwrap();
                let prefix = &source[..offset];
                let row = prefix.bytes().filter(|byte| *byte == b'\n').count();
                let column = prefix.rsplit('\n').next().unwrap().len();
                let attribute = nodes
                    .iter()
                    .find(|node| {
                        node.kind() == "attribute_name"
                            && node.utf8_text(source.as_bytes()).unwrap() == "value"
                    })
                    .unwrap();
                assert_eq!(attribute.start_byte(), offset);
                assert_eq!(
                    attribute.start_position(),
                    tree_sitter::Point::new(row, column)
                );
            }
            _ => {}
        }
    }
}

/// Check malformed input and both sides of the scanner size limits.
#[test]
fn syntax_and_scanner_limits() {
    let mut parser = Parser::new();
    parser.set_language(&LANGUAGE.into()).unwrap();
    let name_255 = "a".repeat(255);
    let name_256 = "a".repeat(256);
    for (source, expected) in [
        ("<Card>".to_owned(), false),
        ("<A></B>".to_owned(), false),
        ("{value + }".to_owned(), false),
        (format!("<{name_255}></{name_255}>"), true),
        (format!("<{name_256}></{name_256}>"), false),
        (
            format!("{}x{}", "<a>".repeat(255), "</a>".repeat(255)),
            true,
        ),
        (
            format!("{}x{}", "<a>".repeat(256), "</a>".repeat(256)),
            false,
        ),
    ] {
        let tree = parser.parse(&source, None).unwrap();
        assert_eq!(!tree.root_node().has_error(), expected, "{source}");
    }
}

/// Find Astro files without following links or entering build directories.
fn astro_paths(directory: &Path) -> std::io::Result<Vec<PathBuf>> {
    let mut paths = Vec::new();
    for entry in std::fs::read_dir(directory)? {
        let entry = entry?;
        let kind = entry.file_type()?;
        let path = entry.path();
        if kind.is_dir() {
            if !matches!(
                entry.file_name().to_str(),
                Some(
                    ".astro"
                        | ".git"
                        | ".next"
                        | ".svelte-kit"
                        | ".vercel"
                        | "build"
                        | "coverage"
                        | "dist"
                        | "node_modules"
                        | "out"
                        | "target"
                )
            ) {
                paths.extend(astro_paths(&path)?);
            }
        } else if kind.is_file()
            && path
                .extension()
                .is_some_and(|extension| extension == "astro")
        {
            paths.push(path);
        }
    }
    paths.sort();
    Ok(paths)
}

/// Parse every Astro source file in the checkout given by ASTRO_TEST_REPO.
#[test]
#[ignore = "set ASTRO_TEST_REPO to a local Astro checkout"]
fn local_repository_parses() -> std::io::Result<()> {
    let directory = std::env::var_os("ASTRO_TEST_REPO").expect("set ASTRO_TEST_REPO");
    let paths = astro_paths(Path::new(&directory))?;
    assert!(!paths.is_empty(), "repository must contain Astro files");
    let mut parser = Parser::new();
    parser.set_language(&LANGUAGE.into()).unwrap();
    for path in &paths {
        let source = std::fs::read(path)?;
        let tree = parser.parse(source, None).unwrap();
        assert!(!tree.root_node().has_error(), "{}", path.display());
    }
    println!("Parsed {} Astro files", paths.len());
    Ok(())
}
