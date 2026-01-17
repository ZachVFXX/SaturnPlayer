Music player in C

TODO: Custom https://github.com/raysan5/raylib/blob/master/src/config.h with custom raylib flavor for smaller binaries.

build test command : 
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address -g \
    main.c mpris/mpris.c \
    -o main \
    -I/usr/local/include \
    $(pkg-config --cflags gio-2.0) \
    -L../raylib/src -lraylib \
    -L../id3v2lib/src -lid3v2lib \
    $(pkg-config --libs gio-2.0) \
    -lGL -lX11 -lm -lpthread -ldl -lrt && ./main /home/zach/.pymusicterm/musics/


yt-dlp \
  -f bestaudio \
  -x \
  --audio-format mp3 \
  --audio-quality 0 \
  --embed-metadata \
  --embed-thumbnail \
  --add-metadata \
   "https://music.youtube.com/playlist?list=OLAK5uy_nhWRCTbsDHSN4WgNAf0loQDcuuqrCLBn8"
(Query the playlist is the only reliable way to get all the tag)   

Using: 
https://github.com/larsbs/id3v2lib 
https://github.com/raysan5/raylib
https://github.com/nicbarker/clay
