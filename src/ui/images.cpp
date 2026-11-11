#include "ui/images.hpp"

#include <dirent.h>
#include <stb_image.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <deque>
#include <fstream>
#include <sstream>

#include "core/log.hpp"
#include "ui/halo.hpp"

namespace playfin::ui {

namespace {

bool isCacheFile(const std::string& name) {
    if (name.size() != 20 || name.compare(16, 4, ".img") != 0) {
        return false;
    }
    return std::all_of(name.begin(), name.begin() + 16,
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::size_t textureBytes(const Texture& texture) {
    return static_cast<std::size_t>(texture.width) * static_cast<std::size_t>(texture.height) * 4;
}

}  // namespace

std::uint64_t fnv1a(std::string_view text) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (char c : text) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 1099511628211ULL;
    }
    return hash;
}

bool looksLikeImage(const std::string& bytes) {
    if (bytes.empty() || bytes.size() > 64u * 1024u * 1024u) {
        return false;
    }
    int w = 0;
    int h = 0;
    int channels = 0;
    if (stbi_info_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()), static_cast<int>(bytes.size()), &w,
                              &h, &channels) == 0) {
        return false;
    }
    return w > 0 && h > 0 && w <= kMaxImageSide && h <= kMaxImageSide &&
           static_cast<long long>(w) * static_cast<long long>(h) <= kMaxImagePixels;
}

namespace {

// `image` scaled (bilinear) so that it covers width x height, then cut to exactly that size around its centre
DecodedImage coverResize(const DecodedImage& image, int width, int height) {
    DecodedImage out;
    out.width = width;
    out.height = height;
    out.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    const double scale = std::max(static_cast<double>(width) / image.width, static_cast<double>(height) / image.height);
    const double offsetX = (image.width * scale - width) / 2;
    const double offsetY = (image.height * scale - height) / 2;
    const auto texel = [&image](int x, int y, int c) {
        x = std::clamp(x, 0, image.width - 1);
        y = std::clamp(y, 0, image.height - 1);
        return image.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                           static_cast<std::size_t>(x)) * 4 + static_cast<std::size_t>(c)];
    };
    for (int y = 0; y < height; ++y) {
        const double sy = (y + 0.5 + offsetY) / scale - 0.5;
        const int y0 = static_cast<int>(std::floor(sy));
        const double fy = sy - y0;
        for (int x = 0; x < width; ++x) {
            const double sx = (x + 0.5 + offsetX) / scale - 0.5;
            const int x0 = static_cast<int>(std::floor(sx));
            const double fx = sx - x0;
            std::uint8_t* dst = &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                                           static_cast<std::size_t>(x)) * 4];
            for (int c = 0; c < 4; ++c) {
                const double top = texel(x0, y0, c) * (1 - fx) + texel(x0 + 1, y0, c) * fx;
                const double bottom = texel(x0, y0 + 1, c) * (1 - fx) + texel(x0 + 1, y0 + 1, c) * fx;
                dst[c] = static_cast<std::uint8_t>(std::lround(top * (1 - fy) + bottom * fy));
            }
        }
    }
    return out;
}

}  // namespace

int backdropSplit(int height) { return static_cast<int>(std::lround(height * 0.7)); }

void shadeBackdrop(std::vector<std::uint8_t>& rgba, int width, int height, std::uint8_t groundR, std::uint8_t groundG,
                   std::uint8_t groundB) {
    const int split = backdropSplit(height);
    for (int y = 0; y < height; ++y) {
        // as the shade drawn over it: NanoVG's gradient sampled at the pixel's centre, then the ground
        const float t = y < split ? (static_cast<float>(y) + 0.5f) / static_cast<float>(split) : 1.0f;
        const float a = y < split ? (kBackdropTopAlpha + (255 - kBackdropTopAlpha) * t) / 255.0f : 1.0f;
        const int keep = static_cast<int>(std::lround((1 - a) * 256));
        const int add[3] = {static_cast<int>(std::lround(groundR * a * 256)), static_cast<int>(std::lround(groundG * a * 256)),
                            static_cast<int>(std::lround(groundB * a * 256))};
        std::uint8_t* p = &rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) * 4];
        for (int x = 0; x < width; ++x, p += 4) {
            for (int c = 0; c < 3; ++c) {
                p[c] = static_cast<std::uint8_t>(std::min(255, (p[c] * keep + add[c] + 128) >> 8));
            }
            p[3] = 255;
        }
    }
}

