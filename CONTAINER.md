# qBittorrent container packaging

This branch contains the fork-specific container packaging for the RSS WebUI
fix. The upstream pull request does not include these files.

The tested image is published as:

```text
ghcr.io/sedlund/qbittorrent:fix-rss-downloader-rule-import-export
```

`container.nix` packages an existing headless `build-headless/qbittorrent-nox`
build using Nix's OCI image tooling. `Dockerfile` provides a conventional
multi-stage fallback that compiles the headless application inside the image.
