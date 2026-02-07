Music player in C

TODO: Custom https://github.com/raysan5/raylib/blob/master/src/config.h with custom raylib flavor for smaller binaries.

build test command : 
make


802 MO -> 408MO for 390 mp3


yt-dlp \
  -f bestaudio \
  -x \
  --audio-format mp3 \
  --audio-quality 0 \
  --embed-metadata \
  --embed-thumbnail \
  --add-metadata \
  --no-progress \
  --print after_move:filepath \
   "https://music.youtube.com/watch?v=QTHV3cuXnRY&si=lr7zSuqr-e8erE7b"
TODO: (Query the playlist is the only reliable way to get all the tag)   

Using: 
https://github.com/larsbs/id3v2lib 
https://github.com/raysan5/raylib
https://github.com/nicbarker/clay
https://fonts.google.com/icons
