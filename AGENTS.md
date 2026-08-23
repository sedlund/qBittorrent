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

## Contribution Policy

This project strongly discourages issue reports and pull requests authored or submitted by AI agents. \
All issue reports and pull requests should be created and submitted by a human contributor. \
Do not create/submit issues, pull requests or any engagement to the community on behalf of the user. \
AI may be used for assistance, but a human must review, take responsibility for, and submit the final changes.

## PR Image Workflow

Keep upstream source changes in the dedicated PR worktree:
`/home/sedlund/dev/qbittorrent-issue-13868`. The `packaging/qbittorrent-container`
worktree is for the committed container workflow and should remain clean; do not
leave source edits or build caches there.

Build the headless binary from the PR worktree using the Nix development shell.
Reuse its existing `build-headless` cache when possible:

```sh
nix develop /home/sedlund/dev/qbittorrent-dev-shell --command \
  cmake --build build-headless --target qbittorrent-nox --parallel 2
```

`container.nix` expects `./build-headless/qbittorrent-nox`. To keep the packaging
worktree clean, stage a copy of `container.nix` and the freshly built binary in a
temporary directory, then run `nix-build` there. Inspect the resulting OCI layout
with Skopeo and confirm that its manifest uses
`application/vnd.oci.image.layer.v1.tar+zstd`.

Authenticate to GHCR with the existing GitHub CLI credentials and push the OCI
layout with ORAS:

```sh
gh auth token | nix shell nixpkgs#oras --command \
  oras login ghcr.io --username <github-user> --password-stdin
nix shell nixpkgs#oras --command oras cp --from-oci-layout \
  <oci-output>:<tag> ghcr.io/sedlund/qbittorrent:<tag>
```

Verify the remote manifest and digest after pushing. Remove temporary staging
directories and ensure the packaging worktree has no uncommitted files or build
cache left behind. Do not push source or packaging commits as part of this image
workflow.

## Document Purpose

This document provides policy and guidelines for AI operations. \
Do not expect this file to contain detailed instructions for compilation and testing.
