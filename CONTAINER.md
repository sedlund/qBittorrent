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

The image includes standard `/bin` tooling required by Kubernetes hooks:
`sh`, `curl`, and `sleep`. It runs as UID/GID `1000:1000`, exposes WebUI port
8080, and provides writable `/config` and `/downloads` volumes.
