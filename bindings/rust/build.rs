fn main() {
    let sources = ["src/parser.c", "src/scanner.c"];
    cc::Build::new()
        .std("c11")
        .include("src")
        .files(sources)
        .compile("tree-sitter-astro");

    for path in sources.into_iter().chain([
        "src/typescript_scanner.h",
        "src/tree_sitter/parser.h",
    ]) {
        println!("cargo:rerun-if-changed={path}");
    }
}
