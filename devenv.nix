{ pkgs, lib, ... }:

{
  packages = [
    pkgs.git
    pkgs.just
    pkgs.platformio
    pkgs.mosquitto
    pkgs.curl
    pkgs.bitwarden-cli
  ];

  languages.python = {
    enable = true;
    uv.enable = true;
  };

  languages.javascript = {
    enable = true;
    npm.enable = true;
  };

  env = {
    SECRETSPEC_PROVIDER = "bw://";
    LD_LIBRARY_PATH = lib.makeLibraryPath [ pkgs.stdenv.cc.cc.lib pkgs.zlib ];
  };

  enterShell = ''
    export PATH="$PWD/.bin:$PATH"
  '';
}
