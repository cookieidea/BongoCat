#include "ui_font.h"
#include "bongo_cat/file.h"
#include "bongo_cat/path.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef BONGO_CAT_HAVE_FONTCONFIG
#include <fontconfig/fontconfig.h>
#endif

static bool readable(const char *path) {
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) return false;
    fclose(file);
    return true;
}

#ifdef BONGO_CAT_HAVE_FONTCONFIG
static bool rasterizable(const char *path) {
    FILE *file = bongo_cat_file_open(path, "rb");
    if (!file) return false;
    bool ok = false;
    unsigned char header[16];
    if (fread(header, 1, sizeof header, file) == sizeof header) {
        long base = 0;
        if (memcmp(header, "ttcf", 4) == 0)
            base = (long)((unsigned long)header[12] << 24
                | (unsigned long)header[13] << 16
                | (unsigned long)header[14] << 8 | header[15]);
        unsigned char count[2];
        if (fseek(file, base + 4, SEEK_SET) == 0
            && fread(count, 1, 2, file) == 2) {
            unsigned tables = ((unsigned)count[0] << 8) | count[1];
            if (fseek(file, base + 12, SEEK_SET) == 0) {
                ok = true;
                for (unsigned i = 0; i < tables && ok; ++i) {
                    unsigned char record[16];
                    size_t got = fread(record, 1, sizeof record, file);
                    if (got != sizeof record) {
                        ok = false;
                        break;
                    }
                    if (memcmp(record, "CFF2", 4) == 0) ok = false;
                }
            }
        }
    }
    fclose(file);
    return ok;
}

static bool fontconfig_lookup(char *path, size_t capacity, const char *family,
    const char *language, unsigned int probe, bool bold) {
    FcPattern *pattern = FcNameParse((const FcChar8 *)family);
    if (!pattern) return false;
    if (language)
        FcPatternAddString(pattern, FC_LANG, (const FcChar8 *)language);
    if (bold)
        FcPatternAddInteger(pattern, FC_WEIGHT, FC_WEIGHT_BOLD);
    FcConfigSubstitute(NULL, pattern, FcMatchPattern);
    FcDefaultSubstitute(pattern);
    FcResult result;
    FcFontSet *set = FcFontSort(NULL, pattern, FcFalse, NULL, &result);
    FcPatternDestroy(pattern);
    if (!set) return false;
    bool found = false;
    for (int i = 0; i < set->nfont && !found; ++i) {
        FcPattern *font = set->fonts[i];
        FcCharSet *charset = NULL;
        FcChar8 *file = NULL;
        if (probe && (FcPatternGetCharSet(font, FC_CHARSET, 0, &charset)
                != FcResultMatch || !charset
                || !FcCharSetHasChar(charset, probe)))
            continue;
        if (FcPatternGetString(font, FC_FILE, 0, &file) != FcResultMatch
            || !file)
            continue;
        if (!rasterizable((const char *)file)) continue;
        snprintf(path, capacity, "%s", (const char *)file);
        found = true;
    }
    FcFontSetDestroy(set);
    return found;
}

static bool fontconfig_family(char *path, size_t capacity,
    const char *const *families, size_t count, const char *language,
    unsigned int probe, bool bold) {
    for (size_t i = 0; i < count; ++i)
        if (fontconfig_lookup(path, capacity, families[i], language, probe,
                bold))
            return true;
    return false;
}
#endif

const char *bongo_cat_ui_system_font(char *path, size_t capacity, bool multilingual) {
#ifdef _WIN32
    const char *windows = SDL_getenv("WINDIR");
    if (!windows) windows = SDL_getenv("SystemRoot");
    if (windows) {
        const char *multi[] = {"Fonts/msyh.ttc", "Fonts/msyhl.ttc"};
        const char *latin[] = {"Fonts/segoeui.ttf", "Fonts/msyhl.ttc"};
        const char **candidates = multilingual ? multi : latin;
        for (size_t i = 0; i < 2; ++i) {
            bongo_cat_path_join(path, capacity, windows, candidates[i]);
            if (readable(path)) return path;
        }
    }
#elif defined(__APPLE__)
    const char *candidates[] = {multilingual ? "/System/Library/Fonts/PingFang.ttc" :
        "/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc"};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (!readable(candidates[i])) continue;
        snprintf(path, capacity, "%s", candidates[i]);
        return path;
    }
#else
    /* Every Linux distribution ships fonts to different directories, so the
       candidate lists must cover the common layouts. CJK-capable fonts come
       first for multilingual fallbacks; the CJK-less DejaVu remains last as a
       final resort so Chinese text never silently falls back to it. */
