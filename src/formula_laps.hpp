#pragma once
#include <cmath>
#include <algorithm>
namespace engine {
// Application race rules. Ordered gates and forward seam crossing prevent lap farming.
struct FormulaLaps {
    int lap=1,completed=0,nextGate=1;
    double seconds=0,last=0,best=0;
    float previous=0;
    bool started=false,valid=true,offTrack=false,wrongWay=false;
    void reset(float distance=0) { nextGate=1;seconds=0;previous=distance;started=false;valid=true;offTrack=false;wrongWay=false; }
    void update(float distance,float lateral,float speed,double dt,float length) {
        if(dt<=0)return;
        offTrack=lateral>9;wrongWay=speed < -2;
        if(!started&&std::abs(speed)>1)started=true;
        if(started)seconds+=dt;
        float delta=distance-previous;
        if(delta < -length*.5f)delta+=length;
        if(delta > length*.5f)delta-=length;
        if(std::abs(delta)>std::max(12.f,float(dt)*110.f)||offTrack)valid=false;
        if(delta>0&&started&&valid) {
            if(nextGate<12&&previous<length*nextGate/12&&distance>=length*nextGate/12)++nextGate;
            if(previous>length*.9f&&distance<length*.1f&&nextGate==12) {
                last=seconds;if(best==0||last<best)best=last;
                ++completed;++lap;seconds=0;nextGate=1;
            }
        }
        previous=distance;
    }
};
}
