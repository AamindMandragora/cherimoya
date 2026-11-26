#pragma once

#include "diagnostics.hpp"

// the codes of the rules sema reports, each with the section of the reference that explains it
namespace codes {
// an index has to be an unsigned integer
inline constexpr Code indexUnsigned{"S001", "2.9"};
// a top-level binding can't be moved, since every function can use it
inline constexpr Code moveGlobal{"S002", "4.5"};
// only a binding that owns what it holds can move it
inline constexpr Code moveBorrowed{"S003", "4.5"};
// an array's size has to be known when the program compiles
inline constexpr Code arraySizeKnown{"S004", "4.3"};
// only an array or a pointer can be unscoped
inline constexpr Code unscopedOwns{"S005", "4.1"};
// an address names what a pointer points at, never the pointer
inline constexpr Code addressOfPointer{"S006", "2.9"};
// a condition has to be a bool
inline constexpr Code conditionBool{"S007", "3.6"};
// every input and output has a type
inline constexpr Code signatureTyped{"S008", "6.1"};
// tests go in a .toya file
inline constexpr Code testInToya{"S009", "12"};
// @test goes on a function
inline constexpr Code testOnFunction{"S010", "12"};
// a @test's arguments are traps and timeout, named, each at most once
inline constexpr Code testArgument{"S011", "12"};
// traps is a string and timeout a whole number of seconds above 0
inline constexpr Code testArgumentValue{"S012", "12"};
// only inputs have defaults
inline constexpr Code outputDefault{"S013", "6.1"};
// every path through a scope that yields a value has to yield one
inline constexpr Code everyPathYields{"S014", "3.4"};
// a view is of a place
inline constexpr Code viewOfPlace{"S015", "4.7"};
// an uninitialized binding has a type
inline constexpr Code uninitializedBindingTyped{"S016", "4.8"};
// an uninitialized binding owns nothing
inline constexpr Code uninitializedBindingOwns{"S017", "4.8"};
// an uninitialized binding is mutable
inline constexpr Code uninitializedBindingMutable{"S018", "4.8"};
// a binding holds a value, never void
inline constexpr Code bindingVoid{"S019", "3.2"};
// a view or an address is only made in a function
inline constexpr Code topLevelView{"S020", "4.7"};
// an address is of a place
inline constexpr Code addressOfPlace{"S021", "2.9"};
// an address is never mut
inline constexpr Code addressImmutable{"S022", "2.9"};
// a view writes only what the place it views can write
inline constexpr Code viewWritable{"S023", "4.7"};
// a top-level binding can't be unscoped
inline constexpr Code topLevelUnscoped{"S024", "4.1"};
// a value has the type its place expects
inline constexpr Code typeMismatch{"S025", "2.4"};
// a function that yields a view yields a view of one of its inputs
inline constexpr Code yieldViewOfInput{"S026", "4.7"};
// a function that yields an address yields one of its inputs
inline constexpr Code yieldAddressOfInput{"S027", "2.9"};
// a yield is inside a function or scope
inline constexpr Code yieldOutside{"S028", "3.4"};
// a yield's target is a function or scope around it
inline constexpr Code yieldTarget{"S029", "3.4"};
// a function's yields have its output's type
inline constexpr Code yieldType{"S030", "3.4"};
// every yield to one scope has the same type
inline constexpr Code yieldsAgree{"S031", "3.4"};
// a pattern has the subject's type
inline constexpr Code patternType{"S032", "3.6"};
// a match covers every value when a missing case would matter
inline constexpr Code matchCovers{"S033", "3.6"};
// only a binding that owns what it holds can be replaced
inline constexpr Code replaceBorrowed{"S034", "4.6"};
// a value is used or discarded
inline constexpr Code valueDiscarded{"S035", "3.3"};
// a name is declared before it's used
inline constexpr Code undeclaredName{"S036", "6.3"};
// a nested function can't use the locals of the function around it
inline constexpr Code nestedCapture{"S037", "2.12"};
// only mutable elements can be assigned
inline constexpr Code elementsImmutable{"S038", "4.2"};
// a statement after one that always yields never runs
inline constexpr Code unreachableStatement{"W001", "3.4"};
// a string pattern of another length than a fixed subject never matches
inline constexpr Code patternNeverMatches{"W002", "3.6"};
// an arm after ones that match every value never runs
inline constexpr Code unreachableArm{"W003", "3.6"};
// a type that isn't built in, which definitions, the prelude and the standard library will bring
inline constexpr Code namedTypes{"N001", "5.1"};
// an allocator named in a handle's brackets
inline constexpr Code allocators{"N002", "4.9"};
// a wit mode, for a value only the verifier sees
inline constexpr Code witMode{"N003", "3.2"};
// a mode on anything but an input, a binding, or an output
inline constexpr Code modePosition{"N004", "4.1"};
// a tuple type
inline constexpr Code tupleTypes{"N005", "2.10"};
// a record type
inline constexpr Code recordTypes{"N006", "2.10"};
// a function with more than one output
inline constexpr Code multipleOutputs{"N007", "6.1"};
// a function named by an operator, which overloads it
inline constexpr Code operatorOverloading{"N008", "5.3"};
// a function with generic parameters
inline constexpr Code genericFunctions{"N009", "5.4"};
// a binding whose pattern takes a value apart
inline constexpr Code destructuring{"N010", "3.2"};
// a value yielded out of a loop with a header, which will make the loop an Option
inline constexpr Code loopHeaderValue{"N011", "3.8"};
// a variant pattern, which sums bring
inline constexpr Code variantPatterns{"N012", "3.6"};
// a pack pattern, which decision tables use
inline constexpr Code packPatterns{"N013", "3.6"};
// a range pattern
inline constexpr Code rangePatterns{"N014", "3.6"};
// a set-builder pattern
inline constexpr Code setBuilderPatterns{"N015", "3.6"};
// a for-each loop
inline constexpr Code forEachLoops{"N016", "3.8"};
// an extra operator, which programs will define
inline constexpr Code extraOperators{"N017", "2.13"};
// an assertion that a type follows a protocol
inline constexpr Code protocolAssertions{"N018", "5.3"};
// a type definition
inline constexpr Code typeDefinitions{"N019", "5.1"};
// an import or an export, which modules bring
inline constexpr Code modules{"N020", "8"};
// a function used as a value rather than called
inline constexpr Code functionValues{"N021", "2.12"};
// a field, which records bring
inline constexpr Code fields{"N022", "2.10"};
// a named pack, which is a record
inline constexpr Code namedPacks{"N023", "2.10"};
// a slice of an array
inline constexpr Code slicing{"N024", "2.9"};
// a call of anything but a function's name
inline constexpr Code calleeName{"N025", "2.2"};
// old outside the refinements that will give it meaning
inline constexpr Code oldValues{"N026", "5.2"};
// a construction from a pack
inline constexpr Code construction{"N027", "2.4"};
// a range as a value
inline constexpr Code ranges{"N028", "2.6"};
// a comprehension
inline constexpr Code comprehensions{"N029", "2.7"};
// a fold, and the operators that are its reducers
inline constexpr Code folds{"N030", "2.8"};
// a type used as a value
inline constexpr Code typeValues{"N031", "7"};
// a closure
inline constexpr Code closures{"N032", "2.12"};
// moving out of an element or a pointer, which take will do
inline constexpr Code moveOutOfElement{"N033", "4.5"};
// a test with inputs or outputs, which will be fuzzed against its contracts
inline constexpr Code testWithSignature{"N034", "12"};
// only a binding can be assigned, never a function
inline constexpr Code assignFunction{"S039", "3.3"};
// only a binding declared mut can be assigned
inline constexpr Code assignImmutable{"S040", "3.3"};
// a number literal that's a boolf has to be from 0 to 1
inline constexpr Code boolfLiteralRange{"S041", "5.9"};
// an integer literal that stands for a float has to be exact in it
inline constexpr Code floatLiteralExact{"S042", "5.6"};
// an integer literal has to fit in its type
inline constexpr Code intLiteralRange{"S043", "5.5"};
// a float literal has to fit in its type
inline constexpr Code floatLiteralRange{"S044", "5.6"};
// a char literal has to be ASCII
inline constexpr Code charLiteralAscii{"S045", "5.7"};
// a string literal of chars has to be ASCII
inline constexpr Code stringLiteralAscii{"S046", "5.8"};
// an operator only takes operands of the types it works on
inline constexpr Code operandType{"S047", "5.5"};
// only a signed or wrapping integer can be negated
inline constexpr Code negateUnsigned{"S048", "5.5"};
// both sides of an operator have to have the same type
inline constexpr Code operandsSameType{"S049", "5.5"};
// a conversion's value is never named
inline constexpr Code conversionUnnamed{"S050", "2.4"};
// a conversion takes exactly one value
inline constexpr Code conversionCount{"S051", "2.4"};
// a boolf only becomes a bool by a cutoff
inline constexpr Code boolfCutoff{"S052", "5.9"};
// a conversion is only between numbers, characters and integers, or a boolf and a float or a byte
inline constexpr Code conversionKinds{"S053", "2.4"};
// an array's length has to be an integer
inline constexpr Code lengthInteger{"S054", "4.3"};
// an array's length known only at run time has to be unsigned
inline constexpr Code lengthUnsigned{"S055", "4.3"};
// making an array or a handle takes as many values as its kind does
inline constexpr Code makeCount{"S056", "4.3"};
// an array made without a fill needs elements with a default
inline constexpr Code elementsDefault{"S057", "4.3"};
// an uninitialized array's elements have to own nothing
inline constexpr Code uninitializedOwns{"S058", "4.8"};
// an uninitialized array's elements have to be mutable
inline constexpr Code uninitializedMutable{"S059", "4.8"};
// an array's length goes in brackets, not as an argument
inline constexpr Code lengthInBrackets{"S060", "4.3"};
// an array on the heap is made from a fixed array or another kind of string
inline constexpr Code makeSource{"S061", "4.3"};
// a call passes as many arguments as its function takes
inline constexpr Code argumentCount{"S062", "2.2"};
// a named argument has to name an input
inline constexpr Code argumentName{"S063", "2.2"};
// unnamed arguments come before named ones
inline constexpr Code argumentOrder{"S064", "2.2"};
// an input gets one argument
inline constexpr Code argumentTwice{"S065", "2.2"};
// every input without a default gets an argument
inline constexpr Code argumentMissing{"S066", "2.2"};
// a method without its object has to be piped into
inline constexpr Code methodUnpiped{"S067", "2.3"};
// a method has to be called
inline constexpr Code methodUncalled{"S068", "2.2"};
// a method has to be one the object's type has
inline constexpr Code methodUnknown{"S069", "2.2"};
// an empty array's type has to be known
inline constexpr Code packEmptyType{"S070", "2.10"};
// only a pointer is dereferenced
inline constexpr Code derefPointer{"S071", "2.9"};
// only an array is indexed
inline constexpr Code indexArray{"S072", "2.9"};
// an array takes one index
inline constexpr Code indexCount{"S073", "2.9"};
// an index has to be an integer
inline constexpr Code indexInteger{"S074", "2.9"};
// an index known when the program compiles has to be in range
inline constexpr Code indexBounds{"S075", "2.9"};
// only a function is called
inline constexpr Code calleeFunction{"S076", "2.2"};
// what an input can write has to be writable where it comes from
inline constexpr Code passImmutable{"S077", "4.6"};
// '_' is only a place or an initializer
inline constexpr Code wildcardValue{"S078", "3.3"};
// a program has a start function
inline constexpr Code startMissing{"S079", "6.4"};
// start is declared the way the runtime calls it
inline constexpr Code startSignature{"S080", "6.4"};
// a .toya file has no start
inline constexpr Code testsStart{"S081", "12"};
// names in one scope are distinct
inline constexpr Code namesDistinct{"S082", "3.2"};
// a binding that was moved holds nothing until it's assigned again
inline constexpr Code useAfterMove{"S083", "4.5"};
// an unscoped owner has to be moved before its scope ends
inline constexpr Code unscopedUnmoved{"S084", "4.1"};
// a handle moved in a loop has to be assigned again before the iteration ends
inline constexpr Code loopMove{"S085", "3.8"};
// nothing a live view points into can be freed or moved
inline constexpr Code viewedMoved{"S086", "4.7"};
// a .toya file with no test runs nothing
inline constexpr Code testsMissing{"W004", "12"};
// a pack's elements share one type until tuples come
inline constexpr Code packTuple{"N035", "2.10"};
}
