#include "ui.hpp"

#include <core/log.hpp>
#include <core/math.hpp>
#include <core/color.hpp>

#include <ft2build.h>
#include <freetype/freetype.h>
#include <freetype/ftglyph.h>
#include <freetype/ftstroke.h>

struct UiFont
{
    FT_Library ft;
    FT_Face face;

    ~UiFont()
    {
        FT_Done_Face(face);
        FT_Done_FreeType(ft);
    }
};

static
void ft_check(FT_Error error)
{
    debug_assert(!error, "FreeType error: {}", FT_Error_String(error));
}

auto ui_font_load(const char* path, f32 size) -> Ref<UiFont>
{
    auto font = ref_create<UiFont>();

    ft_check(FT_Init_FreeType(&font->ft));
    ft_check(FT_New_Face(font->ft, path, 0, &font->face));
    ft_check(FT_Set_Pixel_Sizes(font->face, 0, num_cast<u32>(size)));

    return font;
}

auto ui_font_get_glyph_index(UiFont* font, c32 codepoint) -> u32
{
    return FT_Get_Char_Index(font->face, codepoint);
}

auto ui_font_get_glyph_metrics(UiFont* font, u32 glyph_index) -> UiGlyphMetrics
{
    ft_check(FT_Load_Glyph(font->face, glyph_index, FT_LOAD_NO_BITMAP));
    return {
        .advance = num_cast<f32>(font->face->glyph->advance.x) / 64.f,
    };
}

// -----------------------------------------------------------------------------

static
auto composite_glyphs(Gpu* gpu, std::span<const std::pair<FT_BitmapGlyph, vec4f32>> glyphs) -> UiGlyph
{
    aabb2i32 bounds = aabb_make_empty<i32>();

    for (auto&[glyph, _] : glyphs) {
        bounds = aabb_outer<i32>(bounds, {{glyph->left, -glyph->top}, {num_cast<i32>(glyph->bitmap.width), num_cast<i32>(glyph->bitmap.rows)}, xywh});
    }

    auto rect = rect_cast<i32>(bounds);

    std::vector<vec4u8> pixels;
    pixels.resize(num_cast<u32>(rect.extent.x * rect.extent.y));

    for (auto[glyph, color] : glyphs) {
        i32 width  = num_cast<i32>(glyph->bitmap.width);
        i32 height = num_cast<i32>(glyph->bitmap.rows);
        for (i32 y = 0; y < height; ++y) {
            for (i32 x = 0; x < width; ++x) {
                f32 alpha = glyph->bitmap.buffer[y * width + x] / 255.f;
                vec4u8& pixel = pixels[num_cast<usz>((y - glyph->top  - rect.origin.y) * rect.extent.x
                                                   + (x + glyph->left - rect.origin.x))];

                auto dst = srgb_eotf(unpack_unorm(pixel));
                auto src = color;
                src.w *= alpha;
                pixel = pack_unorm<u8>(srgb_oetf(blend_linear_premultiplied(dst, premultiply(src))));
            }
        }
    }

    UiGlyph composite = {};
    composite.rect = rect_cast<f32>(rect);
    if (rect.extent.x && rect.extent.y) {
        composite.image = gpu_image_create(gpu, {
            .extent = vec_cast<u32>(rect.extent),
            .format = gpu_format_from_vulkan(VK_FORMAT_R8G8B8A8_SRGB),
            .usage = GpuImageUsage::texture | GpuImageUsage::transfer_dst,
        });
        gpu_copy_memory_to_image(composite.image.get(), as_bytes(pixels), {{
            {
                .image_extent = composite.image->base()->extent,
            }
        }});
    }

    return composite;
}

auto ui_rasterize_glyph(Gpu* gpu, UiFont* font, u32 glyph_index, const UiGlyphRasterInfo& info) -> UiGlyph
{
    ft_check(FT_Load_Glyph(font->face, glyph_index, FT_LOAD_NO_BITMAP));

    FT_Glyph glyph;
    ft_check(FT_Get_Glyph(font->face->glyph, &glyph));
    defer { FT_Done_Glyph(glyph); };

    // Fill

    FT_Glyph fill_glyph;
    ft_check(FT_Glyph_Copy(glyph, &fill_glyph));
    defer { FT_Done_Glyph(fill_glyph); };
    ft_check(FT_Glyph_To_Bitmap(&fill_glyph, FT_RENDER_MODE_NORMAL, NULL, 1));
    FT_BitmapGlyph fill_bitmap = reinterpret_cast<FT_BitmapGlyph>(fill_glyph);

    if (info.border.width == 0) {
        return composite_glyphs(gpu, {{{fill_bitmap, info.color}}});
    }

    // Outline

    FT_Stroker outliner;
    ft_check(FT_Stroker_New(font->ft, &outliner));
    defer { FT_Stroker_Done(outliner); };
    FT_Stroker_Set(outliner, int(info.border.width * 64.f), FT_STROKER_LINECAP_ROUND, FT_STROKER_LINEJOIN_ROUND, 0);

    FT_Glyph outline_glyph;
    ft_check(FT_Glyph_Copy(glyph, &outline_glyph));
    defer { FT_Done_Glyph(outline_glyph); };
    ft_check(FT_Glyph_StrokeBorder(&outline_glyph, outliner, false, true));
    ft_check(FT_Glyph_To_Bitmap(&outline_glyph, FT_RENDER_MODE_NORMAL, NULL, 1));
    FT_BitmapGlyph outline_bitmap = reinterpret_cast<FT_BitmapGlyph>(outline_glyph);

    return composite_glyphs(gpu, {{
        {outline_bitmap, info.border.color},
        {fill_bitmap,    info.color},
    }});
}
