# Apple validation prepared for native animated battles

The workflow has been extended to compile the exact M3G runtime shader for
`iphoneos`, build the actual ARM64 application, run all portable native tests
on the hosted Apple machine, and execute a separate M3G offscreen pixel probe
when a Metal device is available. **These new Apple checks are prepared, not
recorded as passing.** The prior successful interaction build predates them.

`MetalM3GProbe.mm` uses the same upload, portable frame planner, GPU-retained
asset identities and `encode_frame` path as the battle renderer. It contains
only synthetic geometry and generated image pixels. Its twelve pixel
assertions compare all four channels and cover:

- Mesh UV orientation and image channel order.
- Original source-alpha blending of both color and alpha channels.
- Near/far depth rejection and independent depth writes with an ALWAYS test.
- RGBA REPLACE overriding the previously applied node alpha.
- Projected Sprite3D rectangles, UV orientation and alpha compositing.

The common CPU geometry initializer and Sprite3D projection were checked on
Linux. That is not Objective-C++ compilation, Metal shader compilation or GPU
execution. The Apple probe returns77 if no device exists; this remains a skip,
never a successful pixel result. The workflow records its JSON and PNG output.

The exact runtime MSL string is extracted from `MetalM3GShader.hpp` by
`analysis_tools/m3g/extract_metal_shader.py`; a separately maintained shader
cannot pass while the app ships different source. Runtime compilation also
keeps fast math disabled.

The job checks that the selected iPhoneOS Metal compiler actually runs before
using it. If the optional toolchain is absent, it uses Apple's documented
`xcodebuild -downloadComponent metalToolchain` installation command, then checks
the compiler again. Reference: [Apple's additional Xcode components guide](https://developer.apple.com/documentation/xcode/downloading-and-installing-additional-xcode-components).

The portable core and its consumers compile with `-ffp-contract=off` on GNU
and Clang-family compilers. This prevents a fused multiply-add from replacing
the separately rounded operations used by the original ARM soft-float code.
The new Apple CPU test step exercises the recovered animation, projection,
controller helpers and gameplay fixtures on the actual hosted architecture.
Host math-library boundaries and documented matrix tolerances remain explicit
in the component reports.

None of these gates establishes an on-device iPhone XR playthrough, sustained
60Hz performance, thermal behavior, audio output or UIKit lifecycle correctness.
Those require the matching packaged app and physical-device testing.
