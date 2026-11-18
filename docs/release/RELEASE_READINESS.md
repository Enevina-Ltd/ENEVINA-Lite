# Enevina Lite — Release readiness and history hygiene

Checked: **18 November 2026**. Scope: the public source repository and developer preview `v0.1.0-alpha.1`.

## Confirmed

- The first release is a **pre-release**, with a tag pointing to the `main` commit at publication; no separately uploaded firmware binary accompanies it.
- The current source tree has a custom Enevina noncommercial license, a contributor guide and general-purpose product-facing documentation. This is **source-available**, not OSI open source.
- A local read-only Git-object scan examined **430 commits / 718 blobs** reachable from the cloned branches and tags. No high-confidence committed private signing keys, GitHub access tokens or AWS access keys were identified by the patterns used. Pattern-based scanning **cannot certify absence of secrets**.
- Two apparent password-assignment hits came from human-readable Wi-Fi error-message constants, not credentials. Test MAC values, reserved private IP examples and test coordinates may appear in source/test fixtures.
- The current main branch's source and documents were scanned for named site references. Older commits **do contain identifying test-site text**, even after it was removed from the current branch.

## Blocking work before claiming privacy-clean Git history

The original source repository was publicly accessible **before** the history audit. Some older file versions, merged branches, PR refs and the published tag can retain identifying test-site prose. Merely editing the current file or adding `.gitignore` **does not remove those objects**.

A proper history purge, if desired, requires all of the following:

1. Take an **offline, access-controlled mirror backup** of the complete repository (branches, tags, release metadata and relevant pull request references).
2. Inventory every remote branch, tag, release, open/closed PR ref and any other Git ref containing the content. Run a maintained history filter on a **disposable clone**, preserving unrelated code and verifying the filtered result on every rewritten ref.
3. Review history rewrite impact: all rewritten SHA identifiers, collaborator clones, open PR diffs, CI provenance, external links and especially `v0.1.0-alpha.1` and its automatically generated source archives.
4. Publish the filtered refs with protected, reviewed **force-with-lease** operations only when the repository is quiescent. Update/recreate affected releases and tags as necessary and communicate required re-clones.
5. Contact GitHub Support for guidance on purging cached views or unreachable sensitive blobs and PR refs. Already downloaded forks and local clones cannot be recalled.
6. Rotate any actual credentials if ever discovered; rewriting Git history does not make exposed secrets safe.
7. Re-clone **all** visible refs and repeat the history and secret scan. Confirm the release tag and files are the intended approved snapshot.

**Not completed by this documentation change:** the destructive multi-ref history rewrite, retagging or cache purge. The public historical content must not be described as removed until independently verified. This work must not happen implicitly as part of a normal documentation PR.

## License and binary release checklist

- [x] Document third-party dependencies and external license references in [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).
- [x] Correct README's outdated statement that the repository is not yet publicly released.
- [ ] Obtain an independent review of the custom Enevina license and any contributor grants needed for future commercial licensing.
- [ ] Produce exact firmware-component SBOM from actual production builds; include full applicable third-party license texts, notices and any source/relinking obligations.
- [ ] Complete supported-device tests, OTA/recovery verification, hardware safety checks and product compliance before publishing installable firmware.
- [ ] Complete the history purge process above and confirm there are no site-identifying references in accessible historic refs, **if public history must be privacy-clean**.

## Preventive checks

`.gitignore` excludes local credentials and private signing-key patterns. This reduces accidental additions but **does not protect already-tracked files, Git history, or uploaded artifacts**. Run additional dedicated secret-scanning software across the full history whenever dependencies and tools permit. Do not commit output containing actual secrets.
