/**
 *  @filename   :   epdpaint.cpp
 *  @brief      :   Paint tools
 *  @author     :   Yehui from Waveshare
 *  
 *  Copyright (C) Waveshare     September 9 2017
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documnetation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to  whom the Software is
 * furished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS OR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <pgmspace.h>
#include "epdpaint.h"

namespace {
enum CzechDiacritic : uint8_t {
    kDiaNone = 0,
    kDiaAcute,
    kDiaCaron,
    kDiaRing,
};

enum BlockGlyph : uint8_t {
    kBlockNone = 0,
    kBlockUpperHalf,
    kBlockLowerHalf,
    kBlockLeftHalf,
    kBlockRightHalf,
    kBlockFull,
};

bool decodeUtf8BlockGlyph(const char *text, uint8_t &glyph) {
    glyph = kBlockNone;
    const uint8_t b0 = static_cast<uint8_t>(text[0]);
    const uint8_t b1 = static_cast<uint8_t>(text[1]);
    const uint8_t b2 = static_cast<uint8_t>(text[2]);

    if (b0 != 0xE2 || b1 != 0x96) {
        return false;
    }

    if (b2 == 0x80) {
        glyph = kBlockUpperHalf;  // U+2580
        return true;
    }
    if (b2 == 0x84) {
        glyph = kBlockLowerHalf;  // U+2584
        return true;
    }
    if (b2 == 0x8C) {
        glyph = kBlockLeftHalf;  // U+258C
        return true;
    }
    if (b2 == 0x90) {
        glyph = kBlockRightHalf;  // U+2590
        return true;
    }
    if (b2 == 0x88) {
        glyph = kBlockFull;  // U+2588
        return true;
    }

    return false;
}

void drawBlockGlyph(Paint *paint, int x, int y, sFONT *font, uint8_t glyph, int colored) {
    if (glyph == kBlockNone) {
        return;
    }

    int startX = x;
    int endX = x + font->Width - 1;
    int startY = y;
    int endY = y + font->Height - 1;

    if (glyph == kBlockUpperHalf) {
        endY = y + (font->Height / 2) - 1;
    } else if (glyph == kBlockLowerHalf) {
        startY = y + (font->Height / 2);
    } else if (glyph == kBlockLeftHalf) {
        endX = x + (font->Width / 2) - 1;
    } else if (glyph == kBlockRightHalf) {
        startX = x + (font->Width / 2);
    }

    if (endX < startX || endY < startY) {
        return;
    }

    paint->DrawFilledRectangle(startX, startY, endX, endY, colored);
}

void drawDiacriticMark(Paint *paint, int x, int y, sFONT *font, uint8_t diacritic, int colored) {
    if (diacritic == kDiaNone) {
        return;
    }

    const int mid = x + font->Width / 2;
    const int top = y + 1;

    if (diacritic == kDiaAcute) {
        paint->DrawPixel(mid - 1, top + 1, colored);
        paint->DrawPixel(mid, top, colored);
        paint->DrawPixel(mid + 1, top - 1, colored);
        return;
    }

    if (diacritic == kDiaCaron) {
        paint->DrawPixel(mid - 2, top - 1, colored);
        paint->DrawPixel(mid - 1, top, colored);
        paint->DrawPixel(mid, top + 1, colored);
        paint->DrawPixel(mid + 1, top, colored);
        paint->DrawPixel(mid + 2, top - 1, colored);
        return;
    }

    if (diacritic == kDiaRing) {
        paint->DrawPixel(mid - 1, top, colored);
        paint->DrawPixel(mid + 1, top, colored);
        paint->DrawPixel(mid, top - 1, colored);
        paint->DrawPixel(mid, top + 1, colored);
    }
}

bool decodeUtf8CzechGlyph(const char *&text, char &baseAscii, uint8_t &diacritic) {
    const uint8_t b0 = static_cast<uint8_t>(*text);
    if (b0 == 0) {
        return false;
    }

    diacritic = kDiaNone;
    if (b0 < 0x80) {
        baseAscii = static_cast<char>(b0);
        ++text;
        return true;
    }

    const uint8_t b1 = static_cast<uint8_t>(*(text + 1));

    if (b0 == 0xC3) {
        if (b1 == 0xA1) { baseAscii = 'a'; diacritic = kDiaAcute; text += 2; return true; }
        if (b1 == 0x81) { baseAscii = 'A'; diacritic = kDiaAcute; text += 2; return true; }
        if (b1 == 0xA9) { baseAscii = 'e'; diacritic = kDiaAcute; text += 2; return true; }
        if (b1 == 0x89) { baseAscii = 'E'; diacritic = kDiaAcute; text += 2; return true; }
        if (b1 == 0xAD) { baseAscii = 'i'; diacritic = kDiaAcute; text += 2; return true; }
        if (b1 == 0x8D) { baseAscii = 'I'; diacritic = kDiaAcute; text += 2; return true; }
        if (b1 == 0xB3) { baseAscii = 'o'; diacritic = kDiaAcute; text += 2; return true; }
        if (b1 == 0x93) { baseAscii = 'O'; diacritic = kDiaAcute; text += 2; return true; }
        if (b1 == 0xBA) { baseAscii = 'u'; diacritic = kDiaAcute; text += 2; return true; }
        if (b1 == 0x9A) { baseAscii = 'U'; diacritic = kDiaAcute; text += 2; return true; }
        if (b1 == 0xBD) { baseAscii = 'y'; diacritic = kDiaAcute; text += 2; return true; }
        if (b1 == 0x9D) { baseAscii = 'Y'; diacritic = kDiaAcute; text += 2; return true; }
    }

    if (b0 == 0xC4) {
        if (b1 == 0x8D) { baseAscii = 'c'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0x87) { baseAscii = 'C'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0x8F) { baseAscii = 'd'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0x8E) { baseAscii = 'D'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0x9B) { baseAscii = 'e'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0x9A) { baseAscii = 'E'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0xA5) { baseAscii = 't'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0xA4) { baseAscii = 'T'; diacritic = kDiaCaron; text += 2; return true; }
    }

    if (b0 == 0xC5) {
        if (b1 == 0x88) { baseAscii = 'n'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0x87) { baseAscii = 'N'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0x99) { baseAscii = 'r'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0x98) { baseAscii = 'R'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0xA1) { baseAscii = 's'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0xA0) { baseAscii = 'S'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0xA5) { baseAscii = 't'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0xA4) { baseAscii = 'T'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0xBE) { baseAscii = 'z'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0xBD) { baseAscii = 'Z'; diacritic = kDiaCaron; text += 2; return true; }
        if (b1 == 0xAF) { baseAscii = 'u'; diacritic = kDiaRing; text += 2; return true; }
        if (b1 == 0xAE) { baseAscii = 'U'; diacritic = kDiaRing; text += 2; return true; }
    }

    baseAscii = '?';
    if ((b0 & 0xE0) == 0xC0 && b1 != 0) {
        text += 2;
    } else {
        ++text;
    }
    return true;
}
}  // namespace

Paint::Paint(unsigned char* image, int width, int height) {
    this->rotate = ROTATE_0;
    this->image = image;
    /* 1 byte = 8 pixels, so the width should be the multiple of 8 */
    this->width = width % 8 ? width + 8 - (width % 8) : width;
    this->height = height;
}

