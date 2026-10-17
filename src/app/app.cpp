#include "app/app.hpp"

#include <GLES2/gl2.h>
#include <nanovg.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "app/gallery.hpp"
#include "app/settings.hpp"
#include "app/shell/screens.hpp"
#include "app/shell/music_player.hpp"
#include "app/shell/session.hpp"
#include "jellyfin/models.hpp"
#include "core/boot_trace.hpp"
#include "core/data_dir.hpp"
#include "core/task.hpp"
#include "core/version.hpp"
#include "net/http.hpp"
#include "platform/platform.hpp"
#include "render/canvas.hpp"
#include "render/composite.hpp"
#include "render/nvg_sw.hpp"
#include "render/raster.hpp"
#include "ui/application.hpp"
#include "ui/halo.hpp"
#include "ui/i18n.hpp"
#include "ui/images.hpp"
#include "ui/nvg_backend.hpp"

#ifdef PLAYFIN_TESTING
#include <fstream>
#include <sstream>

#include "testing/image.hpp"
#include "testing/script.hpp"
#endif

namespace playfin::app {

namespace {

constexpr float kLogicalWidth = 1920.0f;
constexpr double kSlowFrameMs = 20.0;  // software frames slower than this are logged with their raster counts

double millisecondsSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// Frame times, logged every 300 frames (and at the end of a bench): how many frames were drawn and how long the
// CPU drawing and the presentation took.
class FrameStats {
public:
    void add(bool drawn, double rasterMs, double presentMs) {
        ++frames_;
        if (drawn) {
            raster_.push_back(rasterMs);
        }
        present_.push_back(presentMs);
    }
    bool due() const { return frames_ >= 300; }
    void log(const char* label) {
        PF_LOG_INFO("%s: drawn %d/%d, raster median %.1f ms max %.1f ms, present median %.1f ms", label,
                    static_cast<int>(raster_.size()), frames_, median(raster_), maximum(raster_), median(present_));
        frames_ = 0;
        raster_.clear();
        present_.clear();
    }

private:
    static double median(std::vector<double> values) {
        std::sort(values.begin(), values.end());
        return values.empty() ? 0.0 : values[values.size() / 2];
    }
    static double maximum(const std::vector<double>& values) {
        return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end());
    }
    int frames_ = 0;
    std::vector<double> raster_;
    std::vector<double> present_;
};
#ifdef PLAYFIN_TESTING
constexpr float kFrameMs = 1000.0f / 60.0f;  // fixed clock of test mode
#endif

#ifdef PLAYFIN_TESTING
// Plays a UI test script: frame-counted waits, injected actions and text, screenshots after drawing.
class TestDriver {
public:
    bool load(const std::string& path, const std::string& output) {
        std::ifstream in(path);
        std::stringstream text;
        text << in.rdbuf();
        if (!in) {
            PF_LOG_ERROR("cannot read the test script %s", path.c_str());
            return false;
        }
        script_ = testing::parseScript(text.str());
        if (!script_.ok()) {
            PF_LOG_ERROR("%s:%d: %s", path.c_str(), script_.errorLine, script_.error.c_str());
            return false;
        }
        output_ = output;
        return true;
    }

    void beginFrame(int frame, std::vector<platform::Event>& injected) {
        if (pendingUp_) {
            injected.push_back(platform::actionEvent(platform::Event::Type::ActionUp, *pendingUp_));
            pendingUp_.reset();
        }
        while (frame >= waitUntil_ && next_ < script_.steps.size() && capture_.empty() && !quit_) {
            const testing::Step& step = script_.steps[next_++];
            switch (step.kind) {
            case testing::Step::Kind::Wait:
                waitUntil_ = frame + step.frames;
                break;
            case testing::Step::Kind::Press:
                injected.push_back(platform::actionEvent(platform::Event::Type::ActionDown, step.action));
                pendingUp_ = step.action;
                waitUntil_ = frame + 1;
                break;
            case testing::Step::Kind::Type: {
                platform::Event text;
                text.type = platform::Event::Type::Text;
                text.text = step.name;
                injected.push_back(text);
                waitUntil_ = frame + 1;
                break;
            }
            case testing::Step::Kind::Screenshot:
                capture_ = step.name;
                break;
            case testing::Step::Kind::Quit:
                quit_ = true;
                break;
            }
        }
        if (next_ >= script_.steps.size() && frame >= waitUntil_ && capture_.empty()) {
            quit_ = true;
        }
    }

