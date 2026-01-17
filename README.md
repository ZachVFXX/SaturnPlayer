Music player in C

TODO: Custom https://github.com/raysan5/raylib/blob/master/src/config.h with custom raylib flavor for smaller binaries.

build test command : 
gcc -std=c99 -Wall -Wextra -Werror -fsanitize=address -g \
    -o main main.c \
    -I/usr/local/include \
    -L../raylib/src -lraylib \
    -L/usr/local/lib -lid3v2lib \
    -lGL -lX11 -lm -lpthread -ldl -lrt
    
build release command:

gcc -std=c99 -Wall -Wextra -Werror -O3 \
    -o main main.c \
    -I/usr/local/include \
    -L../raylib/src -lraylib \
    -L/usr/local/lib -lid3v2lib \
    -lGL -lX11 -lm -lpthread -ldl -lrt


Using: 
https://github.com/larsbs/id3v2lib 
https://github.com/raysan5/raylib
https://github.com/nicbarker/clay
