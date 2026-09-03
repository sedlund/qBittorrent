# Excluded-file completion test matrix

Last updated: 2026-09-03

This file records the completion-event experiments for the excluded-file
completion issue. “Finished” means qBittorrent emitted its
`Session::torrentFinished` signal; the production image test additionally
checks whether the configured completion command ran.

qBittorrent 5.2.3 and master/5.3.0alpha1 must be treated as separate source
versions. Master contains later changes to excluded-file handling that are not
present in 5.2.3. The 5.2.3 experiment below applies the newer regression test
to the older source only to compare the behavior; it is not a 5.2.3 upstream
test or a drop-in substitute for the master test.

## Source revisions

| Label | Worktree / branch | Revision | qBittorrent |
| --- | --- | --- | --- |
| Fixed | `qbittorrent-excluded-file-fix`, `fix/excluded-file-finished-event` | `0dbbf0e6e` | 5.3.0alpha1 |
| No-fix | `qbittorrent-completion-no-fix`, `excluded-file-completion-logs-no-fix` | `e76f8d08b` | 5.3.0alpha1 |
| 5.2.3 baseline / 2.x diagnostics | `qbittorrent-5.2.3-completion-logs-no-fix-2_x`, `experiment/5.2.3-completion-logs-no-fix-2_x` | `f0154ec9e` | 5.2.3 |
| 5.2.3 / 1.2 test branch | `qbittorrent-5.2.3-libtorrent-1_2`, `experiment/5.2.3-libtorrent-1_2` | `b188d7afa` | 5.2.3 |

The 5.2.3 experiment contains the regression test applied to the 5.2.3 tag;
it is not an upstream commit on that tag.

## Local unit-test results

The test process totals include QtTest setup/cleanup tests. The test fixture
was made libtorrent-1.2-compatible with test-only API guards; production code
was not changed for that compatibility adjustment.

| qBittorrent source | libtorrent | Boost | Result | Notable observation |
| --- | --- | --- | --- | --- |
| Master / fixed (5.3.0alpha1) | Nix default, 2.0.13 | Nix default | **7 passed, 0 failed** | All seven completion tests passed. |
| Master / fixed (5.3.0alpha1) | Nix `#libtorrent-1_2`, 1.2.19 | 1.86 | **4 passed, 5 failed** | `testSharedPieceWithExcludedFile` did not emit `torrentFinished`; several tests assumed a different 1.2 state/order or failed asynchronous cleanup. |
| Master / no-fix (5.3.0alpha1) | Nix `#libtorrent-1_2`, 1.2.19 | 1.86 | **2 passed, 7 failed** | Ignored-file and shared-piece completion tests failed, along with version-sensitive state/cleanup assertions. |
| Release 5.2.3 baseline branch | Nix `#libtorrent-1_2`, 1.2.19 | 1.86 | **2 passed, 5 failed** | `testFinishedWithIgnoredFile` reported `finishedCount == 0`; other failures were synthetic state/cleanup checks. Results are not directly comparable to master because excluded-file handling changed after 5.2.3. |

### Rename-queue regression test

The focused test `testFinishedWithIgnoredFileInUnwantedFolder` enables the
unwanted-folder feature, queues an ignored-file rename, then exercises the
normal `torrent_finished_alert` completion path.

| Revision | Branch / test commit | Result |
| --- | --- | --- |
| Current master after `d63dec11b` | `experiment/rename-queue-regression-test` / `9a51c3976` | **Failed**: `finishedCount == 0` after 10 seconds. |
| Parent of `d63dec11b` | `experiment/rename-queue-regression-pre-d63` / `cf7804292` | **Passed**: `finishedCount == 1`. |

This establishes a reproducible regression boundary at `d63dec11b`,
independent of the missing-alert fallback fix.

The fixed 2.0.13 run is the cleanest local result. The 1.2.19 runs show
that the shared-piece ignored-file case is not yet proven for the 1.2 line.
The exact CI 1.2.20 build has not been run locally.

## OCI images

