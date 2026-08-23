# qBittorrent container packaging

This branch contains the fork-specific container packaging for the RSS WebUI
fix. The upstream pull request does not include these files.

The tested image is published as an OCI image with zstd-compressed layers:

```text
ghcr.io/sedlund/qbittorrent:fix-rss-downloader-rule-import-export
```

The current test image digest is:

```text
sha256:2ff5137f4025f9e02d39886bdaee652ef73fcd31856830de691720101bf4f948
```

`container.nix` packages an existing headless `build-headless/qbittorrent-nox`
build using Nix's OCI image tooling. `Dockerfile` provides a conventional
multi-stage fallback that compiles the headless application inside the image.
When Docker is unavailable, convert the Nix-built image to an OCI layout with
zstd compression and publish it with ORAS; Skopeo may normalize the layer back
to gzip when copying to a registry.

The image includes standard `/bin` tooling required by Kubernetes hooks:
`sh`, `curl`, and `sleep`. It runs as UID/GID `1000:1000`, exposes WebUI port
8080, and provides writable `/config` and `/downloads` volumes.
