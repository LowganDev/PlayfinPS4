#include <doctest/doctest.h>

#include <stb/stb_image_write.h>
#include <sys/stat.h>
#include <utime.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "core/task.hpp"
#include "support/files.hpp"
#include "support/fixed_metrics.hpp"
#include "ui/animation.hpp"
#include "ui/context.hpp"
#include "ui/image_view.hpp"
#include "ui/images.hpp"

using namespace playfin;
using namespace std::chrono_literals;
using ui::ImageLoader;
using ui::TextureRef;

namespace {

void appendTo(void* context, void* data, int size) {
    static_cast<std::string*>(context)->append(static_cast<const char*>(data), static_cast<std::size_t>(size));
}

std::string png(int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 4), 0x80);
    std::string out;
    stbi_write_png_to_func(appendTo, &out, width, height, 4, pixels.data(), width * 4);
    return out;
}

std::string opaquePng(int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width * height * 3), 0x80);
    std::string out;
    stbi_write_png_to_func(appendTo, &out, width, height, 3, pixels.data(), width * 3);
    return out;
}

struct FakeFactory : ui::TextureFactory {
    int next = 1;
    int created = 0;
    int destroyed = 0;
    int create(const std::uint8_t*, int, int) override {
        ++created;
        return next++;
    }
    void destroy(int) override { ++destroyed; }
};

struct TempDir {
    std::string path;
    TempDir() {
        char tmpl[] = "/tmp/playfin-img-XXXXXX";
        REQUIRE(mkdtemp(tmpl) != nullptr);
        path = tmpl;
    }
    ~TempDir() { std::filesystem::remove_all(path); }
};

struct Rig {
    core::WorkerPool pool{2};
    core::MainQueue mainQueue;
    FakeFactory factory;
    std::atomic<int> fetches{0};
    std::unique_ptr<ImageLoader> loader;

    explicit Rig(ImageLoader::Config config = {}) {
        loader = std::make_unique<ImageLoader>(pool, mainQueue, factory, config);
        loader->setFetch([this](const std::string& url, const core::CancelToken&) -> core::Result<std::string> {
            ++fetches;
            if (url.find("fail") != std::string::npos) {
                return core::makeError(core::ErrorKind::Network, "down");
            }
            if (url.find("junk") != std::string::npos) {
                return std::string("not an image");
            }
            if (url.find("opaque") != std::string::npos) {
                return opaquePng(200, 100);
            }
            return png(200, 100);
        });
    }
    ~Rig() { loader.reset(); }

    bool pumpUntil(const std::function<bool()>& done) {
        for (int i = 0; i < 300; ++i) {
            mainQueue.drain();
            loader->uploadPending();
            if (done()) {
                return true;
            }
            std::this_thread::sleep_for(10ms);
        }
        return false;
    }
};

}  // namespace

TEST_CASE("decoding downscales to cover the target and never upscales") {
    const auto small = ui::decodeImage(png(200, 100), 50, 50);
    REQUIRE(small.has_value());
    CHECK(small->width == 100);
    CHECK(small->height == 50);
    CHECK(small->rgba.size() == 100u * 50u * 4u);
    const auto same = ui::decodeImage(png(200, 100), 400, 400);
    REQUIRE(same.has_value());
    CHECK(same->width == 200);
    CHECK_FALSE(ui::decodeImage("garbage", 10, 10).has_value());
    CHECK_FALSE(ui::decodeImage("", 10, 10).has_value());
}

