# the gdb pretty-printers a debug build embeds, for the values gdb would print wrong, found by each type's name: the floats DWARF can't
# describe, FP8, bfloat16, and f128, which the debug info holds as their raw bits, the 1-byte integers, which gdb would print as C chars,
# alone or in an array, which it would print as a C string, runes, which it would print as hex escapes, and arrays on the heap, which it
# would print as an address and a length, where CMake puts this file's text in printers.hpp, so it can't hold the raw string's closing
# delimiter, a ) then py then a quote
import gdb
from decimal import Decimal, localcontext
from fractions import Fraction

# each encoding by the names its types are written with: its exponent and significand bits, and whether it's IEEE's kind, with infinities and a NaN for every all-ones exponent, where E4M3 has no infinity and only all ones is a NaN
FORMATS = {}
for names, layout in (
    (("f8", "f8e4m3n"), (4, 3, False)),
    (("f8e5m2", "f8e5m2n"), (5, 2, True)),
    (("f16e8m7", "f16e8m7n"), (8, 7, True)),
    (("f128", "f128n"), (15, 112, True)),
):
    for name in names:
        FORMATS[name] = layout

# the value a float's bits stand for, written the way gdb writes a float, where f128 keeps its 34 significant digits and the narrower ones fit a double exactly
def decode(bits, exponent, significand, isIeee):
    isNegative = bits >> (exponent + significand) & 1
    raw = (bits >> significand) & ((1 << exponent) - 1)
    fraction = bits & ((1 << significand) - 1)
    allOnes = (1 << exponent) - 1
    if isIeee and raw == allOnes:
        return "nan(0x%x)" % fraction if fraction else ("-inf" if isNegative else "inf")
    if not isIeee and raw == allOnes and fraction == (1 << significand) - 1:
        return "nan(0x%x)" % fraction
    if raw == 0 and fraction == 0:
        return "-0" if isNegative else "0"
    bias = (1 << (exponent - 1)) - 1
    scale = Fraction(fraction, 1 << significand)
    magnitude = (scale if raw == 0 else 1 + scale) * Fraction(2) ** ((1 if raw == 0 else raw) - bias)
    value = -magnitude if isNegative else magnitude
    if exponent + significand < 64:
        return repr(float(value))
    with localcontext() as context:
        context.prec = 34
        return str((Decimal(value.numerator) / Decimal(value.denominator)).normalize())

class FloatPrinter:
    def __init__(self, value, layout):
        self.value = value
        self.layout = layout

    def to_string(self):
        bits = int.from_bytes(bytes(self.value.bytes), "little")
        return decode(bits, *self.layout)

# a 1-byte integer as the number it is, like every wider integer
class BytePrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        return str(int(self.value))

# a rune as its code point and the character, the way gdb prints a char, written with cherimoya's escapes when it doesn't print or would end the quotes
class RunePrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        point = int(self.value)
        character = chr(point) if point < 0xD800 or 0xDFFF < point <= 0x10FFFF else ""
        if character in ("'", "\\"):
            text = "\\" + character
        elif character and character.isprintable():
            text = character
        else:
            text = "\\u{%X}" % point
        return "%d '%s'" % (point, text)

BYTES = ("i8", "i8u", "i8w", "i8uw")

# an array of 1-byte integers as its numbers, since gdb would print it as a C string
class ByteArrayPrinter:
    def __init__(self, value):
        self.value = value

    def to_string(self):
        return None

    def display_hint(self):
        return "array"

    def children(self):
        low, high = self.value.type.strip_typedefs().range()
        for i in range(low, high + 1):
            yield "[%d]" % i, int(self.value[i])

# an array on the heap, a structure of its elements' address and how many there are, as its elements, the way a fixed array prints,
# where 1-byte elements print as their numbers too
class HeapArrayPrinter:
    def __init__(self, value, name):
        self.value = value
        self.name = name

    def to_string(self):
        return "%s of %d" % (self.name, int(self.value["len"]))

    def display_hint(self):
        return "array"

    def children(self):
        data = self.value["data"]
        isByte = data.type.target().strip_typedefs().unqualified().name in BYTES
        for i in range(int(self.value["len"])):
            element = (data + i).dereference()
            yield "[%d]" % i, int(element) if isByte else element

# finds the printer for a value, by its type's name without const, or none for every type gdb already prints right
def lookup(value):
    name = value.type.strip_typedefs().unqualified().name
    if name in FORMATS:
        return FloatPrinter(value, FORMATS[name])
    if name in BYTES:
        return BytePrinter(value)
    if name == "rune":
        return RunePrinter(value)
    # a structure is named by its tag, the way C names it
    stripped = value.type.strip_typedefs().unqualified()
    if stripped.code == gdb.TYPE_CODE_STRUCT and (stripped.tag or "").startswith("arr "):
        return HeapArrayPrinter(value, stripped.tag)
    if stripped.code == gdb.TYPE_CODE_ARRAY and stripped.target().strip_typedefs().unqualified().name in BYTES:
        return ByteArrayPrinter(value)
    return None

objfile = gdb.current_objfile()
(objfile.pretty_printers if objfile else gdb.pretty_printers).append(lookup)
