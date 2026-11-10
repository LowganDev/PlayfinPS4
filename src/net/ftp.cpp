#include "net/ftp.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "core/log.hpp"
#include "net/http.hpp"

namespace playfin::net {

namespace {

constexpr std::size_t kMaxListBytes = 4 * 1024 * 1024;
constexpr std::size_t kReadAhead = 4 * 1024 * 1024;  // the reader pauses its transfer past this much unread

void applyCommon(CURL* curl, const std::string& url) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "ftp");
    curl_easy_setopt(curl, CURLOPT_FTP_FILEMETHOD, static_cast<long>(CURLFTPMETHOD_NOCWD));
    curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, 0L);  // GoldHEN's server answers 502: straight to PASV
    curl_easy_setopt(curl, CURLOPT_FTP_SKIP_PASV_IP, 1L);  // the data connection goes where the control one does
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 20L);
#ifdef PLAYFIN_PS4
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, static_cast<long>(CURL_IPRESOLVE_V4));
    curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 0L);
#endif
}

core::Error errorOf(CURLcode code, const std::string& what) {
    core::ErrorKind kind = core::ErrorKind::Network;
    switch (code) {
    case CURLE_ABORTED_BY_CALLBACK:
        kind = core::ErrorKind::Cancelled;
        break;
    case CURLE_OPERATION_TIMEDOUT:
        kind = core::ErrorKind::Timeout;
        break;
    case CURLE_REMOTE_FILE_NOT_FOUND:
    case CURLE_FTP_COULDNT_RETR_FILE:
        kind = core::ErrorKind::NotFound;
        break;
    case CURLE_WRITE_ERROR:
    case CURLE_READ_ERROR:
    case CURLE_UPLOAD_FAILED:
    case CURLE_QUOTE_ERROR:
    case CURLE_REMOTE_ACCESS_DENIED:
    case CURLE_REMOTE_DISK_FULL:
        kind = core::ErrorKind::Storage;
        break;
    default:
        break;
    }
    return core::makeError(kind, what + ": " + curl_easy_strerror(code));
}

struct TextSink {
    std::string* text;
    std::size_t limit;
    bool tooBig = false;
};

std::size_t onText(char* data, std::size_t size, std::size_t count, void* user) {
    auto* sink = static_cast<TextSink*>(user);
    const std::size_t bytes = size * count;
    if (sink->text->size() + bytes > sink->limit) {
        sink->tooBig = true;
        return 0;
    }
    sink->text->append(data, bytes);
    return bytes;
}

int onCancel(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    const auto* token = static_cast<const core::CancelToken*>(user);
    return token != nullptr && token->cancelled() ? 1 : 0;
}

void cancellable(CURL* curl, const core::CancelToken& token) {
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, onCancel);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, const_cast<core::CancelToken*>(&token));
}

struct Handle {
    CURL* curl = curl_easy_init();
    ~Handle() {
        if (curl != nullptr) {
            curl_easy_cleanup(curl);
        }
    }
};

bool isDigit(char c) { return c >= '0' && c <= '9'; }

// curl hands the FTP replies of a request without a body to the write function as headers: dropped
std::size_t discard(char*, std::size_t size, std::size_t count, void*) { return size * count; }

}  // namespace

bool validFtpPath(std::string_view path) {
    if (path.empty() || path.front() != '/' || path.size() > 1024) {
        return false;
    }
    std::size_t start = 1;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find('/', start), path.size());
        const std::string_view part = path.substr(start, end - start);
        if ((part.empty() && end != path.size()) || part == "." || part == "..") {
            return false;
        }
        start = end + 1;
    }
    return std::none_of(path.begin(), path.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; });
}

