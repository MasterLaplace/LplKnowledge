/**
 * @file CurlFetcher.cpp
 * @brief Fetching with libcurl.
 *
 * @author MasterLaplace
 * @version 0.1.0
 * @copyright MIT License
 */

#include <lpl/harvest/OaiPmh.hpp>

#include <curl/curl.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <utility>

namespace lpl::harvest {

namespace {

/**
 * @brief Initialises libcurl exactly once, for the life of the process.
 *
 * @warning `curl_global_init` is documented as NOT thread-safe and as needing to run before any other
 * curl call. A function-local static gives both: the C++ runtime guarantees one initialisation
 * and orders every other thread behind it.
 *
 * @warning `curl_global_cleanup` is deliberately never called. It is only safe once no handle anywhere
 * is alive, which nothing here can know; a process-lifetime allocation released by exit is the
 * correct trade, and pretending otherwise would risk tearing down state another fetcher is using.
 *
 * @return true when libcurl came up.
 */
[[nodiscard]] bool globalInit()
{
    static const bool ready = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    return ready;
}

/**
 * @brief Appends a chunk of body.
 *
 * @param data  The bytes.
 * @param size  Element size, always 1.
 * @param count How many.
 * @param user  The destination string.
 * @return Bytes consumed; anything else aborts the transfer.
 */
std::size_t appendBody(char *data, std::size_t size, std::size_t count, void *user)
{
    const std::size_t bytes = size * count;
    static_cast<std::string *>(user)->append(data, bytes);
    return bytes;
}

/**
 * @brief Reads `Retry-After` out of the response headers.
 *
 * @warning Taken from the RESPONSE rather than guessed, because rule 2 is about obeying what the server
 * said. And reset on each header block: `CURLOPT_FOLLOWLOCATION` means several blocks arrive, and
 * a value from a redirect that was followed does not describe the response finally received.
 *
 * @param data  The header line.
 * @param size  Element size, always 1.
 * @param count How many.
 * @param user  Where to put the value.
 * @return Bytes consumed.
 */
std::size_t readHeader(char *data, std::size_t size, std::size_t count, void *user)
{
    const std::size_t bytes = size * count;
    int &retryAfter = *static_cast<int *>(user);

    const std::string_view line{data, bytes};
    if (line.size() >= 5u && (line.compare(0, 5u, "HTTP/") == 0))
    {
        retryAfter = 0;
        return bytes;
    }

    static constexpr std::string_view kName = "retry-after:";
    if (line.size() <= kName.size())
        return bytes;
    for (std::size_t i = 0u; i < kName.size(); ++i)
    {
        char c = line[i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        if (c != kName[i])
            return bytes;
    }
    // @warning Only the delta-seconds form is read. The header may also carry an HTTP date, and a
    // mis-parsed date would silently become "0 seconds" — that is, no wait at all, which is the
    // behaviour the rule exists to prevent. Left at zero, the harvester falls back to its own
    // small default rather than to none.
    retryAfter = static_cast<int>(std::strtol(line.data() + kName.size(), nullptr, 10));
    return bytes;
}

} // namespace

CurlFetcher::CurlFetcher(int timeoutSeconds, std::string userAgent)
    : _timeoutSeconds(timeoutSeconds), _userAgent(std::move(userAgent))
{
    if (globalInit())
        _handle = curl_easy_init();
}

CurlFetcher::~CurlFetcher()
{
    if (_handle != nullptr)
        curl_easy_cleanup(static_cast<CURL *>(_handle));
}

void CurlFetcher::sleepFor(int seconds)
{
    if (seconds <= 0)
        return;
    ::timespec request{static_cast<std::time_t>(seconds), 0};
    ::timespec remaining{};
    while (::nanosleep(&request, &remaining) != 0 && errno == EINTR)
        request = remaining;
}

bool CurlFetcher::get(const std::string &url, HttpResponse &out)
{
    out = HttpResponse{};
    if (_handle == nullptr)
        return false;

    // @warning ONE handle, reused for every page. That is the reason to link the library rather than
    // run the binary: a harvest is thousands of requests to one host, and a fresh process would
    // mean a fresh TCP connection and a fresh TLS handshake each time — slower for us and ruder
    // to a repository that asked to be harvested politely.
    CURL *handle = static_cast<CURL *>(_handle);
    curl_easy_reset(handle);

    int retryAfter = 0;
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &out.body);
    curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, readHeader);
    curl_easy_setopt(handle, CURLOPT_HEADERDATA, &retryAfter);
    // OAI endpoints move — arXiv's answers 301 from the address its own documentation gives.
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, static_cast<long>(_timeoutSeconds));
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 20L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, _userAgent.c_str());
    // Whatever the library was actually built to decode, asked for by name. An empty string is
    // curl's "everything you support", so this cannot request an encoding it then cannot undo.
    curl_easy_setopt(handle, CURLOPT_ACCEPT_ENCODING, "");
    // No alarm-based timeouts: they are process-wide, and this is a library.
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    // @warning NOT `CURLOPT_FAILONERROR`. A 503 is a response the harvester must SEE — it is how a
    // repository says "later" — and failing the transfer on it would turn being throttled into
    // being unreachable.

    const CURLcode result = curl_easy_perform(handle);
    if (result != CURLE_OK)
    {
        // A transfer that never completed. Distinguished from a response carrying an error
        // status, because the two mean different things to a caller: one is "the network or the
        // address is wrong", the other is "the repository answered, and said no".
        out.body.clear();
        return false;
    }

    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    out.status = static_cast<int>(status);
    out.retryAfterSeconds = retryAfter;
    return true;
}

} // namespace lpl::harvest
