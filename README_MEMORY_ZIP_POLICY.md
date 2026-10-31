# G1 Awakening Android development archive policy

Preserve this file at the root of every ZIP export, together with the archive's
directory structure, source identity, licence files and validation records.

This Android port uses the iOS Build 017 source commit
`be203a911bc26313615ea450758413e5c9cf6462` and the exact owner-supplied 017R1
resource package. `SOURCE_PROVENANCE.json` fingerprints the unmodified iOS
native source and licence. `validation/ios-resource-manifest.json` fingerprints
the original resource bytes. Keep code, package hashes and validation reports
from the same Android checkpoint together.

Source exports contain the Android platform adapters, unmodified portable
engine, build scripts, tests and documentation. They exclude build caches,
private signing keys, original IPA files, original executables, user saves and
the original resource payload. Import the exact resources locally with
`tools/import_ios_assets.py` when building an owner APK.

A delivered owner APK contains the native Android ARM64 executable and the
verified original data, clean texture replacements and iOS introduction movie.
The original iOS executable and iOS signatures are never imported. A source-only
CI compile is not a playable package.

Compilation, 16 KB alignment, source/asset checksums and host render probes
prove the properties listed in their reports. They do not prove physical A36
execution, audible Android playback, every campaign path, or perfect iOS visual
equivalence. Device results must identify the APK hash, device model, OS build
and tested path. `PORT_STATUS.json` and `VALIDATION.md` describe this checkpoint's
actual evidence and remaining work.

Do not replace source assets, invent successful tests, remove these limitations
from a development export, or treat an old checkpoint as evidence for a newer
APK. Signing-key backups are private maintainer artifacts and must remain
separate from public source archives.