namespace {

std::uint8_t toByte(double value) { return static_cast<std::uint8_t>(std::lround(std::min(255.0, std::max(0.0, value)))); }

// One pass of a box blur of `radius` along rows (`horizontal`) or columns of a w x h image of 3 channels, the
// edges clamped; running sums, so the cost does not depend on the radius.
void boxPass(std::vector<std::uint16_t>& img, std::vector<std::uint16_t>& tmp, int w, int h, int radius,
             bool horizontal) {
    const int length = horizontal ? w : h;
    const int lines = horizontal ? h : w;
    const int window = 2 * radius + 1;
    for (int line = 0; line < lines; ++line) {
        const auto index = [&](int i) {
            const int c = std::min(length - 1, std::max(0, i));
            return horizontal ? (static_cast<std::size_t>(line) * static_cast<std::size_t>(w) + static_cast<std::size_t>(c)) * 3
                              : (static_cast<std::size_t>(c) * static_cast<std::size_t>(w) + static_cast<std::size_t>(line)) * 3;
        };
        for (int ch = 0; ch < 3; ++ch) {
            std::uint32_t sum = 0;
            for (int i = -radius; i <= radius; ++i) {
                sum += img[index(i) + static_cast<std::size_t>(ch)];
            }
            for (int i = 0; i < length; ++i) {
                tmp[index(i) + static_cast<std::size_t>(ch)] = static_cast<std::uint16_t>((sum + static_cast<std::uint32_t>(window / 2)) / static_cast<std::uint32_t>(window));
                sum += img[index(i + radius + 1) + static_cast<std::size_t>(ch)];
                sum -= img[index(i - radius) + static_cast<std::size_t>(ch)];
            }
        }
    }
    img.swap(tmp);
}

// A soft round blur of about 19 px on a 1920-wide picture (scaled with the picture's width), as the original
// themes' CSS blur(18px): the picture halved (2 x 2 averages), three box passes each way (close to a Gaussian),
// then bilinear back to full size. On a worker, once per picture.
void softBlur(std::vector<std::uint8_t>& rgba, int width, int height) {
    if (width < 2 || height < 2) {
        return;
    }
    const int hw = width / 2;
    const int hh = height / 2;
    std::vector<std::uint16_t> half(static_cast<std::size_t>(hw) * static_cast<std::size_t>(hh) * 3);
    for (int y = 0; y < hh; ++y) {
        for (int x = 0; x < hw; ++x) {
            for (int c = 0; c < 3; ++c) {
                unsigned sum = 0;
                for (int dy = 0; dy < 2; ++dy) {
                    for (int dx = 0; dx < 2; ++dx) {
                        sum += rgba[((static_cast<std::size_t>(2 * y + dy)) * static_cast<std::size_t>(width) +
                                     static_cast<std::size_t>(2 * x + dx)) * 4 + static_cast<std::size_t>(c)];
                    }
                }
                // 4 fractional bits kept: smooth gradients without banding
                half[(static_cast<std::size_t>(y) * static_cast<std::size_t>(hw) + static_cast<std::size_t>(x)) * 3 +
                     static_cast<std::size_t>(c)] = static_cast<std::uint16_t>(sum * 4);
            }
        }
    }
    const int radius = std::max(1, static_cast<int>(std::lround(9.0 * width / 1920.0)));
    std::vector<std::uint16_t> tmp(half.size());
    for (int pass = 0; pass < 3; ++pass) {
        boxPass(half, tmp, hw, hh, radius, true);
        boxPass(half, tmp, hw, hh, radius, false);
    }
    const auto sample = [&](int sx, int sy, int c) {
        sx = std::min(hw - 1, std::max(0, sx));
        sy = std::min(hh - 1, std::max(0, sy));
        return static_cast<double>(half[(static_cast<std::size_t>(sy) * static_cast<std::size_t>(hw) +
                                         static_cast<std::size_t>(sx)) * 3 + static_cast<std::size_t>(c)]) / 16.0;
    };
    for (int y = 0; y < height; ++y) {
        const double fy = (y + 0.5) / 2.0 - 0.5;
        const int y0 = static_cast<int>(std::floor(fy));
        const double ty = fy - y0;
        for (int x = 0; x < width; ++x) {
            const double fx = (x + 0.5) / 2.0 - 0.5;
            const int x0 = static_cast<int>(std::floor(fx));
            const double tx = fx - x0;
            std::uint8_t* p = &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
            for (int c = 0; c < 3; ++c) {
                const double top = sample(x0, y0, c) * (1 - tx) + sample(x0 + 1, y0, c) * tx;
                const double bottom = sample(x0, y0 + 1, c) * (1 - tx) + sample(x0 + 1, y0 + 1, c) * tx;
                p[c] = toByte(top * (1 - ty) + bottom * ty);
            }
            p[3] = 255;
        }
    }
}

}  // namespace

