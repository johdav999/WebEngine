#include "engine/asset_desktop.hpp"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>

namespace engine {
namespace {
using Clock=std::chrono::steady_clock;
void validate(AssetSourceOptions options) {
    if(!options.maxBytes||options.maxBytes>desktopFileLimit||!options.timeoutMilliseconds||options.timeoutMilliseconds>0x7fffffff)
        throw std::invalid_argument("Invalid background source limits");
}
}
struct DesktopAssetExecutor::Impl {
    struct Job {
        AssetLoadContext context;
        std::function<std::filesystem::path()> path;
        Decode decode;
        AssetSourceOptions options;
        Clock::time_point deadline{};
        bool stopped=false; // All job fields below construction use the pool mutex.
    };
    DesktopAssetExecutorSettings settings;
    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::shared_ptr<Job>> queue;
    std::vector<std::shared_ptr<Job>> active;
    std::vector<std::thread> workers;
    std::thread timer;
    bool closing=false;
    explicit Impl(DesktopAssetExecutorSettings s):settings(s) {}
    void start() {
        try {
            for(std::size_t i=0;i<settings.workers;++i)workers.emplace_back([this]{run();});
            timer=std::thread([this]{watch();});
        }catch(...){shutdown();throw;}
    }
    void shutdown() {
        std::deque<std::shared_ptr<Job>> discarded;
        {std::lock_guard lock(mutex);closing=true;
            for(auto& job:queue){job->stopped=true;job->context.fail("Background asset executor stopped");}
            discarded.swap(queue);
            for(auto& job:active){job->stopped=true;job->context.fail("Background asset executor stopped");}}
        wake.notify_all();
        if(timer.joinable())timer.join();
        for(auto& worker:workers)if(worker.joinable())worker.join();
    }
    // Called under mutex. Check deadline here as well as in the watchdog so a
    // scheduling delay cannot publish a result after its admission deadline.
    bool stopped(const std::shared_ptr<Job>& job) {
        if(job->stopped||job->context.cancelled())return true;
        if(Clock::now()>=job->deadline){job->stopped=true;job->context.fail("Background asset timeout");return true;}
        return false;
    }
    bool checkpoint(const std::shared_ptr<Job>& job) {std::lock_guard lock(mutex);return !stopped(job);}
    void watch() {
        std::unique_lock lock(mutex);
        while(!closing){
            wake.wait_for(lock,std::chrono::milliseconds(5));
            for(const auto& job:active)(void)stopped(job);
            std::erase_if(queue,[](const auto& job){return job->stopped||job->context.cancelled();});
        }
    }
    void run() {
        for(;;) {
            std::shared_ptr<Job> job;
            {std::unique_lock lock(mutex);wake.wait(lock,[&]{return closing||!queue.empty();});if(closing)return;
                job=std::move(queue.front());queue.pop_front();
                if(job->stopped||job->context.cancelled())continue;
                job->deadline=Clock::now()+std::chrono::milliseconds(job->options.timeoutMilliseconds);active.push_back(job);}
            try {
                if(checkpoint(job)) {
                    const auto path=job->path();
                    std::ifstream file(path,std::ios::binary|std::ios::ate);
                    if(!file)throw std::runtime_error("Could not open background asset file");
                    const auto length=file.tellg();
                    if(length<0||std::uint64_t(length)>job->options.maxBytes)throw std::runtime_error("Background asset exceeds source byte limit");
                    std::vector<std::byte> bytes(static_cast<std::size_t>(length));file.seekg(0);
                    job->context.progress(0,bytes.size());bool proceed=true;
                    for(std::size_t offset=0;offset<bytes.size();) {
                        if(!checkpoint(job)){proceed=false;break;}
                        const auto count=std::min<std::size_t>(64*1024,bytes.size()-offset);
                        if(!file.read(reinterpret_cast<char*>(bytes.data()+offset),std::streamsize(count)))throw std::runtime_error("Could not read complete background asset");
                        offset+=count;job->context.progress(offset,bytes.size());
                    }
                    if(proceed&&checkpoint(job)) {
                        if(file.peek()!=std::char_traits<char>::eof())throw std::runtime_error("Background asset file changed during read");
                        auto publish=job->decode(bytes);
                        std::lock_guard lock(mutex);if(!stopped(job))publish(job->context);
                    }
                }
            }catch(const std::exception& error){std::lock_guard lock(mutex);if(!stopped(job))job->context.fail(error.what());}
            catch(...){std::lock_guard lock(mutex);if(!stopped(job))job->context.fail("Background asset decode failed");}
            {std::lock_guard lock(mutex);std::erase(active,job);}wake.notify_all();
        }
    }
};
DesktopAssetExecutor::DesktopAssetExecutor(DesktopAssetExecutorSettings settings) {
    if(!settings.workers||settings.workers>8||!settings.maxQueued||settings.maxQueued>256)throw std::invalid_argument("Invalid background executor bounds");
    impl_=std::make_shared<Impl>(settings);impl_->start();
}
DesktopAssetExecutor::~DesktopAssetExecutor(){impl_->shutdown();}
DesktopAssetExecutorDiagnostics DesktopAssetExecutor::diagnostics() const {std::lock_guard lock(impl_->mutex);return {impl_->active.size(),impl_->queue.size()};}
std::filesystem::path DesktopAssetExecutor::checkedPath(std::filesystem::path path) {
    if(path.empty()||path.native().find(L'\0')!=std::wstring::npos)throw std::invalid_argument("Invalid background asset path");
    return std::filesystem::absolute(path); // Snapshot CWD on owner, never on a worker.
}
AssetLoader DesktopAssetExecutor::makeLoader(std::function<std::filesystem::path()> path,Decode decode,AssetSourceOptions options) const {
    validate(options);std::weak_ptr<Impl> service=impl_;
    return [service,path=std::move(path),decode=std::move(decode),options](AssetLoadContext context)->AssetCancel {
        const auto pool=service.lock();if(!pool){context.fail("Background asset executor expired");return {};}
        auto job=std::make_shared<Impl::Job>(Impl::Job{context,path,decode,options});
        {std::lock_guard lock(pool->mutex);
            std::erase_if(pool->queue,[](const auto& item){return item->stopped||item->context.cancelled();});
            if(pool->closing){context.fail("Background asset executor stopped");return {};}
            if(pool->queue.size()>=pool->settings.maxQueued){context.fail("Background asset queue full");return {};}
            pool->queue.push_back(job);}
        pool->wake.notify_all();
        return [service,weak=std::weak_ptr<Impl::Job>(job)]{
            if(auto pool=service.lock())if(auto job=weak.lock()){
                {std::lock_guard lock(pool->mutex);job->stopped=true;std::erase(pool->queue,job);}pool->wake.notify_all();}
        };
    };
}
DesktopAssetExecutor::Decode DesktopAssetExecutor::decoder(AssetDecodeKind kind,AssetDecodeOptions options) {
    validate(options.source);
    if(kind<AssetDecodeKind::Fbx||kind>AssetDecodeKind::Fingerprint||!options.maxResultBytes||options.maxResultBytes>desktopFileLimit)
        throw std::invalid_argument("Invalid background decode options");
    return [kind,options](std::span<const std::byte> bytes)->Publish {
        ArchiveLimits limits;limits.maxBytes=limits.maxAllocatedBytes=options.maxResultBytes;
        auto resource=hydrateDecodedAsset(decodeAssetBytes(bytes,kind,limits),kind,limits);
        return [resource=std::move(resource)](const AssetLoadContext& context){std::visit([&](auto value){context.complete(std::move(value));},resource);};
    };
}
AssetLoader DesktopAssetExecutor::decodedFileLoader(std::filesystem::path path,AssetDecodeKind kind,AssetDecodeOptions options) const {
    return makeLoader([path=checkedPath(std::move(path))]{return path;},decoder(kind,options),options.source);
}
AssetLoader DesktopAssetExecutor::decodedAssetLoader(const DesktopFiles& files,std::filesystem::path relative,AssetDecodeKind kind,AssetDecodeOptions options) const {
    (void)files.assetPath(relative);
    return makeLoader([files,relative=std::move(relative)]{return files.assetPath(relative);},decoder(kind,options),options.source);
}
}
