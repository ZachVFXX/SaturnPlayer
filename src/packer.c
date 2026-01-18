#include "raylib.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#define ASSETS_PATH "../assets/fonts/Poppins/"


Font LoadFontWithExtendedUnicode(const char* fileName, int fontSize) {
    #define MAX_GLYPHS 2048
    int *codepoints = (int*)malloc(MAX_GLYPHS * sizeof(int));
    int index = 0;

    // Basic Latin (0x0020 - 0x007F)
    for (int i = 0x0020; i <= 0x007F && index < MAX_GLYPHS; i++) {
        codepoints[index++] = i;
    }

    // Latin-1 Supplement (0x0080 - 0x00FF) - French, Spanish, etc.
    for (int i = 0x0080; i <= 0x00FF && index < MAX_GLYPHS; i++) {
        codepoints[index++] = i;
    }

    // Latin Extended-A (0x0100 - 0x017F) - More European languages
    for (int i = 0x0100; i <= 0x017F && index < MAX_GLYPHS; i++) {
        codepoints[index++] = i;
    }

    // Latin Extended-B (0x0180 - 0x024F)
    for (int i = 0x0180; i <= 0x024F && index < MAX_GLYPHS; i++) {
        codepoints[index++] = i;
    }

    // General Punctuation (0x2000 - 0x206F)
    for (int i = 0x2000; i <= 0x206F && index < MAX_GLYPHS; i++) {
        codepoints[index++] = i;
    }

    // Currency Symbols (0x20A0 - 0x20CF)
    for (int i = 0x20A0; i <= 0x20CF && index < MAX_GLYPHS; i++) {
        codepoints[index++] = i;
    }

    // Geometric Shapes (0x25A0 - 0x25FF)
    for (int i = 0x25A0; i <= 0x25FF && index < MAX_GLYPHS; i++) {
        codepoints[index++] = i;
    }

    // Miscellaneous Symbols (0x2600 - 0x26FF)
    for (int i = 0x2600; i <= 0x26FF && index < MAX_GLYPHS; i++) {
        codepoints[index++] = i;
    }

    // Box Drawing (0x2500 - 0x257F)
    for (int i = 0x2500; i <= 0x257F && index < MAX_GLYPHS; i++) {
        codepoints[index++] = i;
    }

    // Mathematical Operators (0x2200 - 0x22FF)
    for (int i = 0x2200; i <= 0x22FF && index < MAX_GLYPHS; i++) {
        codepoints[index++] = i;
    }

    Font font = LoadFontEx(fileName, fontSize, codepoints, index);
    free(codepoints);

    return font;
}


int main(void) {
    InitWindow(1, 1, "Packer");
    FilePathList assets = LoadDirectoryFiles(ASSETS_PATH);
    TraceLog(LOG_INFO, "PACKER: FOUND %d files in %s.", assets.count, ASSETS_PATH);
    for (size_t i = 0; i < assets.count; i++) {
        char* path = assets.paths[i];
        const char* final_path = TextFormat(ASSETS_PATH"%s.h", TextReplace(GetFileNameWithoutExt(path), "-", "_"));
        if (IsFileExtension(path, ".png")) {
            Image image = LoadImage(path);
            ExportImageAsCode(image, final_path);
            TraceLog(LOG_INFO, "PACKER: IMAGE Generated successfuly at %s.", final_path);
        } else if (IsFileExtension(path, ".ttf")) {
            Font font = LoadFontWithExtendedUnicode(path, 24);
            ExportFontAsCode(font, final_path);
            TraceLog(LOG_INFO, "PACKER: FONT Generated successfuly at %s.", final_path);
        }
    }
    CloseWindow();
}
