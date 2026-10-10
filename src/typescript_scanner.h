/* Scanner helpers derived from tree-sitter-typescript v0.23.2 (MIT).
 * Kept separate from Astro's state machine. See LICENSE.upstream.
 * Token ordering must match TypeScript's ten inherited external symbols.
 */
#ifndef ASTRO_TYPESCRIPT_SCANNER_H
#define ASTRO_TYPESCRIPT_SCANNER_H
#include "tree_sitter/parser.h"
#include <wctype.h>

enum TypeScriptToken {
    AUTOMATIC_SEMICOLON, TEMPLATE_CHARS, TERNARY_QMARK, HTML_COMMENT,
    LOGICAL_OR, ESCAPE_SEQUENCE, REGEX_PATTERN, JSX_TEXT,
    FUNCTION_SIGNATURE_AUTOMATIC_SEMICOLON, ERROR_RECOVERY,
};

static void advance(TSLexer *lexer) { lexer->advance(lexer, false); }
static void skip(TSLexer *lexer) { lexer->advance(lexer, true); }

static bool scan_template_chars(TSLexer *lexer) {
    lexer->result_symbol = TEMPLATE_CHARS;
    for (bool has_content = false;; has_content = true) {
        lexer->mark_end(lexer);
        switch (lexer->lookahead) {
            case '`': return has_content;
            case '\0': return false;
            case '$':
                advance(lexer);
                if (lexer->lookahead == '{') return has_content;
                break;
            case '\\': return has_content;
            default: advance(lexer);
        }
    }
}

static bool scan_whitespace_and_comments(TSLexer *lexer, bool *scanned_comment) {
    for (;;) {
        while (iswspace(lexer->lookahead)) skip(lexer);
        if (lexer->lookahead != '/') return true;
        skip(lexer);
        if (lexer->lookahead == '/') {
            skip(lexer);
            while (lexer->lookahead != 0 && lexer->lookahead != '\n') skip(lexer);
            *scanned_comment = true;
        } else if (lexer->lookahead == '*') {
            skip(lexer);
            while (lexer->lookahead != 0) {
                if (lexer->lookahead == '*') {
                    skip(lexer);
                    if (lexer->lookahead == '/') { skip(lexer); break; }
                } else skip(lexer);
            }
        } else return false;
    }
}

static bool scan_automatic_semicolon(TSLexer *lexer, const bool *valid, bool *comment) {
    lexer->result_symbol = AUTOMATIC_SEMICOLON;
    lexer->mark_end(lexer);
    for (;;) {
        if (lexer->lookahead == 0) return true;
        if (lexer->lookahead == '}') {
            do { skip(lexer); } while (iswspace(lexer->lookahead));
            if (lexer->lookahead == ':') return valid[LOGICAL_OR];
            return true;
        }
        if (!iswspace(lexer->lookahead)) return false;
        if (lexer->lookahead == '\n') break;
        skip(lexer);
    }
    skip(lexer);
    if (!scan_whitespace_and_comments(lexer, comment)) return false;
    switch (lexer->lookahead) {
        case '`': case ',': case '.': case ';': case '*': case '%': case '>':
        case '<': case '=': case '?': case '^': case '|': case '&': case '/': case ':':
            return false;
        case '{':
            if (valid[FUNCTION_SIGNATURE_AUTOMATIC_SEMICOLON]) return false;
            break;
        case '(': case '[':
            if (valid[LOGICAL_OR]) return false;
            break;
        case '+': skip(lexer); return lexer->lookahead == '+';
        case '-': skip(lexer); return lexer->lookahead == '-';
        case '!': skip(lexer); return lexer->lookahead != '=';
        case 'i':
            skip(lexer);
            if (lexer->lookahead != 'n') return true;
            skip(lexer);
            if (!iswalpha(lexer->lookahead)) return false;
            for (unsigned i = 0; i < 8; i++) {
                if (lexer->lookahead != "stanceof"[i]) return true;
                skip(lexer);
            }
            if (!iswalpha(lexer->lookahead)) return false;
            break;
    }
    return true;
}

static bool scan_ternary_qmark(TSLexer *lexer) {
    while (iswspace(lexer->lookahead)) skip(lexer);
    if (lexer->lookahead != '?') return false;
    advance(lexer);
    if (lexer->lookahead == '?' || lexer->lookahead == '.') return false;
    lexer->mark_end(lexer);
    lexer->result_symbol = TERNARY_QMARK;
    while (iswspace(lexer->lookahead)) advance(lexer);
    if (lexer->lookahead == ':' || lexer->lookahead == ')' || lexer->lookahead == ',') return false;
    if (lexer->lookahead == '.') { advance(lexer); return iswdigit(lexer->lookahead); }
    return true;
}

static bool scan_typescript(TSLexer *lexer, const bool *valid) {
    if (valid[TEMPLATE_CHARS]) {
        if (valid[AUTOMATIC_SEMICOLON]) return false;
        return scan_template_chars(lexer);
    }
    if (valid[AUTOMATIC_SEMICOLON] || valid[FUNCTION_SIGNATURE_AUTOMATIC_SEMICOLON]) {
        bool comment = false;
        bool result = scan_automatic_semicolon(lexer, valid, &comment);
        if (!result && !comment && valid[TERNARY_QMARK] && lexer->lookahead == '?')
            return scan_ternary_qmark(lexer);
        return result;
    }
    return valid[TERNARY_QMARK] && scan_ternary_qmark(lexer);
}
#endif