namespace {
// a backdrop's shade colour in its cache key (the shade is baked into the picture)
std::string groundKey(Color ground) {
    char text[8];
    std::snprintf(text, sizeof(text), "%02x%02x%02x", ground.r, ground.g, ground.b);
    return text;
}
}  // namespace

BackdropTreatment backdropTreatment(const Theme& theme) {
    BackdropTreatment t;
    t.kind = theme.backdropKind;
    t.brightness = theme.backdropBrightness;
    t.saturation = theme.backdropSaturation;
    t.contrast = theme.backdropContrast;
    t.ground = theme.background;
    return t;
}

void bakeBackdrop(std::vector<std::uint8_t>& rgba, int width, int height, const BackdropTreatment& treatment) {
    const Color g = treatment.ground;
    switch (treatment.kind) {
    case BackdropKind::Shade:
        shadeBackdrop(rgba, width, height, g.r, g.g, g.b);
        return;
    case BackdropKind::Blur:
    case BackdropKind::Tint: {
        softBlur(rgba, width, height);
        const bool tint = treatment.kind == BackdropKind::Tint;
        for (std::size_t i = 0; i + 3 < rgba.size(); i += 4) {
            double c[3] = {static_cast<double>(rgba[i]), static_cast<double>(rgba[i + 1]), static_cast<double>(rgba[i + 2])};
            if (tint) {
                const double ground[3] = {static_cast<double>(g.r), static_cast<double>(g.g), static_cast<double>(g.b)};
                for (int k = 0; k < 3; ++k) {
                    c[k] = c[k] * 0.3 + ground[k] * 0.7;
                }
            } else {  // as CSS filters in order: saturate, contrast, brightness
                const double luma = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
                for (double& v : c) {
                    v = luma + (v - luma) * treatment.saturation;
                    v = (v - 127.5) * treatment.contrast + 127.5;
                    v *= treatment.brightness;
                }
            }
            for (int k = 0; k < 3; ++k) {
                rgba[i + static_cast<std::size_t>(k)] = toByte(c[k]);
            }
            rgba[i + 3] = 255;
        }
        if (tint) {
            addGrain(rgba, width, height);
        }
        return;
    }
    case BackdropKind::Diagonal: {
        // CSS linear-gradient(54deg, ground 42 %, ground at 70 % opacity 100 %)
        const double angle = 54.0 * 3.14159265358979 / 180.0;
        const double dx = std::sin(angle);
        const double dy = -std::cos(angle);
        const double length = std::fabs(width * dx) + std::fabs(height * dy);
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const double t = ((x + 0.5 - width / 2.0) * dx + (y + 0.5 - height / 2.0) * dy) / length + 0.5;
                const double alpha = t <= 0.42 ? 1.0 : 1.0 - 0.3 * std::min(1.0, (t - 0.42) / 0.58);
                std::uint8_t* p = &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
                p[0] = toByte(g.r * alpha + p[0] * (1 - alpha));
                p[1] = toByte(g.g * alpha + p[1] * (1 - alpha));
                p[2] = toByte(g.b * alpha + p[2] * (1 - alpha));
                p[3] = 255;
            }
        }
        return;
    }
    }
}

