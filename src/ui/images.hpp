#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/result.hpp"
#include "core/task.hpp"

namespace playfin::ui {

// GPU textures (NanoVG in the app, a fake in tests). Main thread only.
class TextureFactory {
public:
    virtual ~TextureFactory() = default;
    virtual int create(const std::uint8_t* rgba, int width, int height) = 0;
    virtual void destroy(int id) = 0;
};

struct Texture {
    int id = 0;
    int width = 0;
    int height = 0;
    bool opaque = false;  // every pixel has alpha 255
};
using TextureRef = std::shared_ptr<const Texture>;

struct DecodedImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    bool opaque = false;  // every pixel has alpha 255
};

// Larger pictures are refused before decoding (a small file can declare a huge picture).
constexpr int kMaxImageSide = 4096;
constexpr long long kMaxImagePixels = 8LL * 1024 * 1024;  // a progressive JPEG needs about 13 bytes per pixel

// Whole: the full picture, box-filtered down so it still covers width x height. Crop: the same, then the centre
// cut to width x height, so a covering view shows it pixel for pixel (the CPU renderer then copies rows).
enum class ImageShape { Whole, Crop };

// JPEG/PNG → RGBA, box-filtered down (never upscaled). nullopt on bad data or over the limits above.
std::optional<DecodedImage> decodeImage(const std::string& bytes, int width, int height,
                                        ImageShape shape = ImageShape::Whole);
// The header names a supported format within the limits (cheap: nothing is decoded).
bool looksLikeImage(const std::string& bytes);
std::uint64_t fnv1a(std::string_view text);

// Downloaded image bytes in <directory>/<fnv1a(url) as 16 hex>.img, oldest deleted beyond the budget.
// Only files named like that are ever deleted. Thread-safe.
class DiskCache {
public:
    DiskCache(std::string directory, std::size_t budgetBytes);
    std::optional<std::string> read(const std::string& url);
    void write(const std::string& url, const std::string& bytes);
    void remove(const std::string& url);
    void trim();
    std::size_t size() const;

private:
    std::string pathFor(const std::string& url) const;
    void scan();

    std::string directory_;
    std::size_t budget_;
    mutable std::mutex mutex_;
    std::size_t size_ = 0;
};

// Destroying a request drops its callback; work no request wants any more is skipped.
class ImageRequest {
public:
    explicit ImageRequest(core::CancelToken token) : token_(std::move(token)) {}
    ~ImageRequest() { token_.cancel(); }
    ImageRequest(const ImageRequest&) = delete;
    ImageRequest& operator=(const ImageRequest&) = delete;

private:
    core::CancelToken token_;
};

// memory LRU of textures → disk cache → fetch (injected). Fetch and decode on the pool, uploads on the main thread
// (uploadPending, a few per frame). Callbacks run on the main thread with the texture, or nullptr on failure.
class ImageLoader {
public:
    using Fetch = std::function<core::Result<std::string>(const std::string& url, const core::CancelToken& token)>;
    using Callback = std::function<void(TextureRef)>;
    struct Config {
        std::size_t memoryBudget = 192u * 1024u * 1024u;
        std::string diskDirectory;  // empty: no disk cache
        std::size_t diskBudget = 300u * 1024u * 1024u;
        int uploadsPerFrame = 4;
        bool synchronous = false;  // test mode: fetch and decode inline (deterministic)
    };

    ImageLoader(core::WorkerPool& pool, core::MainQueue& mainQueue, TextureFactory& factory, Config config);
    ~ImageLoader();

    void setFetch(Fetch fetch);
    std::unique_ptr<ImageRequest> request(const std::string& url, int width, int height, Callback ready,
                                          ImageShape shape = ImageShape::Whole);
    // Uploads up to uploadsPerFrame decoded images; returns how many requests finished (delivered or failed).
    std::size_t uploadPending();
    std::size_t memoryUsed() const { return memoryUsed_; }
    std::size_t cachedCount() const { return cache_.size(); }
    bool idle() const { return waiters_.empty(); }  // no request in flight

private:
    struct Waiter {
        core::CancelToken token;
        Callback callback;
    };
    struct Decoded {
        std::string key;
        std::string url;
        int width = 0;
        int height = 0;
        ImageShape shape = ImageShape::Whole;
        bool skipped = false;  // nobody wanted it any more when the worker got to it
        std::optional<DecodedImage> image;
    };
    struct Shared;  // state reachable from worker jobs
    struct Job;     // the tokens of the requests waiting for one key, readable from workers

    void start(const std::string& key, const std::string& url, int width, int height, ImageShape shape);
    void finish(const Decoded& decoded, TextureRef texture);
    void insert(const std::string& key, const TextureRef& texture);

    core::WorkerPool& pool_;
    core::MainQueue& mainQueue_;
    TextureFactory& factory_;
    Config config_;
    std::shared_ptr<Shared> shared_;
    std::map<std::string, std::vector<Waiter>> waiters_;
    std::map<std::string, std::shared_ptr<Job>> jobs_;
    std::list<std::pair<std::string, TextureRef>> lru_;  // most recent first
    std::map<std::string, std::list<std::pair<std::string, TextureRef>>::iterator> cache_;
    std::size_t memoryUsed_ = 0;
};

}  // namespace playfin::ui
