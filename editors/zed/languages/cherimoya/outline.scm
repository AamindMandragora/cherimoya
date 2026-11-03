; the outline, which matches the language server's: every top-level function and binding, with the functions and bindings in a function's body under it, and types, where a binding is one with a plain name
(functionDeclaration
  "let" @context
  name: (_) @name) @item

(sourceFile
  (letStatement
    "let" @context
    pattern: (identifier) @name) @item)

(functionDeclaration
  body: (block
    (letStatement
      "let" @context
      pattern: (identifier) @name) @item))

(typeDeclaration
  "type" @context
  name: (_) @name) @item
