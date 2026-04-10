/**
 * SDL2 + SDL2_ttf GUI for the C++ vocoder — layout and palette aligned with live_vocoder_gtk.py
 * (dark gradient, purple card borders, pill presets, header + voice card).
 */

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <SDL_ttf.h>

#include <portaudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <list>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "carrier_convert.hpp"
#include "gui_audio_engine.hpp"
#include "linux_pulse_env.hpp"
#include "pa_duplex.hpp"
#include "vocoder.hpp"

#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#if defined(__linux__)
#include <unistd.h>
#endif

namespace {

/** Skip welcome / font SDL message boxes (automation, dummy video, CI). Errors still use modals. */
bool sdl_skip_startup_modals() {
    const char* e = std::getenv("LIVE_VOCODER_SDL_SKIP_STARTUP_MODALS");
    return e != nullptr && e[0] != '\0' && std::strcmp(e, "0") != 0;
}

// GTK vm-root / vm-card palette (live_vocoder_gtk._vm_css); header logo = assets/app-icon.png
struct Rgba {
    Uint8 r, g, b, a;
};
constexpr Rgba kBgTop{11, 14, 20, 255};   // ~#0B0E14 bar reference
constexpr Rgba kBgMid{16, 18, 26, 255};
constexpr Rgba kBgBot{9, 10, 15, 255};
constexpr Rgba kCardFill{24, 27, 38, 255};
constexpr Rgba kCardBorder{72, 62, 118, 140};
constexpr Rgba kCardShadow{0, 0, 0, 72};
constexpr Rgba kSection{148, 128, 220, 255};
constexpr Rgba kBrand{244, 245, 248, 255};
constexpr Rgba kMuted{132, 138, 158, 255};
constexpr Rgba kFoot{98, 104, 124, 255};
constexpr Rgba kDivider{124, 110, 180, 55};
constexpr Rgba kPrimaryBtn{59, 166, 129, 255};   // ~#3BA681
constexpr Rgba kPrimaryBorder{34, 118, 92, 255};
constexpr Rgba kDangerBtn{200, 90, 90, 255};   // ~#C85A5A
constexpr Rgba kDangerBorder{130, 50, 55, 255};
constexpr Rgba kGhostFace{49, 54, 65, 255};   // ~#313641
constexpr Rgba kGhostBorder{88, 78, 130, 160};
constexpr Rgba kChipFace{30, 33, 44, 255};
constexpr Rgba kChipBorder{70, 64, 98, 200};
constexpr Rgba kChipSelFace{56, 48, 88, 255};
constexpr Rgba kChipSelBorder{150, 128, 220, 220};
constexpr Rgba kMicPink{255, 166, 243, 255};

/** sdl_show_themed_message_box uses the OS light theme; colorScheme matches vm-card palette where SDL supports it. */
void sdl_show_themed_message_box(Uint32 flags, const char* title, const char* message, SDL_Window* window) {
    SDL_MessageBoxButtonData button{};
    button.flags = SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT;
    button.buttonid = 0;
    button.text = "OK";

    SDL_MessageBoxColorScheme scheme{};
    scheme.colors[SDL_MESSAGEBOX_COLOR_BACKGROUND] = {kCardFill.r, kCardFill.g, kCardFill.b};
    scheme.colors[SDL_MESSAGEBOX_COLOR_TEXT] = {kBrand.r, kBrand.g, kBrand.b};
    scheme.colors[SDL_MESSAGEBOX_COLOR_BUTTON_BORDER] = {kSection.r, kSection.g, kSection.b};
    scheme.colors[SDL_MESSAGEBOX_COLOR_BUTTON_BACKGROUND] = {kGhostFace.r, kGhostFace.g, kGhostFace.b};
    scheme.colors[SDL_MESSAGEBOX_COLOR_BUTTON_SELECTED] = {kPrimaryBtn.r, kPrimaryBtn.g, kPrimaryBtn.b};

    SDL_MessageBoxData data{};
    data.flags = flags;
    data.window = window;
    data.title = title;
    data.message = message;
    data.numbuttons = 1;
    data.buttons = &button;
    data.colorScheme = &scheme;

    int buttonid = 0;
    (void)SDL_ShowMessageBox(&data, &buttonid);
}

using App = lv_gui::LiveVocoderAudioApp;

struct PresetDef {
    const char* label;
    double wet;
    double presence_db;
    float reverb_mix;
};
constexpr PresetDef kPresets[] = {
    {"Clean", 1.0, 4.0, 0.f},
    {"Radio", 1.2, 6.5, 0.08f},
    {"Deep", 1.35, 2.0, 0.18f},
    {"Studio", 1.15, 5.0, 0.12f},
};

std::vector<double> load_carrier_f32(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        throw std::runtime_error(std::string("open carrier: ") + path);
    }
    f.seekg(0, std::ios::end);
    const auto sz = static_cast<std::size_t>(f.tellg());
    f.seekg(0);
    if (sz % sizeof(float) != 0) {
        throw std::runtime_error("carrier file size not multiple of float32");
    }
    const std::size_t n = sz / sizeof(float);
    std::vector<float> tmp(n);
    f.read(reinterpret_cast<char*>(tmp.data()), static_cast<std::streamsize>(sz));
    std::vector<double> c(n);
    for (std::size_t i = 0; i < n; ++i) {
        c[i] = static_cast<double>(tmp[i]);
    }
    return c;
}

std::filesystem::path resolve_exe_dir(char* argv0) {
#if defined(_WIN32)
    wchar_t wbuf[MAX_PATH];
    if (GetModuleFileNameW(nullptr, wbuf, MAX_PATH) != 0U) {
        return std::filesystem::path(wbuf).parent_path();
    }
#elif defined(__linux__)
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[static_cast<size_t>(n)] = '\0';
        return std::filesystem::path(buf).parent_path();
    }
#endif
    std::error_code ec;
    std::filesystem::path p = std::filesystem::weakly_canonical(std::filesystem::path(argv0), ec);
    if (!ec) {
        return p.parent_path();
    }
    return std::filesystem::path(argv0).parent_path();
}

/** Same folder as Python GTK: Documents/LiveVocoderCarriers (see carrier_library.py). */
std::filesystem::path documents_livevocoder_dir() {
#if defined(_WIN32)
    {
        const std::filesystem::path shell_docs = carrier_win32_documents_folder();
        if (!shell_docs.empty()) {
            return shell_docs / "LiveVocoderCarriers";
        }
    }
    const char* up = std::getenv("USERPROFILE");
    if (up != nullptr && up[0] != '\0') {
        return std::filesystem::path(up) / "Documents" / "LiveVocoderCarriers";
    }
    const char* h = std::getenv("HOME");
    if (h != nullptr && h[0] != '\0') {
        return carrier_win32_localize_path_for_filesystem(std::filesystem::path(h) / "Documents" /
                                                          "LiveVocoderCarriers");
    }
    return {};
#else
    const char* h = std::getenv("HOME");
    if (h == nullptr || h[0] == '\0') {
        return {};
    }
    return std::filesystem::path(h) / "Documents" / "LiveVocoderCarriers";
#endif
}

std::filesystem::path carrier_library_dir(const std::filesystem::path& exe_dir) {
    std::filesystem::path d = documents_livevocoder_dir();
    if (!d.empty()) {
        return d;
    }
    return exe_dir / "LiveVocoderCarriers";
}

constexpr std::uintmax_t kMinCarrierF32Bytes = sizeof(float) * 64;

bool carrier_f32_file_usable(const std::filesystem::path& p, std::error_code& ec) {
    ec.clear();
    if (!std::filesystem::is_regular_file(p, ec)) {
        return false;
    }
    if (!carrier_path_is_raw_f32(p)) {
        return false;
    }
    ec.clear();
    const std::uintmax_t sz = std::filesystem::file_size(p, ec);
    return !ec && sz >= kMinCarrierF32Bytes;
}

/** Prefer carrier.f32; else newest valid *.f32 in the library; else exe_dir/carrier.f32. */
std::filesystem::path pick_default_carrier_f32(const std::filesystem::path& lib, const std::filesystem::path& exe_dir) {
    std::error_code ec;
    const auto lib_carrier = lib / "carrier.f32";
    if (carrier_f32_file_usable(lib_carrier, ec)) {
        return lib_carrier;
    }
    std::filesystem::path best;
    std::filesystem::file_time_type best_t{};
    bool have = false;
    ec.clear();
    if (std::filesystem::is_directory(lib, ec)) {
        for (const std::filesystem::directory_entry& ent : std::filesystem::directory_iterator(lib, ec)) {
            if (ec) {
                break;
            }
            if (!ent.is_regular_file()) {
                continue;
            }
            const std::filesystem::path& path = ent.path();
            if (!carrier_path_is_raw_f32(path)) {
                continue;
            }
            if (!carrier_f32_file_usable(path, ec)) {
                continue;
            }
            ec.clear();
            const auto wt = std::filesystem::last_write_time(path, ec);
            if (ec) {
                continue;
            }
            if (!have || wt > best_t) {
                best = path;
                best_t = wt;
                have = true;
            }
        }
    }
    if (have) {
        return best;
    }
    const auto def = exe_dir / "carrier.f32";
    ec.clear();
    if (carrier_f32_file_usable(def, ec)) {
        return def;
    }
    return {};
}

/** Copy dropped .f32 or convert other audio (ffmpeg) into the library folder; sets out_path to the .f32 used. */
bool ingest_dropped_carrier(const std::filesystem::path& exe_dir, const std::filesystem::path& dropped,
                            std::filesystem::path& out_path, std::string& err) {
    err.clear();
#if defined(_WIN32)
    const std::filesystem::path dropped_use = carrier_win32_localize_path_for_filesystem(dropped);
#else
    const std::filesystem::path& dropped_use = dropped;
#endif
    std::error_code ec;
    if (!std::filesystem::is_regular_file(dropped_use, ec)) {
        err = "not a regular file";
        return false;
    }
    const std::filesystem::path lib = carrier_library_dir(exe_dir);
    std::filesystem::create_directories(lib, ec);
    if (ec) {
        err = ec.message();
        return false;
    }

    if (carrier_path_is_raw_f32(dropped_use)) {
        const std::filesystem::path dest = lib / dropped_use.filename();
        ec.clear();
        if (std::filesystem::exists(dest)) {
            if (std::filesystem::equivalent(dropped_use, dest, ec)) {
                out_path = dest;
                return true;
            }
            ec.clear();
        }
        std::filesystem::copy_file(dropped_use, dest, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            err = ec.message();
            return false;
        }
        out_path = dest;
        return true;
    }

    std::filesystem::path stem = dropped_use.stem();
    if (stem.empty()) {
        stem = "carrier";
    }
    const std::filesystem::path dest = lib / (stem.string() + ".f32");
    if (!carrier_ffmpeg_to_f32(lv_gui::kSampleRate, dropped_use, dest, err)) {
        return false;
    }
    out_path = dest;
    return true;
}