TEST_CASE("cropped decoding gives exactly the target size from the centre") {
    const auto cut = ui::decodeImage(png(200, 100), 50, 30, ui::ImageShape::Crop);
    REQUIRE(cut.has_value());
    CHECK(cut->width == 50);
    CHECK(cut->height == 30);
    CHECK(cut->rgba.size() == 50u * 30u * 4u);
    CHECK(cut->opaque == ui::decodeImage(png(200, 100), 50, 30)->opaque);
    // smaller than asked: scaled up to cover, then cut (the view then copies it 1:1)
    const auto small = ui::decodeImage(png(200, 100), 400, 50, ui::ImageShape::Crop);
    REQUIRE(small.has_value());
    CHECK(small->width == 400);
    CHECK(small->height == 50);
    const auto tall = ui::decodeImage(png(200, 100), 260, 390, ui::ImageShape::Crop);
    REQUIRE(tall.has_value());
    CHECK(tall->width == 260);
    CHECK(tall->height == 390);
    CHECK(tall->rgba.size() == 260u * 390u * 4u);
    CHECK(tall->rgba[(195u * 260u + 130u) * 4u] == 0x80);  // the picture's content, not a border
}

TEST_CASE("an image is fetched once, decoded and delivered") {
    Rig rig;
    TextureRef first;
    TextureRef second;
    auto a = rig.loader->request("http://x/1.jpg", 50, 50, [&](TextureRef t) { first = t; });
    auto b = rig.loader->request("http://x/1.jpg", 50, 50, [&](TextureRef t) { second = t; });
    REQUIRE(rig.pumpUntil([&] { return first && second; }));
    CHECK(rig.fetches == 1);
    CHECK(first->width == 100);
    CHECK(first == second);
    TextureRef third;
    auto c = rig.loader->request("http://x/1.jpg", 50, 50, [&](TextureRef t) { third = t; });
    CHECK(third == first);  // memory hit: immediate
    CHECK(rig.fetches == 1);
}

TEST_CASE("a dropped request is never delivered") {
    Rig rig;
    bool called = false;
    auto request = rig.loader->request("http://x/2.jpg", 50, 50, [&](TextureRef) { called = true; });
    request.reset();
    REQUIRE(rig.pumpUntil([&] { return rig.loader->idle(); }));
    CHECK_FALSE(called);
}

TEST_CASE("failures deliver nothing usable") {
    Rig rig;
    int calls = 0;
    TextureRef got = std::make_shared<ui::Texture>();
    auto failed = rig.loader->request("http://x/fail.jpg", 50, 50, [&](TextureRef t) {
        ++calls;
        got = t;
    });
    auto junk = rig.loader->request("http://x/junk.jpg", 50, 50, [&](TextureRef t) {
        ++calls;
        got = t;
    });
    REQUIRE(rig.pumpUntil([&] { return calls == 2; }));
    CHECK(got == nullptr);
    CHECK(rig.loader->cachedCount() == 0);
}

TEST_CASE("uploads are spread over frames") {
    ImageLoader::Config config;
    config.synchronous = true;
    config.uploadsPerFrame = 4;
    Rig rig(config);
    int delivered = 0;
    std::vector<std::unique_ptr<ui::ImageRequest>> requests;
    for (int i = 0; i < 6; ++i) {
        requests.push_back(rig.loader->request("http://x/" + std::to_string(i), 50, 50, [&](TextureRef) { ++delivered; }));
    }
    rig.loader->uploadPending();
    CHECK(delivered == 4);
    rig.loader->uploadPending();
    CHECK(delivered == 6);
}

TEST_CASE("the memory cache keeps to its budget; textures are freed when unused") {
    ImageLoader::Config config;
    config.synchronous = true;
    config.memoryBudget = 2 * 100 * 50 * 4;
    Rig rig(config);
    for (int i = 0; i < 3; ++i) {
        auto request = rig.loader->request("http://x/m" + std::to_string(i), 50, 50, [](TextureRef) {});
        rig.loader->uploadPending();
    }
    CHECK(rig.loader->cachedCount() == 2);
    CHECK(rig.factory.destroyed == 1);
}

