# Bonfyre Statement Import

A small offline C core for turning normalized bank-statement text into verified CSV plus a machine-readable receipt. It uses declarative statement packs for bank-specific recognition and boundary labels, integer cents for monetary arithmetic, and rejects output when running balances or the closing total do not reconcile.

## Build and verify

```bash
make
make test
./bonfyre-statement-import \
  --pack packs/demo-bank.pack \
  --input fixtures/demo-statement.txt \
  --csv transactions.csv \
  --receipt receipt.json
```

The input is normalized text, typically produced locally by a PDF text extractor or OCR tool. This release does not upload statements, invoke network services, store data, guess unmatched layouts, or silently emit transactions after a failed invariant.

## Compact transaction storage (opt-in)

Add `--compact` to the same import command to store row metadata in a small
array and variable-length descriptions in a shared arena. The default storage
path is unchanged. Both paths use the same normalized text parser, integer-cent
balance checks, 10,000-transaction ceiling, CSV format and receipt fields.

The C test suite compares successful CSV and JSON receipts byte-for-byte across
fixture sizes, longer descriptions and escaping, and verifies the same refusal
behavior on invalid balances, unsupported banks and oversized statements.
Compact storage reduces **reserved row-description capacity** for short
descriptions. A repeated Linux GitHub Actions measurement on **one synthetic
10,000-row statement**, alternating 24 executions of each path, recorded:

| Measure | Existing storage | `--compact` storage |
|---|---:|---:|
| Median peak native RSS | 7,016 KiB | 2,280 KiB |
| Median elapsed time | 9.274 ms | 7.049 ms |

Every invocation reproduced the same CSV and JSON receipt bytes. The benchmark
measures direct monotonic wall time and the native child's peak resident memory
using GNU `time`; the run and its raw results are available in
[GitHub Actions](https://github.com/Nickgonzales76017/bonfyre-statement-import/actions/runs/38346957294).
Reproduce on a Linux machine with `BONFYRE_MEASURE_COMPACT=1 make test`.

**Scope:** one CI host and one synthetic cohort—not an end-to-end production
throughput or real-bank performance guarantee. No customer statement or real
bank pack is included.

## Text contract

Transaction rows use `YYYY-MM-DD DESCRIPTION SIGNED_AMOUNT RUNNING_BALANCE`. The included pack declares the bank identifier, opening/closing labels, and currency. Pack differences remain data; the C core owns bounded scanning, ISO date checks, amount normalization, extraction, invariant verification, CSV escaping, and receipt emission.

## Verification receipt

Successful imports record the bank, currency, source filename (never its directory), byte count, non-cryptographic FNV-1a source checksum, transaction count, balances, and passed invariants. FNV-1a is an accidental-change checksum, not a cryptographic signature.

## Commercial use

The core is MIT licensed. Bonfyre offers a fixed-scope **Statement Import Pack** at an initial asking price of **$750 USD** for one bank/layout and one CSV or ERP import projection. Delivery includes a declarative pack, owner-authorized or synthetic fixtures, reconciliation tests, and a private/offline runbook. No private statement should be posted publicly.

Start with the structured [Statement Import Pack intake](https://github.com/Nickgonzales76017/bonfyre-statement-import/issues/new?template=statement-import-pack.yml), containing only the public bank/layout name, a redacted description of the text layout, the desired projection, and the required reconciliation rule. Scope, availability, secure fixture transfer, terms, and payment must be agreed before work begins.

## Clean-room and license boundary

This repository is an independently authored behavioral implementation. It does not copy or translate the AGPL-3.0 Monopoly implementation. The evidence base is documented statement-to-CSV behavior and independently created synthetic fixtures. Only the synthetic `Demo Bank` fixture is included.

## Limits

- No PDF renderer or OCR engine is bundled.
- The included pack is synthetic and is not evidence of support for a real bank.
- A layout is supported only after its pack and reconciliation fixtures pass.
- Source authenticity is outside this parser; use cryptographic provenance upstream when required.

## License

MIT. See [LICENSE](LICENSE).
