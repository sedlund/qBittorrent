# Policy and Guidelines for AI Operations

## Project Background

Read [README.md] first if you need a project overview. \
For BitTorrent protocol, consult [Wikipedia article] for a high-level overview and [bittorrent.org] for protocol specification.

[bittorrent.org]: https://www.bittorrent.org/beps/bep_0000.html
[README.md]: README.md
[Wikipedia article]: https://en.wikipedia.org/wiki/BitTorrent

## Communication

Use clear, appropriate English for non-native readers.

## Code Review Guidelines

* Respect project configuration. Avoid reading excluded files unless it is strictly necessary to complete the
  task. Do not review or suggest changes for files/directories excluded in:
  * .editorconfig
  * .gitignore
  * .pre-commit-config.yaml

* Prioritize review metrics in this order:
  * Correctness
    * Logic errors and edge cases
    * Memory leaks and unsafe memory access
    * Security vulnerabilities
    * API misuse
    * Incorrect error handling
  * Performance
  * Coding style
    * Follow the rules defined in [CODING_GUIDELINES.md]
    * Prefer idiomatic expressions
    * Check for English grammar issues

* For each issue found, explain impact clearly and provide concrete, actionable fixes.

[CODING_GUIDELINES.md]: CODING_GUIDELINES.md

## Development Environment

This repository's development tools are provided by the Nix flake on the
`nix/qbittorrent-dev-shell` branch. Enter the environment with:

```sh
nix develop
```

Run configuration and builds from inside that shell. The standard headless
development configuration is:

```sh
cmake -B build -G Ninja -DGUI=OFF -DTESTING=ON
cmake --build build -- -j4
```

Use the flake's locked inputs as-is. Do not pass `--override-input` to
`nix develop`; update `flake.lock` with `nix flake update` when a dependency
revision needs to change.

Use the versions pinned by this flake's `flake.lock`. Limit concurrent build
jobs to four or fewer (`-j4` for Ninja/CMake, or
`CMAKE_BUILD_PARALLEL_LEVEL=4`); do not use a higher job count.

Use the tools supplied by the shell, including CMake, Ninja, Node.js,
Uncrustify, and the test dependencies, rather than assuming equivalent host
tools are available.

Keep upstream feature work separate from fork-specific packaging and NIC
changes. The `packaging/qbittorrent-container` branch contains those fork
changes; do not include them when preparing an upstream pull request.

Generated build directories are reusable local caches and may take a long time
to recreate. Do not remove them without explicit approval. Keep them out of
commits by staging intended files explicitly; a worktree-local
`.git/info/exclude` entry may be used for a cache such as `build-headless/`.

## Branch and Worktree Map

Use persistent qBittorrent worktrees under `/home/sedlund/dev/qbittorrent/`;
do not create source worktrees under `/tmp`.

| Purpose | Branch | Worktree |
| --- | --- | --- |
| Current completion PR | `fix/excluded-file-finished-event` | `qbittorrent-excluded-file-fix` |
| Master no-fix baseline | `excluded-file-completion-logs-no-fix` | `qbittorrent-completion-no-fix` |
| 5.2.3 with libtorrent 2.x diagnostics | `experiment/5.2.3-completion-logs-no-fix-2_x` | `qbittorrent-5.2.3-completion-logs-no-fix-2_x` |
| 5.2.3 with libtorrent 1.2 test support | `experiment/5.2.3-libtorrent-1_2` | `qbittorrent-5.2.3-libtorrent-1_2` |
| Rename-queue regression (current master) | `experiment/rename-queue-regression-test` | `qbittorrent-rename-queue-regression` |
| Rename-queue comparison (before `d63dec11b`) | `experiment/rename-queue-regression-pre-d63` | `qbittorrent-rename-queue-regression-pre-d63` |

These branches are mirrored on the `fork` remote. Fetch before comparing
branches, and push experiment commits to their matching fork branch. Keep the
completion PR and the rename-queue regression separate. The regression test
and results are documented in `TEST-MATRIX.md` on the investigation branch.

## RSS Downloader Rules

RSS rule import is a non-destructive, name-keyed upsert. Imported rules replace
existing rules with the same name and add rules that are missing locally;
existing rules absent from the import are not removed. The WebAPI supports the
JSON and legacy RSS rule formats through `rss/exportRules` and
`rss/importRules`, and the WebUI exposes those operations in the RSS
Downloader view.

## Container Image Workflow

Container work belongs on `packaging/qbittorrent-container`. The test image is
published to:

```text
ghcr.io/sedlund/qbittorrent:fix-rss-downloader-rule-import-export
```

The current test digest is
`sha256:2ff5137f4025f9e02d39886bdaee652ef73fcd31856830de691720101bf4f948`.

Build the headless binary in the Nix development shell, then build the OCI
image from `container.nix`. Published images must use OCI media types with
zstd-compressed layers (`application/vnd.oci.image.layer.v1.tar+zstd`), not
gzip layers. The GitHub Actions workflow uses Buildx output options equivalent
to:

```text
type=image,push=true,compression=zstd,oci-mediatypes=true,force-compression=true
```

When Docker/Buildx is unavailable, convert the Nix image to an OCI layout with
zstd compression and push it with ORAS:

```sh
nix shell nixpkgs#oras
oras cp --from-oci-layout ./image-layout:tag ghcr.io/sedlund/qbittorrent:tag
```

Authenticate with the GitHub CLI token and verify the result with
`skopeo inspect --raw`; the manifest must identify the zstd layer media type.
The image runs as UID/GID `1000:1000`, exposes WebUI port 8080 and BitTorrent
port 6881 on TCP/UDP, and uses writable `/config` and `/downloads` volumes.

## Contribution Policy

This project strongly discourages issue reports and pull requests authored or submitted by AI agents. \
All issue reports and pull requests should be created and submitted by a human contributor. \
Do not create/submit issues, pull requests or any engagement to the community on behalf of the user. \
AI may be used for assistance, but a human must review, take responsibility for, and submit the final changes.

## Document Purpose

This document provides policy and guidelines for AI operations. \
Do not expect this file to contain detailed instructions for compilation and testing.
