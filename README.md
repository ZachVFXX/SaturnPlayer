# Music Player in C 

WORK IN PROGRESS, FEEL FREE TO CONTRIBUTE

Minimal desktop music player written in C.

Supports downloading songs directly from **YouTube Music**, MP3/WAV/OGG/FLAC playback, metadata extraction, and embedded cover rendering.

---

## Features

* MP3/WAV/OGG/FLAC playback
* Download songs from YouTube Music
* Metadata extraction (artist, album, cover)
* Embedded album art rendering
* Threaded song loading

---

## Dependencies

* [FFmpeg](https://github.com/FFmpeg/FFmpeg) (`libavformat`)
* [raylib](https://github.com/raysan5/raylib)
* [Clay](https://github.com/nicbarker/clay)
* [yt-dlp](https://github.com/yt-dlp/yt-dlp)
  
---

## Build

Requirements:

* C compiler (gcc / clang)
* `make`
* FFmpeg development libraries
* raylib
* yt-dlp downloaded and set in path

```bash
make release && ./main_release ~/musics_folders/
```

---

## Size (778 MP3s)

* Debug:          380MB RAM - 73MB binary
* Release:         92MB RAM - 73MB binary
* Release + strip: 70MB RAM - 17MB binary

---

## TODO

* Custom raylib `config.h` for smaller binaries
* Arena linked list (remove `realloc`)
* Custom install script (build raylib + FFmpeg)

---

## License

This project is licensed under the MIT License.
See the [LICENSE](./LICENSE) file for details.