TEST_CASE("the disk cache serves images after a restart") {
    TempDir dir;
    ImageLoader::Config config;
    config.synchronous = true;
    config.diskDirectory = dir.path;
    {
        Rig rig(config);
        TextureRef got;
        auto request = rig.loader->request("http://x/disk.jpg", 50, 50, [&](TextureRef t) { got = t; });
        rig.loader->uploadPending();
        REQUIRE(got);
    }
    Rig offline(config);
    offline.loader->setFetch([](const std::string&, const core::CancelToken&) -> core::Result<std::string> {
        return core::makeError(core::ErrorKind::Network, "offline");
    });
    TextureRef got;
    auto request = offline.loader->request("http://x/disk.jpg", 50, 50, [&](TextureRef t) { got = t; });
    offline.loader->uploadPending();
    CHECK(got);
}

TEST_CASE("disk trimming removes the oldest cache files only") {
    TempDir dir;
    std::ofstream(dir.path + "/notes.txt") << "not ours";
    ui::DiskCache cache(dir.path, 1000);
    for (int i = 0; i < 5; ++i) {
        cache.write("http://x/" + std::to_string(i), std::string(400, 'a'));
        const std::string file = dir.path + "/" + [&] {
            char name[32];
            std::snprintf(name, sizeof(name), "%016llx.img",
                          static_cast<unsigned long long>(ui::fnv1a("http://x/" + std::to_string(i))));
            return std::string(name);
        }();
        utimbuf times{static_cast<time_t>(1000 + i), static_cast<time_t>(1000 + i)};
        utime(file.c_str(), &times);
    }
    cache.trim();
    CHECK(cache.size() <= 1000);
    CHECK_FALSE(cache.read("http://x/0").has_value());
    CHECK(cache.read("http://x/4").has_value());
    CHECK(std::filesystem::exists(dir.path + "/notes.txt"));
}

TEST_CASE("an unusable disk folder does not stop images") {
    ImageLoader::Config config;
    config.synchronous = true;
    config.diskDirectory = "/nonexistent/playfin-cache";
    Rig rig(config);
    TextureRef got;
    auto request = rig.loader->request("http://x/ok.jpg", 50, 50, [&](TextureRef t) { got = t; });
    rig.loader->uploadPending();
    CHECK(got);
}

TEST_CASE("pictures over 4096 pixels on a side or 8 megapixels are refused") {
    CHECK(ui::decodeImage(png(4096, 2), 10, 10).has_value());
    CHECK_FALSE(ui::decodeImage(png(4097, 1), 10, 10).has_value());
    CHECK_FALSE(ui::decodeImage(png(1, 4097), 10, 10).has_value());
    CHECK_FALSE(ui::decodeImage(png(4096, 2049), 10, 10).has_value());
}

TEST_CASE("a damaged cache file is fetched again and replaced") {
    TempDir dir;
    {
        ui::DiskCache cache(dir.path, 1u << 20);
        cache.write("http://x/broken.jpg", "half a picture");
    }
    ImageLoader::Config config;
    config.synchronous = true;
    config.diskDirectory = dir.path;
    Rig rig(config);
    TextureRef got;
    auto request = rig.loader->request("http://x/broken.jpg", 50, 50, [&](TextureRef t) { got = t; });
    rig.loader->uploadPending();
    CHECK(got);
    CHECK(rig.fetches == 1);
    ui::DiskCache cache(dir.path, 1u << 20);
    const auto bytes = cache.read("http://x/broken.jpg");
    REQUIRE(bytes.has_value());
    CHECK(ui::decodeImage(*bytes, 1, 1).has_value());
}

TEST_CASE("rewriting a cached URL counts its size once, even from several threads") {
    TempDir dir;
    ui::DiskCache cache(dir.path, 1u << 30);
    cache.write("http://x/same", std::string(400, 'a'));
    cache.write("http://x/same", std::string(400, 'b'));
    CHECK(cache.size() == 400);
    const std::string a(256 * 1024, 'a');
    const std::string b(256 * 1024, 'b');
    for (int round = 0; round < 20; ++round) {
        std::thread first([&] { cache.write("http://x/race", a); });
        std::thread second([&] { cache.write("http://x/race", b); });
        first.join();
        second.join();
        const auto bytes = cache.read("http://x/race");
        REQUIRE(bytes.has_value());
        CHECK((*bytes == a || *bytes == b));
    }
    CHECK(cache.size() == 400 + a.size());
}

