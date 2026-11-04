// the tree-sitter grammar for cherimoya, which mirrors cheri's lexer and parser in src/lexer and src/parser, so an editor reads every program the way cheri does

// every operator cheri knows, built the way operatorTable in src/lexer/operators.hpp builds it, in the same order and with the same levels, from 1 (tightest) to 17 (loosest), so moving an operator there means moving it here the same way
function operatorTable() {
  const table = [];
  const compoundable = [];
  // adds an entry and remembers it if it should get an X<- form
  const add = (spelling, category, isOverloadable, level, prefixLevel, assoc, isCompoundable) => {
    const entry = {spelling, category, isOverloadable, level, prefixLevel, assoc};
    table.push(entry);
    if (isCompoundable) compoundable.push(entry);
  };
  const has = (spelling) => table.some((entry) => entry.spelling === spelling);
  // the built-ins, starting with flow, where <- inside an expression is construction, at the postfix level
  add('<-', 'flow', false, 1, 0, 'left', false);
  add('->', 'flow', false, 0, 0, 'none', false);
  add('-|', 'flow', false, 0, 0, 'none', false);
  add('|>', 'pipe', false, 16, 0, 'left', false);
  add('<|', 'pipe', false, 17, 0, 'right', false);
  add('+', 'arithmetic', true, 5, 0, 'left', true);
  add('-', 'arithmetic', true, 5, 3, 'left', true);
  add('*', 'arithmetic', true, 4, 0, 'left', true);
  add('/', 'arithmetic', true, 4, 0, 'left', true);
  add('%', 'arithmetic', true, 4, 0, 'left', true);
  add('&', 'bitwise', true, 7, 0, 'left', true);
  add('|', 'bitwise', true, 9, 0, 'left', true);
  add('^', 'bitwise', true, 8, 0, 'left', true);
  add('~', 'bitwise', true, 0, 3, 'none', false);
  add('<<', 'bitwise', true, 6, 0, 'left', true);
  add('>>', 'bitwise', true, 6, 0, 'left', true);
  add('&&', 'logical', false, 12, 0, 'left', true);
  add('||', 'logical', false, 14, 0, 'left', true);
  add('^^', 'logical', false, 13, 0, 'left', true);
  add('!', 'logical', true, 0, 3, 'none', false);
  add('<', 'comparison', false, 10, 0, 'chain', false);
  add('<=', 'comparison', false, 10, 0, 'chain', false);
  add('>', 'comparison', false, 10, 0, 'chain', false);
  add('>=', 'comparison', false, 10, 0, 'chain', false);
  add('=', 'equality', false, 11, 0, 'none', false);
  add('!=', 'equality', false, 11, 0, 'none', false);
  add('==', 'equality', true, 11, 0, 'none', false);
  add('!==', 'equality', false, 11, 0, 'none', false);
  // the named extra operators: the doubles and triples, three-way comparison, which never chains, and the combinators
  add('**', 'extra', true, 2, 0, 'right', true);
  add('***', 'extra', true, 2, 0, 'right', true);
  add('//', 'extra', true, 4, 0, 'left', true);
  add('%%', 'extra', true, 4, 0, 'left', true);
  add('++', 'extra', true, 5, 0, 'left', true);
  add('+++', 'extra', true, 5, 0, 'left', true);
  add('--', 'extra', true, 5, 0, 'left', true);
  add('<<<', 'extra', true, 6, 0, 'left', true);
  add('>>>', 'extra', true, 6, 0, 'left', true);
  add('&&&', 'extra', true, 12, 0, 'left', true);
  add('^^^', 'extra', true, 13, 0, 'left', true);
  add('|||', 'extra', true, 14, 0, 'left', true);
  add('<=>', 'extra', true, 10, 0, 'none', false);
  add('>>=', 'extra', true, 15, 0, 'left', false);
  add('=<<', 'extra', true, 15, 0, 'right', false);
  add('$', 'extra', true, 15, 0, 'right', false);
  add('<$>', 'extra', true, 15, 0, 'left', false);
  add('<~>', 'extra', true, 15, 0, 'left', false);
  add('~>', 'extra', true, 15, 0, 'left', false);
  add('<~', 'extra', true, 15, 0, 'left', false);
  // the wrapped extra operators: every single-symbol binary operator inside a wrapper takes that operator's level and associativity, except that a wrapped comparison never chains
  const wrappers = [['<', '>'], ['<', '<'], ['>', '>'], ['|', '|'], ['|', '>'], ['<', '|'], ['|', '<'], ['>', '|']];
  const inners = table.filter((entry) => entry.spelling.length === 1 && entry.level > 0);
  for (const [left, right] of wrappers) {
    for (const inner of inners) {
      const spelling = left + inner.spelling + right;
      if (spelling.includes('<-') || spelling.includes('->') || spelling.includes('-|') || has(spelling)) continue;
      const assoc = inner.assoc === 'chain' ? 'none' : inner.assoc;
      add(spelling, 'extra', true, inner.level, 0, assoc, inner.category === 'arithmetic' || inner.category === 'bitwise');
    }
  }
  // the compounds, one X<- for every operator above that gets one
  for (const base of compoundable) {
    table.push({spelling: base.spelling + '<-', category: 'compound', isOverloadable: false, level: 0, prefixLevel: 0, assoc: 'none'});
  }
  return table;
}

const OPERATORS = operatorTable();

// the spellings of every operator an entry test keeps, in table order
const spellings = (isKept) => OPERATORS.filter(isKept).map((entry) => entry.spelling);

// the prefix operators, - ~ !, the compounds, the operators a function can be named after, and every operator, which is what a fold can take as its reducer, since cheri's fold takes any operator token there
const PREFIX_OPERATORS = spellings((entry) => entry.prefixLevel > 0);
const COMPOUND_OPERATORS = spellings((entry) => entry.category === 'compound');
const OVERLOADABLE_OPERATORS = spellings((entry) => entry.isOverloadable);
const ALL_OPERATORS = spellings(() => true);

