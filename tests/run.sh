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


# Compact description arena is opt-in; the verified CSV and receipt must be
# byte-for-byte identical to the existing path, even at the original 10,000
# record ceiling. Neither path may publish output after a failed invariant.
for n in 1 16 17 255 10000; do
  ./bonfyre-statement-import --compact --pack packs/demo-bank.pack \
    --input "$test_root/growth-$n.txt" \
    --csv "$test_root/growth-$n.compact.csv" \
    --receipt "$test_root/growth-$n.compact.json" >/dev/null
  cmp "$test_root/growth-$n.csv" "$test_root/growth-$n.compact.csv"
  cmp "$test_root/growth-$n.json" "$test_root/growth-$n.compact.json"
done
for fixture in demo-statement csv-escaping; do
  ./bonfyre-statement-import --pack packs/demo-bank.pack \
    --input "fixtures/$fixture.txt" --csv "$test_root/$fixture.baseline.csv" \
    --receipt "$test_root/$fixture.baseline.json" >/dev/null
  ./bonfyre-statement-import --compact --pack packs/demo-bank.pack \
    --input "fixtures/$fixture.txt" --csv "$test_root/$fixture.compact.csv" \
    --receipt "$test_root/$fixture.compact.json" >/dev/null
  cmp "$test_root/$fixture.baseline.csv" "$test_root/$fixture.compact.csv"
  cmp "$test_root/$fixture.baseline.json" "$test_root/$fixture.compact.json"
done
for fixture in bad-balance wrong-bank; do
  if ./bonfyre-statement-import --compact --pack packs/demo-bank.pack \
      --input "fixtures/$fixture.txt" --csv "$test_root/$fixture.failure.csv" \
      --receipt "$test_root/$fixture.failure.json" >"$test_root/$fixture.stdout" 2>"$test_root/$fixture.stderr"; then
    echo "compact path accepted invalid $fixture" >&2; exit 1
  fi
  test ! -e "$test_root/$fixture.failure.csv"
  test ! -e "$test_root/$fixture.failure.json"
done
if ./bonfyre-statement-import --compact --pack packs/demo-bank.pack \
    --input "$test_root/growth-10001.txt" \
    --csv "$test_root/compact-overflow.csv" --receipt "$test_root/compact-overflow.json" \
    2>"$test_root/compact-overflow.err"; then
  echo "compact path exceeded transaction bound" >&2; exit 1
fi
grep -q 'transaction limit exceeded' "$test_root/compact-overflow.err"
test ! -e "$test_root/compact-overflow.csv"
test ! -e "$test_root/compact-overflow.json"

# Long fields and quote/comma escaping hit the arena's variable-width path.
python3 - "$test_root" <<'PY'
from pathlib import Path
import sys
root=Path(sys.argv[1])
lines=['DEMO BANK STATEMENT','Opening balance: 1000.00']
for i in range(1,300):
    length=(i*37)%490
    description=('X'*length)+' COFFEE, "SPECIAL"'
    balance=100000+i
    lines.append(f'2026-07-02 {description} 0.01 {balance//100}.{balance%100:02d}')
lines.append('Closing balance: 1002.99')
(root/'variable-description.txt').write_text('\n'.join(lines)+'\n',encoding='utf-8')
PY
./bonfyre-statement-import --pack packs/demo-bank.pack \
  --input "$test_root/variable-description.txt" \
  --csv "$test_root/variable.baseline.csv" --receipt "$test_root/variable.baseline.json" >/dev/null
./bonfyre-statement-import --compact --pack packs/demo-bank.pack \
  --input "$test_root/variable-description.txt" \
  --csv "$test_root/variable.compact.csv" --receipt "$test_root/variable.compact.json" >/dev/null
cmp "$test_root/variable.baseline.csv" "$test_root/variable.compact.csv"
cmp "$test_root/variable.baseline.json" "$test_root/variable.compact.json"

echo "compact arena parity and failure boundaries passed"

echo "statement import tests passed"
