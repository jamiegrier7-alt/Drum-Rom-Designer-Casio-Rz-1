// Loads icon textures and provides icon rendering utilities for the UI.
#include "drumrom/main_ui_icons.h"

#include "imgui.h"

#include <SDL2/SDL_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <vector>

namespace drumrom::main_ui_icons {

void ensure_drum_type_textures_loaded(State* state) {
    if (state == nullptr || state->drum_type_textures_attempted == nullptr ||
        state->drum_type_textures == nullptr) {
        return;
    }
    if (*state->drum_type_textures_attempted || state->sdl_renderer == nullptr) {
        return;
    }
    *state->drum_type_textures_attempted = true;

    struct IconDef {
        std::size_t texture_index;
        const char* filename;
    };
    static const std::array<IconDef, 5> defs{{
        {0u, "kick.png"},
        {1u, "snare.png"},
        {2u, "hihat.png"},
        {3u, "tom.png"},
        {4u, "clap.png"},
    }};

    for (const auto& d : defs) {
        const std::filesystem::path p = std::filesystem::path("icons") / d.filename;
        if (!std::filesystem::exists(p)) {
            continue;
        }
        SDL_Surface* surface = IMG_Load(p.string().c_str());
        if (!surface) {
            continue;
        }
        SDL_Texture* tex = SDL_CreateTextureFromSurface(state->sdl_renderer, surface);
        SDL_FreeSurface(surface);
        if (!tex) {
            continue;
        }
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        (*state->drum_type_textures)[d.texture_index] = tex;
    }
}

void ensure_drum_icons_texture_loaded(State* state) {
    if (state == nullptr || state->drum_icons_texture_attempted == nullptr ||
        state->drum_icons_texture == nullptr || state->drum_icons_uv_ready == nullptr ||
        state->drum_icons_uvs == nullptr) {
        return;
    }
    if (*state->drum_icons_texture_attempted || state->sdl_renderer == nullptr) {
        return;
    }
    *state->drum_icons_texture_attempted = true;

    const std::array<std::filesystem::path, 3> candidates{{
        std::filesystem::path("drums-icons.jpg"),
        std::filesystem::path("assets") / "drums-icons.jpg",
        std::filesystem::path("images") / "drums-icons.jpg",
    }};

    for (const auto& p : candidates) {
        if (!std::filesystem::exists(p)) {
            continue;
        }
        SDL_Surface* surface = IMG_Load(p.string().c_str());
        if (!surface) {
            continue;
        }

        const int w = std::max(1, surface->w);
        const int h = std::max(1, surface->h);
        const float inv_w = 1.0f / static_cast<float>(w);
        const float inv_h = 1.0f / static_cast<float>(h);

        for (int i = 0; i < 16; ++i) {
            const int col = i % 4;
            const int row = i / 4;
            (*state->drum_icons_uvs)[static_cast<std::size_t>(i)] = ImVec4(col * 0.25f, row * 0.25f, (col + 1) * 0.25f, (row + 1) * 0.25f);
        }

        auto read_pixel = [&](int x, int y) -> Uint32 {
            const int bpp = surface->format->BytesPerPixel;
            const Uint8* ppx = static_cast<const Uint8*>(surface->pixels) + (y * surface->pitch) + (x * bpp);
            switch (bpp) {
                case 1: return *ppx;
                case 2: return *reinterpret_cast<const Uint16*>(ppx);
                case 3:
                    if (SDL_BYTEORDER == SDL_BIG_ENDIAN) {
                        return (ppx[0] << 16) | (ppx[1] << 8) | ppx[2];
                    }
                    return ppx[0] | (ppx[1] << 8) | (ppx[2] << 16);
                case 4: return *reinterpret_cast<const Uint32*>(ppx);
                default: return 0;
            }
        };

        if (SDL_MUSTLOCK(surface)) {
            SDL_LockSurface(surface);
        }

        auto is_non_white = [&](int x, int y) {
            const Uint32 px = read_pixel(x, y);
            Uint8 r, g, b, a;
            SDL_GetRGBA(px, surface->format, &r, &g, &b, &a);
            return (a > 10) && ((r < 242) || (g < 242) || (b < 242));
        };

        struct ComponentBox {
            int min_x;
            int min_y;
            int max_x;
            int max_y;
            int count;
        };

        std::vector<std::uint8_t> visited(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
        std::vector<ComponentBox> components;
        std::vector<int> stack;
        stack.reserve(4096);

        const auto idx_of = [w](int x, int y) { return (y * w) + x; };

        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int seed_idx = idx_of(x, y);
                if (visited[static_cast<std::size_t>(seed_idx)] != 0 || !is_non_white(x, y)) {
                    continue;
                }

                visited[static_cast<std::size_t>(seed_idx)] = 1;
                stack.clear();
                stack.push_back(seed_idx);

                ComponentBox box{x, y, x, y, 0};
                while (!stack.empty()) {
                    const int cur = stack.back();
                    stack.pop_back();
                    const int cx = cur % w;
                    const int cy = cur / w;
                    box.min_x = std::min(box.min_x, cx);
                    box.min_y = std::min(box.min_y, cy);
                    box.max_x = std::max(box.max_x, cx);
                    box.max_y = std::max(box.max_y, cy);
                    ++box.count;

                    for (int dy = -1; dy <= 1; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if (dx == 0 && dy == 0) {
                                continue;
                            }
                            const int nx = cx + dx;
                            const int ny = cy + dy;
                            if (nx < 0 || nx >= w || ny < 0 || ny >= h) {
                                continue;
                            }
                            const int nidx = idx_of(nx, ny);
                            if (visited[static_cast<std::size_t>(nidx)] != 0 || !is_non_white(nx, ny)) {
                                continue;
                            }
                            visited[static_cast<std::size_t>(nidx)] = 1;
                            stack.push_back(nidx);
                        }
                    }
                }

                if (box.count >= 20) {
                    components.push_back(box);
                }
            }
        }