void set_title(SDL_Window* w, const std::string& carrier_label, bool streaming, bool clean_mic, bool monitor_on,
               bool virt_sink_hot) {
    std::string t = "Live Vocoder — ";
    if (streaming) {
        t += clean_mic ? "mic (dry)" : "vocoding";
        if (!monitor_on) {
            t += virt_sink_hot ? " · to virtual sink (speakers off)" : " · muted";
        }
    } else {
        t += "stopped";
    }
    t += " · ";
    if (clean_mic && !carrier_label.empty()) {
        t += "carrier idle · ";
    }
    t += carrier_label.empty() ? "no carrier" : carrier_label;
    t += " · memesdudeguy";
    SDL_SetWindowTitle(w, t.c_str());
}

bool point_in_rect(int x, int y, const SDL_FRect& r) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

void set_color(SDL_Renderer* ren, const Rgba& c) {
    SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
}

/** Horizontal peak meter 0..1 (for mic / playback sanity check, including virtual cable path). */
void draw_audio_meter_bar(SDL_Renderer* ren, float x, float y, float w, float h, float level01, const Rgba& fill) {
    level01 = std::clamp(level01, 0.f, 1.f);
    const SDL_FRect back{x, y, w, h};
    set_color(ren, kGhostFace);
    SDL_RenderFillRectF(ren, &back);
    if (level01 > 0.0005f) {
        set_color(ren, fill);
        const SDL_FRect fr{x, y, w * level01, h};
        SDL_RenderFillRectF(ren, &fr);
    }
    set_color(ren, kChipBorder);
    SDL_RenderDrawRectF(ren, &back);
}

/** Filled circle without SDL_RenderFillCircleF (older / some MinGW headers omit it). */
void fill_disk(SDL_Renderer* ren, float cx, float cy, float rad, const Rgba& col) {
    if (rad <= 0.f) {
        return;
    }
    set_color(ren, col);
    const int ir = static_cast<int>(std::ceil(static_cast<double>(rad)));
    const double r2 = static_cast<double>(rad) * static_cast<double>(rad);
    for (int dy = -ir; dy <= ir; ++dy) {
        const double y = static_cast<double>(dy);
        const double disc = r2 - y * y;
        if (disc < 0.0) {
            continue;
        }
        const float hx = static_cast<float>(std::floor(std::sqrt(disc)));
        SDL_RenderDrawLineF(ren, cx - hx, cy + static_cast<float>(dy), cx + hx, cy + static_cast<float>(dy));
    }
}

void draw_gradient_bg(SDL_Renderer* ren, int w, int h) {
    for (int y = 0; y < h; ++y) {
        const float t = h > 1 ? static_cast<float>(y) / static_cast<float>(h - 1) : 0.f;
        Uint8 r, g, b;
        if (t < 0.45f) {
            const float u = t / 0.45f;
            r = static_cast<Uint8>(kBgTop.r + (kBgMid.r - kBgTop.r) * u);
            g = static_cast<Uint8>(kBgTop.g + (kBgMid.g - kBgTop.g) * u);
            b = static_cast<Uint8>(kBgTop.b + (kBgMid.b - kBgTop.b) * u);
        } else {
            const float u = (t - 0.45f) / 0.55f;
            r = static_cast<Uint8>(kBgMid.r + (kBgBot.r - kBgMid.r) * u);
            g = static_cast<Uint8>(kBgMid.g + (kBgBot.g - kBgMid.g) * u);
            b = static_cast<Uint8>(kBgMid.b + (kBgBot.b - kBgMid.b) * u);
        }
        SDL_SetRenderDrawColor(ren, r, g, b, 255);
        SDL_RenderDrawLine(ren, 0, y, w, y);
    }
}

/** Bake gradient once per window size; avoids hundreds of draw calls per frame. */
bool ensure_gradient_bg_texture(SDL_Renderer* ren, SDL_Texture** tex, int* cached_w, int* cached_h, int w, int h) {
    if (w <= 0 || h <= 0) {
        return false;
    }
    if (*tex != nullptr && *cached_w == w && *cached_h == h) {
        return true;
    }
    if (*tex != nullptr) {
        SDL_DestroyTexture(*tex);
        *tex = nullptr;
    }
    if (SDL_RenderTargetSupported(ren) == SDL_FALSE) {
        return false;
    }
    *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (*tex == nullptr) {
        return false;
    }
    SDL_SetTextureBlendMode(*tex, SDL_BLENDMODE_NONE);
    if (SDL_SetRenderTarget(ren, *tex) != 0) {
        SDL_DestroyTexture(*tex);
        *tex = nullptr;
        return false;
    }
    draw_gradient_bg(ren, w, h);
    SDL_SetRenderTarget(ren, nullptr);
    *cached_w = w;
    *cached_h = h;
    return true;
}

void fill_round_rect(SDL_Renderer* ren, const SDL_FRect& box, float rad, const Rgba& fill) {
    rad = std::min(rad, std::min(box.w, box.h) * 0.5f);
    set_color(ren, fill);
    SDL_FRect mid{box.x + rad, box.y, box.w - 2.f * rad, box.h};
    SDL_RenderFillRectF(ren, &mid);
    SDL_FRect left{box.x, box.y + rad, rad, box.h - 2.f * rad};
    SDL_RenderFillRectF(ren, &left);
    SDL_FRect right{box.x + box.w - rad, box.y + rad, rad, box.h - 2.f * rad};
    SDL_RenderFillRectF(ren, &right);
    const float r = rad;
    fill_disk(ren, box.x + r, box.y + r, r, fill);
    fill_disk(ren, box.x + box.w - r, box.y + r, r, fill);
    fill_disk(ren, box.x + r, box.y + box.h - r, r, fill);
    fill_disk(ren, box.x + box.w - r, box.y + box.h - r, r, fill);
}

void stroke_round_rect(SDL_Renderer* ren, const SDL_FRect& box, float, const Rgba& stroke) {
    set_color(ren, stroke);
    SDL_RenderDrawRectF(ren, &box);
}

/** 1px-style outline that follows rounded corners (avoids axis-aligned box mismatch vs pill fill). */
void stroke_round_round_rect(SDL_Renderer* ren, const SDL_FRect& box, float rad, const Rgba& stroke) {
    set_color(ren, stroke);
    const float x = box.x;
    const float y = box.y;
    const float w = box.w;
    const float h = box.h;
    rad = std::min(rad, std::min(w, h) * 0.5f);
    constexpr float kPi = 3.14159265358979323846f;
    constexpr int kSeg = 18;
    auto arc = [&](float cx, float cy, float a0, float a1) {
        for (int i = 0; i < kSeg; ++i) {
            const float t0 = a0 + (a1 - a0) * static_cast<float>(i) / static_cast<float>(kSeg);
            const float t1 = a0 + (a1 - a0) * static_cast<float>(i + 1) / static_cast<float>(kSeg);
            const float x0 = cx + std::cos(t0) * rad;
            const float y0 = cy + std::sin(t0) * rad;
            const float x1 = cx + std::cos(t1) * rad;
            const float y1 = cy + std::sin(t1) * rad;
            SDL_RenderDrawLineF(ren, x0, y0, x1, y1);
        }
    };
    arc(x + rad, y + rad, kPi, 1.5f * kPi);
    arc(x + w - rad, y + rad, 1.5f * kPi, 2.f * kPi);
    arc(x + w - rad, y + h - rad, 0.f, 0.5f * kPi);
    arc(x + rad, y + h - rad, 0.5f * kPi, kPi);
    SDL_RenderDrawLineF(ren, x + rad, y, x + w - rad, y);
    SDL_RenderDrawLineF(ren, x + w, y + rad, x + w, y + h - rad);
    SDL_RenderDrawLineF(ren, x + rad, y + h, x + w - rad, y + h);
    SDL_RenderDrawLineF(ren, x, y + rad, x, y + h - rad);
}

/** Second hairline stroke (offset) so outlines survive 1px loss on Wine / some GL backends. */
void stroke_round_round_rect_double(SDL_Renderer* ren, const SDL_FRect& box, float rad, const Rgba& stroke) {
    stroke_round_round_rect(ren, box, rad, stroke);
    SDL_FRect inset{box.x + 0.55f, box.y + 0.55f, box.w - 1.1f, box.h - 1.1f};
    if (inset.w < 8.f || inset.h < 8.f) {
        return;
    }
    const float ir = std::max(2.f, rad - 0.55f);
    stroke_round_round_rect(ren, inset, std::min(ir, std::min(inset.w, inset.h) * 0.5f), stroke);
}

void lighten_rgba(Rgba& c, int d) {
    c.r = static_cast<Uint8>(std::min(255, static_cast<int>(c.r) + d));
    c.g = static_cast<Uint8>(std::min(255, static_cast<int>(c.g) + d));
    c.b = static_cast<Uint8>(std::min(255, static_cast<int>(c.b) + d));
}

/** LRU TTF→texture cache: labels/buttons were re-rasterized every frame (major CPU cost). */
struct TextTexCache {
    struct Entry {
        SDL_Texture* tex = nullptr;
        int w = 0;
        int h = 0;
        std::list<std::string>::iterator lru_it{};
    };
    std::unordered_map<std::string, Entry> map_;
    std::list<std::string> order_;
    static constexpr std::size_t kMax = 160;

    static std::string make_key(TTF_Font* font, const Rgba& c, const char* t) {
        std::string k;
        const auto fp = reinterpret_cast<std::uintptr_t>(font);
        k.append(reinterpret_cast<const char*>(&fp), sizeof(fp));
        k.push_back(static_cast<char>(c.r));
        k.push_back(static_cast<char>(c.g));
        k.push_back(static_cast<char>(c.b));
        k.push_back(static_cast<char>(c.a));
        k.append(t);
        return k;
    }

    void clear() {
        for (auto& kv : map_) {
            if (kv.second.tex != nullptr) {
                SDL_DestroyTexture(kv.second.tex);
            }
        }
        map_.clear();
        order_.clear();
    }

    void blit(SDL_Renderer* ren, TTF_Font* font, const char* text, const Rgba& color, float x, float y) {
        if (!font || !text || !*text) {
            return;
        }
        const std::string k = make_key(font, color, text);
        auto it = map_.find(k);
        if (it != map_.end()) {
            order_.erase(it->second.lru_it);
            order_.push_back(k);
            it->second.lru_it = std::prev(order_.end());
            SDL_FRect dst{x, y, static_cast<float>(it->second.w), static_cast<float>(it->second.h)};
            SDL_RenderCopyF(ren, it->second.tex, nullptr, &dst);
            return;
        }
        SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text, SDL_Color{color.r, color.g, color.b, color.a});
        if (!surf) {
            return;
        }
        SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
        SDL_FreeSurface(surf);
        if (!tex) {
            return;
        }
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        int tw = 0, th = 0;
        SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
        while (map_.size() >= kMax) {
            const std::string& old = order_.front();
            auto oit = map_.find(old);
            if (oit != map_.end()) {
                SDL_DestroyTexture(oit->second.tex);
                map_.erase(oit);
            }
            order_.pop_front();
        }
        order_.push_back(k);
        map_[k] = {tex, tw, th, std::prev(order_.end())};
        SDL_FRect dst{x, y, static_cast<float>(tw), static_cast<float>(th)};
        SDL_RenderCopyF(ren, tex, nullptr, &dst);
    }
};

static TextTexCache g_text_tex_cache;