// the operators tree-sitter's lexer could read as two shorter ones when the parser can't take them, which is any operator that starts with another one followed by - ! or ~, the characters a prefix operator starts with, like <- as < -, -- as - -, and <<<- as <<< -, where cheri's lexer always takes the longer one, so src/scanner.c lexes these itself
const SPLITTABLE_OPERATORS = spellings((entry) => {
  for (let i = 1; i < entry.spelling.length; i++) {
    if (OPERATORS.some((other) => other.spelling === entry.spelling.slice(0, i)) && '-!~'.includes(entry.spelling[i])) return true;
  }
  return false;
});

// the binary operators of a level, split into built-ins and extras, since cheri builds a BinaryExpr for one and an ExtraBinaryExpr for the other, and then by associativity, since only operators that group the same way can share a level without parentheses
function levelOperators(level) {
  const groups = {};
  for (const entry of OPERATORS) {
    if (entry.level !== level || entry.category === 'compound' || entry.category === 'pipe') continue;
    const kind = entry.category === 'extra' ? 'extra' : 'binary';
    groups[entry.assoc] ||= {binary: [], extra: []};
    groups[entry.assoc][kind].push(entry.spelling);
  }
  return groups;
}

// the levels with binary operators below the pipes, where level 1 is postfix and level 3 is prefix only
const BINARY_LEVELS = [2, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15];

// every keyword, in the order src/lexer/keywords.hpp lists them
const KEYWORDS = [
  'let', 'type', 'export', 'import',
  'match', 'repeat', 'chunk',
  'unscoped', 'peek', 'mut', 'ptr', 'arr',
  'take', 'swap', 'replace',
  'wit', 'assert', 'old',
  'true', 'false',
];

// every built-in type spelling, which lexes as a TypeName in src/lexer/keywords.hpp
const BUILTIN_TYPES = [
  'i8', 'i16', 'i32', 'i64', 'i128',
  'i8u', 'i16u', 'i32u', 'i64u', 'i128u',
  'i8w', 'i16w', 'i32w', 'i64w', 'i128w',
  'i8uw', 'i16uw', 'i32uw', 'i64uw', 'i128uw',
  'f8', 'f8e4m3', 'f8e5m2', 'f16', 'f16e8m7', 'f32', 'f64', 'f128',
  'f8e4m3n', 'f8e5m2n', 'f16n', 'f16e8m7n', 'f32n', 'f64n', 'f128n',
  'bool', 'int', 'float', 'char', 'rune', 'boolf',
];

// cheri's parser reads an expression differently depending on where it sits, which the grammar can only follow by keeping a copy of the expression rules for each place: go is a nested expression, where <- after a value is construction, stop is the top of a statement, a pattern, or a comprehension clause, where cheri's arrowStops makes <- end the expression and keeps ( ) -> from being a closure, lead is the start of a statement, which can't begin with a scope since a scope there is a statement of its own, and argument is a generic argument, which cheri reads as a type whenever it starts like one, unless it's a type spelling called on something, which is a conversion
const FAMILIES = [
  {name: 'go', base: 'go', isStop: false, excluded: ['conversion', 'calledPath']},
  {name: 'stop', base: 'stop', isStop: true, excluded: ['plainClosure', 'conversion', 'calledPath']},
  {name: 'lead', base: 'stop', isStop: true, excluded: ['plainClosure', 'scope', 'conversion', 'calledPath']},
  {name: 'argument', base: 'go', isStop: false, excluded: ['type', 'bracket']},
];

// what an expression can start with, cheri's null denotations, each tagged with what a family might leave out, where a name can come after the token that tells the scanner an expression starts there, so it can report if (
const atoms = ($) => [
  ['literal', $.integer], ['literal', $.float], ['literal', $.string], ['literal', $.character], ['literal', $.boolean],
  ['name', seq(optional($._expressionStart), $.identifier)], ['name', $.intrinsic], ['name', $.wildcard], ['name', $.methodExpression],
  ['paren', $.parenthesizedExpression], ['paren', $.annotationExpression], ['paren', $.foldExpression],
  ['plainClosure', alias($._plainClosure, $.closure)],
  ['bracket', $.pack], ['bracket', $.range], ['bracket', $.comprehension], ['bracket', alias($._captureClosure, $.closure)],
  ['type', $.typeExpression],
  ['conversion', alias($._conversion, $.callExpression)],
  ['calledPath', alias($._calledPath, $.callExpression)],
  ['scope', $.block], ['scope', $.matchExpression], ['scope', $.repeatExpression], ['scope', $.chunkExpression],
];

// one of several tokens, or the token itself when there's only one, since tree-sitter warns about a choice of one
const oneOf = (tokens) => (tokens.length === 1 ? tokens[0] : choice(...tokens));

// a comma-separated list with no trailing comma, which is how cheri reads arguments, packs, and generic arguments
const commaSeparated = (rule) => optional(seq(rule, repeat(seq(',', rule))));

// a comma-separated list that may end in a comma, which is how cheri reads parameters, generic parameters, tuple types, protocols, and module items, since their loops stop at the closing bracket after a comma
const commaSeparatedTrailing = (rule) => optional(seq(rule, repeat(seq(',', rule)), optional(',')));

// a bracketed list of the same elements a range starts with, written out to its third element so the parser doesn't have to decide between a list and a range before it sees whether ... comes next
const bracketedList = (rule) => seq('[', optional(seq(rule, optional(seq(',', rule, repeat(seq(',', rule)))))), ']');

// the five shapes a range can take, with exactly one ..., at most two values before it and one after, at least one bound, and exclusivity markers only on the endpoints, the first value and the last
const rangeBody = ($) => {
  const endpoint = (name) => field(name, choice($._expression, $.exclusiveBound));
  return choice(
    seq('[', endpoint('first'), ',', '...', ']'),
    seq('[', endpoint('first'), ',', field('second', $._expression), ',', '...', ']'),
    seq('[', '...', ',', endpoint('last'), ']'),
    seq('[', endpoint('first'), ',', '...', ',', endpoint('last'), ']'),
    seq('[', endpoint('first'), ',', field('second', $._expression), ',', '...', ',', endpoint('last'), ']'),
  );
};

