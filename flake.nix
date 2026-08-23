{
  description = "qBittorrent development shell";

  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";

  outputs = { self, nixpkgs }:
    let
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: nixpkgs.lib.genAttrs systems (system: f (import nixpkgs {
        inherit system;
      }));
    in {
      devShells = forAllSystems (pkgs: {
        default = pkgs.mkShell {
          packages = with pkgs; [
            cmake
            ninja
            pkg-config
            boost
            libtorrent-rasterbar
            openssl
            zlib
            qt6.qtbase
            qt6.qttools
            qt6.qttranslations
            python3
            nodejs
            uncrustify
            gdb
            lldb
          ];

          shellHook = ''
            export CMAKE_BUILD_PARALLEL_LEVEL="''${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
            echo "qBittorrent shell ready. Configure with: cmake -B build -G Ninja -DGUI=OFF -DTESTING=ON"
          '';
        };
      });
    };
}