std::vector<FtpEntry> parseFtpList(std::string_view text) {
    std::vector<FtpEntry> out;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.size() < 10 || (line[0] != 'd' && line[0] != '-')) {
            continue;  // links, totals, anything else
        }
        // mode, links, owner, group, size, month, day, year or time, then the name (which may hold spaces)
        std::size_t pos = 0;
        std::string_view fields[8];
        int field = 0;
        while (field < 8 && pos < line.size()) {
            while (pos < line.size() && line[pos] == ' ') {
                ++pos;
            }
            const std::size_t from = pos;
            while (pos < line.size() && line[pos] != ' ') {
                ++pos;
            }
            fields[field++] = line.substr(from, pos - from);
        }
        if (field < 8 || pos >= line.size()) {
            continue;
        }
        const std::string_view name = line.substr(pos + 1);
        if (name.empty() || name == "." || name == ".." || name.find('/') != std::string_view::npos ||
            std::any_of(name.begin(), name.end(), [](char c) { return static_cast<unsigned char>(c) < 0x20; })) {
            continue;
        }
        FtpEntry entry;
        entry.name = std::string(name);
        entry.directory = line[0] == 'd';
        const std::string_view size = fields[4];
        if (!size.empty() && size.size() <= 18 && std::all_of(size.begin(), size.end(), isDigit)) {
            entry.size = std::stoll(std::string(size));
        }
        out.push_back(std::move(entry));
    }
    return out;
}

FtpClient::FtpClient(std::string base) : base_(std::move(base)) {
    globalInit();
    while (!base_.empty() && base_.back() == '/') {
        base_.pop_back();
    }
}

std::string FtpClient::url(const std::string& path) const {
    // "ftp://host:port//mnt/usb0/x": the double slash makes the path absolute for curl (sent as "/mnt/usb0/x")
    std::string escaped;
    for (const char raw : path) {
        const auto c = static_cast<unsigned char>(raw);
        if (std::isalnum(c) || c == '/' || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped += static_cast<char>(c);
        } else {
            static const char* hex = "0123456789ABCDEF";
            escaped += '%';
            escaped += hex[c >> 4];
            escaped += hex[c & 15];
        }
    }
    return base_ + "/" + escaped;
}

