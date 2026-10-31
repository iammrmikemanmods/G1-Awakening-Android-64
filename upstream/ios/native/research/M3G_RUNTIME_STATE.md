# Native M3G runtime state for battle integration

The additive API retains serialized Texture2D identity through decoded surfaces, permits target-restricted animation sampling, and implements the runtime state operations needed by the recovered battle caller. It does not execute the original game or choose a battle phase.

`M3GAnimations::sample_targets` evaluates only explicitly selected Object3D targets. The battle graph supplies the actual recursive target set; an untracked parent does not implicitly sample a child. Existing sampling arithmetic and whole-file `sample` behavior are unchanged.

`M3GScenePose` now exposes general-matrix assignment, absolute component translation, additive local Translate, rendering enable, and detach. General assignment preserves TRS. Detach preserves all local components and removes former-parent matrix/alpha/enable inheritance. Translate follows the public-vector finite/subnormal boundary and the all-components epsilon no-op recovered in M3G_CAMERA_TRANSLATE_SLOT.md. Its component additions round separately to binary32. Nonfinite/overflowing updates fail before mutation.

The Texture2D object index distinguishes two textures sharing one image. Runtime SetImage is therefore applied to every occurrence of its selected Texture2D identity, without changing another texture which happens to use the old Image2D. Native resource IDs for replacement images are separate bookkeeping, not original serialized object identities.

Validation: 26 focused runtime contracts passed a strict optimized host build. They cover isolated sampling, excluded unsupported tracks, TRS/general composition, repeated additive translation, detach inheritance, public subnormal/epsilon behavior, and failed-update atomicity. The existing scene contract now also checks retained texture identity for each tested image format. No new original-code execution, Apple compile, Metal rendering, or device result is claimed by this change. Existing static/original evidence is in M3G_CAMERA_TRANSLATE_SLOT.md, M3G_MODEL_ATTACHMENT.md, and M3G_PUBLIC_ANIMATE.md.