Paint::~Paint() {
}

/**
 *  @brief: clear the image
 */
void Paint::Clear(int colored) {
    for (int x = 0; x < this->width; x++) {
        for (int y = 0; y < this->height; y++) {
            DrawAbsolutePixel(x, y, colored);
        }
    }
}

/**
 *  @brief: this draws a pixel by absolute coordinates.
 *          this function won't be affected by the rotate parameter.
 */
void Paint::DrawAbsolutePixel(int x, int y, int colored) {
    if (x < 0 || x >= this->width || y < 0 || y >= this->height) {
        return;
    }
    if (IF_INVERT_COLOR) {
        if (colored) {
            image[(x + y * this->width) / 8] |= 0x80 >> (x % 8);
        } else {
            image[(x + y * this->width) / 8] &= ~(0x80 >> (x % 8));
        }
    } else {
        if (colored) {
            image[(x + y * this->width) / 8] &= ~(0x80 >> (x % 8));
        } else {
            image[(x + y * this->width) / 8] |= 0x80 >> (x % 8);
        }
    }
}

/**
 *  @brief: Getters and Setters
 */
unsigned char* Paint::GetImage(void) {
    return this->image;
}

int Paint::GetWidth(void) {
    return this->width;
}

void Paint::SetWidth(int width) {
    this->width = width % 8 ? width + 8 - (width % 8) : width;
}

int Paint::GetHeight(void) {
    return this->height;
}

void Paint::SetHeight(int height) {
    this->height = height;
}

int Paint::GetRotate(void) {
    return this->rotate;
}

void Paint::SetRotate(int rotate){
    this->rotate = rotate;
}

