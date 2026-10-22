#include "engine/asset_browser.hpp"
#include "asset_fresh_url.hpp"
#include <emscripten/fetch.h>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace engine {
namespace {
struct Transfer {
    AssetLoadContext context;
    AssetByteDecoder decode;
    BrowserAssetOptions options;
    emscripten_fetch_t* fetch=nullptr;
    bool closing=false;
    Transfer(AssetLoadContext context,AssetByteDecoder decode,BrowserAssetOptions options):
        context(std::move(context)),decode(std::move(decode)),options(options) {}
    void close() {
        if(auto* value=std::exchange(fetch,nullptr)) {
            closing=true;emscripten_fetch_close(value);closing=false;
        }
    }
};
void succeeded(emscripten_fetch_t* fetch) {
    auto& transfer=*static_cast<Transfer*>(fetch->userData);
    if(transfer.closing) return;
    try {
        if(!transfer.context.cancelled()) {
            if(fetch->status<200||fetch->status>=300) throw std::runtime_error("Asset HTTP status "+std::to_string(fetch->status));
            if(!fetch->data||!fetch->numBytes||fetch->numBytes>transfer.options.maxBytes)
                throw std::runtime_error("Asset response is empty or exceeds byte limit");
            transfer.context.progress(fetch->numBytes,fetch->numBytes);
            transfer.decode({reinterpret_cast<const std::byte*>(fetch->data),static_cast<std::size_t>(fetch->numBytes)},transfer.context);
        }
    } catch(const std::exception& error) { transfer.context.fail(error.what()); }
    catch(...) { transfer.context.fail("Asset decoder threw an unknown exception"); }
    transfer.close();
}
void failed(emscripten_fetch_t* fetch) {
    auto& transfer=*static_cast<Transfer*>(fetch->userData);
    if(transfer.closing) return;
    transfer.context.fail("Asset HTTP/network failure (status "+std::to_string(fetch->status)+")");transfer.close();
}
void progressed(emscripten_fetch_t* fetch) {
    auto& transfer=*static_cast<Transfer*>(fetch->userData);
    if(transfer.closing) return;
    const auto completed=fetch->dataOffset+fetch->numBytes;
    if(completed>transfer.options.maxBytes||fetch->totalBytes>transfer.options.maxBytes) {
        transfer.context.fail("Asset response exceeds byte limit");transfer.close();return;
    }
    transfer.context.progress(completed,fetch->totalBytes);
}
}
AssetLoader browserAssetLoader(std::string url,AssetByteDecoder decoder,BrowserAssetOptions options) {
    if(url.empty()||url.find('\0')!=std::string::npos||!decoder||!options.maxBytes||!options.timeoutMilliseconds)
        throw std::invalid_argument("Invalid browser asset loader configuration");
    return [url=std::move(url),decoder=std::move(decoder),options](AssetLoadContext context)->AssetCancel {
        auto transfer=std::make_shared<Transfer>(context,decoder,options);
        emscripten_fetch_attr_t attributes;emscripten_fetch_attr_init(&attributes);
        std::strcpy(attributes.requestMethod,"GET");
        attributes.attributes=EMSCRIPTEN_FETCH_LOAD_TO_MEMORY|EMSCRIPTEN_FETCH_REPLACE;
        attributes.timeoutMSecs=options.timeoutMilliseconds;attributes.userData=transfer.get();
        attributes.onsuccess=succeeded;attributes.onerror=failed;attributes.onprogress=progressed;
        const auto location=options.fresh?asset_detail::freshUrl(url):url;
        transfer->fetch=emscripten_fetch(&attributes,location.c_str());
        if(!transfer->fetch) context.fail("Could not start asset HTTP request");
        return [transfer]{transfer->close();};
    };
}
AssetLoader browserAssetFingerprintLoader(std::string url,AssetSourceOptions options){options.fresh=true;return browserDecodedAssetLoader(std::move(url),AssetDecodeKind::Fingerprint,AssetDecodeOptions{options,4096},"asset-worker.js");}
} // namespace engine
