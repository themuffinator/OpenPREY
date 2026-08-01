# macOS Package Layout And Release Policy

Updated: 2026-08-01

This document records the current macOS package support contract for openPREY.
The client application is self-contained; loose binaries remain beside it for
dedicated-server and diagnostic use while macOS remains experimental.

## Current Layout Decision

The supported macOS client layout is a self-contained, drag-installable
`openPREY.app`:

- `Contents/MacOS/openPREY` contains the client executable.
- `Contents/Resources/basepr/` contains `mod.json`, `pak0.pk4`, and `pak1.pk4`.
- `Contents/Frameworks/` contains the flat, signed unified
  `game_<arch>.dylib` module used by both single-player and multiplayer.
- `Contents/Resources/assets/splash/` contains the startup splash resource.

Mach-O game modules deliberately live in `Contents/Frameworks`, not
`Contents/Resources`: Apple treats `Frameworks` as a nested-code location and
`Resources` as data. The packager signs each module before signing the outer
app, does not copy app entitlements onto those nested libraries, validates their
architecture, dependencies, install names, and deployment floor, and rejects
stale or wrong-platform modules in either location.

The distribution root also keeps `openPREY-client_<arch>`,
`openPREY-ded_<arch>`, the support collector, version/symbol manifests, and
documentation for command-line diagnostics and dedicated servers. Those loose
binaries resolve content and game modules from the sibling self-contained app;
the `basepr` payload is not duplicated beside the bundle.

`.install/basepr/` remains the build/staging contract. Package generation
moves that staged payload into the app's code/data locations through
`tools/build/package_nightly.py` without changing the repository staging
layout. `Contents/Info.plist` declares
`OpenPREYRuntimeLayout=self-contained-v1`, so a damaged current app still gets
the self-contained-runtime diagnostic even when its embedded data or module
directories have been removed completely.

Release packages and archive validation require the bundle to be named
`openPREY.app`. Runtime app-bundle detection still recognizes renamed `.app`
bundles that keep the standard `*.app/Contents/MacOS` layout, so a hand-renamed
bundle retains the same self-contained runtime discovery instead of falling
through to generic base-path probing.

## Supported Launch Flows

Supported for experimental macOS signoff:

- Double-click `openPREY.app` from the mounted signed/notarized DMG payload.
- Drag only `openPREY.app` to `/Applications` or another user-writable folder,
  then launch the copied app from Finder.
- Copy the whole package payload when the loose client, dedicated server, or
  support collector is also wanted.
- Finder/LaunchServices may supply an unrelated process working directory.
  The app validates `Contents/Resources` and uses it as `fs_cdpath`; retail
  Prey install discovery remains an independent `fs_basepath` source for the
  original `base/` assets.
- Launch the app executable from Terminal with any working directory.
- Launch the loose `openPREY-client_<arch>` or `openPREY-ded_<arch>` binaries from
  package root; they discover the sibling app's embedded runtime.

Retail Prey assets are not bundled. Do not copy them into `openPREY.app`,
because modifying a signed bundle invalidates its signature. Use CD-era install
discovery or explicit base-path selection instead.

An incomplete new app produces a localized diagnostic that names missing
`Contents/Resources/basepr` data or the `Contents/Frameworks` module. Engines
from the transition period still accept a complete legacy adjacent package,
but newly generated packages must use the self-contained layout.

The legacy adjacent-package startup diagnostic title is:

```text
openPREY.app adjacent package root is incomplete
```

The diagnostic must name this contract:

```text
Expected adjacent package-root contract: `openPREY.app`, loose binaries, and `basepr/` together
```

It must also make clear that this applies only to legacy adjacent packages:

```text
Legacy adjacent packages need the app, loose binaries, and data together.
Current self-contained packages support moving only `openPREY.app` to `/Applications`.
```

Hosted release validation launches the app executable from an unrelated
temporary working directory and requires the log's `fs_cdpath` to resolve to
`openPREY.app/Contents/Resources`. This closes the path-selection blind spot without
claiming that CI has exercised Finder UI, Gatekeeper prompts, mounted-DMG
gameplay, or a copied package on end-user hardware.

## Renderer Module And Bundled MoltenVK

Added: 2026-07-25.

Both existing macOS package variants also carry the Vulkan renderer module and
its translation layer as nested code:

- `Contents/Frameworks/renderer-vk_<arch>.dylib` — openPREY's Vulkan renderer
  module, built with hidden symbol visibility and an export list that exposes
  only `GetRenderAPI`, because the macOS client still links a second copy of the
  renderer statically and any further exported symbol would interpose at
  `dlopen` time. Its install name is `@loader_path/renderer-vk_<arch>.dylib`.
- `Contents/Frameworks/libMoltenVK.dylib` — MoltenVK, a Vulkan-on-Metal
  translation layer, pinned to `v1.4.1` because that is the newest release
  compatible with the documented macOS 11 floor. Staging rewrites its install
  name to `@executable_path/../Frameworks/libMoltenVK.dylib` and validates its
  architecture set, minimum-OS load command, and dependency allowlist
  (`/System/Library/` and `/usr/lib/` only).

These are additions to the two existing package variants, not a new variant.
There is no third macOS download and no new `macos_graphics_bridge` value.
OpenGL remains the default renderer in both packages; the Vulkan renderer is
selected at run time with `r_renderApi vulkan`. The decision plan is
[macos-moltenvk-decision.md](macos-moltenvk-decision.md).