    bool endFrame(int width, int height, const std::function<std::vector<std::uint8_t>()>& pixels) {
        if (capture_.empty()) {
            return true;
        }
        testing::Image image;
        image.width = width;
        image.height = height;
        image.rgba = pixels();
        const std::string path = output_ + "/" + capture_ + ".png";
        capture_.clear();
        if (!testing::writePng(path, image)) {
            PF_LOG_ERROR("cannot write %s", path.c_str());
            return false;
        }
        PF_LOG_INFO("screenshot %s", path.c_str());
        return true;
    }

    bool quit() const { return quit_; }

private:
    testing::Script script_;
    std::string output_;
    std::size_t next_ = 0;
    int waitUntil_ = 0;
    std::optional<platform::Action> pendingUp_;
    std::string capture_;
    bool quit_ = false;
};
#endif

std::unique_ptr<ui::Screen> firstScreen(const std::string& name, AppSession& session) {
    if (name == "gallery") {
        return makeGallery();
    }
    if (name != "shell") {
        PF_LOG_WARN("unknown screen %s, starting the app", name.c_str());
    }
    return makeStart(session);
}

}  // namespace

int run(const Options& options) {
    log::setLevel(options.logLevel);
    platform::Config config;
    config.hidden = options.hidden;
    config.dataDirOverride = options.dataDir;
    if (!platform::init(config)) {
        log::closeFile();
        platform::shutdown();
        return 1;
    }
    PF_LOG_INFO("startup: network");
    net::globalInit();
    const std::string language = platform::systemLanguage();
    PF_LOG_INFO("Playfin %s, language %s, data %s", core::version(), language.c_str(), core::dataRoot().c_str());

    bool testMode = false;
#ifdef PLAYFIN_TESTING
    TestDriver driver;
    testMode = !options.testScript.empty();
    if (testMode && !driver.load(options.testScript, options.testOutput)) {
        log::closeFile();
        platform::shutdown();
        return 1;
    }
#endif

#ifdef PLAYFIN_PS4
    const bool software = true;
#else
    const bool software = options.renderer != "gl";
#endif
    render::Surface surface(software ? 1920 : 0, software ? 1080 : 0);
    PF_LOG_INFO("startup: canvas (%s)", software ? "software" : "OpenGL");
    NVGcontext* vg = software ? render::createSoftwareCanvas(surface) : render::createCanvas();
    if (vg == nullptr) {
        PF_LOG_ERROR("cannot create the canvas");
        log::closeFile();
        platform::shutdown();
        return 1;
    }
    int exitCode = 0;
    {
        PF_LOG_INFO("startup: fonts and translations");
        const std::string resources = platform::resourceDir();
        net::HttpConfig httpConfig;
        httpConfig.caFile = resources + "/certs/cacert.pem";
        const auto storePath = core::dataPath("servers.json");
        const auto settingsPath = core::dataPath("settings.json");
        AppSession session(httpConfig, storePath ? *storePath : std::string(),
                           settingsPath ? *settingsPath : std::string());
        // the user's choices apply before the first frame
        session.licensesDir = resources + "/licenses";
        session.noticesFile = resources + "/THIRD_PARTY_LICENSES";
        const std::string uiLanguage = session.settings().language.empty() ? language : session.settings().language;
        ui::applyAccent(accentById(session.settings().accent));
        ui::NanoVgText metrics(vg);
        if (!metrics.loadFonts(resources + "/fonts", uiLanguage)) {
            exitCode = 1;
        }
        ui::translator().load(resources + "/i18n", uiLanguage);
        session.synchronous = testMode;
        session.frozenPlayback = testMode;
        if (!options.fixedTime.empty()) {
            session.fixedNow = jellyfin::parseUtc(options.fixedTime);
            session.fixedUtcOffset = options.utcOffsetMinutes;
        }
        session.discovery = options.discovery;
        if (!storePath || !session.load().ok()) {
            PF_LOG_WARN("servers.json unavailable: servers will not be remembered");
        }
        core::MainQueue mainQueue;  // declared before the pool: outlives it (jobs still post while it joins)
        core::WorkerPool pool(
#ifdef PLAYFIN_PS4
            2
#else
            4
#endif
        );
        ui::NvgTextures textures(vg);
        ui::ImageLoader::Config imageConfig;
        imageConfig.synchronous = testMode;
        if (const auto cacheDir = core::dataPath("cache/images"); cacheDir && core::makeDirs(*cacheDir)) {
            imageConfig.diskDirectory = *cacheDir;
        }
        ui::ImageLoader images(pool, mainQueue, textures, imageConfig);
        if (options.screen == "gallery") {
            images.setFetch([](const std::string& url, const core::CancelToken&) { return galleryImage(url); });
        } else {
            images.setFetch([&session](const std::string& url, const core::CancelToken& token) {
                return session.fetchImage(url, token);
            });
        }
        ui::Application application(ui::Application::Services{pool, mainQueue, metrics, &images});
        application.setTransitions(!testMode);  // screenshots of script tests show screens at rest
        // the halo background, rebuilt when the accent changes
        unsigned haloGeneration = 0;
        int halo = 0;
        const auto refreshHalo = [&] {
            if (haloGeneration == ui::themeGeneration()) {
                return;
            }
            haloGeneration = ui::themeGeneration();
            if (halo > 0) {
                nvgDeleteImage(vg, halo);
            }
            const std::vector<std::uint8_t> pixels = ui::haloImage(1920, 1080, ui::theme().background,
                                                                   ui::theme().accent);
            halo = nvgCreateImageRGBA(vg, 1920, 1080, 0, pixels.data());
            application.setBackgroundImage(halo, 1920, 1080);
        };
        refreshHalo();
        application.push(firstScreen(options.screen, session));
        PF_LOG_INFO("startup: first frame");

        bool running = exitCode == 0;
        FrameStats stats;
        int frame = 0;
        std::uint32_t last = platform::ticksMs();
        while (running) {
            const std::uint32_t now = platform::ticksMs();
            float dt = std::clamp(static_cast<float>(now - last), 0.0f, 100.0f);
            last = now;
            std::vector<platform::Event> events;
            std::uint32_t clock = now;
#ifdef PLAYFIN_TESTING
            if (testMode) {
                dt = kFrameMs;
                clock = static_cast<std::uint32_t>(frame) * 1000u / 60u;
                driver.beginFrame(frame, events);
            }
#endif
            platform::Event event;
            while (platform::pollEvent(event, clock)) {
                if (testMode && event.type != platform::Event::Type::Quit) {
                    continue;  // only scripted input in test mode
                }
                events.push_back(event);
            }
            for (const platform::Event& e : events) {
                if (e.type == platform::Event::Type::Quit) {
                    running = false;
                } else {
                    application.handle(e);
                }
            }
            refreshHalo();
            if (session.updateMusic()) {
                application.requestRedraw();
            }
            application.update(dt);

            // test mode too: a frame that should have changed but was not redrawn fails the screenshot comparison
            const bool redraw = !software || options.benchFrames > 0 || application.needsDraw();
            // a video under the UI (player): a new picture when mpv has one, the UI drawn on transparency
            ui::VideoSource* videoSource = software ? application.videoSource() : nullptr;
            const bool newPicture = videoSource != nullptr && videoSource->renderVideo();
            const platform::Size size = platform::drawableSize();
            const auto rasterStart = std::chrono::steady_clock::now();
            double viewsMs = 0;
            if (software) {
                if (redraw) {
                    if (frame >= 3) {
                        render::resetRasterStats();
                    }
                    const ui::Color clear = application.clearColor();
                    surface.clear(clear.r, clear.g, clear.b, clear.a);
                    nvgBeginFrame(vg, static_cast<float>(surface.width), static_cast<float>(surface.height), 1.0f);
                    application.draw(vg);
                    viewsMs = millisecondsSince(rasterStart);
                    nvgEndFrame(vg);
                    application.markDrawn();
                }
            } else {
                const float ratio = static_cast<float>(size.width) / kLogicalWidth;
                glViewport(0, 0, size.width, size.height);
                const ui::Color clear = application.clearColor();
                glClearColor(clear.r / 255.0f, clear.g / 255.0f, clear.b / 255.0f, clear.a / 255.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                nvgBeginFrame(vg, kLogicalWidth, static_cast<float>(size.height) / ratio, ratio);
                application.draw(vg);
                nvgEndFrame(vg);
                application.markDrawn();
            }
            const double rasterMs = software && redraw ? millisecondsSince(rasterStart) : 0.0;
            if (frame == 0 || frame == 300) {
                PF_LOG_INFO("startup: frame %d drawn", frame);
            }
            if (software && redraw && frame >= 3 && rasterMs > kSlowFrameMs && options.benchFrames == 0) {
                const render::RasterStats r = render::rasterStats();
                const render::FlushTimes t = render::lastFlushTimes();
                PF_LOG_INFO("slow frame: %.1f ms (views %.1f ms, flush %.1f ms, threads %.1f %.1f %.1f %.1f), "
                            "triangles %ld, pixels %ld: shaded %ld (text %ld, image %ld), image path %ld (copied %ld); "
                            "time %% image fills %d solid fills %d other fills %d strokes %d text %d",
                            rasterMs, viewsMs, t.wallMs, t.busyMs[0], t.busyMs[1], t.busyMs[2], t.busyMs[3],
                            r.triangles, r.pixels, r.shaded, r.shadedText, r.shadedImage, r.imaged, r.copied, t.share[0],
                            t.share[1], t.share[2], t.share[3], t.share[4]);
            }
#ifdef PLAYFIN_TESTING
            if (testMode) {
                const int width = software ? surface.width : size.width;
                const int height = software ? surface.height : size.height;
                if (!driver.endFrame(width, height, [&] {
                        if (videoSource != nullptr) {
                            std::vector<std::uint8_t> composed;
                            render::compositeForTest(composed, surface.width, surface.height,
                                                     videoSource->videoLayer(), surface.rgba);
                            return composed;
                        }
                        return software ? surface.rgba : render::readFramebuffer(size.width, size.height);
                    })) {
                    exitCode = 1;
                    running = false;
                }
                if (driver.quit()) {
                    running = false;
                }
            }
#endif
            const auto presentStart = std::chrono::steady_clock::now();
            if (software) {
                if (videoSource != nullptr) {
                    platform::VideoLayer layer = videoSource->videoLayer();
                    layer.changed = newPicture;
                    platform::presentFrame(surface.rgba.data(), surface.width, surface.height, redraw, &layer);
                } else {
                    platform::presentFrame(surface.rgba.data(), surface.width, surface.height, redraw);
                }
            } else {
                platform::swap();
            }
            stats.add(redraw, rasterMs, millisecondsSince(presentStart));
            if (stats.due() && options.benchFrames == 0) {
                stats.log("frames");
            }
            ++frame;
            if (options.benchFrames > 0 && frame >= options.benchFrames) {
                stats.log("bench");
                running = false;
            }
            if (options.maxFrames > 0 && frame >= options.maxFrames) {
                running = false;
            }
        }
        PF_LOG_INFO("exiting after %d frames", frame);
        // the player's and the music's stop reports and the live stream's close go out before the pool stops
        application.closeScreens();
        session.music().stop();
        if (!pool.finish(std::chrono::seconds(2))) {
            PF_LOG_WARN("exit: network jobs still running after 2 s, dropped");
        }
    }
    if (software) {
        render::destroySoftwareCanvas(vg);
    } else {
        render::destroyCanvas(vg);
    }
    log::closeFile();
    platform::shutdown();
    return exitCode;
}

}  // namespace playfin::app