/**
 *  @brief: this draws a pixel by the coordinates
 */
void Paint::DrawPixel(int x, int y, int colored) {
    int point_temp;
    if (this->rotate == ROTATE_0) {
        if(x < 0 || x >= this->width || y < 0 || y >= this->height) {
            return;
        }
        DrawAbsolutePixel(x, y, colored);
    } else if (this->rotate == ROTATE_90) {
        if(x < 0 || x >= this->height || y < 0 || y >= this->width) {
          return;
        }
        point_temp = x;
        x = this->width - y;
        y = point_temp;
        DrawAbsolutePixel(x, y, colored);
    } else if (this->rotate == ROTATE_180) {
        if(x < 0 || x >= this->width || y < 0 || y >= this->height) {
          return;
        }
        x = this->width - x;
        y = this->height - y;
        DrawAbsolutePixel(x, y, colored);
    } else if (this->rotate == ROTATE_270) {
        if(x < 0 || x >= this->height || y < 0 || y >= this->width) {
          return;
        }
        point_temp = x;
        x = y;
        y = this->height - point_temp;
        DrawAbsolutePixel(x, y, colored);
    }
}

/**
 *  @brief: this draws a charactor on the frame buffer but not refresh
 */
void Paint::DrawCharAt(int x, int y, char ascii_char, sFONT* font, int colored) {
    int i, j;
    unsigned int char_offset = (ascii_char - ' ') * font->Height * (font->Width / 8 + (font->Width % 8 ? 1 : 0));
    const unsigned char* ptr = &font->table[char_offset];

    for (j = 0; j < font->Height; j++) {
        for (i = 0; i < font->Width; i++) {
            if (pgm_read_byte(ptr) & (0x80 >> (i % 8))) {
                DrawPixel(x + i, y + j, colored);
            }
            if (i % 8 == 7) {
                ptr++;
            }
        }
        if (font->Width % 8 != 0) {
            ptr++;
        }
    }
}

/**
*  @brief: this displays a string on the frame buffer but not refresh
*/
void Paint::DrawStringAt(int x, int y, const char* text, sFONT* font, int colored) {
    const char* p_text = text;
    unsigned int counter = 0;
    int refcolumn = x;
    
    /* Send the string character by character on EPD */
    while (*p_text != 0) {
        /* Display one character on EPD */
        DrawCharAt(refcolumn, y, *p_text, font, colored);
        /* Decrement the column position by 16 */
        refcolumn += font->Width;
        /* Point on the next character */
        p_text++;
        counter++;
    }
}

void Paint::DrawStringAtUtf8(int x, int y, const char* text, sFONT* font, int colored) {
    const char* p_text = text;
    int refcolumn = x;

    while (*p_text != 0) {
        uint8_t blockGlyph = kBlockNone;
        if (decodeUtf8BlockGlyph(p_text, blockGlyph)) {
            drawBlockGlyph(this, refcolumn, y, font, blockGlyph, colored);
            p_text += 3;
            refcolumn += font->Width;
            continue;
        }

        char base = '?';
        uint8_t diacritic = kDiaNone;
        if (!decodeUtf8CzechGlyph(p_text, base, diacritic)) {
            break;
        }

        DrawCharAt(refcolumn, y, base, font, colored);
        drawDiacriticMark(this, refcolumn, y, font, diacritic, colored);
        refcolumn += font->Width;
    }
}

void Paint::DrawStringAtUtf8Compact(int x, int y, const char* text, sFONT* font, int colored, int glyphAdvance) {
    if (glyphAdvance <= 0) {
        glyphAdvance = font->Width;
    }

    const char* p_text = text;
    int refcolumn = x;

    while (*p_text != 0) {
        uint8_t blockGlyph = kBlockNone;
        if (decodeUtf8BlockGlyph(p_text, blockGlyph)) {
            drawBlockGlyph(this, refcolumn, y, font, blockGlyph, colored);
            p_text += 3;
            refcolumn += glyphAdvance;
            continue;
        }

        char base = '?';
        uint8_t diacritic = kDiaNone;
        if (!decodeUtf8CzechGlyph(p_text, base, diacritic)) {
            break;
        }

        DrawCharAt(refcolumn, y, base, font, colored);
        drawDiacriticMark(this, refcolumn, y, font, diacritic, colored);
        refcolumn += glyphAdvance;
    }
}

