#pragma once

#include "diagnostics.hpp"

// every rule's code, one named constant per rule, so a report names the rule it breaks instead of writing its code
#include "codes/lexer.hpp"
#include "codes/parser.hpp"
#include "codes/sema.hpp"
