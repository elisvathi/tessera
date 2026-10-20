{ pkgs ? import <nixpkgs> { } }:

let
  inherit (pkgs) lib stdenv;
  sdkVersion = "2.6.6"; # Keep in sync with .github/workflows/build.yml.
  sdkPlatforms = {
    x86_64-linux = {
      platform = "lin-x64";
      sha256 = "420da2452def7b195f98e3a0650c35f1e0f058a98a71840b4347d3d203618532";
    };
    x86_64-darwin = {
      platform = "mac-x64";
      sha256 = "9b8b0d7582ca25fac879f8f64de40f2481df2fd903b65f3abb5d6801689060ec";
    };
    aarch64-darwin = {
      platform = "mac-arm64";
      sha256 = "29414e52417992cbafa47e30f947c3c0c7a34e5c424bb83c5a0af8c24840481f";
    };
  };
  sdkPlatform = sdkPlatforms.${stdenv.hostPlatform.system}
    or (throw "Tessera's Rack SDK supports x86_64-linux, x86_64-darwin and aarch64-darwin.");
  sdkArchive = pkgs.fetchurl {
    url = "https://vcvrack.com/downloads/Rack-SDK-${sdkVersion}-${sdkPlatform.platform}.zip";
    inherit (sdkPlatform) sha256;
  };
  rackSdk = pkgs.runCommand "rack-sdk-${sdkVersion}-${sdkPlatform.platform}" {
    nativeBuildInputs = [ pkgs.unzip ];
  } ''
    unzip -q ${sdkArchive}
    mv Rack-SDK "$out"
    # The Linux archive is group-writable; Nix rejects group/world-writable outputs.
    chmod -R go-w "$out"
  '';
in
pkgs.mkShell {
  name = "tessera";

  nativeBuildInputs = with pkgs; [
    gnumake
    pkg-config
    jq
    zstd
    gnutar
    coreutils
    rsync
    unzip
    git
    clang-tools
  ];
  buildInputs = [ pkgs.jansson ] ++ lib.optionals stdenv.hostPlatform.isLinux [ pkgs.libGL ];

  # Rack uses GCC on Linux (including -fno-gnu-unique) and Clang on macOS.
  # Use the same wrapped compiler for the sanitizer-enabled unit tests.
  TEST_CXX = "${stdenv.cc}/bin/c++";

  shellHook = ''
    export RACK_DIR="''${RACK_DIR:-${rackSdk}}"
  '' + lib.optionalString stdenv.hostPlatform.isDarwin ''
    # Rack's packaging step signs the dylib with Apple's host signing tool.
    export CODESIGN="/usr/bin/codesign -f -s -"
  '';
}