        std::sort(components.begin(), components.end(), [](const ComponentBox& a, const ComponentBox& b) {
            return a.count > b.count;
        });

        int min_x = w - 1;
        int min_y = h - 1;
        int max_x = 0;
        int max_y = 0;
        bool found = false;

        const int keep = std::min(16, static_cast<int>(components.size()));
        for (int i = 0; i < keep; ++i) {
            const ComponentBox& c = components[static_cast<std::size_t>(i)];
            min_x = std::min(min_x, c.min_x);
            min_y = std::min(min_y, c.min_y);
            max_x = std::max(max_x, c.max_x);
            max_y = std::max(max_y, c.max_y);
            found = true;
        }

        if (!found) {
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    if (!is_non_white(x, y)) {
                        continue;
                    }
                    found = true;
                    min_x = std::min(min_x, x);
                    min_y = std::min(min_y, y);
                    max_x = std::max(max_x, x);
                    max_y = std::max(max_y, y);
                }
            }
        }

        if (found) {
            const int margin = 3;
            min_x = std::max(0, min_x - margin);
            min_y = std::max(0, min_y - margin);
            max_x = std::min(w - 1, max_x + margin);
            max_y = std::min(h - 1, max_y + margin);

            const int box_w = std::max(1, max_x - min_x + 1);
            const int box_h = std::max(1, max_y - min_y + 1);
            int side = std::max(box_w, box_h);
            side = std::min(side, std::min(w, h));

            const float cx = 0.5f * static_cast<float>(min_x + max_x);
            const float cy = 0.5f * static_cast<float>(min_y + max_y);

            int sq_x0 = static_cast<int>(std::round(cx - (0.5f * static_cast<float>(side))));
            int sq_y0 = static_cast<int>(std::round(cy - (0.5f * static_cast<float>(side))));
            sq_x0 = std::clamp(sq_x0, 0, std::max(0, w - side));
            sq_y0 = std::clamp(sq_y0, 0, std::max(0, h - side));

            const float step = static_cast<float>(side) / 4.0f;
            for (int i = 0; i < 16; ++i) {
                const int col = i % 4;
                const int row = i / 4;
                const int x0 = sq_x0 + static_cast<int>(std::floor(col * step));
                const int y0 = sq_y0 + static_cast<int>(std::floor(row * step));
                const int x1 = sq_x0 + static_cast<int>(std::floor((col + 1) * step));
                const int y1 = sq_y0 + static_cast<int>(std::floor((row + 1) * step));

                const int cx0 = std::clamp(x0, 0, w - 1);
                const int cy0 = std::clamp(y0, 0, h - 1);
                const int cx1 = std::clamp(x1, 1, w);
                const int cy1 = std::clamp(y1, 1, h);

                int icon_min_x = cx1 - 1;
                int icon_min_y = cy1 - 1;
                int icon_max_x = cx0;
                int icon_max_y = cy0;
                bool icon_found = false;

                for (int py = cy0; py < cy1; ++py) {
                    for (int px = cx0; px < cx1; ++px) {
                        if (!is_non_white(px, py)) {
                            continue;
                        }
                        icon_found = true;
                        icon_min_x = std::min(icon_min_x, px);
                        icon_min_y = std::min(icon_min_y, py);
                        icon_max_x = std::max(icon_max_x, px);
                        icon_max_y = std::max(icon_max_y, py);
                    }
                }

                if (icon_found) {
                    const int margin_inner = 2;
                    icon_min_x = std::max(cx0, icon_min_x - margin_inner);
                    icon_min_y = std::max(cy0, icon_min_y - margin_inner);
                    icon_max_x = std::min(cx1 - 1, icon_max_x + margin_inner);
                    icon_max_y = std::min(cy1 - 1, icon_max_y + margin_inner);

                    const int bw = std::max(1, icon_max_x - icon_min_x + 1);
                    const int bh = std::max(1, icon_max_y - icon_min_y + 1);
                    int s = std::max(bw, bh);
                    s = std::min(s, std::min(cx1 - cx0, cy1 - cy0));

                    const float bcx = 0.5f * static_cast<float>(icon_min_x + icon_max_x);
                    const float bcy = 0.5f * static_cast<float>(icon_min_y + icon_max_y);
                    int sx0 = static_cast<int>(std::round(bcx - (0.5f * static_cast<float>(s))));
                    int sy0 = static_cast<int>(std::round(bcy - (0.5f * static_cast<float>(s))));
                    sx0 = std::clamp(sx0, cx0, std::max(cx0, cx1 - s));
                    sy0 = std::clamp(sy0, cy0, std::max(cy0, cy1 - s));
                    const int sx1 = sx0 + s;
                    const int sy1 = sy0 + s;

                    (*state->drum_icons_uvs)[static_cast<std::size_t>(i)] = ImVec4(
                        static_cast<float>(sx0) * inv_w,
                        static_cast<float>(sy0) * inv_h,
                        static_cast<float>(sx1) * inv_w,
                        static_cast<float>(sy1) * inv_h
                    );
                } else {
                    (*state->drum_icons_uvs)[static_cast<std::size_t>(i)] = ImVec4(
                        static_cast<float>(cx0) * inv_w,
                        static_cast<float>(cy0) * inv_h,
                        static_cast<float>(cx1) * inv_w,
                        static_cast<float>(cy1) * inv_h
                    );
                }
            }
        }

        if (SDL_MUSTLOCK(surface)) {
            SDL_UnlockSurface(surface);
        }
        *state->drum_icons_uv_ready = true;

        *state->drum_icons_texture = SDL_CreateTextureFromSurface(state->sdl_renderer, surface);
        SDL_FreeSurface(surface);
        if (*state->drum_icons_texture != nullptr) {
            SDL_SetTextureBlendMode(*state->drum_icons_texture, SDL_BLENDMODE_BLEND);
            return;
        }
    }
}

void sprite_uv_for_tile(const State& state, int tile, ImVec2* uv0, ImVec2* uv1) {
    if (uv0 == nullptr || uv1 == nullptr || state.drum_icons_uv_ready == nullptr || state.drum_icons_uvs == nullptr) {
        return;
    }

    const int idx = std::max(1, tile) - 1;
    if (idx >= 0 && idx < static_cast<int>(state.drum_icons_uvs->size()) && *state.drum_icons_uv_ready) {
        const ImVec4 uv = (*state.drum_icons_uvs)[static_cast<std::size_t>(idx)];
        *uv0 = ImVec2(uv.x, uv.y);
        *uv1 = ImVec2(uv.z, uv.w);
        return;
    }

    const int col = idx % 4;
    const int row = idx / 4;
    *uv0 = ImVec2(static_cast<float>(col) * 0.25f, static_cast<float>(row) * 0.25f);
    *uv1 = ImVec2(static_cast<float>(col + 1) * 0.25f, static_cast<float>(row + 1) * 0.25f);
}

}  // namespace drumrom::main_ui_icons
