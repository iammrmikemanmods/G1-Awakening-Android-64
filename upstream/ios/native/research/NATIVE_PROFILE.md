# Native campaign profile storage

`SaveStore` defines a version1 native profile format with an explicit signature,
size, fixed campaign record counts, bounded unlock indices and an Adler32
corruption check. It preserves both completion flags, unlock progress and all25
three-value mission records. It does not import original ARM-memory save files.

The file writer uses a unique temporary sibling with owner-only permissions,
checks every write, flushes the file, atomically replaces the destination, then
flushes the parent directory. Any write/rename/flush error is returned to the
caller. Files written before a failed pre-rename attempt remain untouched;
a directory-flush error after rename still reports failure. The campaign must
not transition on a fabricated success boolean.

Host checks cover lossless signed values, corruption/version/truncation,
invalid profile shape, closed-file readback, replacing an existing save, missing
parent errors, failed rename, preservation of the prior save and temporary-file
cleanup. The same atomic-file test is included in the Apple-host CI job.

This component stores campaign records. A complete mutable mission snapshot,
actual app launch/relaunch restore, app interruption behavior and physical
XR cold reload remain distinct implementation/verification requirements.