std::optional<DecodedImage> decodeImage(const std::string& bytes, int width, int height, ImageShape shape,
                                        const BackdropTreatment& treatment) {
    if (!looksLikeImage(bytes)) {
        return std::nullopt;
    }
    int w = 0;
    int h = 0;
    int channels = 0;
    unsigned char* data = stbi_load_from_memory(reinterpret_cast<const unsigned char*>(bytes.data()),
                                                static_cast<int>(bytes.size()), &w, &h, &channels, 4);
    if (data == nullptr || w <= 0 || h <= 0) {
        stbi_image_free(data);
        return std::nullopt;
    }
    const float scale = std::min(1.0f, std::max(static_cast<float>(std::max(width, 1)) / static_cast<float>(w),
                                                 static_cast<float>(std::max(height, 1)) / static_cast<float>(h)));
    DecodedImage out;
    out.width = std::max(1, static_cast<int>(static_cast<float>(w) * scale + 0.5f));
    out.height = std::max(1, static_cast<int>(static_cast<float>(h) * scale + 0.5f));
    out.rgba.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height) * 4);
    if (out.width == w && out.height == h) {
        std::copy(data, data + out.rgba.size(), out.rgba.begin());
    } else {
        // box filter: each output pixel averages the source pixels it covers
        for (int y = 0; y < out.height; ++y) {
            const int y0 = y * h / out.height;
            const int y1 = std::max(y0 + 1, (y + 1) * h / out.height);
            for (int x = 0; x < out.width; ++x) {
                const int x0 = x * w / out.width;
                const int x1 = std::max(x0 + 1, (x + 1) * w / out.width);
                unsigned sum[4] = {0, 0, 0, 0};
                for (int sy = y0; sy < y1; ++sy) {
                    const unsigned char* row = data + (static_cast<std::size_t>(sy) * static_cast<std::size_t>(w) +
                                                       static_cast<std::size_t>(x0)) * 4;
                    for (int sx = x0; sx < x1; ++sx, row += 4) {
                        for (int c = 0; c < 4; ++c) {
                            sum[c] += row[c];
                        }
                    }
                }
                const unsigned count = static_cast<unsigned>((y1 - y0) * (x1 - x0));
                std::uint8_t* dst = &out.rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) +
                                               static_cast<std::size_t>(x)) * 4];
                for (int c = 0; c < 4; ++c) {
                    dst[c] = static_cast<std::uint8_t>(sum[c] / count);
                }
            }
        }
    }
    stbi_image_free(data);
    const bool crop = shape == ImageShape::Crop || shape == ImageShape::Backdrop || shape == ImageShape::ThemedBackdrop;
    if (crop && (out.width > width || out.height > height)) {
        const int cw = std::min(out.width, std::max(width, 1));
        const int ch = std::min(out.height, std::max(height, 1));
        const int left = (out.width - cw) / 2;
        const int top = (out.height - ch) / 2;
        std::vector<std::uint8_t> cropped(static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch) * 4);
        for (int y = 0; y < ch; ++y) {
            const std::uint8_t* from = &out.rgba[(static_cast<std::size_t>(y + top) * static_cast<std::size_t>(out.width) +
                                                  static_cast<std::size_t>(left)) * 4];
            std::copy(from, from + static_cast<std::size_t>(cw) * 4,
                      cropped.begin() + static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) *
                                                                     static_cast<std::size_t>(cw) * 4));
        }
        out.rgba = std::move(cropped);
        out.width = cw;
        out.height = ch;
    }
    if (crop && width > 0 && height > 0 && (out.width != width || out.height != height)) {
        out = coverResize(out, width, height);  // smaller than the frame: scaled up to cover it, centre kept
    }
    if (shape == ImageShape::Backdrop || shape == ImageShape::ThemedBackdrop) {
        bakeBackdrop(out.rgba, out.width, out.height, treatment);
    }
    out.opaque = true;
    for (std::size_t i = 3; i < out.rgba.size() && out.opaque; i += 4) {
        out.opaque = out.rgba[i] == 255;
    }
    return out;
}

