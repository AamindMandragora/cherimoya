#pragma once

#include "diagnostics.hpp"

// the codes of the rules the parser reports, each with the section of the reference that explains it
namespace codes {
// an argument list is written 'f(a, b)'
inline constexpr Code argumentList{"P001", "2.2"};
// an empty pack is written '[]', not '()'
inline constexpr Code emptyPack{"P002", "2.10"};
// a bare operator in parentheses is only a fold's reducer
inline constexpr Code bareOperator{"P003", "2.8"};
// a tuple is a pack in brackets, not a list in parentheses
inline constexpr Code tupleParens{"P004", "2.10"};
// an annotation is written '(value :: Type)'
inline constexpr Code annotationShape{"P005", "2.4"};
// a parenthesized expression is closed with ')'
inline constexpr Code groupingShape{"P006", "2.1"};
// a fold is written '(reducer, seed)[xs]'
inline constexpr Code foldShape{"P007", "2.8"};
// an exclusivity marker on a negative bound is written '< -1', not '<-1'
inline constexpr Code negativeMarker{"P008", "2.6"};
// a map is a pack of pairs, not 'key -> value'
inline constexpr Code mapArrow{"P009", "2.11"};
// a comprehension's clauses are joined with '&&'
inline constexpr Code clauseJoin{"P010", "2.7"};
// a comprehension's head is a plain expression
inline constexpr Code comprehensionHead{"P011", "2.7"};
// a comprehension is written '[head : clauses]'
inline constexpr Code comprehensionShape{"P012", "2.7"};
// a pack is written '[a, b]'
inline constexpr Code packShape{"P013", "2.10"};
// a range has exactly one '...', at most two values before it and one after, and at least one bound
inline constexpr Code rangeShape{"P014", "2.6"};
// a range's elements are unnamed
inline constexpr Code rangeUnnamed{"P015", "2.6"};
// an exclusivity marker goes only on a range's endpoints
inline constexpr Code markerPlacement{"P016", "2.6"};
// generic arguments are written 'Name[A, B]'
inline constexpr Code genericArguments{"P017", "5.4"};
// a refinement is written 'T {predicate}'
inline constexpr Code refinementShape{"P018", "5.2"};
// a parameter list is written '(name :: Type, name :: Type)'
inline constexpr Code parameterList{"P019", "6.1"};
// a closure is written '(params) -> (outputs) { body }'
inline constexpr Code closureShape{"P020", "2.12"};
// a pattern is a literal, a name, _, a variant, a pack, a range, or a set-builder
inline constexpr Code patternShape{"P021", "3.6"};
// a statement ends with ';'
inline constexpr Code statementEnd{"P022", "3.1"};
// a binding has an initializer
inline constexpr Code bindingInitializer{"P023", "3.2"};
// a wit statement is written 'wit {predicate};'
inline constexpr Code witShape{"P024", "3.10"};
// an assert is written 'assert (condition);'
inline constexpr Code assertShape{"P025", "3.10"};
// a match's equality is '=' or '=='
inline constexpr Code matchEquality{"P026", "3.6"};
// a match is written 'match (subject) { arms }'
inline constexpr Code matchShape{"P027", "3.6"};
// a match arm is written 'pattern -> { body }'
inline constexpr Code matchArm{"P028", "3.6"};
// a subjectless match's arms are '(condition)' or '_'
inline constexpr Code subjectlessArm{"P029", "3.6"};
// a loop is written 'repeat (binder) <- source wit {invariant} { body }'
inline constexpr Code repeatShape{"P030", "3.8"};
// generic parameters are written '[T :: Bound <- Fallback]'
inline constexpr Code genericParameters{"P031", "5.4"};
// a function is written 'let (inputs) -> name -> (outputs) { body }'
inline constexpr Code signatureShape{"P032", "6.1"};
// a sum's variant is a name with an optional payload
inline constexpr Code sumVariant{"P033", "5.1"};
// a type definition is written 'type Name <- definition;'
inline constexpr Code typeDefinition{"P034", "5.1"};
// a protocol is written 'type Name <- [let ..., let ...];'
inline constexpr Code protocolShape{"P035", "5.3"};
// an export or import list is written '[Name, Name[field]]'
inline constexpr Code moduleItems{"P036", "8"};
// an import names its module as 'import [items] <- module.path;'
inline constexpr Code importPath{"P037", "8"};
// an attribute is written '@name(args)'
inline constexpr Code attributeShape{"P038", "9"};
// a handle is written 'arr[T]', 'arr[size, T]', or 'ptr[T]', with a type for what it holds
inline constexpr Code handleShape{"P039", "4.3"};
// an address type takes only the type it names, 'addr[T]'
inline constexpr Code addressShape{"P040", "2.9"};
// a record type is written '[name :: Type, name :: Type]'
inline constexpr Code recordType{"P041", "2.10"};
// a tuple type is written '[A, B]'
inline constexpr Code tupleType{"P042", "2.10"};
// a type goes where a type is expected
inline constexpr Code typeExpected{"P043", "5.1"};
// a block is written '{ statements }'
inline constexpr Code blockShape{"P044", "3.1"};
// the left side of an assignment is a place
inline constexpr Code assignmentPlace{"P045", "3.3"};
// conditional flow is a match, since there is no 'if'
inline constexpr Code noIf{"P046", "3.5"};
// a piped method is written 'x |> .method'
inline constexpr Code pipedMethod{"P047", "2.3"};
// an expression goes where an expression is expected
inline constexpr Code expressionExpected{"P048", "2.1"};
// a closure's captures are unnamed
inline constexpr Code captureUnnamed{"P049", "2.12"};
// index arguments are unnamed
inline constexpr Code indexUnnamed{"P050", "2.9"};
// a field access is written 'value.field'
inline constexpr Code fieldAccess{"P051", "2.10"};
// construction builds from a pack, 'T <- [...]'
inline constexpr Code constructionPack{"P052", "2.4"};
// operators on one level with different or no associativity need parentheses to mix
inline constexpr Code operatorMixing{"P053", "2.1"};
// '|>' and '<|' need parentheses to mix
inline constexpr Code pipeMixing{"P054", "2.3"};
}
