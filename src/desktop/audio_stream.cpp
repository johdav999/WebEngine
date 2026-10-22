#include "audio_stream.hpp"
#include "engine/audio_desktop.hpp"
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <propvarutil.h>
#include <array>
#include <cctype>
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>

namespace engine {
std::string desktopAudioStreamUri(const std::filesystem::path& path) {
    const auto p=std::filesystem::absolute(path).lexically_normal().generic_u8string();
    std::string uri="file:///";constexpr char digits[]="0123456789ABCDEF";
    for(auto byte:p){const auto c=static_cast<unsigned char>(byte);if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='/'||c==':'||c=='-'||c=='_'||c=='.'||c=='~')uri+=char(c);else{uri+='%';uri+=digits[c>>4];uri+=digits[c&15];}}
    desktop_audio::streamPath(uri);return uri;
}
namespace desktop_audio {
using Microsoft::WRL::ComPtr;
namespace {
void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Media Foundation audio failure ("+std::to_string(static_cast<unsigned long>(hr))+")");}
struct Com {Com(){check(CoInitializeEx(nullptr,COINIT_MULTITHREADED));}~Com(){CoUninitialize();}};
struct Foundation {Foundation(){check(MFStartup(MF_VERSION,MFSTARTUP_LITE));}~Foundation(){MFShutdown();}};
std::shared_ptr<Foundation> foundation(){static std::mutex mutex;static std::weak_ptr<Foundation> weak;std::lock_guard lock(mutex);auto p=weak.lock();if(!p){p=std::make_shared<Foundation>();weak=p;}return p;}
}
std::filesystem::path streamPath(const std::string& uri) {
    if(!uri.starts_with("file:///")||uri.size()<12||uri.size()>4096||uri.find_first_of("?#")!=std::string::npos)throw std::invalid_argument("Native streams require a local file URI from desktopAudioStreamUri");
    std::string text;auto hex=[](char c)->int{if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='F')return c-'A'+10;if(c>='a'&&c<='f')return c-'a'+10;return -1;};
    for(std::size_t i=8;i<uri.size();++i){unsigned char c=uri[i];if(c=='%'){if(i+2>=uri.size()||hex(uri[i+1])<0||hex(uri[i+2])<0)throw std::invalid_argument("Invalid file URI escape");c=static_cast<unsigned char>(hex(uri[i+1])*16+hex(uri[i+2]));i+=2;}if(c<32||c==127)throw std::invalid_argument("Invalid file URI character");text+=char(c);}
    if(text.size()<3||text[1]!=':'||text[2]!='/'||!std::isalpha(static_cast<unsigned char>(text[0]))||text.find(':',2)!=std::string::npos||text.find('\\')!=std::string::npos||text.find_first_of("?*|<>\"")!=std::string::npos)throw std::invalid_argument("Only local drive audio files are supported");
    if(!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0))throw std::invalid_argument("Invalid UTF-8 audio path");
    return std::filesystem::path(std::u8string(text.begin(),text.end()));
}
struct Stream::Impl {
    static constexpr std::size_t capacity=32768;
    mutable std::mutex mutex;std::condition_variable cv;
    std::array<std::array<float,2>,capacity> ring{};
    std::size_t head=0,count=0;double phase=0,position=0,duration=-1,requested=0;
    std::uint64_t generation=1,decoded=0,underruns=0;
    bool stop=false,eof=false,failed=false,loop=false;
    std::string error;std::thread worker;
    Impl(std::filesystem::path path,double offset,bool looping):position(offset),requested(offset),loop(looping) {worker=std::thread([this,path=std::move(path)]{run(path);});}
    ~Impl(){{std::lock_guard lock(mutex);stop=true;cv.notify_all();}if(worker.joinable())worker.join();}
    void run(const std::filesystem::path& path) noexcept {
        try {
            Com com;auto mf=foundation();ComPtr<IMFSourceReader> reader;check(MFCreateSourceReaderFromURL(path.c_str(),nullptr,&reader));
            check(reader->SetStreamSelection(DWORD(MF_SOURCE_READER_ALL_STREAMS),FALSE));check(reader->SetStreamSelection(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM),TRUE));
            ComPtr<IMFMediaType> format;check(MFCreateMediaType(&format));check(format->SetGUID(MF_MT_MAJOR_TYPE,MFMediaType_Audio));check(format->SetGUID(MF_MT_SUBTYPE,MFAudioFormat_Float));
            check(format->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,2));check(format->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,48000));check(format->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE,32));check(format->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT,8));check(format->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,384000));
            check(reader->SetCurrentMediaType(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM),nullptr,format.Get()));
            PROPVARIANT value{};check(reader->GetPresentationAttribute(DWORD(MF_SOURCE_READER_MEDIASOURCE),MF_PD_DURATION,&value));
            const double length=value.vt==VT_UI8?double(value.uhVal.QuadPart)/10000000:-1;PropVariantClear(&value);
            if(!std::isfinite(length)||length<=0||length>86400)throw std::runtime_error("Invalid/unsupported stream duration (maximum 24 hours)");
            {std::lock_guard lock(mutex);duration=length;if(loop)requested=position=std::fmod(requested,duration);if(requested>=duration)throw std::runtime_error("Stream offset outside duration");}
            std::uint64_t current=0;std::size_t emptyReads=0;double target=0;bool trim=false,doSeek=false;
            for(;;) {
                {std::unique_lock lock(mutex);cv.wait(lock,[&]{return stop||generation!=current||(!eof&&count<capacity/2);});if(stop)return;
                    if(generation!=current){current=generation;target=requested;trim=true;doSeek=true;eof=false;}}
                if(doSeek){doSeek=false;PROPVARIANT seek{};InitPropVariantFromInt64(static_cast<LONGLONG>(target*10000000),&seek);check(reader->SetCurrentPosition(GUID_NULL,seek));PropVariantClear(&seek);}
                DWORD flags=0;LONGLONG timestamp=0;ComPtr<IMFSample> sample;check(reader->ReadSample(DWORD(MF_SOURCE_READER_FIRST_AUDIO_STREAM),0,nullptr,&flags,&timestamp,&sample));
                if(flags&MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)throw std::runtime_error("Changing audio format is unsupported");
                if(sample)emptyReads=0;else if(++emptyReads>256)throw std::runtime_error("Audio decoder produced no samples");
                if(sample){ComPtr<IMFMediaBuffer> buffer;check(sample->ConvertToContiguousBuffer(&buffer));DWORD bytes=0;check(buffer->GetCurrentLength(&bytes));
                    if(bytes>1024*1024||bytes%8)throw std::runtime_error("Invalid decoded audio block");BYTE* data=nullptr;check(buffer->Lock(&data,nullptr,nullptr));
                    std::vector<float> pcm;try{const auto* f=reinterpret_cast<const float*>(data);pcm.assign(f,f+bytes/4);}catch(...){buffer->Unlock();throw;}check(buffer->Unlock());
                    std::size_t first=0;if(trim){const double skip=(target-double(timestamp)/10000000)*48000;if(skip>0)first=std::min(pcm.size()/2,std::size_t(std::ceil(skip)));if(first<pcm.size()/2)trim=false;}
                    for(std::size_t at=first;at<pcm.size()/2;){std::unique_lock lock(mutex);cv.wait(lock,[&]{return stop||generation!=current||count<capacity;});if(stop)return;if(generation!=current)break;
                        const auto n=std::min(capacity-count,pcm.size()/2-at);for(std::size_t i=0;i<n;++i){const float l=pcm[(at+i)*2],r=pcm[(at+i)*2+1];if(!std::isfinite(l)||!std::isfinite(r))throw std::runtime_error("Nonfinite stream PCM");ring[(head+count+i)%capacity]={std::clamp(l,-1.f,1.f),std::clamp(r,-1.f,1.f)};}count+=n;decoded+=n;at+=n;}
                }
                if(flags&MF_SOURCE_READERF_ENDOFSTREAM){std::lock_guard lock(mutex);if(generation==current){if(loop){target=0;trim=true;doSeek=true;}else eof=true;}}
            }
        }catch(const std::exception& e){std::lock_guard lock(mutex);failed=true;error=e.what();count=0;}
    }
};
Stream::Stream(std::filesystem::path path,double offset,bool loop):impl_(std::make_unique<Impl>(std::move(path),offset,loop)){}
Stream::~Stream()=default;
bool Stream::render(std::span<float> out,float rate) noexcept {
    auto& p=*impl_;std::lock_guard lock(p.mutex);std::fill(out.begin(),out.end(),0.f);
    if(p.failed)return false;
    for(std::size_t i=0;i+1<out.size();i+=2){const auto needed=std::size_t(p.phase+rate);if(!p.count||(!p.eof&&p.count<std::max(std::size_t(2),needed))){if(!p.eof)++p.underruns;break;}
        const auto& a=p.ring[p.head];const auto& b=p.ring[(p.head+(p.count>1?1:0))%Impl::capacity];out[i]=a[0]+float(p.phase)*(b[0]-a[0]);out[i+1]=a[1]+float(p.phase)*(b[1]-a[1]);
        p.phase+=rate;const auto consumed=std::min(std::size_t(p.phase),p.count);p.phase-=consumed;p.head=(p.head+consumed)%Impl::capacity;p.count-=consumed;p.position+=rate/48000;if(p.loop&&p.duration>0)p.position=std::fmod(p.position,p.duration);}
    p.cv.notify_all();return !p.eof||p.count>0;
}
bool Stream::seek(double time) noexcept {auto& p=*impl_;std::lock_guard lock(p.mutex);if(p.failed||!std::isfinite(time)||time<0||time>=86400||(p.duration>0&&time>=p.duration))return false;p.requested=p.position=time;p.count=p.head=0;p.phase=0;p.eof=false;++p.generation;p.cv.notify_all();return true;}
double Stream::position() const noexcept {std::lock_guard lock(impl_->mutex);return impl_->position;}
bool Stream::playing() const noexcept {std::lock_guard lock(impl_->mutex);return !impl_->failed&&(!impl_->eof||impl_->count>0);}
std::size_t Stream::buffered() const noexcept {std::lock_guard lock(impl_->mutex);return impl_->count;}
std::uint64_t Stream::decoded() const noexcept {std::lock_guard lock(impl_->mutex);return impl_->decoded;}
std::uint64_t Stream::underruns() const noexcept {std::lock_guard lock(impl_->mutex);return impl_->underruns;}
std::string Stream::error() const {std::lock_guard lock(impl_->mutex);return impl_->error;}
}
}