// ---- DiskCache

DiskCache::DiskCache(std::string directory, std::size_t budgetBytes)
    : directory_(std::move(directory)), budget_(budgetBytes) {
    scan();
}

std::string DiskCache::pathFor(const std::string& url) const {
    char name[32];
    std::snprintf(name, sizeof(name), "%016llx.img", static_cast<unsigned long long>(fnv1a(url)));
    return directory_ + "/" + name;
}

void DiskCache::scan() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_ = 0;
    DIR* dir = ::opendir(directory_.c_str());
    if (dir == nullptr) {
        return;
    }
    while (dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        struct stat info {};
        if (isCacheFile(name) && ::stat((directory_ + "/" + name).c_str(), &info) == 0) {
            size_ += static_cast<std::size_t>(info.st_size);
        } else if (name.size() > 24 && isCacheFile(name.substr(0, 20)) && name.compare(name.size() - 4, 4, ".tmp") == 0) {
            ::unlink((directory_ + "/" + name).c_str());  // a write cut short (the app closed meanwhile)
        }
    }
    ::closedir(dir);
}

std::optional<std::string> DiskCache::read(const std::string& url) {
    if (directory_.empty()) {
        return std::nullopt;
    }
    const std::string path = pathFor(url);
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    ::utime(path.c_str(), nullptr);  // refresh the access order
    return buffer.str();
}

void DiskCache::write(const std::string& url, const std::string& bytes) {
    if (directory_.empty()) {
        return;
    }
    const std::string path = pathFor(url);
    static std::atomic<unsigned> counter{0};
    const std::string temp = path + "." + std::to_string(counter.fetch_add(1)) + ".tmp";  // one per writer
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return;
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) {
            ::unlink(temp.c_str());
            return;
        }
    }
    bool over = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);  // replacing and counting together: a rewrite counts once
        struct stat old {};
        const std::size_t replaced = ::stat(path.c_str(), &old) == 0 ? static_cast<std::size_t>(old.st_size) : 0;
        if (std::rename(temp.c_str(), path.c_str()) != 0) {
            ::unlink(temp.c_str());
            return;
        }
        size_ = size_ - std::min(size_, replaced) + bytes.size();
        over = size_ > budget_;
    }
    if (over) {
        trim();
    }
}

void DiskCache::remove(const std::string& url) {
    if (directory_.empty()) {
        return;
    }
    const std::string path = pathFor(url);
    std::lock_guard<std::mutex> lock(mutex_);
    struct stat info {};
    if (::stat(path.c_str(), &info) == 0 && ::unlink(path.c_str()) == 0) {
        size_ -= std::min(size_, static_cast<std::size_t>(info.st_size));
    }
}

