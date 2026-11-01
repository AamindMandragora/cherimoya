#pragma once

// the gdb pretty-printers a debug build embeds, in Python, for the values gdb would print wrong, found by each type's name: the floats DWARF can't describe, FP8, bfloat16, and f128, which the debug info holds as their raw bits, the 1-byte integers, which gdb would print as C chars, and runes, which it would print as hex escapes
inline const char* gdbPrinters = R"py(
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

# finds the printer for a value, by its type's name without const, or none for every type gdb already prints right
def lookup(value):
    name = value.type.strip_typedefs().unqualified().name
    if name in FORMATS:
        return FloatPrinter(value, FORMATS[name])
    if name in BYTES:
        return BytePrinter(value)
    if name == "rune":
        return RunePrinter(value)
    return None

objfile = gdb.current_objfile()
(objfile.pretty_printers if objfile else gdb.pretty_printers).append(lookup)
)py";
