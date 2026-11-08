#include "font.h"

#include "gfx.h"

typedef struct {
    char c;
    const char *rows; /* 5 rows x 3 columns, '#' = pixel */
} glyph_t;

static const glyph_t k_glyphs[] = {
    {'0', "####.##.##.####"},
    {'1', ".#.##..#..#.###"},
    {'2', "###..#####..###"},
    {'3', "###..####..####"},
    {'4', "#.##.####..#..#"},
    {'5', "####..###..####"},
    {'6', "####..####.####"},
    {'7', "###..#..#..#..#"},
    {'8', "####.#####.####"},
    {'9', "####.####..####"},
    {'A', ".#.#.#####.##.#"},
    {'B', "##.#.###.#.###."},
    {'C', "####..#..#..###"},
    {'D', "##.#.##.##.###."},
    {'E', "####..##.#..###"},
    {'F', "####..##.#..#.."},
    {'G', "####..#.##.####"},
    {'H', "#.##.#####.##.#"},
    {'I', "###.#..#..#.###"},
    {'J', "..#..#..##.####"},
    {'K', "#.##.###.#.##.#"},
    {'L', "#..#..#..#..###"},
    {'M', "#.########.##.#"},
    {'N', "##.#.##.##.##.#"},
    {'O', ".#.#.##.##.#.#."},
    {'P', "####.#####..#.."},
    {'Q', "####.##.####..#"},
    {'R', "####.###.#.##.#"},
    {'S', ".###...#...###."},
    {'T', "###.#..#..#..#."},
    {'U', "#.##.##.##.####"},
    {'V', "#.##.##.##.#.#."},
    {'W', "#.##.########.#"},
    {'X', "#.##.#.#.#.##.#"},
    {'Y', "#.##.#.#..#..#."},
    {'Z', "###..#.#.#..###"},
    {':', "....#.....#...."},
    {'!', ".#..#..#.....#."},
    {'-', "......###......"},
    {'/', "..#..#.#.#..#.."},
    {'.', ".............#."},
    {'+', "....#.###.#...."},
    {' ', "..............."},
};

static const char *rows_for(char c)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    for (unsigned i = 0; i < sizeof(k_glyphs) / sizeof(k_glyphs[0]); i++)
        if (k_glyphs[i].c == c) return k_glyphs[i].rows;
    return k_glyphs[sizeof(k_glyphs) / sizeof(k_glyphs[0]) - 1].rows;
}

int font_width(const char *s, int scale)
{
    int n = 0;
    while (s[n]) n++;
    return n ? (n * 4 - 1) * scale : 0;
}

void font_draw(uint16_t *fb, int x, int y, int scale, uint16_t color, const char *s)
{
    for (; *s; s++, x += 4 * scale) {
        const char *g = rows_for(*s);
        for (int i = 0; g[i] && i < 15; i++)
            if (g[i] == '#') gfx_fill_rect(fb, x + (i % 3) * scale, y + (i / 3) * scale, scale, scale, color);
    }
}

void font_draw_centered(uint16_t *fb, int cx, int y, int scale, uint16_t color, const char *s)
{
    font_draw(fb, cx - font_width(s, scale) / 2, y, scale, color, s);
}
