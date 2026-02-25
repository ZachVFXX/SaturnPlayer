{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  packages = with pkgs; [
    gcc
    clang
    gnumake
    pkg-config
    ffmpeg
    raylib
    freetype
    fontconfig
    yt-dlp
    deno
  ];
}
