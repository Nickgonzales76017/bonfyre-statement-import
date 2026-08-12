#!/usr/bin/env bash
set -euo pipefail

test_root=$(mktemp -d)
trap 'rm -rf "$test_root"' EXIT

./bonfyre-statement-import --pack packs/demo-bank.pack --input fixtures/demo-statement.txt --csv "$test_root/transactions.csv" --receipt "$test_root/receipt.json"
diff -u tests/want-transactions.csv "$test_root/transactions.csv"
python3 -m json.tool "$test_root/receipt.json" >/dev/null
grep -q '"transaction_count": 3' "$test_root/receipt.json"
grep -q '"closing_total": true' "$test_root/receipt.json"

if ./bonfyre-statement-import --pack packs/demo-bank.pack --input fixtures/bad-balance.txt --csv "$test_root/bad.csv" --receipt "$test_root/bad.json" 2>"$test_root/bad.err"; then
  echo "bad balance fixture unexpectedly passed" >&2
  exit 1
fi
grep -q 'running balance mismatch' "$test_root/bad.err"
test ! -e "$test_root/bad.csv"

./bonfyre-statement-import --pack packs/demo-bank.pack --input fixtures/csv-escaping.txt --csv "$test_root/escaped.csv" --receipt "$test_root/escaped.json"
grep -q '"COFFEE, ""SPECIAL"""' "$test_root/escaped.csv"

if ./bonfyre-statement-import --pack packs/demo-bank.pack --input fixtures/wrong-bank.txt --csv "$test_root/wrong.csv" --receipt "$test_root/wrong.json" 2>"$test_root/wrong.err"; then
  echo "wrong bank fixture unexpectedly passed" >&2
  exit 1
fi
grep -q 'missing identifier' "$test_root/wrong.err"
test ! -e "$test_root/wrong.csv"
echo "statement import tests passed"
