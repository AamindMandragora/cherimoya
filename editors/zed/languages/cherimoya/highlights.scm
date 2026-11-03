; how Zed colors cherimoya before the language server answers, and wherever its semantic tokens leave something out, where a later pattern wins over an earlier one, so names start as variables and the places that make one a function, a type, or a field come after

(identifier) @variable
(wildcard) @variable.special
(comment) @comment

; literals
(integer) @number
(float) @number
(boolean) @boolean
(string) @string
(character) @string
(escapeSequence) @string.escape

; every keyword, where the place intrinsics are keywords called like functions
[
  "let" "type" "export" "import"
  "match" "repeat" "chunk"
  "unscoped" "peek" "mut" "ptr" "arr"
  "wit" "assert" "old"
] @keyword
(intrinsic) @function.builtin

; types: the built-in spellings, every name in a type's position, and what a declaration names
(builtinType) @type.builtin
(namedType name: (identifier) @type)
(namedType module: (identifier) @type)
(typeDeclaration name: (identifier) @type)
(genericParameter name: (identifier) @type)
(variant name: (identifier) @constructor)
(variantPattern name: (identifier) @constructor)

; functions: what a declaration names, what a call calls, and a method, whether called on its object or piped into
(functionDeclaration name: (identifier) @function)
(functionDeclaration name: (operatorName) @operator)
(callExpression callee: (identifier) @function)
(callExpression callee: (memberExpression field: (identifier) @function.method))
(methodExpression name: (identifier) @function.method)
(attribute name: (identifier) @attribute)
"@" @attribute

; inputs and the names that label a value
(parameter name: (identifier) @variable.parameter)
(namedArgument name: (identifier) @property)
(namedElement name: (identifier) @property)
(namedPattern name: (identifier) @property)
(recordField name: (identifier) @property)
(memberExpression field: (identifier) @property)

; operators and punctuation, where an operator named as a value, like a fold's reducer, is an operator too
(unaryExpression operator: _ @operator)
(binaryExpression operator: _ @operator)
(extraBinaryExpression operator: _ @operator)
(chainExpression operator: _ @operator)
(pipeExpression operator: _ @operator)
(assignmentStatement operator: _ @operator)
(operatorReference) @operator
["::" "->" "<-"] @operator
["(" ")" "[" "]" "{" "}"] @punctuation.bracket
["," ";" "."] @punctuation.delimiter