/**
*  @brief: this draws a line on the frame buffer
*/
void Paint::DrawLine(int x0, int y0, int x1, int y1, int colored) {
    /* Bresenham algorithm */
    int dx = x1 - x0 >= 0 ? x1 - x0 : x0 - x1;
    int sx = x0 < x1 ? 1 : -1;
    int dy = y1 - y0 <= 0 ? y1 - y0 : y0 - y1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while((x0 != x1) && (y0 != y1)) {
        DrawPixel(x0, y0 , colored);
        if (2 * err >= dy) {     
            err += dy;
            x0 += sx;
        }
        if (2 * err <= dx) {
            err += dx; 
            y0 += sy;
        }
    }
}

/**
*  @brief: this draws a horizontal line on the frame buffer
*/
void Paint::DrawHorizontalLine(int x, int y, int line_width, int colored) {
    int i;
    for (i = x; i < x + line_width; i++) {
        DrawPixel(i, y, colored);
    }
}

/**
*  @brief: this draws a vertical line on the frame buffer
*/
void Paint::DrawVerticalLine(int x, int y, int line_height, int colored) {
    int i;
    for (i = y; i < y + line_height; i++) {
        DrawPixel(x, i, colored);
    }
}

/**
*  @brief: this draws a rectangle
*/
void Paint::DrawRectangle(int x0, int y0, int x1, int y1, int colored) {
    int min_x, min_y, max_x, max_y;
    min_x = x1 > x0 ? x0 : x1;
    max_x = x1 > x0 ? x1 : x0;
    min_y = y1 > y0 ? y0 : y1;
    max_y = y1 > y0 ? y1 : y0;
    
    DrawHorizontalLine(min_x, min_y, max_x - min_x + 1, colored);
    DrawHorizontalLine(min_x, max_y, max_x - min_x + 1, colored);
    DrawVerticalLine(min_x, min_y, max_y - min_y + 1, colored);
    DrawVerticalLine(max_x, min_y, max_y - min_y + 1, colored);
}

/**
*  @brief: this draws a filled rectangle
*/
void Paint::DrawFilledRectangle(int x0, int y0, int x1, int y1, int colored) {
    int min_x, min_y, max_x, max_y;
    int i;
    min_x = x1 > x0 ? x0 : x1;
    max_x = x1 > x0 ? x1 : x0;
    min_y = y1 > y0 ? y0 : y1;
    max_y = y1 > y0 ? y1 : y0;
    
    for (i = min_x; i <= max_x; i++) {
      DrawVerticalLine(i, min_y, max_y - min_y + 1, colored);
    }
}

/**
*  @brief: this draws a circle
*/
void Paint::DrawCircle(int x, int y, int radius, int colored) {
    /* Bresenham algorithm */
    int x_pos = -radius;
    int y_pos = 0;
    int err = 2 - 2 * radius;
    int e2;

    do {
        DrawPixel(x - x_pos, y + y_pos, colored);
        DrawPixel(x + x_pos, y + y_pos, colored);
        DrawPixel(x + x_pos, y - y_pos, colored);
        DrawPixel(x - x_pos, y - y_pos, colored);
        e2 = err;
        if (e2 <= y_pos) {
            err += ++y_pos * 2 + 1;
            if(-x_pos == y_pos && e2 <= x_pos) {
              e2 = 0;
            }
        }
        if (e2 > x_pos) {
            err += ++x_pos * 2 + 1;
        }
    } while (x_pos <= 0);
}

/**
*  @brief: this draws a filled circle
*/
void Paint::DrawFilledCircle(int x, int y, int radius, int colored) {
    /* Bresenham algorithm */
    int x_pos = -radius;
    int y_pos = 0;
    int err = 2 - 2 * radius;
    int e2;

    do {
        DrawPixel(x - x_pos, y + y_pos, colored);
        DrawPixel(x + x_pos, y + y_pos, colored);
        DrawPixel(x + x_pos, y - y_pos, colored);
        DrawPixel(x - x_pos, y - y_pos, colored);
        DrawHorizontalLine(x + x_pos, y + y_pos, 2 * (-x_pos) + 1, colored);
        DrawHorizontalLine(x + x_pos, y - y_pos, 2 * (-x_pos) + 1, colored);
        e2 = err;
        if (e2 <= y_pos) {
            err += ++y_pos * 2 + 1;
            if(-x_pos == y_pos && e2 <= x_pos) {
                e2 = 0;
            }
        }
        if(e2 > x_pos) {
            err += ++x_pos * 2 + 1;
        }
    } while(x_pos <= 0);
}

/* END OF FILE */























