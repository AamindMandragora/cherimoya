# Error Codes

Every rule cherimoya checks has a code, and `cheri guide <code>` explains it. A code is a letter for where it's reported and a number. Codes are given in order and never reused.

| Letter | Reported by |
|---|---|
| `L` | the lexer |
| `P` | the parser |
| `S` | sema |
| `W` | a warning, from any stage |
| `N` | a feature that isn't built yet |

| Code | Rule |
|---|---|
| [L001](L001.md) | A number's digits have to be digits of its base |
| [L002](L002.md) | A string or char literal has to be closed |
| [L003](L003.md) | An escape has to be one of the escapes a literal holds |
| [L004](L004.md) | A \u{...} escape has to name a Unicode scalar value |
| [L005](L005.md) | A literal's characters have to be UTF-8 |
| [L006](L006.md) | A char literal holds exactly one character |
| [L007](L007.md) | Every character outside a literal has to start a token |
| [L008](L008.md) | Touching operators need whitespace when one is a flow token, a pipe, a compound, or an extra operator |
| [P001](P001.md) | An argument list is written 'f(a, b)' |
| [P002](P002.md) | An empty pack is written '[]', not '()' |
| [P003](P003.md) | A bare operator in parentheses is only a fold's reducer |
| [P004](P004.md) | A tuple is a pack in brackets, not a list in parentheses |
| [P005](P005.md) | An annotation is written '(value :: Type)' |
| [P006](P006.md) | A parenthesized expression is closed with ')' |
| [P007](P007.md) | A fold is written '(reducer, seed)[xs]' |
| [P008](P008.md) | An exclusivity marker on a negative bound is written '< -1', not '<-1' |
| [P009](P009.md) | A map is a pack of pairs, not 'key -> value' |
| [P010](P010.md) | A comprehension's clauses are joined with '&&' |
| [P011](P011.md) | A comprehension's head is a plain expression |
| [P012](P012.md) | A comprehension is written '[head : clauses]' |
| [P013](P013.md) | A pack is written '[a, b]' |
| [P014](P014.md) | A range has one '...', at most two values before it and one after, and a bound |
| [P015](P015.md) | A range's elements are unnamed |
| [P016](P016.md) | An exclusivity marker goes only on a range's endpoints |
| [P017](P017.md) | Generic arguments are written 'Name[A, B]' |
| [P018](P018.md) | A refinement is written 'T {predicate}' |
| [P019](P019.md) | A parameter list is written '(name :: Type, name :: Type)' |
| [P020](P020.md) | A closure is written '(params) -> (outputs) { body }' |
| [P021](P021.md) | A pattern is a literal, a name, _, a variant, a pack, a range, or a set-builder |
| [P022](P022.md) | A statement ends with ';' |
| [P023](P023.md) | A binding has an initializer |
| [P024](P024.md) | A wit statement is written 'wit {predicate};' |
| [P025](P025.md) | An assert is written 'assert (condition);' |
| [P026](P026.md) | A match's equality is '=' or '==' |
| [P027](P027.md) | A match is written 'match (subject) { arms }' |
| [P028](P028.md) | A match arm is written 'pattern -> { body }' |
| [P029](P029.md) | A subjectless match's arms are '(condition)' or '_' |
| [P030](P030.md) | A loop is written 'repeat (binder) <- source wit {invariant} { body }' |
| [P031](P031.md) | Generic parameters are written '[T :: Bound <- Fallback]' |
| [P032](P032.md) | A function is written 'let (inputs) -> name -> (outputs) { body }' |
| [P033](P033.md) | A sum's variant is a name with an optional payload |
| [P034](P034.md) | A type definition is written 'type Name <- definition;' |
| [P035](P035.md) | A protocol is written 'type Name <- [let ..., let ...];' |
| [P036](P036.md) | An export or import list is written '[Name, Name[field]]' |
| [P037](P037.md) | An import names its module as 'import [items] <- module.path;' |
| [P038](P038.md) | An attribute is written '@name(args)' |
| [P039](P039.md) | A handle is written 'arr[T]', 'arr[size, T]', or 'ptr[T]', with a type for what it holds |
| [P040](P040.md) | An address type takes only the type it names, 'addr[T]' |
| [P041](P041.md) | A record type is written '[name :: Type, name :: Type]' |
| [P042](P042.md) | A tuple type is written '[A, B]' |
| [P043](P043.md) | A type goes where a type is expected |
| [P044](P044.md) | A block is written '{ statements }' |
| [P045](P045.md) | The left side of an assignment is a place |
| [P046](P046.md) | Conditional flow is a match, since there is no 'if' |
| [P047](P047.md) | A piped method is written 'x |> .method' |
| [P048](P048.md) | An expression goes where an expression is expected |
| [P049](P049.md) | A closure's captures are unnamed |
| [P050](P050.md) | Index arguments are unnamed |
| [P051](P051.md) | A field access is written 'value.field' |
| [P052](P052.md) | Construction builds from a pack, 'T <- [...]' |
| [P053](P053.md) | Operators on one level with different or no associativity need parentheses to mix |
| [P054](P054.md) | '|>' and '<|' need parentheses to mix |
| [S001](S001.md) | An index has to be unsigned |
| [S002](S002.md) | A top-level binding can't be moved |
| [S003](S003.md) | Only an owner can move what it holds |
| [S004](S004.md) | An array's size has to be known when the program compiles |
| [S005](S005.md) | Only an array or a pointer can be unscoped |
| [S006](S006.md) | An address names what a pointer points at |
| [S007](S007.md) | A condition has to be a bool |
| [S008](S008.md) | Every input and output has a type |
| [S009](S009.md) | Tests go in a .toya file |
| [S010](S010.md) | `@test` goes on a function |
| [S011](S011.md) | A `@test` takes `traps` and `timeout`, named, each once |
| [S012](S012.md) | `traps` is a string and `timeout` a whole number of seconds above 0 |
| [S013](S013.md) | Only inputs have defaults |
| [S014](S014.md) | Every path through a scope that yields a value has to yield one |
| [S015](S015.md) | A view is of a place |
| [S016](S016.md) | An uninitialized binding has a type |
| [S017](S017.md) | An uninitialized binding owns nothing |
| [S018](S018.md) | An uninitialized binding is mutable |
| [S019](S019.md) | A binding holds a value |
| [S020](S020.md) | A view or an address is only made in a function |
| [S021](S021.md) | An address is of a place |
| [S022](S022.md) | An address is never mut |
| [S023](S023.md) | A view writes only what its place can write |
| [S024](S024.md) | A top-level binding can't be unscoped |
| [S025](S025.md) | A value has the type its place expects |
| [S026](S026.md) | A function that yields a view yields a view of one of its inputs |
| [S027](S027.md) | A function that yields an address yields one of its inputs' |
| [S028](S028.md) | A yield is inside a function or scope |
| [S029](S029.md) | A yield's target is a function or scope around it |
| [S030](S030.md) | A function's yields have its output's type |
| [S031](S031.md) | Every yield to one scope has the same type |
| [S032](S032.md) | A pattern has the subject's type |
| [S033](S033.md) | A match covers every value when a missing case would matter |
| [S034](S034.md) | Only an owner can replace what it holds |
| [S035](S035.md) | A value is used or discarded |
| [S036](S036.md) | A name is declared before it's used |
| [S037](S037.md) | A nested function can't use the locals of the function around it |
| [S038](S038.md) | Only mutable elements can be assigned |
| [S039](S039.md) | Only a binding can be assigned |
| [S040](S040.md) | Only a binding declared mut can be assigned |
| [S041](S041.md) | A boolf literal has to be from 0 to 1 |
| [S042](S042.md) | An integer literal has to be exact in the float it stands for |
| [S043](S043.md) | An integer literal has to fit in its type |
| [S044](S044.md) | A float literal has to fit in its type |
| [S045](S045.md) | A char literal has to be ASCII |
| [S046](S046.md) | A string of chars has to be ASCII |
| [S047](S047.md) | An operator only takes the types it works on |
| [S048](S048.md) | Only a signed or wrapping integer can be negated |
| [S049](S049.md) | Both sides of an operator have the same type |
| [S050](S050.md) | A conversion's value is unnamed |
| [S051](S051.md) | A conversion takes one value |
| [S052](S052.md) | A boolf becomes a bool only by a cutoff |
| [S053](S053.md) | A conversion is between kinds of values that convert |
| [S054](S054.md) | An array's length is an integer |
| [S055](S055.md) | A run-time array length is unsigned |
| [S056](S056.md) | Making an array or a handle takes the values its kind takes |
| [S057](S057.md) | An array made without a fill needs elements with a default |
| [S058](S058.md) | An uninitialized array's elements own nothing |
| [S059](S059.md) | An uninitialized array's elements are mutable |
| [S060](S060.md) | An array's length goes in brackets |
| [S061](S061.md) | An array on the heap is made from a fixed array or a string |
| [S062](S062.md) | A call passes as many arguments as its function takes |
| [S063](S063.md) | A named argument names an input |
| [S064](S064.md) | Unnamed arguments come before named ones |
| [S065](S065.md) | An input gets one argument |
| [S066](S066.md) | Every input without a default gets an argument |
| [S067](S067.md) | A method without its object is piped into |
| [S068](S068.md) | A method is called |
| [S069](S069.md) | A method is one its object's type has |
| [S070](S070.md) | An empty array's type is known |
| [S071](S071.md) | Only a pointer is dereferenced |
| [S072](S072.md) | Only an array is indexed |
| [S073](S073.md) | An array takes one index |
| [S074](S074.md) | An index is an integer |
| [S075](S075.md) | An index known when the program compiles is in range |
| [S076](S076.md) | Only a function is called |
| [S077](S077.md) | What an input can write is writable where it comes from |
| [S078](S078.md) | '_' is a place or an initializer |
| [S079](S079.md) | A program has a start function |
| [S080](S080.md) | start is declared the way the runtime calls it |
| [S081](S081.md) | A .toya file has no start |
| [S082](S082.md) | Names in one scope are distinct |
| [S083](S083.md) | A moved binding holds nothing until it's assigned again |
| [S084](S084.md) | An unscoped owner is moved before its scope ends |
| [S085](S085.md) | A handle moved in a loop is assigned again before the iteration ends |
| [S086](S086.md) | Nothing a live view points into is freed or moved |
| [W001](W001.md) | A statement after one that always yields never runs |
| [W002](W002.md) | A string pattern of another length than a fixed subject never matches |
| [W003](W003.md) | An arm after ones that match every value never runs |
| [W004](W004.md) | A .toya file with no test runs nothing |
| [N001](N001.md) | Types that aren't built in aren't supported yet |
| [N002](N002.md) | Allocators aren't supported yet |
| [N003](N003.md) | wit values aren't supported yet |
| [N004](N004.md) | A mode anywhere but on an input, a binding, or an output isn't supported yet |
| [N005](N005.md) | Tuple types aren't supported yet |
| [N006](N006.md) | Record types aren't supported yet |
| [N007](N007.md) | More than one output isn't supported yet |
| [N008](N008.md) | Operator overloading isn't supported yet |
| [N009](N009.md) | Generic functions aren't supported yet |
| [N010](N010.md) | Destructuring isn't supported yet |
| [N011](N011.md) | A value out of a loop with a header isn't supported yet |
| [N012](N012.md) | Variant patterns aren't supported yet |
| [N013](N013.md) | Pack patterns aren't supported yet |
| [N014](N014.md) | Range patterns aren't supported yet |
| [N015](N015.md) | Set-builder patterns aren't supported yet |
| [N016](N016.md) | For-each loops aren't supported yet |
| [N017](N017.md) | Extra operators aren't supported yet |
| [N018](N018.md) | Protocol assertions aren't supported yet |
| [N019](N019.md) | Type definitions aren't supported yet |
| [N020](N020.md) | Modules aren't supported yet |
| [N021](N021.md) | Functions as values aren't supported yet |
| [N022](N022.md) | Fields aren't supported yet |
| [N023](N023.md) | Named packs aren't supported yet |
| [N024](N024.md) | Slicing isn't supported yet |
| [N025](N025.md) | Calling anything but a function's name isn't supported yet |
| [N026](N026.md) | old isn't supported yet |
| [N027](N027.md) | Construction isn't supported yet |
| [N028](N028.md) | Ranges aren't supported yet |
| [N029](N029.md) | Comprehensions aren't supported yet |
| [N030](N030.md) | Folds aren't supported yet |
| [N031](N031.md) | Types as values aren't supported yet |
| [N032](N032.md) | Closures aren't supported yet |
| [N033](N033.md) | Moving out of an element or a pointer isn't built yet |
| [N034](N034.md) | A test with inputs or outputs isn't built yet |
| [N035](N035.md) | Packs whose elements have different types aren't supported yet |
