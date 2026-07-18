#include "ui.hpp"

auto ui_string(Gpu* gpu, UiFont* font, std::string_view text, const UiGlyphRasterInfo& info) -> UiString
{
    UiString out = {};
    out.tree = scene_tree_create();

    out.bounds = aabb_make_empty<f32>();

    f32 x = 0;
    for (char c : text) {
        auto glyph_id = ui_font_get_glyph_index(font, num_cast<c32>(c));
        auto glyph = ui_rasterize_glyph(gpu, font, glyph_id, info);
        if (glyph.image) {
            auto texture = scene_texture_create();
            out.glyphs.emplace_back(texture.get());
            scene_texture_set_image(texture.get(), glyph.image.get(), nullptr, SceneTextureFlag::premultiplied);
            glyph.rect.origin.x += x;
            out.bounds = aabb_outer<f32>(out.bounds, glyph.rect);
            scene_texture_set_dst(texture.get(), glyph.rect);
            scene_tree_place_above(out.tree.get(), nullptr, texture.get());
        }
        x += ui_font_get_glyph_metrics(font, glyph_id).advance;
    }

    return out;
}