// precedences that settle what cheri settles by looking at the next token: a name before <- names an element, a mode or qualifier takes a type after it, a type keeps reading its path and arguments, a lone name where a generic argument goes is a type, which is a dynamic precedence since a pattern can look the same until later, and a yield takes the name after it as its target
const PREC = {
  bareType: 1,
  pathType: 2,
  named: 3,
  greedy: 1,
};

// builds every expression rule for one family: the postfix forms, the prefix operators, one rule for each group of binary operators on each level, and the two pipes on top
function expressionRules(family) {
  const rules = {};
  const f = family.name;
  const b = family.base;
  const isSpine = f !== b;
  const ref = ($, name) => $[`_${name}`];

  rules[`_${f}Atom`] = ($) => choice(...atoms($).filter(([kind]) => !family.excluded.includes(kind)).map(([, rule]) => rule));

  // the postfix forms all sit at level 1 and chain onto whatever came before them, where construction is only there when <- doesn't stop the expression
  const postfix = ['Call', 'Index', 'Member'].concat(family.isStop ? [] : ['Construct']);
  const postfixNodes = {Call: 'callExpression', Index: 'indexExpression', Member: 'memberExpression', Construct: 'constructExpression'};
  rules[`_${f}Postfix`] = ($) => choice(ref($, `${f}Atom`), ...postfix.map((kind) => alias(ref($, `${f}${kind}`), $[postfixNodes[kind]])));
  rules[`_${f}Call`] = ($) => seq(field('callee', ref($, `${f}Postfix`)), field('arguments', $.argumentList));
  rules[`_${f}Index`] = ($) => seq(field('object', ref($, `${f}Postfix`)), $._indexBody);
  rules[`_${f}Member`] = ($) => seq(field('object', ref($, `${f}Postfix`)), '.', field('field', $.identifier));
  if (!family.isStop) {
    rules[`_${f}Construct`] = ($) => seq(field('target', ref($, `${f}Postfix`)), '<-', field('value', choice($.pack, $.range, $.comprehension)));
  }

  // the prefix operators and old, whose operand is read at level 3, so it takes in postfix forms and **, and a family that only changes how an expression starts shares its base's, since these start with their own token
  if (!isSpine) {
    rules[`_${f}Unary`] = ($) => seq(field('operator', choice(...PREFIX_OPERATORS)), field('operand', ref($, `${f}Level2`)));
    rules[`_${f}Old`] = ($) => seq('old', field('operand', ref($, `${f}Level2`)));
  }

  // each level is the level below it or an operation on this one, where a left-associative group recurses on its left, a right-associative group on its right, a non-associative group on neither, and a chain collects every comparison at once, and only groups that group the same way can sit next to each other, which is cheri's needsParens
  let lower = null;
  for (const level of BINARY_LEVELS) {
    const groups = levelOperators(level);
    const below = lower;
    const nodes = [];
    const assocs = Object.keys(groups);
    const isMixed = assocs.length > 1;
    const lowerRule = ($) => (below === null ? ref($, `${f}Postfix`) : ref($, `${f}Level${below}`));
    const baseLower = ($) => (below === null ? ref($, `${b}Postfix`) : ref($, `${b}Level${below}`));
    for (const assoc of assocs) {
      for (const kind of ['binary', 'extra']) {
        const operators = groups[assoc][kind];
        if (operators.length === 0) continue;
        const name = `${f}${kind === 'binary' ? 'Binary' : 'Extra'}${level}${assoc[0].toUpperCase()}${assoc.slice(1)}`;
        const node = kind === 'binary' ? 'binaryExpression' : 'extraBinaryExpression';
        const operator = field('operator', oneOf(operators));
        if (assoc === 'left') {
          const leftOperand = ($) => (isMixed ? ref($, `${f}Left${level}`) : ref($, `${f}Level${level}`));
          rules[`_${name}`] = ($) => seq(field('left', leftOperand($)), operator, field('right', baseLower($)));
        } else if (assoc === 'right') {
          const rightOperand = ($) => (isMixed ? ref($, `${b}Right${level}`) : ref($, `${b}Level${level}`));
          rules[`_${name}`] = ($) => seq(field('left', lowerRule($)), operator, field('right', rightOperand($)));
        } else if (assoc === 'none') {
          rules[`_${name}`] = ($) => seq(field('left', lowerRule($)), operator, field('right', baseLower($)));
        } else {
          rules[`_${name}`] = ($) => seq(field('operand', lowerRule($)), repeat1(seq(field('operator', oneOf(operators)), field('operand', baseLower($)))));
        }
        nodes.push({assoc, name, node: assoc === 'chain' ? 'chainExpression' : node});
      }
    }
    // a mixed level names what its left and right groups can take, the level below or more of the same group
    if (isMixed) {
      for (const assoc of ['left', 'right']) {
        const same = nodes.filter((node) => node.assoc === assoc);
        if (same.length === 0) continue;
        rules[`_${f}${assoc === 'left' ? 'Left' : 'Right'}${level}`] = ($) => choice(lowerRule($), ...same.map((node) => alias(ref($, node.name), $[node.node])));
      }
    }
    const prefixed = level === 2 ? ($) => [alias(ref($, `${b}Unary`), $.unaryExpression), alias(ref($, `${b}Old`), $.oldExpression)] : () => [];
    rules[`_${f}Level${level}`] = ($) => choice(lowerRule($), ...prefixed($), ...nodes.map((node) => alias(ref($, node.name), $[node.node])));
    lower = level;
  }

  // the pipes, where |> groups to the left and <| to the right, and the two can't share a depth, so a forward pipe's operands never hold a backward one and the other way around
  rules[`_${f}PipeForward`] = ($) => seq(field('left', ref($, `${f}Level16`)), field('operator', '|>'), field('right', ref($, `${b}Level15`)));
  rules[`_${f}Level16`] = ($) => choice(ref($, `${f}Level15`), alias(ref($, `${f}PipeForward`), $.pipeExpression));
  rules[`_${f}PipeBack`] = ($) => seq(field('left', ref($, `${f}Level15`)), field('operator', '<|'), field('right', ref($, `${b}BackOperand`)));
  if (!isSpine) {
    rules[`_${f}BackOperand`] = ($) => choice(ref($, `${f}Level15`), alias(ref($, `${f}PipeBack`), $.pipeExpression));
  }
  rules[`_${f}Level17`] = ($) => choice(ref($, `${f}Level16`), alias(ref($, `${f}PipeBack`), $.pipeExpression));
  return rules;
}