TEST_CASE("work for requests dropped while queued is skipped") {
    ImageLoader::Config config;
    Rig rig(config);
    std::atomic<bool> open{false};
    rig.loader->setFetch([&](const std::string&, const core::CancelToken&) -> core::Result<std::string> {
        ++rig.fetches;
        while (!open) {
            std::this_thread::sleep_for(1ms);
        }
        return png(20, 20);
    });
    int delivered = 0;
    auto busy1 = rig.loader->request("http://x/b1", 10, 10, [&](TextureRef) { ++delivered; });
    auto busy2 = rig.loader->request("http://x/b2", 10, 10, [&](TextureRef) { ++delivered; });
    std::vector<std::unique_ptr<ui::ImageRequest>> scrolledPast;
    for (int i = 0; i < 20; ++i) {
        scrolledPast.push_back(rig.loader->request("http://x/q" + std::to_string(i), 10, 10, [](TextureRef) {}));
    }
    while (rig.fetches < 2) {
        std::this_thread::sleep_for(1ms);
    }
    scrolledPast.clear();
    open = true;
    REQUIRE(rig.pumpUntil([&] { return delivered == 2 && rig.loader->idle(); }));
    CHECK(rig.fetches == 2);
    CHECK(rig.loader->cachedCount() == 2);
}

TEST_CASE("an image announcing huge dimensions is refused before it is decoded") {
    // a PNG header (signature + IHDR) claiming 40000 x 40000 RGBA pixels (6.4 GB once decoded)
    std::string png("\x89PNG\r\n\x1a\n", 8);
    const unsigned char ihdr[] = {0, 0, 0, 13, 'I', 'H', 'D', 'R', 0, 0, 0x9C, 0x40, 0, 0, 0x9C, 0x40, 8, 6, 0, 0, 0,
                                  0, 0, 0, 0};
    png.append(reinterpret_cast<const char*>(ihdr), sizeof(ihdr));
    png.append(std::string(64, '\0'));
    CHECK_FALSE(playfin::ui::decodeImage(png, 400, 225, playfin::ui::ImageShape::Whole).has_value());
}

TEST_CASE("a JPEG whose Huffman tables overflowed stb_image 2.10 is refused or decoded safely") {
    // found by fuzz_image; the ASan and UBSan builds stop on the out-of-bounds write of the old decoder
    const std::string bytes = playfin::test::readFixture("images/stb-2.10-huffman-overflow.jpg");
    (void)playfin::ui::decodeImage(bytes, 64, 64, playfin::ui::ImageShape::Whole);
    CHECK(true);
}

TEST_CASE("a write cut short leaves no temporary file behind at the next start") {
    TempDir dir;
    const std::string leftover = dir.path + "/0123456789abcdef.img.7.tmp";
    std::ofstream(leftover) << "half";
    const std::string other = dir.path + "/notes.tmp";  // not the cache's: left alone
    std::ofstream(other) << "x";
    ui::DiskCache cache(dir.path, 1u << 20);
    CHECK_FALSE(std::ifstream(leftover).good());
    CHECK(std::ifstream(other).good());
}

namespace {

std::vector<std::uint8_t> solid(int width, int height, ui::Color c) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (std::size_t i = 0; i < out.size(); i += 4) {
        out[i] = c.r;
        out[i + 1] = c.g;
        out[i + 2] = c.b;
        out[i + 3] = 255;
    }
    return out;
}

std::vector<std::uint8_t> checkerboard(int width, int height) {
    std::vector<std::uint8_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t v = ((x / 8 + y / 8) % 2) != 0 ? 255 : 0;
            std::uint8_t* p = &out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    }
    return out;
}

// mean and standard deviation of the red channel
std::pair<double, double> stats(const std::vector<std::uint8_t>& rgba) {
    double sum = 0;
    double squares = 0;
    const double n = static_cast<double>(rgba.size() / 4);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        sum += rgba[i];
        squares += static_cast<double>(rgba[i]) * rgba[i];
    }
    const double mean = sum / n;
    return {mean, std::sqrt(std::max(0.0, squares / n - mean * mean))};
}

