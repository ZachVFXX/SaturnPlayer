Music player in C

build test command  
make

TODO: Custom https://github.com/raysan5/raylib/blob/master/src/config.h with custom raylib flavor for smaller binaries.
TODO: Make arena linked list for no realloc
TODO: custom install script that build raylib and ffmpeg

380MO for 778 mp3s in debug (73mo exec)
92MO  for 778 mp3s in release (73mo exec)
70MO  for 778 mp3s in release + strip (17mo exec)

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
https://github.com/larsbs/id3v2lib -> https://codeberg.org/tenacityteam/libid3tag.git -> ffmpeg avformat
https://github.com/raysan5/raylib
https://github.com/nicbarker/clay
https://fonts.google.com/icons