void DiskCache::trim() {
    std::lock_guard<std::mutex> lock(mutex_);
    struct Entry {
        std::string path;
        time_t mtime;
        std::size_t size;
    };
    std::vector<Entry> entries;
    std::size_t total = 0;
    DIR* dir = ::opendir(directory_.c_str());
    if (dir == nullptr) {
        return;
    }
    while (dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (!isCacheFile(name)) {
            continue;
        }
        const std::string path = directory_ + "/" + name;
        struct stat info {};
        if (::stat(path.c_str(), &info) == 0) {
            entries.push_back({path, info.st_mtime, static_cast<std::size_t>(info.st_size)});
            total += static_cast<std::size_t>(info.st_size);
        }
    }
    ::closedir(dir);
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.mtime < b.mtime; });
    const std::size_t goal = budget_ - budget_ / 10;
    for (const Entry& entry : entries) {
        if (total <= goal) {
            break;
        }
        if (::unlink(entry.path.c_str()) == 0) {
            total -= entry.size;
        }
    }
    size_ = total;
}

std::size_t DiskCache::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return size_;
}

// ---- ImageLoader

struct ImageLoader::Job {
    std::mutex mutex;
    std::vector<core::CancelToken> tokens;

    void add(const core::CancelToken& token) {
        std::lock_guard<std::mutex> lock(mutex);
        tokens.push_back(token);
    }
    bool wanted() {
        std::lock_guard<std::mutex> lock(mutex);
        return std::any_of(tokens.begin(), tokens.end(), [](const core::CancelToken& t) { return !t.cancelled(); });
    }
};

struct ImageLoader::Shared {
    std::mutex mutex;
    Fetch fetch;
    std::unique_ptr<DiskCache> disk;
    std::deque<Decoded> decoded;  // filled on the main queue, consumed by uploadPending
    bool alive = true;

    Decoded work(Decoded job, Job& wanted) {
        if (!wanted.wanted()) {
            job.skipped = true;
            return job;
        }
        Fetch fetchCopy;
        {
            std::lock_guard<std::mutex> lock(mutex);
            fetchCopy = fetch;
        }
        if (disk) {
            if (auto cached = disk->read(job.url)) {
                job.image = decodeImage(*cached, job.width, job.height, job.shape, job.treatment);
                if (job.image) {
                    return job;
                }
                disk->remove(job.url);  // damaged (power cut, full disk): fetch it again
            }
        }
        if (!fetchCopy) {
            return job;
        }
        auto fetched = fetchCopy(job.url, core::CancelToken());
        if (!fetched.ok()) {
            PF_LOG_DEBUG("image %s: %s", job.url.c_str(), fetched.error().message.c_str());
            return job;
        }
        const std::string& bytes = fetched.value();
        if (disk && looksLikeImage(bytes)) {
            disk->write(job.url, bytes);
        }
        if (!wanted.wanted()) {
            job.skipped = true;
            return job;
        }
        job.image = decodeImage(bytes, job.width, job.height, job.shape, job.treatment);
        return job;
    }
};

ImageLoader::ImageLoader(core::WorkerPool& pool, core::MainQueue& mainQueue, TextureFactory& factory, Config config)
    : pool_(pool), mainQueue_(mainQueue), factory_(factory), config_(std::move(config)),
      shared_(std::make_shared<Shared>()) {
    if (!config_.diskDirectory.empty()) {
        shared_->disk = std::make_unique<DiskCache>(config_.diskDirectory, config_.diskBudget);
    }
}

ImageLoader::~ImageLoader() {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->alive = false;
}

void ImageLoader::setFetch(Fetch fetch) {
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->fetch = std::move(fetch);
}

std::unique_ptr<ImageRequest> ImageLoader::request(const std::string& url, int width, int height, Callback ready,
                                                   ImageShape shape) {
    core::CancelToken token;
    auto handle = std::make_unique<ImageRequest>(token);
    const std::string key = url + "|" + std::to_string(width) + "x" + std::to_string(height) +
                            (shape == ImageShape::Crop       ? "c"
                             : shape == ImageShape::Backdrop ? "b:" + groundKey(theme().background)
                             : shape == ImageShape::ThemedBackdrop ? "t:" + theme().id
                                                                   : std::string());
    if (auto hit = cache_.find(key); hit != cache_.end()) {
        lru_.splice(lru_.begin(), lru_, hit->second);
        ready(hit->second->second);
        return handle;
    }
    auto& waiting = waiters_[key];
    waiting.push_back(Waiter{token, std::move(ready)});
    if (waiting.size() > 1) {
        jobs_[key]->add(token);  // already being loaded
        return handle;
    }
    jobs_[key] = std::make_shared<Job>();
    jobs_[key]->add(token);
    start(key, url, width, height, shape);
    return handle;
}

