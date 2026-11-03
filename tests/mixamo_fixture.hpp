#pragma once
// Original synthetic FBX, no Adobe assets: two bones, no mesh, one translated hip.
inline constexpr char mixamoAnimationFbx[]=R"FBX(; FBX 7.4.0 project file
FBXHeaderExtension: { FBXHeaderVersion: 1003
 FBXVersion: 7400
}
GlobalSettings: {
 Version: 1000
 Properties70: {
  P: "UpAxis", "int", "Integer", "",1
  P: "UpAxisSign", "int", "Integer", "",1
  P: "FrontAxis", "int", "Integer", "",2
  P: "FrontAxisSign", "int", "Integer", "",-1
  P: "CoordAxis", "int", "Integer", "",0
  P: "CoordAxisSign", "int", "Integer", "",1
  P: "UnitScaleFactor", "double", "Number", "",100
 }
}
Objects: {
 NodeAttribute: 3, "NodeAttribute::Hips", "LimbNode" { TypeFlags: "Skeleton" }
 NodeAttribute: 4, "NodeAttribute::Spine", "LimbNode" { TypeFlags: "Skeleton" }
 Model: 1, "Model::mixamorig:Hips", "LimbNode" {
  Version: 232
  Properties70: { P: "Lcl Translation", "Lcl Translation", "", "A",0,1,0 }
 }
 Model: 2, "Model::mixamorig:Spine", "LimbNode" {
  Version: 232
  Properties70: { P: "Lcl Translation", "Lcl Translation", "", "A",0,1,0 }
 }
 AnimationStack: 10, "AnimStack::Walk", "" {
  Properties70: {
   P: "LocalStart", "KTime", "Time", "",0
   P: "LocalStop", "KTime", "Time", "",46186158000
  }
 }
 AnimationLayer: 11, "AnimLayer::BaseLayer", "" { }
 AnimationCurveNode: 12, "AnimCurveNode::T", "" {
  Properties70: {
   P: "d|X", "Number", "", "A",0
   P: "d|Y", "Number", "", "A",1
   P: "d|Z", "Number", "", "A",0
  }
 }
 AnimationCurve: 13, "AnimCurve::X", "" {
  Default: 0
  KeyVer: 4008
  KeyTime: *2 { a: 0,46186158000 }
  KeyValueFloat: *2 { a: 0,1 }
  KeyAttrFlags: *1 { a: 24836 }
  KeyAttrDataFloat: *4 { a: 0,0,0,0 }
  KeyAttrRefCount: *1 { a: 2 }
 }
}
Connections: {
 C: "OO",3,1
 C: "OO",4,2
 C: "OO",1,0
 C: "OO",2,1
 C: "OO",11,10
 C: "OO",12,11
 C: "OP",12,1,"Lcl Translation"
 C: "OP",13,12,"d|X"
}
)FBX";
