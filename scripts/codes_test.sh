#!/usr/bin/env bash
set -uo pipefail

# checks that every code in src/diagnostics/codes has an explanation in docs/errors, every explanation has a code, and no code is given twice
CODES="$(grep -ohE 'Code [a-zA-Z0-9]+\{"[A-Z][0-9]+"' src/diagnostics/codes/*.hpp | grep -oE '[A-Z][0-9]+' | sort)"
DOCS="$(ls docs/errors | grep -vx README.md | sed 's/\.md$//' | sort)"
STATUS=0

# a code given twice would mean two rules share one explanation
REPEATED="$(uniq -d <<< "$CODES")"
if [ -n "$REPEATED" ]; then
    echo "codes given more than once: $REPEATED"
    STATUS=1
fi

# each side has to hold exactly what the other does
MISSING="$(comm -23 <(uniq <<< "$CODES") <(echo "$DOCS"))"
EXTRA="$(comm -13 <(uniq <<< "$CODES") <(echo "$DOCS"))"
if [ -n "$MISSING" ]; then
    echo "codes with no explanation in docs/errors: $MISSING"
    STATUS=1
fi
if [ -n "$EXTRA" ]; then
    echo "explanations in docs/errors with no code: $EXTRA"
    STATUS=1
fi

# the list of codes has to name every one
for code in $DOCS; do
    if ! grep -q "\[$code\]($code.md)" docs/errors/README.md; then
        echo "docs/errors/README.md doesn't list $code"
        STATUS=1
    fi
done
exit $STATUS
