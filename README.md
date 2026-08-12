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

## Text contract

Transaction rows use `YYYY-MM-DD DESCRIPTION SIGNED_AMOUNT RUNNING_BALANCE`. The included pack declares the bank identifier, opening/closing labels, and currency. Pack differences remain data; the C core owns bounded scanning, ISO date checks, amount normalization, extraction, invariant verification, CSV escaping, and receipt emission.

## Verification receipt

Successful imports record the bank, currency, source path, byte count, non-cryptographic FNV-1a source checksum, transaction count, balances, and passed invariants. FNV-1a is an accidental-change checksum, not a cryptographic signature.

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