#ifdef BONGO_CAT_HAVE_FONTCONFIG
    {
        static const char *const families[] = {"Noto Sans CJK SC",
            "Noto Sans CJK TC", "Noto Sans CJK JP", "Source Han Sans SC",
            "WenQuanYi Micro Hei", "sans-serif"};
        static const char *const latin_families[] = {"DejaVu Sans",
            "Noto Sans", "Liberation Sans", "FreeSans", "sans-serif"};
        const char *const *list = multilingual ? families : latin_families;
        size_t list_count = multilingual ?
            sizeof(families) / sizeof(families[0]) :
            sizeof(latin_families) / sizeof(latin_families[0]);
        const char *language = multilingual ? "zh-cn" : NULL;
        if (fontconfig_family(path, capacity, list, list_count, language,
                multilingual ? 0x4e2d : 'A', false))
            return path;
    }
#endif
    static const char *const cjk[] = {
        /* Arch/Manjaro and openSUSE: noto-fonts-cjk */
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        /* Fedora/RHEL: google-noto-sans-cjk-fonts */
        "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc",
        /* Debian/Ubuntu: fonts-noto-cjk (and language-specific extras) */
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
        "/usr/share/fonts/opentype/noto/NotoSansCJKtc-Regular.otf",
        "/usr/share/fonts/opentype/noto/NotoSansSC-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.otf",
        /* WenQuanYi micro hei/zen hei (Debian, Arch, openSUSE) */
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/wenquanyi/wqy-zenhei/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        /* Older Ubuntu: droid fallback */
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"};
    static const char *const latin[] = {
        /* Debian/Ubuntu, Arch, Fedora ttf-dejavu layouts */
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
        "/usr/share/fonts/freefont/FreeSans.ttf"};
    const char *const *candidates = multilingual ? cjk : latin;
    size_t count = multilingual ?
        sizeof(cjk) / sizeof(cjk[0]) : sizeof(latin) / sizeof(latin[0]);
    for (size_t i = 0; i < count; ++i) {
        if (!readable(candidates[i])) continue;
        snprintf(path, capacity, "%s", candidates[i]);
        return path;
    }
    if (multilingual)
        for (size_t i = 0; i < sizeof(latin) / sizeof(latin[0]); ++i) {
            if (!readable(latin[i])) continue;
            snprintf(path, capacity, "%s", latin[i]);
            return path;
        }
#endif
    return NULL;
}

const char *bongo_cat_ui_system_heading_font(char *path, size_t capacity,
    bool multilingual) {
#ifdef _WIN32
    const char *windows = SDL_getenv("WINDIR");
    if (!windows) windows = SDL_getenv("SystemRoot");
    if (windows) {
        const char *multi[] = {"Fonts/msyhbd.ttc", "Fonts/msyhl.ttc"};
        const char *latin[] = {"Fonts/seguisb.ttf", "Fonts/segoeui.ttf"};
        const char **candidates = multilingual ? multi : latin;
        for (size_t i = 0; i < 2; ++i) {
            bongo_cat_path_join(path, capacity, windows, candidates[i]);
            if (readable(path)) return path;
        }
    }
#elif defined(__APPLE__)
    const char *candidates[] = {multilingual ? "/System/Library/Fonts/PingFang.ttc" :
        "/System/Library/Fonts/Helvetica.ttc", "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/Hiragino Sans GB.ttc"};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (!readable(candidates[i])) continue;
        snprintf(path, capacity, "%s", candidates[i]);
        return path;
    }
#else
#ifdef BONGO_CAT_HAVE_FONTCONFIG
    {
        static const char *const families[] = {"Noto Sans CJK SC",
            "Noto Sans CJK TC", "Noto Sans CJK JP", "Source Han Sans SC",
            "WenQuanYi Micro Hei", "sans-serif"};
        static const char *const latin_families[] = {"DejaVu Sans",
            "Noto Sans", "Liberation Sans", "FreeSans", "sans-serif"};
        const char *const *list = multilingual ? families : latin_families;
        size_t list_count = multilingual ?
            sizeof(families) / sizeof(families[0]) :
            sizeof(latin_families) / sizeof(latin_families[0]);
        const char *language = multilingual ? "zh-cn" : NULL;
        if (fontconfig_family(path, capacity, list, list_count, language,
                multilingual ? 0x4e2d : 'A', true))
            return path;
    }