void blit_text(SDL_Renderer* ren, TTF_Font* font, const char* text, const Rgba& color, float x, float y) {
    g_text_tex_cache.blit(ren, font, text, color, x, y);
}

std::string ellipsize_utf8(const std::string& s, TTF_Font* font, int max_px) {
    if (s.empty() || !font) {
        return s;
    }
    int w = 0;
    if (TTF_SizeUTF8(font, s.c_str(), &w, nullptr) == 0 && w <= max_px) {
        return s;
    }
    const std::string ell = "…";
    int ew = 0;
    TTF_SizeUTF8(font, ell.c_str(), &ew, nullptr);
    for (int n = static_cast<int>(s.size()) - 1; n > 4; --n) {
        std::string t = s.substr(0, static_cast<std::size_t>(n)) + ell;
        if (TTF_SizeUTF8(font, t.c_str(), &w, nullptr) == 0 && w <= max_px) {
            return t;
        }
    }
    return ell;
}

struct UiFont {
    TTF_Font* brand = nullptr;
    TTF_Font* body = nullptr;
    TTF_Font* small = nullptr;
};

void close_fonts(UiFont& f) {
    if (f.brand) {
        TTF_CloseFont(f.brand);
        f.brand = nullptr;
    }
    if (f.body) {
        TTF_CloseFont(f.body);
        f.body = nullptr;
    }
    if (f.small) {
        TTF_CloseFont(f.small);
        f.small = nullptr;
    }
}

bool try_open_ttf(TTF_Font** out, const std::filesystem::path& path, int pt) {
    if (!std::filesystem::is_regular_file(path)) {
        return false;
    }
    TTF_Font* f = TTF_OpenFont(path.string().c_str(), pt);
    if (!f) {
        return false;
    }
    TTF_SetFontHinting(f, TTF_HINTING_NORMAL);
    *out = f;
    return true;
}

bool load_ui_fonts(const std::filesystem::path& exe_dir, UiFont& out) {
    static const std::array<const char*, 2> kNames = {"DejaVuSans.ttf", "DejaVuSansCondensed.ttf"};
    std::vector<std::filesystem::path> candidates;
    for (const char* n : kNames) {
        candidates.push_back(exe_dir / "fonts" / n);
        candidates.push_back(exe_dir / n);
    }
#if defined(_WIN32)
    wchar_t windir[MAX_PATH];
    if (GetEnvironmentVariableW(L"WINDIR", windir, MAX_PATH) > 0) {
        std::filesystem::path wd(windir);
        candidates.push_back(wd / "Fonts" / "segoeui.ttf");
        candidates.push_back(wd / "Fonts" / "arial.ttf");
    }
#elif defined(__linux__)
    candidates.emplace_back("/usr/share/fonts/TTF/DejaVuSans.ttf");
    candidates.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
#endif
    std::filesystem::path chosen;
    for (const auto& p : candidates) {
        if (std::filesystem::is_regular_file(p)) {
            chosen = p;
            break;
        }
    }
    if (chosen.empty()) {
        return false;
    }
    if (!try_open_ttf(&out.brand, chosen, 21)) {
        return false;
    }
    if (!try_open_ttf(&out.body, chosen, 16)) {
        close_fonts(out);
        return false;
    }
    if (!try_open_ttf(&out.small, chosen, 11)) {
        close_fonts(out);
        return false;
    }
    return true;
}

/** Toolbar / chip width from TTF (fallback min when font missing). */
float measure_label_width(TTF_Font* f, const char* label, float min_inner, float pad_x) {
    if (!f || !label) {
        return min_inner + pad_x * 2.f;
    }
    int lw = 0;
    int lh = 0;
    if (TTF_SizeUTF8(f, label, &lw, &lh) != 0) {
        return min_inner + pad_x * 2.f;
    }
    return std::max(min_inner + pad_x * 2.f, static_cast<float>(lw) + pad_x * 2.f);
}

struct Layout {
    SDL_FRect r_start{};
    SDL_FRect r_stop{};
    SDL_FRect r_quit{};
    SDL_FRect r_help{};
    SDL_FRect card{};
    float card_pad = 22.f;
    float divider_y0 = 0.f;
    float title_y = 0.f;
    float subtitle_y = 0.f;
    float text_left = 84.f;
    float mic_cx = 44.f;
    float mic_cy = 40.f;
    float mic_scale = 0.95f;
    int subtitle_max_px = 400;
    int title_max_px = 600;
    bool header_two_rows = false;
    std::array<SDL_FRect, 4> preset_chips{};
    float divider_io_y = 0.f;
    SDL_FRect r_in_vocode{};
    SDL_FRect r_in_clean{};
    SDL_FRect r_monitor{};
    float io_input_label_y = 0.f;
    float io_monitor_label_y = 0.f;
    int preset_ix_hover = -1;
    int chip_hover = -1;
    int header_hover = -1;  // 0 start 1 stop 2 quit 3 help
    float clarity_label_y = 0.f;
    float reverb_label_y = 0.f;
    SDL_FRect r_clarity_track{};
    SDL_FRect r_reverb_track{};
    SDL_FRect r_carrier_lib_btn{};
};

Layout compute_layout(int ww, int wh, bool fonts_ok, TTF_Font* mfont) {
    Layout L;
    const float margin = 20.f;
    const float g = 10.f;
    float bw_s = 82.f;
    float bw_t = 78.f;
    float bw_q = 70.f;
    float bw_h = 36.f;
    const float btn_h = mfont ? 36.f : 34.f;
    if (mfont) {
        bw_s = measure_label_width(mfont, "Start", 44.f, 14.f);
        bw_t = measure_label_width(mfont, "Stop", 40.f, 14.f);
        bw_q = measure_label_width(mfont, "Quit", 40.f, 14.f);
        bw_h = measure_label_width(mfont, "?", 28.f, 12.f);
    }
    const float total_btns = bw_s + g + bw_t + g + bw_q + g + bw_h;

    float card_top = 0.f;
    L.text_left = 84.f;

    if (fonts_ok) {
        constexpr float kTextBtnGap = 18.f;
        const float btn_left_natural = static_cast<float>(ww) - margin - total_btns;
        // Need a real text column; otherwise toolbar overlaps title (narrow window).
        constexpr float kMinTextColumn = 108.f;
        L.header_two_rows = btn_left_natural < L.text_left + kTextBtnGap + kMinTextColumn;

        const float y0 = margin + 10.f;
        L.title_y = y0;
        L.subtitle_y = y0 + 22.f;
        L.mic_cx = 44.f;
        L.mic_cy = y0 + 18.f;
        L.mic_scale = 0.95f;

        float bx = btn_left_natural;
        if (bx < margin) {
            bx = margin;
        }

        float btn_y = y0 + 1.f;
        if (L.header_two_rows) {
            L.subtitle_max_px = static_cast<int>(std::max(80.f, static_cast<float>(ww) - L.text_left - margin));
            L.title_max_px = L.subtitle_max_px;
            btn_y = L.subtitle_y + 22.f;
        } else {
            L.subtitle_max_px = static_cast<int>(std::max(40.f, bx - L.text_left - kTextBtnGap));
            L.title_max_px = static_cast<int>(std::max(40.f, bx - L.text_left - kTextBtnGap));
        }

        L.r_start = {bx, btn_y, bw_s, btn_h};
        bx += bw_s + g;
        L.r_stop = {bx, btn_y, bw_t, btn_h};
        bx += bw_t + g;
        L.r_quit = {bx, btn_y, bw_q, btn_h};
        bx += bw_q + g;
        L.r_help = {bx, btn_y, bw_h, btn_h};

        const float text_bottom = L.subtitle_y + 16.f;
        const float btn_bottom = btn_y + btn_h;
        card_top = std::max(text_bottom, btn_bottom) + 16.f;
    } else {
        const float hdr_top = margin + 8.f;
        L.title_y = hdr_top;
        L.subtitle_y = hdr_top;
        L.mic_cx = 36.f;
        L.mic_cy = hdr_top + 18.f;
        L.mic_scale = 0.85f;
        float bx = static_cast<float>(ww) - margin - total_btns;
        if (bx < margin) {
            bx = margin;
        }
        L.r_start = {bx, hdr_top, bw_s, btn_h};
        bx += bw_s + g;
        L.r_stop = {bx, hdr_top, bw_t, btn_h};
        bx += bw_t + g;
        L.r_quit = {bx, hdr_top, bw_q, btn_h};
        bx += bw_q + g;
        L.r_help = {bx, hdr_top, bw_h, btn_h};
        card_top = hdr_top + btn_h + 18.f;
    }

    float card_h = wh - card_top - margin - 48.f;
    card_h = std::max(fonts_ok ? 380.f : 260.f, card_h);
    L.card = {margin, card_top, static_cast<float>(ww) - 2.f * margin, card_h};

    const float inner_x = L.card.x + L.card_pad;
    const float inner_w = L.card.w - 2.f * L.card_pad;
    const float y0 = L.card.y + L.card_pad;
    {
        float cl_bw = 92.f;
        constexpr float cl_bh = 28.f;
        if (mfont != nullptr) {
            int lw = 0;
            if (TTF_SizeUTF8(mfont, "Library…", &lw, nullptr) == 0) {
                cl_bw = std::max(92.f, static_cast<float>(lw) + 20.f);
            }
        }
        L.r_carrier_lib_btn = {inner_x + inner_w - cl_bw, y0 + 1.f, cl_bw, cl_bh};
    }
    // Vertical rhythm: section, path, hint, divider, section, chips
    L.divider_y0 = y0 + (fonts_ok ? 92.f : 40.f);
    const float chip_y = y0 + (fonts_ok ? 118.f : 56.f);
    const float chip_h = 36.f;
    const float chip_gap = 10.f;
    const int n = static_cast<int>(L.preset_chips.size());

    if (mfont != nullptr) {
        // Separate pills with gaps — each chip is fully rounded (not one stretched row).
        constexpr float kChipPadX = 18.f;
        float widths[4];
        float content_w = 0.f;
        for (int i = 0; i < n; ++i) {
            int lw = 0;
            (void)TTF_SizeUTF8(mfont, kPresets[static_cast<std::size_t>(i)].label, &lw, nullptr);
            widths[static_cast<std::size_t>(i)] =
                std::max(50.f, static_cast<float>(lw) + kChipPadX * 2.f);
            content_w += widths[static_cast<std::size_t>(i)];
        }
        content_w += chip_gap * static_cast<float>(n - 1);
        float scale = 1.f;
        if (content_w > inner_w && content_w > 1.f) {
            scale = inner_w / content_w;
        }
        float total = 0.f;
        for (int i = 0; i < n; ++i) {
            widths[static_cast<std::size_t>(i)] =
                std::max(42.f, std::floor(widths[static_cast<std::size_t>(i)] * scale));
            total += widths[static_cast<std::size_t>(i)];
        }
        total += chip_gap * static_cast<float>(n - 1);
        float x = inner_x + std::max(0.f, (inner_w - total) * 0.5f);
        for (int i = 0; i < n; ++i) {
            const float cw = widths[static_cast<std::size_t>(i)];
            L.preset_chips[static_cast<std::size_t>(i)] = {x, chip_y, cw, chip_h};
            x += cw + chip_gap;
        }
    } else {
        const float chip_w = (inner_w - chip_gap * static_cast<float>(n - 1)) / static_cast<float>(n);
        for (int i = 0; i < n; ++i) {
            L.preset_chips[static_cast<std::size_t>(i)] = {
                inner_x + static_cast<float>(i) * (chip_w + chip_gap), chip_y, chip_w, chip_h};
        }
    }

    // Clarity + reverb sliders (below preset chips)
    const float slider_top = chip_y + chip_h + 14.f;
    constexpr float kTrackH = 10.f;
    L.clarity_label_y = slider_top;
    L.r_clarity_track = {inner_x + 88.f, slider_top + 20.f, inner_w - 92.f, kTrackH};
    L.reverb_label_y = slider_top + 48.f;
    L.r_reverb_track = {inner_x + 88.f, slider_top + 68.f, inner_w - 92.f, kTrackH};

    // INPUT (Vocode | Clean mic) + MONITOR toggle — below sliders
    const float d_io = slider_top + 98.f;
    L.divider_io_y = d_io;
    L.io_input_label_y = d_io + 8.f;
    const float in_row_y = L.io_input_label_y + 16.f;
    float wv = 72.f;
    float wc = 92.f;
    float wm = 104.f;
    if (mfont != nullptr) {
        wv = measure_label_width(mfont, "Vocode", 44.f, 14.f);
        wc = measure_label_width(mfont, "Clean mic", 52.f, 14.f);
        wm = std::max(measure_label_width(mfont, "Monitor on", 48.f, 14.f),
                      measure_label_width(mfont, "Monitor off", 48.f, 14.f));
    }
    {
        const float tot_in = wv + chip_gap + wc;
        float x0 = inner_x + std::max(0.f, (inner_w - tot_in) * 0.5f);
        L.r_in_vocode = {x0, in_row_y, wv, chip_h};
        L.r_in_clean = {x0 + wv + chip_gap, in_row_y, wc, chip_h};
    }
    L.io_monitor_label_y = in_row_y + chip_h + 10.f;
    const float mon_row_y = L.io_monitor_label_y + 16.f;
    L.r_monitor = {inner_x + (inner_w - wm) * 0.5f, mon_row_y, wm, chip_h};

    return L;
}