const std::uint8_t* at(const std::vector<std::uint8_t>& rgba, int width, int x, int y) {
    return &rgba[(static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)) * 4];
}

}  // namespace

TEST_CASE("a backdrop's Shade treatment is the identity's shade, unchanged") {
    std::vector<std::uint8_t> a = checkerboard(64, 40);
    std::vector<std::uint8_t> b = a;
    ui::shadeBackdrop(a, 64, 40, 0x0E, 0x0E, 0x11);
    ui::BackdropTreatment shade;
    shade.ground = ui::Color{0x0E, 0x0E, 0x11, 0xFF};
    ui::bakeBackdrop(b, 64, 40, shade);
    CHECK(a == b);
}

TEST_CASE("a blurred backdrop: soft, then saturated, contrasted and darkened") {
    std::vector<std::uint8_t> image = checkerboard(640, 360);
    const double before = stats(image).second;
    ui::BackdropTreatment blur;
    blur.kind = ui::BackdropKind::Blur;
    blur.brightness = 0.4f;
    blur.saturation = 1.2f;
    blur.contrast = 1.2f;
    ui::bakeBackdrop(image, 640, 360, blur);
    const auto [mean, deviation] = stats(image);
    CHECK(deviation < before * 0.2);
    CHECK(mean == doctest::Approx(0.4 * 127.5).epsilon(0.08));
    CHECK(at(image, 640, 100, 100)[3] == 255);
}

TEST_CASE("a diagonal backdrop: the ground over the bottom left, the picture showing at the top right") {
    std::vector<std::uint8_t> image = solid(320, 180, ui::Color{255, 255, 255, 255});
    ui::BackdropTreatment diagonal;
    diagonal.kind = ui::BackdropKind::Diagonal;
    diagonal.ground = ui::Color{0x11, 0x0E, 0x1E, 0xFF};
    ui::bakeBackdrop(image, 320, 180, diagonal);
    const std::uint8_t* corner = at(image, 320, 0, 179);
    CHECK(corner[0] == 0x11);
    CHECK(corner[2] == 0x1E);
    const std::uint8_t* top = at(image, 320, 319, 0);  // the ground at 70 %: 30 % of the white shows
    CHECK(top[0] == doctest::Approx(0x11 * 0.7 + 255 * 0.3).epsilon(0.04));
    CHECK(at(image, 320, 300, 20)[0] > at(image, 320, 160, 90)[0]);
}

TEST_CASE("a tinted backdrop: blurred, mostly the ground colour, with grain") {
    std::vector<std::uint8_t> image = solid(320, 180, ui::Color{200, 40, 40, 255});
    ui::BackdropTreatment tint;
    tint.kind = ui::BackdropKind::Tint;
    tint.ground = ui::Color{0x15, 0x1C, 0x19, 0xFF};
    ui::bakeBackdrop(image, 320, 180, tint);
    const auto [mean, deviation] = stats(image);
    CHECK(mean == doctest::Approx(0.3 * 200 + 0.7 * 0x15).epsilon(0.06));
    CHECK(deviation > 0.5);  // the grain
}

TEST_CASE("a theme's backdrop treatment comes from its tokens") {
    const ui::BackdropTreatment chroma = ui::backdropTreatment(ui::themeById("chroma").tokens);
    CHECK(chroma.kind == ui::BackdropKind::Blur);
    CHECK(chroma.brightness == doctest::Approx(0.4));
    CHECK(ui::backdropTreatment(ui::themeById("projecteur").tokens).kind == ui::BackdropKind::Shade);
    CHECK(ui::backdropTreatment(ui::themeById("violet").tokens).ground.r == 0x11);
}

