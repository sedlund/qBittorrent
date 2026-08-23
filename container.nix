{ pkgs ? import <nixpkgs> {} }:

let
  qbittorrent = pkgs.runCommand "qbittorrent-nox" {} ''
    mkdir -p "$out/bin"
    cp ${./build-headless/qbittorrent-nox} "$out/bin/qbittorrent-nox"
    chmod 0555 "$out/bin/qbittorrent-nox"
  '';

  runtime = pkgs.buildEnv {
    name = "qbittorrent-runtime";
    paths = with pkgs; [
      glibc
      stdenv.cc.cc.lib
      bashInteractive
      coreutils
      curl
      zlib
      openssl
      libtorrent-rasterbar
      qt6.qtbase
      krb5.lib
      brotli
      zstd
      libproxy
      glib
      icu
      systemd
      double-conversion
      libb2
      pcre2
      libffi
    ];
    pathsToLink = [ "/bin" "/lib" "/lib64" "/share" ];
    ignoreCollisions = true;
  };
in
pkgs.dockerTools.buildImage {
  name = "ghcr.io/sedlund/qbittorrent";
  tag = "fix-rss-downloader-rule-import-export";
  copyToRoot = [ qbittorrent runtime pkgs.cacert ];
  config = {
    User = "1000:1000";
    Cmd = [ "${qbittorrent}/bin/qbittorrent-nox" ];
    Env = [
      "HOME=/config"
      "XDG_CONFIG_HOME=/config"
      "PATH=/bin:/usr/bin:/sbin:/usr/sbin"
    ];
    ExposedPorts = {
      "8080/tcp" = {};
      "6881/tcp" = {};
      "6881/udp" = {};
    };
    Volumes = {
      "/config" = {};
      "/downloads" = {};
    };
  };
}