constexpr float kCarrierPickerRowH = 22.f;
constexpr int kCarrierPickerMaxVisible = 12;

bool frect_contains_point(const SDL_FRect& r, float x, float y) {
    return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

SDL_FRect carrier_picker_panel_rect(const Layout& L, std::size_t n_entries) {
    const float ix = L.card.x + L.card_pad;
    const float iw = L.card.w - 2.f * L.card_pad;
    const float n = static_cast<float>(std::max<std::size_t>(std::size_t{1}, n_entries));
    const float rows = std::min(static_cast<float>(kCarrierPickerMaxVisible), n);
    const float ph = std::clamp(rows * kCarrierPickerRowH + 10.f, 50.f, 278.f);
    const float py = L.r_carrier_lib_btn.y + L.r_carrier_lib_btn.h + 4.f;
    return {ix, py, iw, ph};
}

int carrier_picker_visible_rows(const SDL_FRect& panel) {
    return std::max(1, static_cast<int>((panel.h - 10.f) / kCarrierPickerRowH));
}

float pill_corner_radius(const SDL_FRect& r) {
    // Fully rounded ends (stadium / pill) so the border follows curves, not a near-rectangle.
    return std::max(4.f, std::min(r.w, r.h) * 0.5f - 0.5f);
}

void draw_button(SDL_Renderer* ren, const SDL_FRect& r, const Rgba& face, const Rgba& border, bool hover,
                 TTF_Font* font, const char* label, const Rgba& text_col) {
    Rgba f = face;
    if (hover) {
        lighten_rgba(f, 12);
    }
    const float pr = pill_corner_radius(r);
    fill_round_rect(ren, r, pr, f);
    stroke_round_round_rect_double(ren, r, pr, border);
    if (font && label) {
        int lw = 0, lh = 0;
        TTF_SizeUTF8(font, label, &lw, &lh);
        blit_text(ren, font, label, text_col, r.x + (r.w - static_cast<float>(lw)) * 0.5f,
                  r.y + (r.h - static_cast<float>(lh)) * 0.5f);
    }
}

int header_hit(int mx, int my, const Layout& L) {
    if (point_in_rect(mx, my, L.r_start)) {
        return 0;
    }
    if (point_in_rect(mx, my, L.r_stop)) {
        return 1;
    }
    if (point_in_rect(mx, my, L.r_quit)) {
        return 2;
    }
    if (point_in_rect(mx, my, L.r_help)) {
        return 3;
    }
    return -1;
}

int preset_hit(int mx, int my, const Layout& L) {
    for (std::size_t i = 0; i < L.preset_chips.size(); ++i) {
        if (point_in_rect(mx, my, L.preset_chips[i])) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/** 0 = Vocode, 1 = Clean mic, 2 = Monitor toggle */
int io_hit(int mx, int my, const Layout& L) {
    if (point_in_rect(mx, my, L.r_in_vocode)) {
        return 0;
    }
    if (point_in_rect(mx, my, L.r_in_clean)) {
        return 1;
    }
    if (point_in_rect(mx, my, L.r_monitor)) {
        return 2;
    }
    return -1;
}

SDL_FRect pad_rect(const SDL_FRect& r, float px, float py) {
    return {r.x - px, r.y - py, r.w + 2.f * px, r.h + 2.f * py};
}

/** 0 = clarity, 1 = reverb */
int slider_hit(int mx, int my, const Layout& L) {
    if (point_in_rect(mx, my, pad_rect(L.r_clarity_track, 6.f, 10.f))) {
        return 0;
    }
    if (point_in_rect(mx, my, pad_rect(L.r_reverb_track, 6.f, 10.f))) {
        return 1;
    }
    return -1;
}

float slider_norm_from_mouse_x(int mx, const SDL_FRect& track) {
    const float t = (static_cast<float>(mx) - track.x) / std::max(track.w, 1.f);
    return std::clamp(t, 0.f, 1.f);
}

void draw_horiz_slider(SDL_Renderer* ren, const SDL_FRect& track, float t_norm, bool hover) {
    t_norm = std::clamp(t_norm, 0.f, 1.f);
    const float rad = std::min(5.f, track.h * 0.5f);
    fill_round_rect(ren, track, rad, kGhostFace);
    stroke_round_round_rect_double(ren, track, rad, kGhostBorder);
    if (t_norm > 0.02f) {
        SDL_FRect fill{track.x, track.y, std::max(2.f, track.w * t_norm), track.h};
        fill_round_rect(ren, fill, rad, kPrimaryBtn);
    }
    const float cx = track.x + t_norm * track.w;
    const float cy = track.y + track.h * 0.5f;
    fill_disk(ren, cx, cy, hover ? 8.f : 7.f, hover ? kBrand : kMuted);
}

/** Same search as CMake POST_BUILD: ``cpp/assets`` when exe lives in ``cpp/build``, else next to exe. */
std::filesystem::path resolve_app_icon_png_path(const std::filesystem::path& exe_dir) {
    const std::filesystem::path candidates[] = {
        exe_dir.parent_path() / "assets" / "app-icon.png",
        exe_dir / "app-icon.png",
    };
    for (const auto& p : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(p, ec)) {
            return p;
        }
    }
    return {};
}

/** Load PNG via stb_image into an RGBA32 SDL surface; caller must SDL_FreeSurface. Returns nullptr on failure. */
SDL_Surface* load_png_rgba_surface(const std::filesystem::path& path) {
    const std::string ps = path.string();
    int iw = 0;
    int ih = 0;
    unsigned char* px = stbi_load(ps.c_str(), &iw, &ih, nullptr, 4);
    if (px == nullptr || iw <= 0 || ih <= 0) {
        return nullptr;
    }
    SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, iw, ih, 32, SDL_PIXELFORMAT_RGBA32);
    if (surf == nullptr) {
        stbi_image_free(px);
        return nullptr;
    }
    if (SDL_MUSTLOCK(surf) != 0) {
        (void)SDL_LockSurface(surf);
    }
    const int row = iw * 4;
    auto* dst = static_cast<unsigned char*>(surf->pixels);
    for (int y = 0; y < ih; ++y) {
        std::memcpy(dst + static_cast<std::size_t>(y) * static_cast<std::size_t>(surf->pitch),
                    px + static_cast<std::size_t>(y) * static_cast<std::size_t>(row), static_cast<std::size_t>(row));
    }
    if (SDL_MUSTLOCK(surf) != 0) {
        SDL_UnlockSurface(surf);
    }
    stbi_image_free(px);
    return surf;
}

void try_apply_window_icon(SDL_Window* window, const std::filesystem::path& exe_dir) {
    const std::filesystem::path p = resolve_app_icon_png_path(exe_dir);
    if (p.empty()) {
        return;
    }
    SDL_Surface* surf = load_png_rgba_surface(p);
    if (surf == nullptr) {
        return;
    }
    SDL_SetWindowIcon(window, surf);
    SDL_FreeSurface(surf);
}

SDL_Texture* load_header_logo_texture(SDL_Renderer* ren, const std::filesystem::path& exe_dir) {
    const std::filesystem::path p = resolve_app_icon_png_path(exe_dir);
    if (p.empty()) {
        return nullptr;
    }
    SDL_Surface* surf = load_png_rgba_surface(p);
    if (surf == nullptr) {
        return nullptr;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(ren, surf);
    SDL_FreeSurface(surf);
    if (tex != nullptr) {
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    }
    return tex;
}

void draw_header_app_icon(SDL_Renderer* ren, SDL_Texture* tex, float cx, float cy, float scale) {
    if (tex == nullptr) {
        return;
    }
    int tw = 0;
    int th = 0;
    if (SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th) != 0 || tw <= 0 || th <= 0) {
        return;
    }
    constexpr float kNominalH = 50.f;
    const float dst_h = kNominalH * scale;
    const float dst_w = dst_h * (static_cast<float>(tw) / static_cast<float>(th));
    const SDL_FRect dst{cx - dst_w * 0.5f, cy - dst_h * 0.5f, dst_w, dst_h};
    SDL_RenderCopyF(ren, tex, nullptr, &dst);
}

}  // namespace