Both files are nested code and follow the existing inside-out signing rule: each
is signed with the Developer ID Application identity, without app entitlements,
before the outer app is signed and notarized. The published MoltenVK release
dylib is ad-hoc signed only, so a credentialed release run must re-sign it; an
ad-hoc signature surviving into a notarized package is a signing defect rather
than an acceptable state.

MoltenVK is Apache-2.0 licensed. Its attribution belongs in the package license
collateral. The provider, pinning, verification, and refresh policy is recorded
in `docs/dev/macos-moltenvk-provider-policy.md`.

For a future universal2 artifact, `renderer-vk` is merged by `lipo` like the
other code items, while `libMoltenVK.dylib` is already universal and is checked
for identity across slices instead of being merged, because merging two
different MoltenVK builds is not meaningful.

## Symbol Artifacts

Runtime macOS packages may include the small root `SYMBOLS.txt` manifest so
support archives can identify matching binaries and dSYM archives.

Runtime DMGs and tarballs must not include `.dSYM` bundles or other debug
payloads. dSYMs are published as separate artifacts named like
`openprey-<version>-macos-arm64-opengl-symbols.tar.xz` and
`openprey-<version>-macos-arm64-metal-symbols.tar.xz`.

See [macOS Symbolication Workflow](macos-symbolication.md) for the crash-log
pairing process.

## Support Path Reports

`collect_macos_support_info.sh` writes `package/path-resolution.txt` without
launching openPREY. The report records the package root, app path, expected loose
runtime paths, embedded `Contents/Resources/basepr` and unified
`Contents/Frameworks/game_<arch>.dylib` paths, and any copied log lines that mention
`fs_basepath`, `fs_cdpath`, or `fs_savepath`.
If `HOME` is absent in a sparse launch environment, the collector keeps the
package-local log checks and records archive notes instead of aborting on
home-scoped log or DiagnosticReports paths.

The same archive also includes `package/binary-architecture.txt` and
`package/dylib-dependencies.txt` without launching openPREY. These reports record
read-only `file`/`lipo` architecture output, `otool -L` dependency output, and
`otool -D` install names for game modules so maintainers can confirm the
package shape when users report architecture, loader, or `@loader_path`
failures.

`package/signing.txt` uses the same Gatekeeper assessment shape as release
validation, including `spctl --assess --type execute --verbose=4` and
`xcrun stapler validate` where the local tools are available. Current release
packages remain arm64-only, but the support archive also inspects any loose
`x64` or `x86` executable names that happen to be present so accidental
wrong-arch package contents are visible in intake data without becoming support
claims.

`system/rosetta.txt` records the collector process architecture and
`sysctl.proc_translated` state. Rosetta remains outside the supported release
matrix, but the report helps maintainers distinguish unsupported translated
sessions from native arm64 package failures.

## Signoff Evidence Requirements

Every completed macOS signoff archive for a release candidate must record:

- Finder launch from the mounted DMG or final release image.
- Finder launch after dragging only `openPREY.app` to `/Applications` or another
  user-writable location.
- Whole-package copied launch for loose-binary and support-tool coverage.
- Terminal launch of the app executable from an unrelated working directory.
- Confirmation that the copied app resolves `fs_cdpath` to its own
  `Contents/Resources` and loads the signed unified game module from
  `Contents/Frameworks`.
- `fs_basepath`, `fs_cdpath`, and `fs_savepath` log lines from Finder/copied
  package and terminal launches.
- Gatekeeper behavior for the package under test, including `spctl` assessment
  for signed/notarized DMGs or the expected approval friction for unsigned
  development archives.

The signoff evidence index keeps these fields even while no accepted completed
archive has been recorded yet.

## Release Policy

First-class macOS releases require signed and notarized DMGs for every supported
macOS artifact. A first-class macOS release job must fail if Apple Developer ID
signing or notarization credentials are missing.

Unsigned `-unsigned.tar.gz` archives are allowed only for experimental or
development fallback output. They must stay clearly marked as unsigned and
unnotarized in artifact names, release notes, and user-facing docs.

Runtime and symbol archive validation must match the declared package format. A
`.tar.gz` runtime artifact is opened as gzip data, a `.tar.xz` runtime artifact
is opened as xz data, and macOS dSYM symbol archives are opened as
xz-compressed tarballs. Mislabeled tarballs fail validation instead of relying
on auto-detection.

Release archive, DMG, notarization, and symbol archive outputs must not be
written inside the package or symbol tree they are archiving.

Credentialed macOS release artifacts must keep these checks mandatory:

- Developer ID Application signing.
- Hardened Runtime.
- entitlement validation that rejects App Sandbox and `get-task-allow` unless a
  reviewed sandbox/file-access design exists.
- app and DMG notarization/stapling.
- `codesign`, `spctl`, `xcrun stapler validate`, and `hdiutil verify`.

## Backward Compatibility

The runtime keeps a legacy adjacent-package fallback so older experimental
downloads can still launch when their original `openPREY.app`, `basepr/`, loose
client, loose dedicated binary, and unified game module remain together. New
package generation,
archive validation, signing, release smoke tests, support intake, and signoff
evidence must use the self-contained contract. Remove the fallback only through
an explicit compatibility decision with release-note notice.