core::Result<std::vector<FtpEntry>> FtpClient::list(const std::string& dir, const core::CancelToken& token) const {
    if (!validFtpPath(dir)) {
        return core::makeError(core::ErrorKind::InvalidAddress, "bad path");
    }
    Handle handle;
    if (handle.curl == nullptr) {
        return core::makeError(core::ErrorKind::Network, "curl is not available");
    }
    std::string text;
    TextSink sink{&text, kMaxListBytes};
    applyCommon(handle.curl, url(dir.back() == '/' ? dir : dir + "/"));
    curl_easy_setopt(handle.curl, CURLOPT_WRITEFUNCTION, onText);
    curl_easy_setopt(handle.curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(handle.curl, CURLOPT_TIMEOUT_MS, 30000L);
    cancellable(handle.curl, token);
    const CURLcode code = curl_easy_perform(handle.curl);
    if (code != CURLE_OK) {
        return errorOf(code, "list " + dir);
    }
    return parseFtpList(text);
}

core::Result<std::int64_t> FtpClient::size(const std::string& path, const core::CancelToken& token) const {
    if (!validFtpPath(path)) {
        return core::makeError(core::ErrorKind::InvalidAddress, "bad path");
    }
    Handle handle;
    if (handle.curl == nullptr) {
        return core::makeError(core::ErrorKind::Network, "curl is not available");
    }
    applyCommon(handle.curl, url(path));
    curl_easy_setopt(handle.curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(handle.curl, CURLOPT_WRITEFUNCTION, discard);
    curl_easy_setopt(handle.curl, CURLOPT_TIMEOUT_MS, 15000L);
    cancellable(handle.curl, token);
    const CURLcode code = curl_easy_perform(handle.curl);
    if (code != CURLE_OK) {
        return errorOf(code, "size " + path);
    }
    curl_off_t length = -1;
    curl_easy_getinfo(handle.curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);
    return static_cast<std::int64_t>(length);
}

core::Result<std::string> FtpClient::fetch(const std::string& path, std::size_t maxBytes,
                                           const core::CancelToken& token) const {
    if (!validFtpPath(path)) {
        return core::makeError(core::ErrorKind::InvalidAddress, "bad path");
    }
    Handle handle;
    if (handle.curl == nullptr) {
        return core::makeError(core::ErrorKind::Network, "curl is not available");
    }
    std::string text;
    TextSink sink{&text, maxBytes};
    applyCommon(handle.curl, url(path));
    curl_easy_setopt(handle.curl, CURLOPT_WRITEFUNCTION, onText);
    curl_easy_setopt(handle.curl, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(handle.curl, CURLOPT_TIMEOUT_MS, 30000L);
    cancellable(handle.curl, token);
    const CURLcode code = curl_easy_perform(handle.curl);
    if (sink.tooBig) {
        return core::makeError(core::ErrorKind::BadResponse, "file too big");
    }
    if (code != CURLE_OK) {
        return errorOf(code, "read " + path);
    }
    return text;
}

core::Result<core::Unit> FtpClient::command(const std::vector<std::string>& commands) const {
    Handle handle;
    if (handle.curl == nullptr) {
        return core::makeError(core::ErrorKind::Network, "curl is not available");
    }
    curl_slist* list = nullptr;
    for (const std::string& line : commands) {
        list = curl_slist_append(list, line.c_str());
    }
    applyCommon(handle.curl, base_ + "/");
    curl_easy_setopt(handle.curl, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(handle.curl, CURLOPT_WRITEFUNCTION, discard);
    curl_easy_setopt(handle.curl, CURLOPT_QUOTE, list);
    curl_easy_setopt(handle.curl, CURLOPT_TIMEOUT_MS, 15000L);
    const CURLcode code = curl_easy_perform(handle.curl);
    curl_slist_free_all(list);
    if (code != CURLE_OK) {
        return errorOf(code, commands.empty() ? std::string("ftp") : commands.front());
    }
    return core::Unit{};
}

core::Result<core::Unit> FtpClient::makeDir(const std::string& path) const {
    if (!validFtpPath(path)) {
        return core::makeError(core::ErrorKind::InvalidAddress, "bad path");
    }
    return command({"*MKD " + path});  // "*": an existing folder is not a failure
}

core::Result<core::Unit> FtpClient::remove(const std::string& path) const {
    if (!validFtpPath(path)) {
        return core::makeError(core::ErrorKind::InvalidAddress, "bad path");
    }
    return command({"DELE " + path});
}

core::Result<core::Unit> FtpClient::rename(const std::string& from, const std::string& to) const {
    if (!validFtpPath(from) || !validFtpPath(to)) {
        return core::makeError(core::ErrorKind::InvalidAddress, "bad path");
    }
    return command({"RNFR " + from, "RNTO " + to});
}

namespace {

struct UploadSource {
    FtpClient::Source* source;
};

std::size_t onUpload(char* buffer, std::size_t size, std::size_t count, void* user) {
    auto* upload = static_cast<UploadSource*>(user);
    return (*upload->source)(buffer, size * count);
}

}  // namespace

core::Result<core::Unit> FtpClient::upload(const std::string& path, Source source, bool append,
                                           const core::CancelToken& token) const {
    if (!validFtpPath(path) || !source) {
        return core::makeError(core::ErrorKind::InvalidAddress, "bad path");
    }
    Handle handle;
    if (handle.curl == nullptr) {
        return core::makeError(core::ErrorKind::Network, "curl is not available");
    }
    UploadSource upload{&source};
    applyCommon(handle.curl, url(path));
    curl_easy_setopt(handle.curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(handle.curl, CURLOPT_APPEND, append ? 1L : 0L);
    curl_easy_setopt(handle.curl, CURLOPT_READFUNCTION, onUpload);
    curl_easy_setopt(handle.curl, CURLOPT_READDATA, &upload);
    cancellable(handle.curl, token);
    const CURLcode code = curl_easy_perform(handle.curl);
    if (code != CURLE_OK) {
        return errorOf(code, "write " + path);
    }
    return core::Unit{};
}

// ---- reader

struct FtpReader::Impl {
    std::string url;
    const FtpClient* client;
    std::string path;
    CURLM* multi = curl_multi_init();
    CURL* easy = nullptr;
    std::string buffer;
    std::size_t taken = 0;  // bytes of `buffer` already read
    bool paused = false;
    bool done = false;
    CURLcode result = CURLE_OK;
    std::int64_t position = 0;
    std::int64_t size = -2;  // -2: not asked yet

    ~Impl() {
        stop();
        if (multi != nullptr) {
            curl_multi_cleanup(multi);
        }
    }
    void stop() {
        if (easy != nullptr) {
            curl_multi_remove_handle(multi, easy);
            curl_easy_cleanup(easy);
            easy = nullptr;
        }
        buffer.clear();
        taken = 0;
        paused = false;
        done = false;
        result = CURLE_OK;
    }
    bool start() {
        stop();
        easy = curl_easy_init();
        if (easy == nullptr || multi == nullptr) {
            return false;
        }
        applyCommon(easy, url);
        curl_easy_setopt(easy, CURLOPT_RESUME_FROM_LARGE, static_cast<curl_off_t>(position));
        curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, onData);
        curl_easy_setopt(easy, CURLOPT_WRITEDATA, this);
        return curl_multi_add_handle(multi, easy) == CURLM_OK;
    }
    static std::size_t onData(char* data, std::size_t size, std::size_t count, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (self->buffer.size() - self->taken >= kReadAhead) {
            self->paused = true;
            return CURL_WRITEFUNC_PAUSE;
        }
        self->buffer.append(data, size * count);
        return size * count;
    }
};

FtpReader::FtpReader(const FtpClient& client, std::string path) : impl_(std::make_unique<Impl>()) {
    impl_->client = &client;
    impl_->path = std::move(path);
    impl_->url = validFtpPath(impl_->path) ? client.url(impl_->path) : std::string();
}

FtpReader::~FtpReader() = default;

std::int64_t FtpReader::size() {
    if (impl_->size == -2) {
        const auto known = impl_->url.empty() ? core::Result<std::int64_t>(core::makeError(core::ErrorKind::NotFound, ""))
                                              : impl_->client->size(impl_->path);
        impl_->size = known.ok() ? known.value() : -1;
    }
    return impl_->size;
}

bool FtpReader::seek(std::int64_t offset) {
    if (offset < 0 || impl_->url.empty()) {
        return false;
    }
    if (offset != impl_->position || impl_->easy == nullptr) {
        impl_->stop();  // the next read starts a transfer there
        impl_->position = offset;
    }
    return true;
}

long long FtpReader::read(char* buffer, std::size_t size) {
    Impl& self = *impl_;
    if (cancelled_) {
        return -1;
    }
    if (self.url.empty() || size == 0) {
        return self.url.empty() ? -1 : 0;
    }
    if (self.size == -2) {
        this->size();
    }
    if (self.size >= 0 && self.position >= self.size) {
        return 0;
    }
    if (self.easy == nullptr && !self.start()) {
        return -1;
    }
    while (true) {
        if (cancelled_) {
            return -1;
        }
        const std::size_t available = self.buffer.size() - self.taken;
        if (available > 0) {
            const std::size_t count = std::min(available, size);
            std::memcpy(buffer, self.buffer.data() + self.taken, count);
            self.taken += count;
            self.position += static_cast<std::int64_t>(count);
            if (self.taken >= 1024 * 1024) {
                self.buffer.erase(0, self.taken);
                self.taken = 0;
            }
            if (self.paused && self.buffer.size() - self.taken < kReadAhead / 2) {
                self.paused = false;
                curl_easy_pause(self.easy, CURLPAUSE_CONT);
            }
            return static_cast<long long>(count);
        }
        if (self.done) {
            if (self.result != CURLE_OK) {
                PF_LOG_WARN("ftp: %s: %s", self.path.c_str(), curl_easy_strerror(self.result));
                return -1;
            }
            return 0;
        }
        int running = 0;
        curl_multi_perform(self.multi, &running);
        int left = 0;
        while (CURLMsg* message = curl_multi_info_read(self.multi, &left)) {
            if (message->msg == CURLMSG_DONE && message->easy_handle == self.easy) {
                self.done = true;
                self.result = message->data.result;
            }
        }
        if (!self.done && self.buffer.size() == self.taken) {
            curl_multi_poll(self.multi, nullptr, 0, 100, nullptr);
        }
    }
}

}  // namespace playfin::net
