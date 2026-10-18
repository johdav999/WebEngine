#pragma once
#include "engine/scene.hpp"
// Application-owned synthesis/mapping. No vehicle policy enters AudioSystem.
void drivingAudioCommand(engine::Scene&,int,float);
void prepareDrivingAudio(engine::Scene&);
void updateDrivingAudio(engine::Scene&,double,bool running);
void stopDrivingAudio();
void suspendDrivingAudio(bool);
void shutdownDrivingAudio();
