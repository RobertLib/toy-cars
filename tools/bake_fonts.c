#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: bake_fonts font.ttf output.tcf\n");
        return 1;
    }
    if (!TTF_Init())
        return 1;
    TTF_Font *font = TTF_OpenFont(argv[1], 72);
    if (!font) {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    const int w = 1536, h = 768, cell = 96;
    unsigned char *atlas = calloc((size_t)w * h, 1);
    float glyphs[96][7] = {0};
    for (int c = 32; c < 128; c++) {
        int minx, maxx, miny, maxy, adv;
        TTF_GetGlyphMetrics(font, c, &minx, &maxx, &miny, &maxy, &adv);
        SDL_Surface *s = TTF_RenderGlyph_Blended(font, c, (SDL_Color){255, 255, 255, 255});
        if (!s)
            continue;
        SDL_Surface *rgba = SDL_ConvertSurface(s, SDL_PIXELFORMAT_RGBA32);
        int x = ((c - 32) % 16) * cell + 2, y = ((c - 32) / 16) * 128 + 2;
        if (rgba->w > cell - 4 || rgba->h > 124) {
            fprintf(stderr, "Glyph overflow\n");
            return 1;
        }
        for (int j = 0; j < rgba->h; j++)
            for (int i = 0; i < rgba->w; i++)
                atlas[(y + j) * w + x + i] =
                    ((unsigned char *)rgba->pixels)[j * rgba->pitch + i * 4 + 3];
        float *g = glyphs[c - 32];
        g[0] = (float)x / w;
        g[1] = (float)y / h;
        g[2] = (float)(x + rgba->w) / w;
        g[3] = (float)(y + rgba->h) / h;
        g[4] = (float)rgba->w;
        g[5] = (float)rgba->h;
        g[6] = (float)adv;
        SDL_DestroySurface(rgba);
        SDL_DestroySurface(s);
    }
    FILE *f = fopen(argv[2], "wb");
    if (!f)
        return 1;
    fwrite("TCF1", 1, 4, f);
    uint32_t dims[2] = {(uint32_t)w, (uint32_t)h};
    fwrite(dims, 4, 2, f);
    fwrite(glyphs, sizeof(glyphs), 1, f);
    fwrite(atlas, 1, (size_t)w * h, f);
    fclose(f);
    free(atlas);
    TTF_CloseFont(font);
    TTF_Quit();
    return 0;
}
