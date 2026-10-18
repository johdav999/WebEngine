// Private implementation, included inside namespace engine after PhysicsWorld::Impl.
void PhysicsWorld::removeVehicle(ComponentId id) noexcept {
    for(std::size_t i=0;i<impl_->vehicles.size();)if(impl_->vehicles[i].component==id)impl_->removeVehicle(i);else ++i;
}
void PhysicsWorld::synchronizeVehicles() {
    auto& p=*impl_;
    for(std::size_t i=0;i<p.vehicles.size();) {
        const auto& e=p.vehicles[i];auto* c=dynamic_cast<VehicleComponent*>(p.scene.findComponent(e.component));
        if(!c||!c->isActiveInHierarchy()||c->definition()!=e.definition||!p.jointBodyLive(e.body)){p.removeVehicle(i);continue;}++i;
    }
    for(const auto& slot:p.scene.components_) {
        auto* c=dynamic_cast<VehicleComponent*>(slot.component);if(!c||!c->isInitialized()||!c->isActiveInHierarchy())continue;
        c->validate();auto* actor=c->owner();
        if(!actor||actor->parent()!=&p.scene||c->parent()!=actor)throw std::invalid_argument("Vehicle requires a scene-root chassis actor");
        std::size_t count=0;for(const auto& child:actor->children())if(auto* v=dynamic_cast<VehicleComponent*>(child.get());v&&v->isActiveInHierarchy())++count;
        if(count!=1)throw std::invalid_argument("One active vehicle per actor");
        auto* body=actor->getComponent<RigidBodyComponent>();
        if(!body)continue; // Missing/removed chassis suspends this component until a valid body returns.
        if(body->settings().motion!=BodyMotion::Dynamic||body->settings().isSensor)throw std::invalid_argument("Vehicle chassis must be dynamic and solid");
        if(!body->hasBody())continue;
        if(std::any_of(p.vehicles.begin(),p.vehicles.end(),[&](const auto& e){return e.component==c->id();}))continue;
        for(const auto& id:c->visuals_)if(!id.empty()){auto* n=p.scene.findSavedNode(id);if(!n||n->parent()!=actor||dynamic_cast<RigidBodyComponent*>(n)||dynamic_cast<ColliderComponent*>(n)||dynamic_cast<VehicleComponent*>(n))throw std::invalid_argument("Wheel visual must be a direct nonphysics child of its chassis");}
        auto& chassis=p.require(body->id());const auto& d=*c->definition_;
        JPH::VehicleConstraintSettings settings;
        for(const auto& w:d.wheels) {
            auto* wheel=new JPH::WheelSettingsWV();settings.mWheels.push_back(wheel);
            wheel->mPosition=jvec(w.position);wheel->mRadius=w.radius;wheel->mWidth=w.width;
            wheel->mSuspensionMinLength=w.suspensionMin;wheel->mSuspensionMaxLength=w.suspensionMax;
            wheel->mSuspensionSpring.mFrequency=w.suspensionFrequency;wheel->mSuspensionSpring.mDamping=w.suspensionDamping;
            wheel->mMaxSteerAngle=w.maxSteerAngle;wheel->mMaxBrakeTorque=w.brakeTorque;wheel->mMaxHandBrakeTorque=w.handBrakeTorque;
            wheel->mLongitudinalFriction.Clear();wheel->mLongitudinalFriction.AddPoint(0,0);wheel->mLongitudinalFriction.AddPoint(.1f,w.friction);wheel->mLongitudinalFriction.AddPoint(1,w.friction);
            wheel->mLateralFriction.Clear();wheel->mLateralFriction.AddPoint(0,0);wheel->mLateralFriction.AddPoint(3,w.friction);wheel->mLateralFriction.AddPoint(90,w.friction);
        }
        auto* controller=new JPH::WheeledVehicleControllerSettings();settings.mController=controller;
        controller->mEngine.mMaxTorque=d.engineTorque;controller->mEngine.mMinRPM=d.minRPM;controller->mEngine.mMaxRPM=d.maxRPM;
        controller->mTransmission.mGearRatios.assign(d.forwardRatios.begin(),d.forwardRatios.end());controller->mTransmission.mReverseGearRatios={d.reverseRatio};
        controller->mTransmission.mShiftUpRPM=d.shiftUpRPM;controller->mTransmission.mShiftDownRPM=d.shiftDownRPM;
        for(int axle=0;axle<2;++axle){JPH::VehicleDifferentialSettings diff;diff.mLeftWheel=axle*2;diff.mRightWheel=axle*2+1;diff.mDifferentialRatio=d.differentialRatio;diff.mEngineTorqueRatio=axle?1-d.frontTorqueFraction:d.frontTorqueFraction;controller->mDifferentials.push_back(diff);
            JPH::VehicleAntiRollBar bar;bar.mLeftWheel=axle*2;bar.mRightWheel=axle*2+1;bar.mStiffness=d.antiRollStiffness;settings.mAntiRollBars.push_back(bar);}
        Impl::VehicleEntry e;e.component=c->id();e.body=body->id();e.definition=c->definition_;
        {JPH::BodyLockWrite lock(p.system.GetBodyLockInterface(),chassis.body);if(!lock.Succeeded())throw std::runtime_error("Vehicle body lock failed");e.constraint=new JPH::VehicleConstraint(lock.GetBody(),settings);}
        e.tester=new JPH::VehicleCollisionTesterCastCylinder(JPH::ObjectLayer(chassis.settings.collisionLayer+16));e.constraint->SetVehicleCollisionTester(e.tester);
        e.constraint->SetCombineFriction([world=&p](JPH::uint,float& longitudinal,float& lateral,const JPH::Body& target,const JPH::SubShapeID& sub){
            auto* entry=world->byBody(target.GetID());auto hit=entry?world->surfaceHit(*entry,*target.GetShape(),sub):PhysicsSurfaceHit{};
            const auto friction=target.GetFriction()*(hit.surface?hit.surface->friction:1.f),grip=hit.surface?hit.surface->tyreGrip:1.f;
            longitudinal=std::sqrt(longitudinal*friction)*grip;lateral=std::sqrt(lateral*friction)*grip;
        });
        // Reserve before publishing registrations; no throwing engine allocations after this point.
        p.vehicles.reserve(p.vehicles.size()+1);
        p.system.AddConstraint(e.constraint);p.system.AddStepListener(e.constraint);
        for(int i=0;i<4;++i){const auto matrix=e.constraint->GetWheelLocalTransform(i,JPH::Vec3::sAxisX(),JPH::Vec3::sAxisY());Mat4 m;for(int a=0;a<16;++a)m.m[a]=matrix(a%4,a/4);e.previous[i]=e.current[i]=poseOf(m);}
        p.vehicles.push_back(std::move(e));
    }
}
void PhysicsWorld::stepVehicles(bool after,double seconds) {
    auto& p=*impl_;auto& bodies=p.system.GetBodyInterface();
    for(auto& e:p.vehicles) {
        auto* c=dynamic_cast<VehicleComponent*>(p.scene.findComponent(e.component));if(!c||!c->isActiveInHierarchy())continue;
        auto& controller=static_cast<JPH::WheeledVehicleController&>(*e.constraint->GetController());
        if(!after) {
            if(c->pending_){const auto checkpoint=*c->pending_;controller.GetEngine().SetCurrentRPM(checkpoint.rpm);controller.GetTransmission().Set(checkpoint.gear,1);for(int i=0;i<4;++i){auto* w=e.constraint->GetWheel(i);w->SetRotationAngle(checkpoint.rotation[i]);w->SetAngularVelocity(checkpoint.angularVelocity[i]);}c->pending_.reset();}
            const auto command=c->command_;controller.SetDriverInput(command.throttle,command.steering,command.brake,command.handbrake);
            if(command.throttle||command.steering||command.brake||command.handbrake)bodies.ActivateBody(p.require(e.body).body);
            float rolling=0;Vec3 normal{};for(const auto& wheel:c->state_.wheels)if(wheel.contact){rolling+=(wheel.surface.surface?wheel.surface.surface->rollingResistance:0.f)*.25f;normal=normal+wheel.contactNormal;}
            if(rolling>0&&seconds>0){const auto& body=p.require(e.body);auto velocity=vec(bodies.GetLinearVelocity(body.body));auto n=normalized(normal);auto tangent=velocity-n*dot(velocity,n);const auto speed=std::sqrt(dot(tangent,tangent));
                if(speed>1e-5f){auto g=vec(p.system.GetGravity());auto deceleration=std::min(float(speed/seconds),rolling*std::sqrt(dot(g,g)));bodies.AddForce(body.body,jvec(tangent*(-body.settings.mass*deceleration/speed)));}}
            e.previous=e.current;continue;
        }
        VehicleState state;state.active=true;state.rpm=controller.GetEngine().GetCurrentRPM();state.gear=controller.GetTransmission().GetCurrentGear();
        const auto& chassis=p.require(e.body);state.speed=dot(vec(bodies.GetLinearVelocity(chassis.body)),transformDirection(chassis.current.matrix(),{0,0,1}));
        for(int i=0;i<4;++i){auto* w=static_cast<JPH::WheelWV*>(e.constraint->GetWheel(i));auto& out=state.wheels[i];out.contact=w->HasContact();if(out.contact){out.contactPoint=vec(w->GetContactPosition());out.contactNormal=vec(w->GetContactNormal());if(auto* hit=p.byBody(w->GetContactBodyID())){out.contactActor=hit->actor;out.surface=p.surfaceHit(*hit,*bodies.GetShape(hit->body),w->GetContactSubShapeID());}}
            out.suspensionLength=w->GetSuspensionLength();out.steerAngle=w->GetSteerAngle();out.rotation=w->GetRotationAngle();out.angularVelocity=w->GetAngularVelocity();out.combinedLongitudinalFriction=w->mCombinedLongitudinalFriction;out.combinedLateralFriction=w->mCombinedLateralFriction;out.longitudinalSlip=w->mLongitudinalSlip;out.lateralSlip=w->mLateralSlip;
            auto matrix=e.constraint->GetWheelLocalTransform(i,JPH::Vec3::sAxisX(),JPH::Vec3::sAxisY());for(int a=0;a<16;++a)out.localPose.m[a]=matrix(a%4,a/4);e.current[i]=poseOf(out.localPose);
        }
        c->state_=state;
    }
}
void PhysicsWorld::bodyPointCommand(ComponentId id,Vec3 value,Vec3 point,int command) {
    checkVector(value);checkVector(point);const auto& e=impl_->require(id);if(e.settings.motion!=BodyMotion::Dynamic)throw std::logic_error("Point force/impulse requires dynamic body");
    auto& bodies=impl_->system.GetBodyInterface();if(command==0)bodies.AddForce(e.body,jvec(value),jvec(point));else bodies.AddImpulse(e.body,jvec(value),jvec(point));bodies.ActivateBody(e.body);
}
Vec3 PhysicsWorld::pointVelocity(ComponentId id,Vec3 point) const {checkVector(point);return vec(impl_->system.GetBodyInterface().GetPointVelocity(impl_->require(id).body,jvec(point)));}