| Image | Digest / location | Dependency line | Status |
| --- | --- | --- | --- |
| Master / no-fix logging image (5.3.0alpha1) | `ghcr.io/sedlund/qbittorrent:excluded-file-completion-logs-no-fix@sha256:f3644606bd9ecc1415224a394e7b5e2eaa4a36747502c4ebd198a5b9fe28af0d` | Default Nix 2.x build | Published; user observed no completion command. |
| Master / fixed logging image (5.3.0alpha1) | `ghcr.io/sedlund/qbittorrent:fix-excluded-file-finished-event@sha256:b5308d5c9051815ddafe0258b9447c05fe48141ce3dd13cbbeb68a5e87069bec` | Default Nix 2.x build | Published; current pod test recorded below. |
| Release 5.2.3 / no-fix diagnostic image | `ghcr.io/sedlund/qbittorrent:test-excluded-file-completion@sha256:fe0ba08929f6fbb39435d6dfa7813d23c871e827e6b78c7fe9896e97390b5cc0` | Default Nix 2.x build (libtorrent 2.0.13) | Published; qBittorrent 5.2.3 with diagnostics and without the completion fix. |
| Fixed libtorrent-1.2 experiment | `/tmp/qbt-container-stage.4WJ1zD/result` | 1.2.19 / Boost 1.86 | Local OCI layout only; not pushed. |

The no-fix pod previously reported an older image digest
`sha256:c0ef845fac9342f2f08b5f1adbcfacae3af9785972bd5299bd77901f5ad94cec`;
the current package page reports the digest above.

## External builds tested

| Build | libtorrent | Observation |
| --- | --- | --- |
| TrueForge `oci.trueforge.org/containerforge/qbittorrent:5.2.3@sha256:8929ad12c4b0e7cddb77b4de7fb250c8d6b68881494fcac7679a284de0510d99` (user-provided pinned image) | 2.0.14, statically linked | Completion command ran; global excludes remained priority 1 and were downloaded. |
| Scoop Extras `qbittorrent` 5.2.3 standard installer | 1.2.20 | The manifest selects `qbittorrent_5.2.3_x64_setup.exe`, not the `lt20` installer. |

The TrueForge image and Scoop desktop build therefore exercise different
libtorrent lines.

## Current fixed-image runtime test

Image deployed in the user's pod:

```text
ghcr.io/sedlund/qbittorrent:fix-excluded-file-finished-event@sha256:b5308d5c9051815ddafe0258b9447c05fe48141ce3dd13cbbeb68a5e87069bec
```

Source line: master / qBittorrent 5.3.0alpha1; dependency line: default Nix
2.x build.

Observed with `Bobs.Burgers.S16E03.1080p.x265-ELiTE`:

- The added-torrent external command ran.
- The torrent was moved from `/downloads/incomplete` to `/downloads/complete`.
- No completion external command ran.
- No `torrent_finished_alert` or completion-diagnostic line appeared in the
  pasted pod output.

This is a real master-image failure and is separate from the 5.2.3 experiment.

## CI coverage

qBittorrent's CMake requirements support libtorrent 1.2.19+ and 2.0.10+.
The GitHub CI matrices build and run the test suite for libtorrent 1.2.20,
2.0.14, and 2.1.1 (with GUI and headless variants). The matrix is defined in
the workflows; the local test executable itself runs against only the library
selected by its build configuration.

## Pending comparisons

1. Compare the current fixed-image runtime result with the no-fix image using
   the same torrent and profile.
2. Compare the published 5.2.3 no-fix diagnostic image with the current
   master image using the same torrent and profile. This isolates source
   changes between the 5.2.3 release and master while keeping the 2.x
   dependency line consistent.
3. If the 1.2 path must be tested as an image, publish matching fixed and
   no-fix 1.2 images; do not compare a fixed 1.2 image with a no-fix 2.x image.
4. Run the exact CI 1.2.20 build only if the 1.2.19 result remains ambiguous or
   exact CI parity is needed.

