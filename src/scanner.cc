#include "scanner/scanner.cpp"

extern "C" {
void *tree_sitter_markdown_external_scanner_create() { return new Scanner(); }

bool tree_sitter_markdown_external_scanner_scan(void *payload, TSLexer *lexer,
                                                const bool *valid_symbols) {
  Scanner *scanner = static_cast<Scanner *>(payload);
  return scanner->scan(lexer, valid_symbols);
}

unsigned tree_sitter_markdown_external_scanner_serialize(void *payload,
                                                         char *buffer) {
  Scanner *scanner = static_cast<Scanner *>(payload);
  return scanner->serialize(buffer);
}

void tree_sitter_markdown_external_scanner_deserialize(void *payload,
                                                       char *buffer,
                                                       unsigned length) {
  Scanner *scanner = static_cast<Scanner *>(payload);
  scanner->deserialize(buffer, length);
}

void tree_sitter_markdown_external_scanner_destroy(void *payload) {
  Scanner *scanner = static_cast<Scanner *>(payload);
  delete scanner;
}
}
