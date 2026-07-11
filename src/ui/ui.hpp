#pragma once

#include <core/object.hpp>
#include <core/containers.hpp>

#include <scene/scene.hpp>

struct UiFont;

auto ui_font_load(const char* path, f32 size) -> Ref<UiFont>;

auto ui_font_get_glyph_index(UiFont*, c32 codepoint) -> u32;

struct UiGlyphMetrics
{
    f32 advance;
};

auto ui_font_get_glyph_metrics(UiFont*, u32 glyph_index) -> UiGlyphMetrics;

// -----------------------------------------------------------------------------

struct UiGlyph
{
    Ref<GpuImage> image;
    rect2f32 rect;
};

struct UiGlyphRasterInfo
{
    vec4f32 color = {1, 1, 1, 1};
    struct {
        vec4f32 color;
        f32 width = 0;
    } border;
};

auto ui_rasterize_glyph(Gpu*, UiFont*, u32 glyph_index, const UiGlyphRasterInfo&) -> UiGlyph;

// -----------------------------------------------------------------------------

struct UiString
{
    Ref<SceneTree> tree;
    RefVector<SceneTexture> glyphs;
    aabb2f32 bounds;
};

auto ui_string(Gpu*, UiFont*, std::string_view text, const UiGlyphRasterInfo&) -> UiString;