int run_sdl_gui(char* argv0, const char* carrier_path_opt) {
    const auto exe_dir = resolve_exe_dir(argv0);
    std::error_code ec_lib;
    const std::filesystem::path lib = carrier_library_dir(exe_dir);
    std::filesystem::create_directories(lib, ec_lib);
    carrier_convert_audio_in_folder(lv_gui::kSampleRate, lib);
    const std::filesystem::path exe_carriers = exe_dir / "LiveVocoderCarriers";
    ec_lib.clear();
    if (std::filesystem::is_directory(exe_carriers, ec_lib)) {
        ec_lib.clear();
        const auto lib_can = std::filesystem::weakly_canonical(lib, ec_lib);
        std::error_code ec2;
        const auto exe_can = std::filesystem::weakly_canonical(exe_carriers, ec2);
        if (!ec_lib && !ec2 && lib_can != exe_can) {
            carrier_convert_audio_in_folder(lv_gui::kSampleRate, exe_carriers);
        }
    }

    std::string carrier_path;
    if (carrier_path_opt != nullptr && carrier_path_opt[0] != '\0') {
        carrier_path = carrier_path_opt;
    } else {
        const char* env_c = std::getenv("LIVE_VOCODER_START_CARRIER");
        if (env_c != nullptr && env_c[0] != '\0') {
            carrier_path = env_c;
        } else {
            const std::filesystem::path picked = pick_default_carrier_f32(lib, exe_dir);
            if (!picked.empty()) {
                carrier_path = picked.string();
            }
        }
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
#if defined(SDL_HINT_RENDER_BATCHING)
    SDL_SetHint(SDL_HINT_RENDER_BATCHING, "1");
#endif

    if (TTF_Init() != 0) {
        std::fprintf(stderr, "TTF_Init: %s\n", TTF_GetError());
        SDL_Quit();
        return 1;
    }

    UiFont fonts;
    const bool fonts_ok = load_ui_fonts(exe_dir, fonts);

    SDL_Window* window = SDL_CreateWindow(
        "Live Vocoder", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 660, 620,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (window == nullptr) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        close_fonts(fonts);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    try_apply_window_icon(window, exe_dir);
    SDL_SetWindowMinimumSize(window, 400, 520);

    SDL_Renderer* ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
    if (ren == nullptr) {
        ren = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (ren == nullptr) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        close_fonts(fonts);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    SDL_EventState(SDL_DROPFILE, SDL_ENABLE);

    SDL_Texture* header_logo_tex = load_header_logo_texture(ren, exe_dir);

    std::string carrier_label =
        carrier_path.empty() ? "" : std::filesystem::path(carrier_path).filename().string();

    if (!carrier_path.empty()) {
        std::filesystem::path cp(carrier_path);
#if defined(_WIN32)
        cp = carrier_win32_localize_path_for_filesystem(cp);
#endif
        std::error_code ec;
        if (std::filesystem::is_regular_file(cp, ec)) {
            if (!carrier_path_is_raw_f32(cp)) {
                const std::filesystem::path lib = carrier_library_dir(exe_dir);
                std::filesystem::create_directories(lib, ec);
                std::filesystem::path stem = cp.stem();
                if (stem.empty()) {
                    stem = "carrier";
                }
                const std::filesystem::path dest = lib / (stem.string() + ".f32");
                std::string conv_err;
                if (carrier_ffmpeg_to_f32(lv_gui::kSampleRate, cp, dest, conv_err)) {
                    carrier_path = dest.string();
                    carrier_label = dest.filename().string();
                } else {
                    if (sdl_skip_startup_modals()) {
                        std::fprintf(stderr, "Live Vocoder: ffmpeg carrier conversion failed: %s\n", conv_err.c_str());
                    } else {
                        sdl_show_themed_message_box(SDL_MESSAGEBOX_WARNING, "Live Vocoder — carrier conversion",
                                                 ("Could not convert startup carrier (see README.txt):\n\n" + conv_err)
                                                     .c_str(),
                                                 window);
                    }
                    carrier_path.clear();
                    carrier_label.clear();
                }
            } else {
                carrier_path = cp.string();
                carrier_label = cp.filename().string();
            }
        }
    }

    double preset_wet = kPresets[0].wet;
    double preset_presence = kPresets[0].presence_db;
    int active_preset = 0;

    if (fonts_ok) {
#if defined(_WIN32)
        const char* kMonitorHelp =
            "Monitor on/off: on normal speakers, off silences them. Pick playback with LIVE_VOCODER_PA_OUTPUT "
            "(name substring) or LIVE_VOCODER_PA_LIST_DEVICES=1; see README for other PortAudio env vars.";
#else
        const char* kMonitorHelp =
            "Monitor on/off: silences local playback when off (including PipeWire default output). "
            "If playback goes to a null sink (PULSE_SINK), Monitor off keeps feeding that sink; "
            "Monitor on adds a loopback from that sink’s monitor to your default speakers.";
#endif
        const std::string welcome =
            std::string("Drop any audio file (or .f32) — it is converted with ffmpeg to mono 48 kHz f32le and "
                        "saved under Documents/LiveVocoderCarriers (same as GTK).\n"
                        "Or click Library… in the voice card to choose a file already in that folder.\n"
                        "Or place carrier.f32 there / next to the app. Needs ffmpeg on PATH for non-.f32.\n"
                        "Clean mic needs no carrier.\n\n") +
            kMonitorHelp + "\n\n— memesdudeguy";
        if (!sdl_skip_startup_modals()) {
            sdl_show_themed_message_box(SDL_MESSAGEBOX_INFORMATION, "Live Vocoder", welcome.c_str(), window);
        }
    } else {
        if (!sdl_skip_startup_modals()) {
            sdl_show_themed_message_box(
                SDL_MESSAGEBOX_WARNING, "Live Vocoder",
                "No UI font found. Install fonts/DejaVuSans.ttf next to the executable (see README), "
                "or use Segoe UI on Windows.\n\n"
                "Controls still work: header Start / Stop / Quit; drop audio or .f32 (saved under LiveVocoderCarriers).\n\n"
                "— memesdudeguy",
                window);
        } else {
            std::fprintf(stderr, "Live Vocoder: no UI font; place fonts/DejaVuSans.ttf next to the executable.\n");
        }
    }

    constexpr float kClarityMaxDb = 12.f;
    float ui_clarity_db = static_cast<float>(kPresets[0].presence_db);
    float ui_reverb_mix = kPresets[0].reverb_mix;

    lv_linux_ensure_pipewire_virt_mic_stack();
    lv_apply_pulse_sink_env_before_portaudio();

    PaError pa_err = Pa_Initialize();
    if (pa_err != paNoError) {
        sdl_show_themed_message_box(SDL_MESSAGEBOX_ERROR, "PortAudio", Pa_GetErrorText(pa_err), window);
        if (header_logo_tex != nullptr) {
            SDL_DestroyTexture(header_logo_tex);
            header_logo_tex = nullptr;
        }
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(window);
        close_fonts(fonts);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }
    pa_log_all_devices_if_requested(stderr);

    std::string virt_mic_status_foot;
    {
        const std::string pl = lv_linux_pulse_virt_mic_status_line();
        const std::string pa = pa_portaudio_virt_capture_hint();
        if (!pl.empty() && !pa.empty()) {
            virt_mic_status_foot = pl + " · " + pa;
        } else {
            virt_mic_status_foot = !pl.empty() ? pl : pa;
        }
    }
#if defined(_WIN32)
    {
        const std::string wh = pa_windows_virt_mic_route_hint();
        if (!wh.empty()) {
            if (!virt_mic_status_foot.empty()) {
                virt_mic_status_foot += " · ";
            }
            virt_mic_status_foot += wh;
        }
    }
#endif

    App app;
    app.reverb_mix.store(ui_reverb_mix, std::memory_order_relaxed);
    PaStream* stream = nullptr;
    bool streaming = false;

    auto refresh_title = [&]() {
        set_title(window, carrier_label, streaming, app.clean_mic.load(std::memory_order_relaxed),
                  app.monitor_on.load(std::memory_order_relaxed),
                  streaming && app.pulse_virt_sink_output.load(std::memory_order_relaxed));
    };
    refresh_title();

    /** When PULSE_SINK is the only output (null sink), PipeWire loopback sends monitor → default speakers. */
    static char s_pulse_sink_buf[256] = {};
    auto sync_virt_sink_speaker_loopback = [&]() {
        auto pulse_sink_for_monitor = []() -> const char* {
            const char* ps = std::getenv("PULSE_SINK");
            if (ps != nullptr && ps[0] != '\0') {
                return ps;
            }
            const char* lv = std::getenv("LIVE_VOCODER_PULSE_SINK");
            if (lv != nullptr && lv[0] != '\0') {
                return lv;
            }
#if defined(_WIN32)
            DWORD n = GetEnvironmentVariableA("PULSE_SINK", s_pulse_sink_buf, sizeof(s_pulse_sink_buf));
            if (n > 0U && n < sizeof(s_pulse_sink_buf)) {
                return s_pulse_sink_buf;
            }
            n = GetEnvironmentVariableA("LIVE_VOCODER_PULSE_SINK", s_pulse_sink_buf, sizeof(s_pulse_sink_buf));
            if (n > 0U && n < sizeof(s_pulse_sink_buf)) {
                return s_pulse_sink_buf;
            }
#endif
            return nullptr;
        };
#if defined(__linux__)
        auto sync_linux_physical_monitor_mute = [&]() {
            const bool v = app.pulse_virt_sink_output.load(std::memory_order_relaxed);
            const bool mon = app.monitor_on.load(std::memory_order_relaxed);
            if (v || !streaming || mon) {
                lv_linux_pulse_set_own_playback_muted(false);
            } else {
                lv_linux_pulse_set_own_playback_muted(true);
            }
        };
#elif defined(_WIN32)
        auto sync_linux_physical_monitor_mute = [&]() {
            lv_win32_wine_pulse_sync_monitor_mute(
                streaming, app.monitor_on.load(std::memory_order_relaxed),
                app.pulse_virt_sink_output.load(std::memory_order_relaxed));
        };
#else
        auto sync_linux_physical_monitor_mute = [&]() {};
#endif
        if (!streaming) {
            lv_linux_sync_speaker_monitor_loopback(false, nullptr);
            sync_linux_physical_monitor_mute();
            return;
        }
        if (!app.monitor_on.load(std::memory_order_relaxed)) {
            lv_linux_sync_speaker_monitor_loopback(false, nullptr);
            sync_linux_physical_monitor_mute();
            return;
        }
        if (!app.pulse_virt_sink_output.load(std::memory_order_relaxed)) {
#if defined(__linux__)
            // App stream may be on a LiveVocoder* null sink (e.g. pavugraph move) while PortAudio still
            // reports "Built-in Audio" — still wire null-sink monitor → default sink for Fix A (Monitor on).
            {
                const std::string ps_r = lv_linux_monitor_pulse_sink_base_for_loopback();
                if (!ps_r.empty()) {
                    lv_linux_sync_speaker_monitor_loopback(true, ps_r.c_str());
                } else {
                    lv_linux_sync_speaker_monitor_loopback(false, nullptr);
                }
            }
#endif
            sync_linux_physical_monitor_mute();
            return;
        }
        const char* ps = pulse_sink_for_monitor();
#if defined(__linux__)
        std::string ps_resolved;
        if (ps == nullptr || ps[0] == '\0') {
            ps_resolved = lv_linux_monitor_pulse_sink_base_for_loopback();
            if (!ps_resolved.empty()) {
                ps = ps_resolved.c_str();
            }
        }
#elif defined(_WIN32)
        std::string ps_wine_resolved;
        if (ps == nullptr || ps[0] == '\0') {
            ps_wine_resolved = lv_wine_monitor_pulse_sink_base_for_loopback();
            if (!ps_wine_resolved.empty()) {
                ps = ps_wine_resolved.c_str();
            }
        }
#endif
        if (ps == nullptr || ps[0] == '\0') {
            lv_linux_sync_speaker_monitor_loopback(false, nullptr);
            sync_linux_physical_monitor_mute();
            return;
        }
        lv_linux_sync_speaker_monitor_loopback(true, ps);
        sync_linux_physical_monitor_mute();
    };

    auto stop_stream = [&]() {
        if (stream != nullptr) {
            Pa_StopStream(stream);
            Pa_CloseStream(stream);
            stream = nullptr;
            pa_duplex_note_stream_closed();
        }
        app.pulse_virt_sink_output.store(false, std::memory_order_relaxed);
        app.voc.reset();
        app.reverb.reset();
        app.meter_in_peak.store(0.f, std::memory_order_relaxed);
        app.meter_out_peak.store(0.f, std::memory_order_relaxed);
        app.test_beep_frames_left.store(0, std::memory_order_relaxed);
        app.test_beep_phase = 0.f;
        streaming = false;
        sync_virt_sink_speaker_loopback();
        refresh_title();
    };

    auto try_start = [&]() -> bool {
        if (app.clean_mic.load(std::memory_order_relaxed)) {
            stop_stream();
            app.voc.reset();
            PaError err = pa_open_livevocoder_duplex(&stream, static_cast<double>(lv_gui::kSampleRate),
                                                   static_cast<unsigned long>(lv_gui::kHop), lv_gui::livevocoder_gui_pa_callback, &app);
            if (err != paNoError) {
                sdl_show_themed_message_box(SDL_MESSAGEBOX_ERROR, "PortAudio", Pa_GetErrorText(err), window);
                return false;
            }
            pa_log_stream_devices(stream);
            app.pulse_virt_sink_output.store(pa_duplex_output_targets_virt_sink_route(), std::memory_order_relaxed);
            err = Pa_StartStream(stream);
            if (err != paNoError) {
                Pa_CloseStream(stream);
                stream = nullptr;
                pa_duplex_note_stream_closed();
                app.pulse_virt_sink_output.store(false, std::memory_order_relaxed);
                sdl_show_themed_message_box(SDL_MESSAGEBOX_ERROR, "PortAudio", Pa_GetErrorText(err), window);
                return false;
            }
#if defined(__linux__)
            lv_linux_move_livevocoder_sink_input_after_pa_start();
#elif defined(_WIN32)
            lv_linux_wine_move_livevocoder_sink_input_after_pa_start();
#endif
            app.reverb.configure(lv_gui::kSampleRate, 0.55f);
            app.reverb_mix.store(ui_reverb_mix, std::memory_order_relaxed);
            streaming = true;
            sync_virt_sink_speaker_loopback();
            refresh_title();
            return true;
        }

        if (carrier_path.empty()) {
            sdl_show_themed_message_box(SDL_MESSAGEBOX_WARNING, "Live Vocoder",
                                     "Vocode mode needs a carrier (audio file or .f32). Use Library… in the voice card, "
                                     "drop a file, choose Clean mic, or put carrier.f32 in "
                                     "Documents/LiveVocoderCarriers or next to the executable.",
                                     window);
            return false;
        }
        std::filesystem::path cp(carrier_path);
#if defined(_WIN32)
        cp = carrier_win32_localize_path_for_filesystem(cp);
#endif
        std::error_code fsec;
        if (!std::filesystem::is_regular_file(cp, fsec)) {
            sdl_show_themed_message_box(SDL_MESSAGEBOX_ERROR, "Live Vocoder", "Carrier file is not available.", window);
            return false;
        }
        if (!carrier_path_is_raw_f32(cp)) {
            const std::filesystem::path lib = carrier_library_dir(exe_dir);
            std::filesystem::create_directories(lib, fsec);
            std::filesystem::path stem = cp.stem();
            if (stem.empty()) {
                stem = "carrier";
            }
            const std::filesystem::path dest = lib / (stem.string() + ".f32");
            std::string conv_err;
            if (!carrier_ffmpeg_to_f32(lv_gui::kSampleRate, cp, dest, conv_err)) {
                sdl_show_themed_message_box(SDL_MESSAGEBOX_ERROR, "Live Vocoder — carrier conversion",
                                         ("Could not convert to .f32 (see README.txt next to the app):\n\n" + conv_err)
                                             .c_str(),
                                         window);
                return false;
            }
            carrier_path = dest.string();
            carrier_label = dest.filename().string();
            refresh_title();
        }
        std::vector<double> carrier;
        try {
            carrier = load_carrier_f32(carrier_path.c_str());
        } catch (const std::exception& e) {
            sdl_show_themed_message_box(SDL_MESSAGEBOX_ERROR, "Carrier", e.what(), window);
            return false;
        }
        stop_stream();
        try {
            app.voc = std::make_unique<StreamingVocoderCpp>(std::move(carrier), lv_gui::kSampleRate, 2048, lv_gui::kHop, preset_wet, 36,
                                                            0.62, 0.88, preset_presence, 1800.0, 1.0);
        } catch (const std::exception& e) {
            sdl_show_themed_message_box(SDL_MESSAGEBOX_ERROR, "Vocoder", e.what(), window);
            return false;
        }
        app.voc->set_wet_level(preset_wet);
        app.voc->set_mod_presence_db(ui_clarity_db);
        app.reverb.configure(lv_gui::kSampleRate, 0.55f);
        app.reverb_mix.store(ui_reverb_mix, std::memory_order_relaxed);

        PaError err = pa_open_livevocoder_duplex(&stream, static_cast<double>(lv_gui::kSampleRate),
                                                 static_cast<unsigned long>(lv_gui::kHop), lv_gui::livevocoder_gui_pa_callback, &app);
        if (err != paNoError) {
            app.voc.reset();
            sdl_show_themed_message_box(SDL_MESSAGEBOX_ERROR, "PortAudio", Pa_GetErrorText(err), window);
            return false;
        }
        pa_log_stream_devices(stream);
        app.pulse_virt_sink_output.store(pa_duplex_output_targets_virt_sink_route(), std::memory_order_relaxed);
        err = Pa_StartStream(stream);
        if (err != paNoError) {
            Pa_CloseStream(stream);
            stream = nullptr;
            pa_duplex_note_stream_closed();
            app.pulse_virt_sink_output.store(false, std::memory_order_relaxed);
            app.voc.reset();
            sdl_show_themed_message_box(SDL_MESSAGEBOX_ERROR, "PortAudio", Pa_GetErrorText(err), window);
            return false;
        }
#if defined(__linux__)
        lv_linux_move_livevocoder_sink_input_after_pa_start();
#elif defined(_WIN32)
        lv_linux_wine_move_livevocoder_sink_input_after_pa_start();
#endif
        streaming = true;
        sync_virt_sink_speaker_loopback();
        refresh_title();
        return true;
    };

    bool carrier_picker_open = false;
    std::vector<std::filesystem::path> carrier_picker_entries;
    int carrier_picker_scroll = 0;

    auto refresh_carrier_picker_list = [&]() {
        carrier_convert_audio_in_folder(lv_gui::kSampleRate, lib);
        carrier_collect_library_entries(lib, carrier_picker_entries);
        carrier_picker_scroll = 0;
    };

    bool quit = false;
    float meter_disp_in = 0.f;
    float meter_disp_out = 0.f;
    int mx = 0, my = 0;
    int slider_drag = -1;
    SDL_Texture* bg_grad_tex = nullptr;
    int bg_grad_cw = 0;
    int bg_grad_ch = 0;
    while (!quit) {
        int ww = 640, wh = 600;
        SDL_GetWindowSize(window, &ww, &wh);

        TTF_Font* lay_font = fonts_ok ? fonts.small : nullptr;
        Layout lay = compute_layout(ww, wh, fonts_ok, lay_font);
        if (streaming) {
            meter_disp_in =
                std::max(app.meter_in_peak.load(std::memory_order_relaxed), meter_disp_in * 0.90f);
            meter_disp_out =
                std::max(app.meter_out_peak.load(std::memory_order_relaxed), meter_disp_out * 0.90f);
        } else {
            meter_disp_in *= 0.82f;
            meter_disp_out *= 0.82f;
        }
        lay.preset_ix_hover = preset_hit(mx, my, lay);
        lay.header_hover = header_hit(mx, my, lay);
        const int io_hover = io_hit(mx, my, lay);
        const int slider_hover_ix = slider_drag >= 0 ? slider_drag : slider_hit(mx, my, lay);

        int carrier_picker_hover_ix = -1;
        if (carrier_picker_open) {
            const SDL_FRect pp = carrier_picker_panel_rect(lay, carrier_picker_entries.size());
            if (frect_contains_point(pp, static_cast<float>(mx), static_cast<float>(my))) {
                const int rel = static_cast<int>((static_cast<float>(my) - pp.y - 5.f) / kCarrierPickerRowH);
                const int ix = carrier_picker_scroll + rel;
                if (rel >= 0 && ix >= 0 && ix < static_cast<int>(carrier_picker_entries.size())) {
                    carrier_picker_hover_ix = ix;
                }
            }
            const int vis = carrier_picker_visible_rows(pp);
            const int smax = std::max(0, static_cast<int>(carrier_picker_entries.size()) - vis);
            if (carrier_picker_scroll > smax) {
                carrier_picker_scroll = smax;
            }
            if (carrier_picker_scroll < 0) {
                carrier_picker_scroll = 0;
            }
        }

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            } else if (e.type == SDL_KEYDOWN && e.key.repeat == 0 && e.key.keysym.sym == SDLK_F9) {
                if (streaming) {
                    app.test_beep_frames_left.store(lv_gui::kSampleRate / 2, std::memory_order_relaxed);
                }
            } else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                // layout refresh next frame
            } else if (e.type == SDL_MOUSEMOTION) {
                mx = e.motion.x;
                my = e.motion.y;
                if (slider_drag >= 0 && (e.motion.state & SDL_BUTTON_LMASK) != 0) {
                    const Layout Lm = compute_layout(ww, wh, fonts_ok, fonts_ok ? fonts.small : nullptr);
                    if (slider_drag == 0) {
                        ui_clarity_db = slider_norm_from_mouse_x(mx, Lm.r_clarity_track) * kClarityMaxDb;
                        if (app.voc) {
                            app.voc->set_mod_presence_db(ui_clarity_db);
                        }
                    } else if (slider_drag == 1) {
                        ui_reverb_mix = slider_norm_from_mouse_x(mx, Lm.r_reverb_track);
                        app.reverb_mix.store(ui_reverb_mix, std::memory_order_relaxed);
                    }
                }
            } else if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
                slider_drag = -1;
            } else if (e.type == SDL_MOUSEWHEEL && carrier_picker_open) {
                const Layout Lw = compute_layout(ww, wh, fonts_ok, fonts_ok ? fonts.small : nullptr);
                const SDL_FRect pan = carrier_picker_panel_rect(Lw, carrier_picker_entries.size());
                if (frect_contains_point(pan, static_cast<float>(mx), static_cast<float>(my))) {
                    carrier_picker_scroll -= e.wheel.y;
                }
            } else if (e.type == SDL_DROPFILE) {
                char* p = e.drop.file;
                if (p != nullptr) {
                    std::string dropped = p;
                    SDL_free(p);
                    if (streaming) {
                        stop_stream();
                    }
                    std::filesystem::path stored;
                    std::string ierr;
                    if (!ingest_dropped_carrier(exe_dir, std::filesystem::path(dropped), stored, ierr)) {
                        sdl_show_themed_message_box(SDL_MESSAGEBOX_ERROR, "Live Vocoder",
                                                 ("Could not add carrier (ffmpeg required for non-.f32):\n" + ierr)
                                                     .c_str(),
                                                 window);
                    } else {
                        carrier_path = stored.string();
                        carrier_label = stored.filename().string();
                        refresh_title();
                    }
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                const int cx = e.button.x;
                const int cy = e.button.y;
                const Layout L = compute_layout(ww, wh, fonts_ok, fonts_ok ? fonts.small : nullptr);
                bool consumed = false;
                const bool on_lib =
                    frect_contains_point(L.r_carrier_lib_btn, static_cast<float>(cx), static_cast<float>(cy));
                if (on_lib) {
                    carrier_picker_open = !carrier_picker_open;
                    if (carrier_picker_open) {
                        refresh_carrier_picker_list();
                    }
                    consumed = true;
                } else if (carrier_picker_open) {
                    const SDL_FRect pan = carrier_picker_panel_rect(L, carrier_picker_entries.size());
                    if (frect_contains_point(pan, static_cast<float>(cx), static_cast<float>(cy))) {
                        const int vis = carrier_picker_visible_rows(pan);
                        const int rel =
                            static_cast<int>((static_cast<float>(cy) - pan.y - 5.f) / kCarrierPickerRowH);
                        if (rel >= 0 && rel < vis) {
                            const int ix = carrier_picker_scroll + rel;
                            if (ix >= 0 && ix < static_cast<int>(carrier_picker_entries.size())) {
                                if (streaming) {
                                    stop_stream();
                                }
                                carrier_path = carrier_picker_entries[static_cast<std::size_t>(ix)].string();
                                carrier_label =
                                    carrier_picker_entries[static_cast<std::size_t>(ix)].filename().string();
                                carrier_picker_open = false;
                                refresh_title();
                            }
                        }
                        consumed = true;
                    } else {
                        carrier_picker_open = false;
                    }
                }
                if (!consumed) {
                const int hh = header_hit(cx, cy, L);
                if (hh == 0) {
                    (void)try_start();
                } else if (hh == 1) {
                    stop_stream();
                } else if (hh == 2) {
                    quit = true;
                } else if (hh == 3) {
#if defined(_WIN32)
                    sdl_show_themed_message_box(SDL_MESSAGEBOX_INFORMATION, "Live Vocoder — Help",
                                             "Vocode: drop WAV/MP3/FLAC/etc. or .f32 — other formats are converted with "
                                             "ffmpeg (install ffmpeg, PATH) to mono 48 kHz f32 under "
                                             "Documents/LiveVocoderCarriers.\n"
                                             "Use Library… in the voice card to pick a carrier from that folder.\n"
                                             "On startup, that carrier folder is scanned: audio files become .f32 when "
                                             "the .f32 is missing or older than the audio file.\n"
                                             "Default: carrier.f32 in that folder or beside the .exe.\n"
                                             "Clean mic: dry passthrough — no carrier; press Start.\n"
                                             "Monitor: hear output on the chosen playback device; Off silences typical "
                                             "local playback (see also the startup note on Monitor).\n"
                                             "Quick sound chips: wet + presence baseline; Clarity / Reverb sliders tune live.\n\n"
                                             "Windows: LIVE_VOCODER_PA_INPUT / LIVE_VOCODER_PA_OUTPUT (name substring) or "
                                             "*_INDEX; LIVE_VOCODER_PA_LIST_DEVICES=1 lists devices.\n"
                                             "Under Wine on Linux, the stream appears on the host as a normal Pulse/PipeWire "
                                             "client — use your desktop’s audio tools to route or monitor it.\n"
                                             "While streaming, Mic in / Output meters show levels.\n"
                                             "Press F9 for a short test beep on the output path.\n\n"
                                             "— memesdudeguy",
                                             window);
#else
                    sdl_show_themed_message_box(SDL_MESSAGEBOX_INFORMATION, "Live Vocoder — Help",
                                             "Vocode: drop WAV/MP3/FLAC/etc. or .f32 — other formats are converted with "
                                             "ffmpeg (install ffmpeg, PATH) to mono 48 kHz f32 under "
                                             "Documents/LiveVocoderCarriers.\n"
                                             "Use Library… in the voice card to pick a carrier from that folder.\n"
                                             "On startup, that carrier folder is scanned: audio files become .f32 when "
                                             "the .f32 is missing or older than the audio file.\n"
                                             "Default: carrier.f32 in that folder or beside the .exe.\n"
                                             "Clean mic: dry passthrough — no carrier; press Start.\n"
                                             "Monitor: local playback on/off; with a null sink (PULSE_SINK / "
                                             "LIVE_VOCODER_PULSE_SINK), Monitor off still feeds that sink while silencing "
                                             "default speakers via loopback control.\n"
                                             "Quick sound chips: wet + presence baseline; Clarity / Reverb sliders tune live.\n"
                                             "Mic in / Output meters show levels while streaming.\n\n"
                                             "Linux routing: LIVE_VOCODER_PULSE_SINK or PULSE_SINK for a null sink before "
                                             "PortAudio opens; LIVE_VOCODER_PA_OUTPUT=Null (substring) or "
                                             "LIVE_VOCODER_PA_LIST_DEVICES=1 to pick devices. The status line summarizes "
                                             "PipeWire when pactl is available.\n"
                                             "While streaming, press F9 for a short test beep on the output path (virtual "
                                             "sink / monitor).\n\n"
                                             "— memesdudeguy",
                                             window);
#endif
                } else {
                    const int sh = slider_hit(cx, cy, L);
                    if (sh == 0) {
                        slider_drag = 0;
                        ui_clarity_db = slider_norm_from_mouse_x(cx, L.r_clarity_track) * kClarityMaxDb;
                        if (app.voc) {
                            app.voc->set_mod_presence_db(ui_clarity_db);
                        }
                    } else if (sh == 1) {
                        slider_drag = 1;
                        ui_reverb_mix = slider_norm_from_mouse_x(cx, L.r_reverb_track);
                        app.reverb_mix.store(ui_reverb_mix, std::memory_order_relaxed);
                    } else {
                    const int ioh = io_hit(cx, cy, L);
                    if (ioh == 0) {
                        if (app.clean_mic.load(std::memory_order_relaxed)) {
                            app.clean_mic.store(false);
                            if (streaming) {
                                if (!try_start()) {
                                    app.clean_mic.store(true);
                                    (void)try_start();
                                }
                            } else {
                                refresh_title();
                            }
                        }
                    } else if (ioh == 1) {
                        if (!app.clean_mic.load(std::memory_order_relaxed)) {
                            app.clean_mic.store(true);
                            if (streaming) {
                                (void)try_start();
                            } else {
                                refresh_title();
                            }
                        }
                    } else if (ioh == 2) {
                        app.monitor_on.store(!app.monitor_on.load(std::memory_order_relaxed));
                        if (streaming) {
                            sync_virt_sink_speaker_loopback();
                        }
                        refresh_title();
                    } else {
                        const int pi = preset_hit(cx, cy, L);
                        if (pi >= 0 && static_cast<std::size_t>(pi) < sizeof(kPresets) / sizeof(kPresets[0])) {
                            active_preset = pi;
                            preset_wet = kPresets[static_cast<std::size_t>(pi)].wet;
                            preset_presence = kPresets[static_cast<std::size_t>(pi)].presence_db;
                            ui_clarity_db = static_cast<float>(preset_presence);
                            ui_reverb_mix = kPresets[static_cast<std::size_t>(pi)].reverb_mix;
                            app.reverb_mix.store(ui_reverb_mix, std::memory_order_relaxed);
                            if (streaming) {
                                (void)try_start();
                            }
                        }
                    }
                    }
                }
                }
            }
        }

        SDL_RenderSetViewport(ren, nullptr);
        if (ensure_gradient_bg_texture(ren, &bg_grad_tex, &bg_grad_cw, &bg_grad_ch, ww, wh)) {
            SDL_RenderCopy(ren, bg_grad_tex, nullptr, nullptr);
        } else {
            draw_gradient_bg(ren, ww, wh);
        }
        // Solid header strip (reference: flat bar over the gradient)
        if (lay.card.y > 1.f) {
            set_color(ren, kBgTop);
            SDL_FRect hdr_bar{0.f, 0.f, static_cast<float>(ww), lay.card.y - 1.f};
            SDL_RenderFillRectF(ren, &hdr_bar);
        }

        // Header: one band — text left (width capped), buttons right (layout.subtitle_max_px)
        if (fonts_ok) {
            draw_header_app_icon(ren, header_logo_tex, lay.mic_cx, lay.mic_cy, lay.mic_scale);
            const std::string title_show = ellipsize_utf8(std::string("Live Vocoder"), fonts.brand, lay.title_max_px);
            blit_text(ren, fonts.brand, title_show.c_str(), kBrand, lay.text_left, lay.title_y);
            const std::string sub_full =
                "memesdudeguy · Vocode or Clean mic · carriers in Documents/LiveVocoderCarriers";
            const std::string sub_show = ellipsize_utf8(sub_full, fonts.small, lay.subtitle_max_px);
            blit_text(ren, fonts.small, sub_show.c_str(), kMuted, lay.text_left, lay.subtitle_y);
        } else {
            draw_header_app_icon(ren, header_logo_tex, lay.mic_cx, lay.mic_cy, lay.mic_scale);
        }

        const bool h0 = lay.header_hover == 0;
        const bool h1 = lay.header_hover == 1;
        const bool h2 = lay.header_hover == 2;
        const bool h3 = lay.header_hover == 3;
        TTF_Font* btn_font = fonts_ok ? fonts.small : nullptr;
        draw_button(ren, lay.r_start, kPrimaryBtn, kPrimaryBorder, h0, btn_font, "Start", kBrand);
        draw_button(ren, lay.r_stop, kDangerBtn, kDangerBorder, h1, btn_font, "Stop", kBrand);
        draw_button(ren, lay.r_quit, kGhostFace, kGhostBorder, h2, btn_font, "Quit", kMuted);
        draw_button(ren, lay.r_help, kGhostFace, kGhostBorder, h3, btn_font, "?", kSection);

        // Card shadow + framed panel
        SDL_FRect csh = lay.card;
        csh.x += 2.f;
        csh.y += 3.f;
        fill_round_rect(ren, csh, 16.f, kCardShadow);
        fill_round_rect(ren, lay.card, 14.f, kCardFill);
        stroke_round_round_rect_double(ren, lay.card, 14.f, kCardBorder);

        const float tx = lay.card.x + lay.card_pad;
        const float tw = lay.card.w - 2.f * lay.card_pad;
        float ty = lay.card.y + lay.card_pad;
        if (fonts_ok) {
            blit_text(ren, fonts.small, "VOICE SOURCE", kSection, tx, ty);
            ty += 24.f;
            const int path_max_px =
                static_cast<int>(std::max(48.f, tw - lay.r_carrier_lib_btn.w - 14.f));
            const std::string path_show =
                carrier_path.empty()
                    ? "No carrier — drop audio or .f32 (ffmpeg converts; saved under LiveVocoderCarriers)"
                    : ellipsize_utf8(carrier_path, fonts.body, path_max_px);
            blit_text(ren, fonts.body, path_show.c_str(), kBrand, tx, ty);
            const bool lib_hover = frect_contains_point(lay.r_carrier_lib_btn, static_cast<float>(mx),
                                                        static_cast<float>(my)) ||
                                   carrier_picker_open;
            draw_button(ren, lay.r_carrier_lib_btn, kGhostFace, kGhostBorder, lib_hover, fonts.small, "Library…",
                        kSection);
            ty += 24.f;
            blit_text(ren, fonts.small, "Pick a loop or pad; your mic shapes the spectrum.", kMuted, tx, ty);

            if (carrier_picker_open) {
                const SDL_FRect pan = carrier_picker_panel_rect(lay, carrier_picker_entries.size());
                fill_round_rect(ren, pan, 8.f, kChipFace);
                stroke_round_round_rect_double(ren, pan, 8.f, kCardBorder);
                if (carrier_picker_entries.empty()) {
                    blit_text(ren, fonts.small,
                              "No carriers found — add .mp3, .wav, .flac, .f32, etc. to LiveVocoderCarriers", kMuted,
                              pan.x + 8.f, pan.y + 8.f);
                } else {
                    const int vis = carrier_picker_visible_rows(pan);
                    for (int r = 0; r < vis; ++r) {
                        const int ix = carrier_picker_scroll + r;
                        if (ix < 0 || ix >= static_cast<int>(carrier_picker_entries.size())) {
                            break;
                        }
                        const float ry = pan.y + 5.f + static_cast<float>(r) * kCarrierPickerRowH;
                        if (ix == carrier_picker_hover_ix) {
                            const SDL_FRect row_rect{pan.x + 4.f, ry - 1.f, pan.w - 8.f, kCarrierPickerRowH};
                            fill_round_rect(ren, row_rect, 5.f, kChipSelFace);
                        }
                        const std::string fn = carrier_picker_entries[static_cast<std::size_t>(ix)].filename().string();
                        const std::string show = ellipsize_utf8(fn, fonts.body, static_cast<int>(pan.w - 20.f));
                        blit_text(ren, fonts.body, show.c_str(), kBrand, pan.x + 10.f, ry);
                    }
                }
            }

            set_color(ren, kDivider);
            SDL_RenderDrawLineF(ren, tx, lay.divider_y0, tx + tw, lay.divider_y0);

            ty = lay.divider_y0 + 14.f;
            blit_text(ren, fonts.small, "QUICK SOUND", kSection, tx, ty);
        } else {
            const bool lib_hover = frect_contains_point(lay.r_carrier_lib_btn, static_cast<float>(mx),
                                                        static_cast<float>(my)) ||
                                   carrier_picker_open;
            draw_button(ren, lay.r_carrier_lib_btn, kGhostFace, kGhostBorder, lib_hover, nullptr, "", kMuted);
        }

        for (std::size_t i = 0; i < lay.preset_chips.size(); ++i) {
            const bool sel = static_cast<int>(i) == active_preset;
            const bool hv = lay.preset_ix_hover == static_cast<int>(i);
            Rgba face = sel ? kChipSelFace : kChipFace;
            Rgba border = sel ? kChipSelBorder : kChipBorder;
            if (hv && !sel) {
                lighten_rgba(face, 10);
                lighten_rgba(border, 18);
            } else if (hv && sel) {
                lighten_rgba(face, 8);
            }
            const SDL_FRect& cr = lay.preset_chips[i];
            const float rad = pill_corner_radius(cr);
            fill_round_rect(ren, cr, rad, face);
            stroke_round_round_rect_double(ren, cr, rad, border);
            if (fonts_ok) {
                int lw = 0, lh = 0;
                TTF_SizeUTF8(fonts.small, kPresets[i].label, &lw, &lh);
                const Rgba& tcol = sel ? kBrand : kMuted;
                blit_text(ren, fonts.small, kPresets[i].label, tcol, cr.x + (cr.w - static_cast<float>(lw)) * 0.5f,
                          cr.y + (cr.h - static_cast<float>(lh)) * 0.5f);
            }
        }

        {
            const float cn = std::clamp(ui_clarity_db / kClarityMaxDb, 0.f, 1.f);
            if (fonts_ok) {
                blit_text(ren, fonts.small, "CLARITY", kSection, tx, lay.clarity_label_y);
                blit_text(ren, fonts.small, "REVERB", kSection, tx, lay.reverb_label_y);
                char vbuf[40];
                std::snprintf(vbuf, sizeof(vbuf), "%.1f dB", static_cast<double>(ui_clarity_db));
                blit_text(ren, fonts.small, vbuf, kMuted, lay.r_clarity_track.x + lay.r_clarity_track.w + 6.f,
                          lay.r_clarity_track.y - 2.f);
                std::snprintf(vbuf, sizeof(vbuf), "%.0f%%", static_cast<double>(ui_reverb_mix * 100.f));
                blit_text(ren, fonts.small, vbuf, kMuted, lay.r_reverb_track.x + lay.r_reverb_track.w + 6.f,
                          lay.r_reverb_track.y - 2.f);
            }
            draw_horiz_slider(ren, lay.r_clarity_track, cn, slider_hover_ix == 0);
            draw_horiz_slider(ren, lay.r_reverb_track, std::clamp(ui_reverb_mix, 0.f, 1.f),
                              slider_hover_ix == 1);
        }

        const bool clean_mode = app.clean_mic.load(std::memory_order_relaxed);
        const bool mon_on = app.monitor_on.load(std::memory_order_relaxed);

        set_color(ren, kDivider);
        SDL_RenderDrawLineF(ren, tx, lay.divider_io_y, tx + tw, lay.divider_io_y);
        if (fonts_ok) {
            blit_text(ren, fonts.small, "INPUT", kSection, tx, lay.io_input_label_y);
            blit_text(ren, fonts.small, "SPEAKERS", kSection, tx, lay.io_monitor_label_y);
        }

        auto draw_mode_chip = [&](const SDL_FRect& cr, const char* label, bool selected, int self_ix) {
            const bool hv = io_hover == self_ix;
            Rgba face = selected ? kChipSelFace : kChipFace;
            Rgba border = selected ? kChipSelBorder : kChipBorder;
            if (hv && !selected) {
                lighten_rgba(face, 10);
                lighten_rgba(border, 18);
            } else if (hv && selected) {
                lighten_rgba(face, 8);
            }
            const float rad = pill_corner_radius(cr);
            fill_round_rect(ren, cr, rad, face);
            stroke_round_round_rect_double(ren, cr, rad, border);
            if (fonts_ok) {
                int lw = 0, lh = 0;
                TTF_SizeUTF8(fonts.small, label, &lw, &lh);
                blit_text(ren, fonts.small, label, selected ? kBrand : kMuted, cr.x + (cr.w - static_cast<float>(lw)) * 0.5f,
                          cr.y + (cr.h - static_cast<float>(lh)) * 0.5f);
            }
        };

        draw_mode_chip(lay.r_in_vocode, "Vocode", !clean_mode, 0);
        draw_mode_chip(lay.r_in_clean, "Clean mic", clean_mode, 1);

        {
            const SDL_FRect& mr = lay.r_monitor;
            const bool hv = io_hover == 2;
            Rgba face = mon_on ? kPrimaryBtn : kGhostFace;
            Rgba border = mon_on ? kPrimaryBorder : kGhostBorder;
            if (hv) {
                lighten_rgba(face, 10);
                lighten_rgba(border, 14);
            }
            const float rad = pill_corner_radius(mr);
            fill_round_rect(ren, mr, rad, face);
            stroke_round_round_rect_double(ren, mr, rad, border);
            if (fonts_ok) {
                const char* ml = mon_on ? "Monitor on" : "Monitor off";
                int lw = 0, lh = 0;
                TTF_SizeUTF8(fonts.small, ml, &lw, &lh);
                blit_text(ren, fonts.small, ml, mon_on ? kBrand : kMuted, mr.x + (mr.w - static_cast<float>(lw)) * 0.5f,
                          mr.y + (mr.h - static_cast<float>(lh)) * 0.5f);
            }
        }

        if (fonts_ok) {
            constexpr float kMeterBarH = 5.f;
            const float mtx = lay.card.x + lay.card_pad;
            const float mtw = lay.card.w - 2.f * lay.card_pad;
            constexpr float kMeterLabelW = 56.f;
            const float mbar_x = mtx + kMeterLabelW;
            const float mbar_w = mtw - kMeterLabelW - 4.f;
            const float foot_y = lay.card.y + lay.card.h;
            if (streaming || meter_disp_in > 0.002f || meter_disp_out > 0.002f) {
                blit_text(ren, fonts.small, "Mic in", kMuted, mtx, foot_y - 52.f);
                draw_audio_meter_bar(ren, mbar_x, foot_y - 50.f, mbar_w, kMeterBarH,
                                     std::min(1.f, meter_disp_in * 2.2f), kMicPink);
                blit_text(ren, fonts.small, "Output", kMuted, mtx, foot_y - 42.f);
                draw_audio_meter_bar(ren, mbar_x, foot_y - 40.f, mbar_w, kMeterBarH,
                                     std::min(1.f, meter_disp_out * 2.2f), kPrimaryBtn);
            }
            std::string st;
            const bool sink_out = app.pulse_virt_sink_output.load(std::memory_order_relaxed);
            if (!streaming) {
                st = clean_mode ? "Status · idle — Clean mic (no carrier needed)"
                                : "Status · idle — Vocode needs a carrier (audio or .f32)";
            } else if (clean_mode) {
                if (mon_on) {
                    st = "Status · clean mic → speakers (dry)";
                } else if (sink_out) {
#if defined(_WIN32)
                    st = "Status · clean mic · to virtual output route";
#else
                    st = "Status · clean mic · to PULSE_SINK (null sink still fed)";
#endif
                } else {
                    st = "Status · clean mic · speakers muted";
                }
            } else {
                if (mon_on) {
                    st = "Status · vocoder → speakers";
                } else if (sink_out) {
#if defined(_WIN32)
                    st = "Status · vocoding · to virtual output route";
#else
                    st = "Status · vocoding · to PULSE_SINK (null sink still fed)";
#endif
                } else {
                    st = "Status · vocoding · speakers muted";
                }
            }
            if (!virt_mic_status_foot.empty()) {
                st += " · ";
                st += virt_mic_status_foot;
            }
            const std::string st_show = ellipsize_utf8(st, fonts.small, static_cast<int>(tw));
            blit_text(ren, fonts.small, st_show.c_str(), kFoot, lay.card.x + lay.card_pad,
                      lay.card.y + lay.card.h - 26.f);
        }

        SDL_RenderPresent(ren);
    }

    stop_stream();
    Pa_Terminate();
    if (bg_grad_tex != nullptr) {
        SDL_DestroyTexture(bg_grad_tex);
        bg_grad_tex = nullptr;
    }
    g_text_tex_cache.clear();
    if (header_logo_tex != nullptr) {
        SDL_DestroyTexture(header_logo_tex);
        header_logo_tex = nullptr;
    }
    close_fonts(fonts);
    TTF_Quit();
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