// every family's rules, which sit among the grammar's rules below
const familyRules = Object.assign({}, ...FAMILIES.map(expressionRules));

module.exports = grammar({
  name: 'cherimoya',

  // identifiers are the words keywords and type spellings are pulled out of, so i64 is a type but i64x is a name
  word: ($) => $.identifier,

  // cheri skips the whitespace isspace accepts, which is ASCII only, and # comments
  extras: ($) => [/[ \t\n\r\f\v]/, $.comment],

  // src/scanner.c lexes the operators that could split, taking the longest spelling in the table whether or not the parser can take it there, as cheri's lexer does, and lexes one the parser can't take, an operator touching another when either needs spacing, or an if before a ( where an expression starts as the invalid operator, which no rule takes, and lexes the empty token that says an attribute's name isn't followed by (, while the last four are never lexed and only tell the scanner where it is: where a yield's target can come next, where what can't be finished is, where an expression can start, and where a signature's first -> goes, which the name of a function named after an operator could touch, and which no other external token would make the scanner see, since tree-sitter only lets two parse states share a lexer when they agree on every external token, which is why the rest of the operators aren't external
  externals: ($) => [...SPLITTABLE_OPERATORS, $._invalidOperator, $._noArguments, $._yieldTargetPosition, $._neverLexed, $._expressionStart, $._signatureArrow],

  // every keyword and type spelling is reserved, since cheri's lexer never reads one as a name, where tree-sitter would otherwise read a keyword the parser can't take as a name
  reserved: {
    global: (_) => [...KEYWORDS, ...BUILTIN_TYPES, '_'],
  },

  // the places cheri decides by looking past what the grammar has seen, which the parser follows by trying each reading until all but one fail
  conflicts: ($) => [
    // a pattern and an expression look the same until what follows them settles which one it was, a <- in a comprehension or a loop, or a ... that makes a pack pattern a range pattern, whose elements are expressions
    [$._goAtom, $._patternBase],
    [$._goAtom, $._negatable],
    [$._goAtom, $._patternName],
    [$._goAtom, $._patternBase, $._patternName],
    [$._stopAtom, $._patternName],
    [$._patternBase, $._patternName],
    [$.pack, $.packPattern],
    [$.range, $.rangePattern],
    // ( ) holds a closure's parameters, a fold, a grouping, or an annotation, and [ ] a closure's captures or a pack, until -> or [ comes after it, and a place in parentheses reads like an expression until <- comes after it
    [$._goAtom, $.parameter],
    [$.captureList, $.pack],
    [$._parenthesizedPlace, $._goAtom],
    [$._parenthesizedPlace, $._goPostfix],
    [$._goAtom, $.parameter, $._parenthesizedPlace],
    // let [ opens a function's generics or a pattern, which the ( after the ] settles, where a fallback after a generic parameter's <- reads like a named pattern element
    [$._patternBase, $.genericParameter],
    [$._goAtom, $._patternBase, $.genericParameter],
    [$._argumentAtom, $._patternBase],
    [$._argumentAtom, $._patternName],
    [$._patternBase, $._bareNamedType],
    [$._goAtom, $._patternBase, $._bareNamedType],
    [$._argumentAtom, $._patternBase, $._bareNamedType],
    [$.parameter, $._goAtom, $._patternBase, $._patternName],
    [$.packPattern, $.tupleType],
    [$.pack, $.packPattern, $.tupleType],
    // a lone name, a mode, or a type spelling where a generic argument goes is a type or a value, which cheri reads as a type, and a type definition's right side reads like a type until a | makes it a sum's first variant
    [$._argumentAtom, $._bareNamedType],
    // a dotted path in a generic argument is a type until a ( after it makes it a call
    [$._calledPathMember, $._pathNamedType],
    [$.modeType, $._valueModeType],
    [$.typeExpression, $._unrefinedType],
    [$.variant, $._genericArgument],
  ],

  rules: {
    // a program is a series of statements, most of them declarations
    sourceFile: ($) => repeat($._statement),

    // a comment runs from # to the end of the line, and there are no block comments
    comment: (_) => token(seq('#', /[^\n]*/)),

    // statements, where an expression comes first and decides between assignment, yield, and expression statement, and every declaration can sit wherever a statement can
    _statement: ($) => choice(
      $.letStatement,
      $.functionDeclaration,
      $.witStatement,
      $.assertStatement,
      $.typeDeclaration,
      $.exportDeclaration,
      $.importDeclaration,
      $.attribute,
      $.yieldStatement,
      $.expressionStatement,
      $.assignmentStatement,
    ),

    // let pattern :: type <- value; where the value is required, and <- _ leaves it uninitialized
    letStatement: ($) => seq(
      'let',
      field('pattern', $._letPattern),
      optional(seq('::', field('type', $._type))),
      '<-',
      field('value', $._expression),
      ';',
    ),

    // let [generics] (inputs) -> name -> (outputs) wit {obligations} body, where an extern ends with ; instead of a body
    functionDeclaration: ($) => seq($._signature, choice(field('body', $.block), ';')),

    // a protocol's member is a signature with neither a body nor a ;
    _signature: ($) => seq(
      'let',
      optional(field('generics', $.genericParameters)),
      field('parameters', $.parameterList),
      optional($._signatureArrow),
      '->',
      field('name', $._functionName),
      '->',
      field('outputs', $.parameterList),
      optional(seq('wit', '{', field('obligations', $._expression), '}')),
    ),

    // a function is named by an identifier or an overloadable operator, built-in or extra
    _functionName: ($) => choice($.identifier, $.operatorName),

    // an overloadable operator where a name goes, in a function's signature or a module's items
    operatorName: (_) => choice(...OVERLOADABLE_OPERATORS),

    // wit {predicate}; or wit {T :: Protocol};
    witStatement: ($) => seq(
      'wit',
      '{',
      field('predicate', $._expression),
      optional(seq('::', field('bound', $._type))),
      '}',
      ';',
    ),

    // assert (condition);
    assertStatement: ($) => seq('assert', '(', field('condition', $._expression), ')', ';'),

    // value -| target, where both are optional, a missing target means the innermost scope, and a name right after -| is always its target, even if, which the scanner would otherwise report when ( follows it
    yieldStatement: ($) => prec.right(PREC.greedy, seq(
      optional(field('value', choice($._leadExpression, $._scope))),
      '-|',
      optional($._yieldTargetPosition),
      optional(field('target', choice($.identifier, 'repeat', 'match'))),
    )),

    // an expression and its ;, or a scope on its own, which takes no ;
    expressionStatement: ($) => choice(seq($._leadExpression, ';'), $._scope),

    // the scopes a statement can start with, where a name in front of { names the block
    _scope: ($) => choice($.block, alias($._namedBlock, $.block), $.matchExpression, $.repeatExpression, $.chunkExpression),

    // place <- value; or place op<- value; where a place is a name, a field, an index, or _, also in parentheses
    assignmentStatement: ($) => seq(
      field('place', $._place),
      field('operator', choice('<-', ...COMPOUND_OPERATORS)),
      field('value', $._expression),
      ';',
    ),

    // what can be assigned to at the start of a statement, where a method with no object counts, since cheri reads it as a field access
    _place: ($) => choice(
      $.identifier,
      $.intrinsic,
      $.wildcard,
      $.methodExpression,
      alias($._leadMember, $.memberExpression),
      alias($._leadIndex, $.indexExpression),
      alias($._parenthesizedPlace, $.parenthesizedExpression),
    ),

    // a place in parentheses, which cheri sees through, where what's inside is a nested expression again
    _parenthesizedPlace: ($) => seq('(', choice(
      $.identifier,
      $.intrinsic,
      $.wildcard,
      $.methodExpression,
      alias($._goMember, $.memberExpression),
      alias($._goIndex, $.indexExpression),
      alias($._parenthesizedPlace, $.parenthesizedExpression),
    ), ')'),

    // @name(args) and the statement it applies to, or ; when it applies to the whole file, where a ( after the name always holds its arguments, so one without them takes the empty token the scanner only lexes when no ( follows
    attribute: ($) => choice(
      seq('@', field('name', $.identifier), field('arguments', $.argumentList), $._attributeEnd),
      seq('@', field('name', $.identifier), $._noArguments, $._attributeEnd),
    ),

    // what follows an attribute: ; for the whole file, or the statement it applies to
    _attributeEnd: ($) => choice(';', field('target', $._statement)),

    // type Name[generics] <- definition; where the definition is a type for newtypes, refinements, and products, variants for sums and enums, and signatures for protocols
    typeDeclaration: ($) => seq(
      'type',
      field('name', $.identifier),
      optional(field('generics', $.genericParameters)),
      '<-',
      field('value', choice($._type, $.sumType, $.protocol)),
      ';',
    ),

    // two or more variants, A | B[T]
    sumType: ($) => seq($.variant, repeat1(seq('|', $.variant))),

    // a variant is a single name with only types in its brackets
    variant: ($) => seq(
      field('name', choice($.identifier, $.builtinType)),
      optional(seq('[', commaSeparated(field('payload', $._argumentType)), ']')),
    ),

    // a pack of signatures, recognized by the let that leads it
    protocol: ($) => seq('[', alias($._signature, $.functionDeclaration), repeat(seq(',', alias($._signature, $.functionDeclaration))), optional(','), ']'),

    // export [items];
    exportDeclaration: ($) => seq('export', $._moduleItems, ';'),

    // import [items] <- dotted.path;
    importDeclaration: ($) => seq('import', $._moduleItems, '<-', field('module', $.modulePath), ';'),

    // the bracketed items of an export or import
    _moduleItems: ($) => seq('[', commaSeparatedTrailing(field('item', $.moduleItem)), ']'),

    // a name to export or import, with the fields that go with it: Result[dist, prev]
    moduleItem: ($) => seq(
      field('name', choice($.identifier, $.builtinType, $.operatorName)),
      optional(seq('[', commaSeparatedTrailing(field('field', $.identifier)), ']')),
    ),

    // a module's dotted path
    modulePath: ($) => seq($.identifier, repeat(seq('.', $.identifier))),

    // [name :: bound & bound <- fallback, ...] after a function's let or a type's name
    genericParameters: ($) => seq('[', commaSeparatedTrailing($.genericParameter), ']'),

    // one generic parameter, whose bounds can't be refined and whose fallback is a type or a comptime value
    genericParameter: ($) => seq(
      field('name', $.identifier),
      optional(seq('::', field('bound', $._unrefinedType), repeat(seq('&', field('bound', $._unrefinedType))))),
      optional(seq('<-', field('default', $._genericArgument))),
    ),

    // (name :: type <- default, ...) for inputs, outputs, and closure parameters
    parameterList: ($) => seq('(', commaSeparatedTrailing($.parameter), ')'),

    // a parameter, where a missing type is left to inference and the default is optional
    parameter: ($) => seq(
      field('name', $.identifier),
      optional(seq('::', field('type', $._type))),
      optional(seq('<-', field('default', $._expression))),
    ),

    // the expression families, generated above
    ...familyRules,

    // an expression where nothing outside it changes how it's read
    _expression: ($) => $._goLevel17,

    // an expression at the start of a statement
    _leadExpression: ($) => $._leadLevel17,

    // an expression where a generic argument goes, which can't start like a type
    _argumentExpression: ($) => $._argumentLevel17,

    // the bracketed part of an index: nothing for a dereference, a list of values, or a range for a slice, or a comprehension
    _indexBody: ($) => choice(bracketedList(choice(field('index', $._expression), $._namedIndex)), field('index', $.range), field('index', $.comprehension)),

    // a name and <- at the start of an index's element, which cheri reads as a name for the element, which an index can't have, so it's never finished and the parser reports it
    _namedIndex: ($) => prec(PREC.named, seq($.identifier, '<-', $._neverLexed)),

    // a parenthesized argument list, a call's or an attribute's
    argumentList: ($) => seq('(', commaSeparated(choice($._expression, $.namedArgument)), ')'),

    // an argument a name and <- in front of it name: the b <- 2 in f(b <- 2)
    namedArgument: ($) => prec(PREC.named, seq(field('name', $.identifier), '<-', field('value', $._expression))),

    // literals, where a number's text is kept raw for sema to read
    integer: (_) => token(choice(
      /0[xX][0-9a-fA-F_]*[0-9a-fA-F][0-9a-fA-F_]*/,
      /0[oO][0-7_]*[0-7][0-7_]*/,
      /0[bB][01_]*[01][01_]*/,
      /[0-9][0-9_]*/,
    )),

    // a point followed by a digit of the number's base makes it a float, in every base
    float: (_) => token(choice(
      /0[xX][0-9a-fA-F_]*[0-9a-fA-F][0-9a-fA-F_]*\.[0-9a-fA-F][0-9a-fA-F_]*/,
      /0[oO][0-7_]*[0-7][0-7_]*\.[0-7][0-7_]*/,
      /0[bB][01_]*[01][01_]*\.[01][01_]*/,
      /[0-9][0-9_]*\.[0-9][0-9_]*/,
    )),

    // a string holds any characters, newlines too, and escapes
    string: ($) => seq('"', repeat(choice($._stringContent, $.escapeSequence)), token.immediate('"')),

    // a run of characters in a string, which outranks a comment so a # inside a string stays in it
    _stringContent: (_) => token.immediate(prec(1, /[^"\\]+/)),

    // a char literal holds exactly one character, written as itself or as an escape
    character: ($) => seq("'", choice($._characterContent, $.escapeSequence), token.immediate("'")),

    // the one character in a char literal
    _characterContent: (_) => token.immediate(prec(1, /[^'\\]/)),

    // \n \t \r \0 \\ \' \", \xHH for a code point up to FF, or \u{...} with 1 to 6 hex digits naming a Unicode scalar value, which is every code point up to 10FFFF but the surrogates, D800 to DFFF
    escapeSequence: (_) => token.immediate(seq('\\', choice(
      /[ntr0\\'"]/,
      /x[0-9a-fA-F]{2}/,
      /u\{([0-9a-fA-F]{1,3}|0{0,2}([0-9a-cA-Ce-fE-F][0-9a-fA-F]{3}|[dD][0-7][0-9a-fA-F]{2})|0?[1-9a-fA-F][0-9a-fA-F]{4}|10[0-9a-fA-F]{4})\}/,
    ))),

    // the bool literals
    boolean: (_) => choice('true', 'false'),

    // names, where _ alone is a wildcard and not a name
    identifier: (_) => /[A-Za-z_][A-Za-z0-9_]*/,

    // _ means discard, uninitialized, or wildcard depending on where it is
    wildcard: (_) => '_',

    // the place intrinsics are keywords, but they're called like functions
    intrinsic: (_) => choice('take', 'swap', 'replace'),

    // every built-in type spelling
    builtinType: (_) => choice(...BUILTIN_TYPES),

    // a method with no object, which is what a pipe gives one: the .len in xs |> .len
    methodExpression: ($) => seq('.', field('name', $.identifier)),

    // a parenthesized expression, which cheri sees through
    parenthesizedExpression: ($) => seq('(', $._expression, ')'),

    // (value :: Type), an expression treated as a type
    annotationExpression: ($) => seq('(', field('value', $._expression), '::', field('type', $._type), ')'),

    // (reducer, seed)[sequence], where the reducer may be a bare operator
    foldExpression: ($) => seq(
      '(',
      field('reducer', choice($.operatorReference, $._expression)),
      ',',
      field('seed', $._expression),
      ')',
      field('sequence', choice($.pack, $.range, $.comprehension)),
    ),

    // a bare operator used as a fold's reducer: the + in (+, 0)[xs]
    operatorReference: (_) => choice(...ALL_OPERATORS),

    // (params) -> (outputs) { body }, with the outputs optional
    _plainClosure: ($) => seq(
      field('parameters', $.parameterList),
      '->',
      optional(field('outputs', $.parameterList)),
      field('body', $.block),
    ),

    // [captures] (params) -> (outputs) { body }, whose captures are unnamed
    _captureClosure: ($) => seq(
      field('captures', $.captureList),
      field('parameters', $.parameterList),
      '->',
      optional(field('outputs', $.parameterList)),
      field('body', $.block),
    ),

    // the values a closure owns
    captureList: ($) => bracketedList($._expression),

    // a pack of values, each optionally named: [x <- 1.0, y <- 2.0]
    pack: ($) => bracketedList(choice($._expression, $.namedElement)),

    // a pack element a name and <- in front of it name
    namedElement: ($) => prec(PREC.named, seq(field('name', $.identifier), '<-', field('value', $._expression))),

    // a range, [first, second, ..., last], whose stride stays as its first two values for sema to work out
    range: ($) => rangeBody($),

    // an endpoint with an exclusivity marker, a comparison on the bound: >0 means stay above 0
    exclusiveBound: ($) => seq(field('marker', choice('<', '>')), field('value', $._expression)),

    // [head : clause && clause], where each clause is read at level 11, so && joins them and || needs parentheses
    comprehension: ($) => seq(
      '[',
      field('head', $._expression),
      ':',
      field('clause', $._clause),
      repeat(seq('&&', field('clause', $._clause))),
      ']',
    ),

    // a membership clause, pattern <- source, or a filter, where <- at the top of the clause is membership and not construction
    _clause: ($) => choice($.membershipClause, $._stopLevel11),

    // pattern <- source
    membershipClause: ($) => prec.dynamic(1, seq(field('pattern', $._pattern), '<-', field('source', $._goLevel11))),

    // a type spelling called on something in a generic argument, which cheri reads as a conversion, a value, rather than a type, like the i64u(n) in arr[i64u(n)] int(0), where the other families read it as a type in value position followed by a call
    _conversion: ($) => seq(field('callee', alias($._conversionType, $.typeExpression)), field('arguments', $.argumentList)),
    _conversionType: ($) => field('type', alias($._builtinNamedType, $.namedType)),

    // a dotted path called on something in a generic argument, which cheri reads as a call or a method call, a value, rather than a type, like the xs.len() in arr[xs.len() + 1] int(0), since a type is never called there
    _calledPath: ($) => seq(field('callee', alias($._calledPathMember, $.memberExpression)), field('arguments', $.argumentList)),
    _calledPathMember: ($) => seq(field('object', choice($.identifier, alias($._calledPathMember, $.memberExpression))), '.', field('field', $.identifier)),

    // a type in value position, which gets called to convert or allocate: the i32 in i32(x), the arr mut T in arr mut T(n)
    typeExpression: ($) => field('type', choice(
      alias($._builtinNamedType, $.namedType),
      $.handleType,
      alias($._valueModeType, $.modeType),
      $.qualifiedType,
    )),

    // the sequence of statements in between {}
    block: ($) => seq('{', repeat($._statement), '}'),

    // a named block, which only a statement can start with
    _namedBlock: ($) => seq(field('name', $.identifier), '{', repeat($._statement), '}'),

    // match name (equality, subject) { arms }, where a subjectless match's arms are (condition) or _
    matchExpression: ($) => seq(
      'match',
      optional(field('name', $.identifier)),
      choice(
        seq('{', repeat(field('arm', alias($._guardArm, $.matchArm))), '}'),
        seq(
          '(',
          optional(seq(field('equality', choice('=', '==')), ',')),
          field('subject', $._expression),
          ')',
          '{',
          repeat(field('arm', $.matchArm)),
          '}',
        ),
      ),
    ),

    // pattern -> { body }
    matchArm: ($) => seq(field('pattern', $._pattern), '->', field('body', $.block)),

    // an arm of a subjectless match, (condition) -> { body } or _ -> { body }
    _guardArm: ($) => seq(field('pattern', choice($.guardPattern, $.wildcard)), '->', field('body', $.block)),

    // the (condition) of a subjectless match's arm
    guardPattern: ($) => seq('(', field('condition', $._expression), ')'),

    // repeat name (binder) <- source wit {invariant} { body }, where a binder means for-each, a condition alone means while, and neither means infinite, and a name has to be followed by the header or the body
    repeatExpression: ($) => seq('repeat', choice(
      seq(optional(field('name', $.identifier)), $._repeatHeader, optional($._invariant), field('body', $.block)),
      seq(optional(field('name', $.identifier)), field('body', $.block)),
      seq($._invariant, field('body', $.block)),
    )),

    // (condition) for a while loop, or (binder) <- source for a for-each
    _repeatHeader: ($) => choice(
      seq('(', field('condition', $._expression), ')'),
      seq('(', field('pattern', $._pattern), ')', '<-', field('source', $._expression)),
    ),

    // wit {invariant}
    _invariant: ($) => seq('wit', '{', field('invariant', $._expression), '}'),

    // chunk name { body }, a block whose allocations default to a bump region
    chunkExpression: ($) => seq('chunk', optional(field('name', $.identifier)), field('body', $.block)),

    // patterns, written like expressions: literals, names, _, Variant[p], packs, ranges, set-builders, and any of them in parentheses, which cheri sees through
    _pattern: ($) => choice($._patternBase, $.variantPattern, $.parenthesizedPattern),

    // a pattern after let, which can't start with ( since let ( starts a function, not even in a variant's name
    _letPattern: ($) => choice($._patternBase, alias($._letVariantPattern, $.variantPattern)),

    // the patterns that start the same way wherever they are
    _patternBase: ($) => choice(
      $.integer,
      $.float,
      $.boolean,
      $.string,
      $.character,
      $.negativeLiteral,
      $.wildcard,
      $.identifier,
      $.intrinsic,
      $.packPattern,
      $.rangePattern,
      $.setBuilderPattern,
    ),

    // a pattern in parentheses
    parenthesizedPattern: ($) => seq('(', $._pattern, ')'),

    // a negated number, -1, where cheri only takes - straight onto a number literal
    negativeLiteral: ($) => seq('-', field('value', $._negatable)),

    // a number, also in parentheses
    _negatable: ($) => choice($.integer, $.float, alias($._parenthesizedNumber, $.parenthesizedPattern)),

    // a number in parentheses
    _parenthesizedNumber: ($) => seq('(', $._negatable, ')'),

    // a variant with a payload, Some[d], Rect[w, h]
    variantPattern: ($) => seq(field('name', $._patternName), $._variantPayload),

    // a variant after let, whose name can't be in parentheses
    _letVariantPattern: ($) => seq(field('name', choice($.identifier, $.intrinsic)), $._variantPayload),

    // a variant's brackets, which hold patterns, or a range or a set-builder, since cheri reads them as an index's
    _variantPayload: ($) => choice(
      bracketedList(field('payload', $._pattern)),
      field('payload', $.rangePattern),
      field('payload', $.setBuilderPattern),
    ),

    // the name a variant pattern or a set-builder starts with, also in parentheses, which cheri sees through
    _patternName: ($) => choice($.identifier, $.intrinsic, alias($._parenthesizedName, $.parenthesizedPattern)),

    // a name in parentheses
    _parenthesizedName: ($) => seq('(', $._patternName, ')'),

    // a pack pattern for tuples, decision tables, and records: [true, _], [x <- 0, y <- py]
    packPattern: ($) => bracketedList(choice($._pattern, $.namedPattern)),

    // a pack pattern element a name and <- in front of it name
    namedPattern: ($) => prec(PREC.named, seq(field('name', $.identifier), '<-', field('pattern', $._pattern))),

    // a range pattern, written like a range: [0, ..., 100], [..., <0], [>100, ...]
    rangePattern: ($) => rangeBody($),

    // a set-builder, a lone binder and the filters it has to pass: [x : x % 2 = 0]
    setBuilderPattern: ($) => seq(
      '[',
      field('binder', $._patternName),
      ':',
      field('predicate', $._stopLevel11),
      repeat(seq('&&', field('predicate', $._stopLevel11))),
      ']',
    ),

    // types: modes and qualifiers in front, then a handle, a tuple or record, or a named type, then an optional refinement
    _type: ($) => choice($._unrefinedType, $.refinedType),

    // a type with no refinement on the outside
    _unrefinedType: ($) => choice(
      alias($._bareNamedType, $.namedType),
      alias($._pathNamedType, $.namedType),
      alias($._builtinNamedType, $.namedType),
      $.modeType,
      $.qualifiedType,
      $.handleType,
      $.tupleType,
      $.recordType,
    ),

    // a type and a refinement: int {p : 0 <= p <= 100}, int {a >= 0}
    refinedType: ($) => seq(field('type', $._unrefinedType), $._refinement),

    // {binder : predicate} or {predicate}
    _refinement: ($) => seq('{', optional(seq(field('binder', $.identifier), ':')), field('predicate', $._expression), '}'),

    // a lone name as a type
    _bareNamedType: ($) => prec.dynamic(PREC.bareType, field('name', $.identifier)),

    // a name with a dotted path or generic arguments: std.mem.Allocator, Map[K, V]
    _pathNamedType: ($) => prec.right(PREC.pathType, choice(
      seq(field('name', $.identifier), field('arguments', $.typeArguments)),
      seq(
        field('module', $.identifier),
        '.',
        repeat(seq(field('module', $.identifier), '.')),
        field('name', $.identifier),
        optional(field('arguments', $.typeArguments)),
      ),
    )),

    // a built-in type, which can take a path and arguments like any other name
    _builtinNamedType: ($) => prec.right(PREC.pathType, choice(
      seq(field('name', $.builtinType), optional(field('arguments', $.typeArguments))),
      seq(
        field('module', $.builtinType),
        '.',
        repeat(seq(field('module', $.identifier), '.')),
        field('name', $.identifier),
        optional(field('arguments', $.typeArguments)),
      ),
    )),

    // unscoped, peek, or wit and the type they apply to, which is left to inference when none follows
    modeType: ($) => prec.right(PREC.greedy, seq(field('mode', choice('unscoped', 'peek', 'wit')), optional(field('type', $._unrefinedType)))),

    // a mode in value position, which can't be wit
    _valueModeType: ($) => prec.right(PREC.greedy, seq(field('mode', choice('unscoped', 'peek')), optional(field('type', $._unrefinedType)))),

    // mut and the type it applies to, which is left to inference when none follows
    qualifiedType: ($) => prec.right(PREC.greedy, seq(field('qualifier', 'mut'), optional(field('type', $._unrefinedType)))),

    // ptr or arr, the optional size or allocator in brackets, and the element type: arr[16] T, arr[Chunk] T, ptr T
    handleType: ($) => seq(field('handle', choice('ptr', 'arr')), choice(
      seq('[', field('argument', $._genericArgument), ']', field('type', $._unrefinedType)),
      field('type', $._unbracketedType),
    )),

    // a type that doesn't start with [, which is what can follow ptr or arr right away, since a [ there always holds the handle's size or allocator
    _unbracketedType: ($) => choice(
      alias($._bareNamedType, $.namedType),
      alias($._pathNamedType, $.namedType),
      alias($._builtinNamedType, $.namedType),
      $.modeType,
      $.qualifiedType,
      $.handleType,
    ),

    // [int, f64]
    tupleType: ($) => seq('[', commaSeparatedTrailing($._type), ']'),

    // [x :: f64, y :: f64]
    recordType: ($) => seq('[', $.recordField, repeat(seq(',', $.recordField)), ']'),

    // name :: type
    recordField: ($) => seq(field('name', $.identifier), '::', field('type', $._type)),

    // [A, B] after a named type
    typeArguments: ($) => seq('[', commaSeparated($._genericArgument), ']'),

    // a generic argument is a type or a comptime value, and cheri reads it as a type when it starts like one
    _genericArgument: ($) => choice($._argumentType, $._argumentExpression),

    // a type where a generic argument goes, where a lone name is followed by , or ] and so can't be refined
    _argumentType: ($) => choice($._unrefinedType, alias($._argumentRefinedType, $.refinedType)),

    // a refined type whose base isn't a lone name
    _argumentRefinedType: ($) => seq(field('type', choice(
      alias($._pathNamedType, $.namedType),
      alias($._builtinNamedType, $.namedType),
      $.modeType,
      $.qualifiedType,
      $.handleType,
      $.tupleType,
      $.recordType,
    )), $._refinement),
  },
});
