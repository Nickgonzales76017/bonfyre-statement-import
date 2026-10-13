#!/usr/bin/env bash
set -euo pipefail

test_root=$(mktemp -d)
trap 'rm -rf "$test_root"' EXIT

./bonfyre-statement-import --pack packs/demo-bank.pack --input "$PWD/fixtures/demo-statement.txt" --csv "$test_root/transactions.csv" --receipt "$test_root/receipt.json"
diff -u tests/want-transactions.csv "$test_root/transactions.csv"
python3 -m json.tool "$test_root/receipt.json" >/dev/null
grep -q '"transaction_count": 3' "$test_root/receipt.json"
grep -q '"closing_total": true' "$test_root/receipt.json"
grep -q '"source": "demo-statement.txt"' "$test_root/receipt.json"
if grep -q "$PWD" "$test_root/receipt.json"; then
  echo "receipt leaked the source directory" >&2
  exit 1
fi

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

# Capacity boundaries: the same parser and 10,000-record hard limit, but
# storage grows only for transactions that actually reach the parser.
# These are synthetic bank statements, not user or customer financial data.
python3 - "$test_root" <<'PY'
from pathlib import Path
import sys
root = Path(sys.argv[1])
for n in (1, 16, 17, 255, 10000, 10001):
    def money(cents):
        return f"{cents // 100}.{cents % 100:02d}"
    lines = ["DEMO BANK STATEMENT", "Account: synthetic-001",
             "Opening balance: $1,000.00"]
    for i in range(1, n + 1):
        lines.append(f"2026-07-02 ITEM-{i:05d} 0.01 {money(100000 + i)}")
    lines.append(f"Closing balance: {money(100000 + n)}")
    (root / f"growth-{n}.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
PY
for n in 1 16 17 255 10000; do
  ./bonfyre-statement-import --pack packs/demo-bank.pack \
    --input "$test_root/growth-$n.txt" \
    --csv "$test_root/growth-$n.csv" \
    --receipt "$test_root/growth-$n.json" >/dev/null
  test "$(wc -l < "$test_root/growth-$n.csv" | tr -d ' ')" -eq "$((n+1))"
  python3 - "$test_root/growth-$n.json" "$n" <<'PY'
import json,sys
with open(sys.argv[1],encoding="utf-8") as f: r=json.load(f)
assert r["valid"] is True and r["transaction_count"] == int(sys.argv[2])
assert r["invariants"] == {"running_balances":True,"closing_total":True}
PY
done
if ./bonfyre-statement-import --pack packs/demo-bank.pack \
    --input "$test_root/growth-10001.txt" \
    --csv "$test_root/growth-overflow.csv" \
    --receipt "$test_root/growth-overflow.json" 2>"$test_root/growth-overflow.err"; then
  echo "10,001-record statement unexpectedly passed" >&2
  exit 1
fi
grep -q 'transaction limit exceeded' "$test_root/growth-overflow.err"
test ! -e "$test_root/growth-overflow.csv"

echo "statement import tests passed"
