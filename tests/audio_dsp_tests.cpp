#include "../src/desktop/audio_dsp.hpp"
#include <iostream>
using namespace engine;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    desktop_audio::LowPass low,high;low.cutoff(800);high.cutoff(22000);double filtered=0,dry=0;
    for(int i=0;i<48000;++i){float l=.2f*std::sin(2*pi*3000*i/48000),r=l,a=l,b=r;low.process(l,r);high.process(a,b);if(i>1000){filtered+=l*l;dry+=a*a;}}
    require(filtered<dry*.01,"Native low-pass did not suppress high frequencies by 20 dB");
    desktop_audio::Room room({.7f,1.5f,.02f});double early=0,tail=0,late=0;
    for(int i=0;i<240000;++i){float l=0,r=0;room.process(i==0?1.f:0,0,l,r);require(std::isfinite(l)&&std::isfinite(r),"Room produced nonfinite samples");if(i<960)early+=l*l+r*r;if(i>4800&&i<24000)tail+=l*l+r*r;if(i>192000)late+=l*l+r*r;}
    require(early==0&&tail>.001&&late<tail*.00001,"Room predelay/decaying tail incorrect");room.clear();
    for(int i=0;i<4800;++i){float l=0,r=0;room.process(0,0,l,r);require(l==0&&r==0,"Cleared room retained tails");}
    bool rejected=false;try{desktop_audio::Room bad({1,0,0});}catch(...){rejected=true;}require(rejected,"Invalid room accepted");
    std::cout<<"Audio DSP: frequency rejection, predelay, tail decay, clear and validation passed\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