void ImageLoader::start(const std::string& key, const std::string& url, int width, int height, ImageShape shape) {
    Decoded job;
    job.key = key;
    job.url = url;
    job.width = width;
    job.height = height;
    job.shape = shape;
    if (shape == ImageShape::ThemedBackdrop) {
        job.treatment = backdropTreatment(theme());
    } else {  // Backdrop: the identity's shade into the theme's background
        job.treatment.ground = theme().background;
    }
    std::shared_ptr<Job> wanted = jobs_[key];
    if (config_.synchronous) {
        shared_->decoded.push_back(shared_->work(std::move(job), *wanted));
        return;
    }
    std::shared_ptr<Shared> shared = shared_;
    core::MainQueue* mainQueue = &mainQueue_;
    pool_.submit([shared, mainQueue, job = std::move(job), wanted]() mutable {
        auto result = std::make_shared<Decoded>(shared->work(std::move(job), *wanted));
        // posting under the lock: once the loader is gone (alive false), nothing reaches the main queue
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (!shared->alive) {
            return;
        }
        mainQueue->post([shared, result] {
            std::lock_guard<std::mutex> inner(shared->mutex);
            if (shared->alive) {
                shared->decoded.push_back(std::move(*result));
            }
        });
    });
}

std::size_t ImageLoader::uploadPending() {
    std::size_t finished = 0;
    for (int i = 0; i < config_.uploadsPerFrame; ++i) {
        Decoded next;
        {
            std::lock_guard<std::mutex> lock(shared_->mutex);
            if (shared_->decoded.empty()) {
                return finished;
            }
            next = std::move(shared_->decoded.front());
            shared_->decoded.pop_front();
        }
        auto job = jobs_.find(next.key);
        if (job == jobs_.end() || !job->second->wanted()) {
            finish(next, nullptr);  // nobody waits any more: no upload
            ++finished;
            continue;
        }
        if (next.skipped) {
            start(next.key, next.url, next.width, next.height, next.shape);  // a request came after the worker gave up
            continue;
        }
        TextureRef texture;
        if (next.image) {
            const int id = factory_.create(next.image->rgba.data(), next.image->width, next.image->height);
            if (id > 0) {
                TextureFactory* factory = &factory_;
                texture = TextureRef(new Texture{id, next.image->width, next.image->height, next.image->opaque},
                                     [factory](const Texture* t) {
                                         factory->destroy(t->id);
                                         delete t;
                                     });
                insert(next.key, texture);
            }
        }
        finish(next, texture);
        ++finished;
    }
    return finished;
}

void ImageLoader::insert(const std::string& key, const TextureRef& texture) {
    lru_.emplace_front(key, texture);
    cache_[key] = lru_.begin();
    memoryUsed_ += textureBytes(*texture);
    while (memoryUsed_ > config_.memoryBudget && lru_.size() > 1) {
        auto last = std::prev(lru_.end());
        memoryUsed_ -= textureBytes(*last->second);
        cache_.erase(last->first);
        lru_.erase(last);
    }
}

void ImageLoader::finish(const Decoded& decoded, TextureRef texture) {
    const std::string& key = decoded.key;
    jobs_.erase(key);
    auto it = waiters_.find(key);
    if (it == waiters_.end()) {
        return;
    }
    std::vector<Waiter> waiting = std::move(it->second);
    waiters_.erase(it);
    for (Waiter& waiter : waiting) {
        if (!waiter.token.cancelled() && waiter.callback) {
            waiter.callback(texture);
        }
    }
}

}  // namespace playfin::ui
