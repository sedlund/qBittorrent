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
      devShells = forAllSystems (pkgs:
        let
          libtorrent12 = pkgs.libtorrent-rasterbar-1_2_x.overrideAttrs (old: {
            configureFlags = builtins.filter (flag: flag != "--enable-python-binding") old.configureFlags;
            nativeBuildInputs = builtins.filter (input: input != pkgs.python311.pkgs.setuptools) old.nativeBuildInputs;
            buildInputs = builtins.filter (input: input != pkgs.python311) old.buildInputs;
            outputs = [ "out" "dev" ];
            postInstall = ''
              moveToOutput "include" "$dev"
            '';
          });
          mkDevShell = { libtorrentPackage, boostPackage ? pkgs.boost }: pkgs.mkShell {
          packages = with pkgs; [
            cmake
            ninja
            pkg-config
            boostPackage
            libtorrentPackage
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
        in {
          default = mkDevShell { libtorrentPackage = pkgs.libtorrent-rasterbar; };
          libtorrent-1_2 = mkDevShell {
            libtorrentPackage = libtorrent12;
            boostPackage = pkgs.boost186;
          };
        });
    };
}