TEST_CASE("a backdrop shaded into one theme's background is not reused for another") {
    Rig rig;
    TextureRef first;
    auto a = rig.loader->request("http://x/b.jpg", 64, 36, [&](TextureRef t) { first = t; }, ui::ImageShape::Backdrop);
    REQUIRE(rig.pumpUntil([&] { return first != nullptr; }));
    ui::applyTheme(ui::themeById("mint"));  // another background colour
    TextureRef second;
    auto b = rig.loader->request("http://x/b.jpg", 64, 36, [&](TextureRef t) { second = t; }, ui::ImageShape::Backdrop);
    REQUIRE(rig.pumpUntil([&] { return second != nullptr; }));
    CHECK(second != first);
    ui::applyTheme(ui::themeById("projecteur"));
}

TEST_CASE("an image view without its fade covers its frame as soon as its picture arrives; with it, after the fade") {
    Rig rig;
    ui::Animator animator;
    ui::Context& ctx = ui::context();
    ui::ImageLoader* images = ctx.images;
    ui::Animator* previous = ctx.animator;
    ctx.images = rig.loader.get();
    ctx.animator = &animator;
    test::FixedMetrics metrics;
    {
        ui::ImageView faded;
        ui::ImageView instant;
        instant.fadeIn = false;
        faded.setUrl("http://x/opaque-a.png");
        instant.setUrl("http://x/opaque-b.png");
        faded.place(ui::Rect{0, 0, 100, 50}, metrics);
        instant.place(ui::Rect{0, 0, 100, 50}, metrics);
        REQUIRE(rig.pumpUntil([&] { return instant.coversFrame(); }));
        REQUIRE(rig.pumpUntil([&] { return animator.active(); }));  // the other one is fading in
        CHECK_FALSE(faded.coversFrame());
        animator.tick(1000);
        CHECK(faded.coversFrame());
    }
    ctx.images = images;
    ctx.animator = previous;
}

namespace {
// a 1920 x 1080 black picture with a white 32 x 32 square centred at (x, y), blurred as Ultrachromic's backdrop
std::vector<std::uint8_t> blurredDot(int x, int y) {
    std::vector<std::uint8_t> image = solid(1920, 1080, ui::Color{0, 0, 0, 255});
    for (int j = y - 16; j < y + 16; ++j) {
        for (int i = x - 16; i < x + 16; ++i) {
            std::uint8_t* p = &image[(static_cast<std::size_t>(j) * 1920 + static_cast<std::size_t>(i)) * 4];
            p[0] = p[1] = p[2] = 255;
        }
    }
    ui::BackdropTreatment blur;
    blur.kind = ui::BackdropKind::Blur;
    ui::bakeBackdrop(image, 1920, 1080, blur);
    return image;
}
}  // namespace

// The blur is a real one (a soft round spot), not a tiny picture stretched (blocks, a spot that changes with where
// the picture's cells fall): the same spot wherever it is, as wide one way as the other.
TEST_CASE("a blurred backdrop has no blocks: the same round spot wherever the light is") {
    const std::vector<std::uint8_t> a = blurredDot(960, 540);
    const std::vector<std::uint8_t> b = blurredDot(990, 560);
    const int offsets[][2] = {{0, 0}, {12, 0}, {0, 12}, {24, 0}, {0, 24}, {17, 17}, {-30, 5}};
    for (const auto& o : offsets) {
        CAPTURE(o[0]);
        CAPTURE(o[1]);
        CHECK(std::abs(int(at(a, 1920, 960 + o[0], 540 + o[1])[0]) - int(at(b, 1920, 990 + o[0], 560 + o[1])[0])) <= 2);
    }
    const int alongX = at(a, 1920, 960 + 24, 540)[0];
    const int alongY = at(a, 1920, 960, 540 + 24)[0];
    CHECK(std::abs(alongX - alongY) <= 2);
    CHECK(at(a, 1920, 960, 540)[0] > alongX);  // brightest at its centre
    CHECK(at(a, 1920, 960 + 140, 540)[0] <= 1);  // gone further away (a blur of about 18 px, as the original's)
}