#endif
    static const char *const cjk[] = {
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Bold.otf",
        "/usr/share/fonts/opentype/noto/NotoSansCJKtc-Bold.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Bold.otf",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/wenquanyi/wqy-zenhei/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"};
    static const char *const latin[] = {
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/noto/NotoSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/freefont/FreeSans-Bold.ttf",
        "/usr/share/fonts/freefont/FreeSans-Bold.ttf"};
    const char *const *candidates = multilingual ? cjk : latin;
    size_t count = multilingual ?
        sizeof(cjk) / sizeof(cjk[0]) : sizeof(latin) / sizeof(latin[0]);
    for (size_t i = 0; i < count; ++i) {
        if (!readable(candidates[i])) continue;
        snprintf(path, capacity, "%s", candidates[i]);
        return path;
    }
    if (multilingual)
        for (size_t i = 0; i < sizeof(latin) / sizeof(latin[0]); ++i) {
            if (!readable(latin[i])) continue;
            snprintf(path, capacity, "%s", latin[i]);
            return path;
        }
#endif
    return NULL;
}

const char *bongo_cat_ui_system_korean_font(char *path, size_t capacity) {
#ifdef _WIN32
    const char *windows = SDL_getenv("WINDIR");
    if (!windows) windows = SDL_getenv("SystemRoot");
    if (windows) {
        const char *candidates[] = {"Fonts/malgun.ttf", "Fonts/malgunsl.ttf"};
        for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
            bongo_cat_path_join(path, capacity, windows, candidates[i]);
            if (readable(path)) return path;
        }
    }
#elif defined(__APPLE__)
    const char *candidates[] = {
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/System/Library/Fonts/Supplemental/AppleGothic.ttf"};
#else
#ifdef BONGO_CAT_HAVE_FONTCONFIG
    {
        static const char *const families[] = {"Noto Sans CJK KR",
            "Noto Sans KR", "NanumGothic", "sans-serif"};
        if (fontconfig_family(path, capacity, families,
                sizeof(families) / sizeof(families[0]), "ko-kr", 0xac00, false))
            return path;
    }
#endif
    const char *candidates[] = {
        /* NotoSansCJK covers Korean on every major distribution */
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        /* Language-specific Noto Sans KR (Debian/Ubuntu, Fedora) */
        "/usr/share/fonts/opentype/noto/NotoSansKR-Regular.otf",
        "/usr/share/fonts/google-noto-sans-kr-fonts/NotoSansKR-Regular.otf",
        /* Nanum Gothic (Debian/Ubuntu, Arch, Fedora) */
        "/usr/share/fonts/truetype/nanum/NanumGothic.ttf",
        "/usr/share/fonts/nanum/NanumGothic.ttf",
        "/usr/share/fonts/nanum-gothic-fonts/NanumGothic.ttf"};
#endif
#ifndef _WIN32
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (!readable(candidates[i])) continue;
        snprintf(path, capacity, "%s", candidates[i]);
        return path;
    }
#endif
    return bongo_cat_ui_system_font(path, capacity, true);
}

const char *bongo_cat_ui_system_korean_heading_font(char *path,
    size_t capacity) {
#ifdef _WIN32
    const char *windows = SDL_getenv("WINDIR");
    if (!windows) windows = SDL_getenv("SystemRoot");
    if (windows) {
        const char *candidates[] = {"Fonts/malgunbd.ttf", "Fonts/malgun.ttf"};
        for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
            bongo_cat_path_join(path, capacity, windows, candidates[i]);
            if (readable(path)) return path;
        }
    }
#elif defined(__APPLE__)
    const char *candidates[] = {
        "/System/Library/Fonts/AppleSDGothicNeo.ttc",
        "/System/Library/Fonts/Supplemental/AppleGothic.ttf"};
#else
#ifdef BONGO_CAT_HAVE_FONTCONFIG
    {
        static const char *const families[] = {"Noto Sans CJK KR",
            "Noto Sans KR", "NanumGothic", "sans-serif"};
        if (fontconfig_family(path, capacity, families,
                sizeof(families) / sizeof(families[0]), "ko-kr", 0xac00, true))
            return path;
    }
#endif
    const char *candidates[] = {
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansKR-Bold.otf",
        "/usr/share/fonts/google-noto-sans-kr-fonts/NotoSansKR-Bold.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Bold.ttc",
        "/usr/share/fonts/truetype/nanum/NanumGothicBold.ttf",
        "/usr/share/fonts/nanum/NanumGothicBold.ttf",
        "/usr/share/fonts/nanum-gothic-fonts/NanumGothicBold.ttf",
        /* Regular weight as a last resort for headings with no bold face */
        "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc"};
#endif
#ifndef _WIN32
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        if (!readable(candidates[i])) continue;
        snprintf(path, capacity, "%s", candidates[i]);
        return path;
    }
#endif
    return bongo_cat_ui_system_heading_font(path, capacity, true);
}
