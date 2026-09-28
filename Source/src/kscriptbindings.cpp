#include "kscriptbindings.h"
#include "kobject.h"
#include "kinputmanager.h"
#include "kaudio.h"
#include "kaudiomanager.h"
#include "kaudiosource.h"
#include "kanimator.h"
#include "kskelanimation.h"
#include "kmesh.h"
#include "kmaterial.h"
#include "kphysicsmanager.h"
#include "kphysicsobject.h"
#include "kcharactercontroller.h"

#include <new>
#include <cstdio>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace kemena
{
    // The manager whose scripts are currently being driven. getSelf() and the
    // time accessors resolve through this. Each kWorld owns its own manager and
    // sets this in its constructor (registerScriptBindings); when several worlds
    // exist (e.g. editor previews), the world about to run scripts must reclaim
    // it via setActiveScriptContext() — see kWorld::updateScripts().
    static kScriptManager *g_boundManager = nullptr;

    // -----------------------------------------------------------------------
    // kVec3 value type helpers
    // -----------------------------------------------------------------------

    static void vec3DefaultCtor(void *mem)                  { new (mem) kVec3(0.0f); }
    static void vec3InitCtor(float x, float y, float z, void *mem) { new (mem) kVec3(x, y, z); }
    static void vec3CopyCtor(const kVec3 &other, void *mem) { new (mem) kVec3(other); }

    static kVec3 vec3Add(const kVec3 &a, const kVec3 &b) { return a + b; }
    static kVec3 vec3Sub(const kVec3 &a, const kVec3 &b) { return a - b; }
    static kVec3 vec3MulF(const kVec3 &a, float s)       { return a * s; }
    static kVec3 vec3Neg(const kVec3 &a)                 { return -a; }
    static bool  vec3Equals(const kVec3 &a, const kVec3 &b) { return a == b; }
    static float vec3Length(const kVec3 &a)              { return glm::length(a); }
    static kVec3 vec3Normalized(const kVec3 &a)
    {
        float len = glm::length(a);
        return len > 0.0f ? a / len : kVec3(0.0f);
    }
    static float vec3Dot(const kVec3 &a, const kVec3 &b)   { return glm::dot(a, b); }
    static kVec3 vec3Cross(const kVec3 &a, const kVec3 &b) { return glm::cross(a, b); }

    // -----------------------------------------------------------------------
    // kObject method wrappers (object-first calling convention)
    // -----------------------------------------------------------------------

    static kString objGetName(kObject *o)                       { return o->getName(); }
    static void    objSetName(kObject *o, const kString &n)     { o->setName(n); }
    static kString objGetUuid(kObject *o)                       { return o->getUuid(); }

    static kString objGetTag(kObject *o)                        { return o->getTag(); }
    static void    objSetTag(kObject *o, const kString &t)      { o->setTag(t); }
    static bool    objCompareTag(kObject *o, const kString &t)  { return o->compareTag(t); }

    static kVec3 objGetPosition(kObject *o)                     { return o->getPosition(); }
    static void  objSetPosition(kObject *o, const kVec3 &v)     { o->setPosition(v); }
    static kVec3 objGetGlobalPosition(kObject *o)               { return o->getGlobalPosition(); }

    static kVec3 objGetScale(kObject *o)                        { return o->getScale(); }
    static void  objSetScale(kObject *o, const kVec3 &v)        { o->setScale(v); }

    // Rotation is exposed to scripts as Euler angles in degrees, which are far
    // friendlier than quaternions for gameplay code.
    static kVec3 objGetRotation(kObject *o)                     { return o->getRotationEuler(); }
    static void  objSetRotation(kObject *o, const kVec3 &deg)
    {
        o->setRotation(kQuat(glm::radians(deg)));
    }

    static kVec3 objForward(kObject *o)                         { return o->calculateForward(); }
    static kVec3 objRight(kObject *o)                           { return o->calculateRight(); }
    static kVec3 objUp(kObject *o)                              { return o->calculateUp(); }

    static void  objRotate(kObject *o, const kVec3 &axis, float spd) { o->rotate(axis, spd); }
    static void  objTranslate(kObject *o, const kVec3 &delta)
    {
        o->setPosition(o->getPosition() + delta);
    }

    static bool  objGetActive(kObject *o)                       { return o->getActive(); }
    static void  objSetActive(kObject *o, bool a)               { o->setActive(a); }
    static kObject *objGetParent(kObject *o)                    { return o->getParent(); }

    // -----------------------------------------------------------------------
    // Global functions
    // -----------------------------------------------------------------------

    static kObject *scriptGetSelf()
    {
        return g_boundManager ? g_boundManager->getActiveObject() : nullptr;
    }
    static float scriptDeltaTime()
    {
        return g_boundManager ? g_boundManager->getDeltaTime() : 0.0f;
    }
    static float scriptFixedDeltaTime()
    {
        return g_boundManager ? g_boundManager->getFixedDeltaTime() : 0.0f;
    }
    static bool scriptGetAction(const kString &action)
    {
        kInputManager *im = g_boundManager ? g_boundManager->getInputManager() : nullptr;
        return im ? im->getAction(action) : false;
    }
    static bool scriptGetActionPressed(const kString &action)
    {
        kInputManager *im = g_boundManager ? g_boundManager->getInputManager() : nullptr;
        return im ? im->getActionPressed(action) : false;
    }
    static bool scriptGetActionReleased(const kString &action)
    {
        kInputManager *im = g_boundManager ? g_boundManager->getInputManager() : nullptr;
        return im ? im->getActionReleased(action) : false;
    }
    static float scriptGetAxis(const kString &action)
    {
        kInputManager *im = g_boundManager ? g_boundManager->getInputManager() : nullptr;
        return im ? im->getAxis(action) : 0.0f;
    }
    static void scriptPrint(const kString &msg)
    {
        kScriptManager::handleScriptPrint(msg.c_str());
    }
    static bool particleGetActive(kObject *o)
    {
        auto &parts = o->getParticles();
        return !parts.empty() && parts[0].isActive;
    }
    static void particleSetActive(kObject *o, bool a)
    {
        auto &parts = o->getParticles();
        if (!parts.empty()) parts[0].isActive = a;
    }

    // -----------------------------------------------------------------------
    // Audio wrappers
    // -----------------------------------------------------------------------

    static kAudioManager *scriptAudioManager()
    {
        return g_boundManager ? g_boundManager->getAudioManager() : nullptr;
    }

    static kAudio *scriptLoadAudio(const kString &path)
    {
        kAudioManager *am = scriptAudioManager();
        return am ? am->loadAudio(path) : nullptr;
    }

    static void scriptUnloadAudio(kAudio *clip)
    {
        kAudioManager *am = scriptAudioManager();
        if (am)
            am->unloadAudio(clip);
    }

    static void scriptPlayAudio(const kString &path, bool loop, float volume, float pitch)
    {
        kAudio *clip = scriptLoadAudio(path);
        if (!clip)
            return;
        clip->setLooping(loop);
        clip->setVolume(volume);
        clip->setPitch(pitch);
        clip->play();
    }

    static void scriptStopAllAudio()
    {
        kAudioManager *am = scriptAudioManager();
        if (am)
            am->stopAll();
    }

    static void scriptSetMasterVolume(float volume)
    {
        kAudioManager *am = scriptAudioManager();
        if (am)
            am->setMasterVolume(volume);
    }

    static float scriptGetMasterVolume()
    {
        kAudioManager *am = scriptAudioManager();
        return am ? am->getMasterVolume() : 1.0f;
    }

    static void scriptSetListenerPosition(const kVec3 &pos)
    {
        kAudioManager *am = scriptAudioManager();
        if (am)
            am->setListenerPosition(pos);
    }

    static void scriptSetListenerDirection(const kVec3 &fwd, const kVec3 &up)
    {
        kAudioManager *am = scriptAudioManager();
        if (am)
            am->setListenerDirection(fwd, up);
    }

    static void scriptSetListenerVelocity(const kVec3 &vel)
    {
        kAudioManager *am = scriptAudioManager();
        if (am)
            am->setListenerVelocity(vel);
    }

    // kAudio methods.
    static void  audioPlay(kAudio *a)                       { if (a) a->play(); }
    static void  audioStop(kAudio *a)                       { if (a) a->stop(); }
    static void  audioPause(kAudio *a)                      { if (a) a->pause(); }
    static void  audioResume(kAudio *a)                     { if (a) a->resume(); }
    static void  audioSetLooping(kAudio *a, bool loop)      { if (a) a->setLooping(loop); }
    static void  audioSetVolume(kAudio *a, float v)         { if (a) a->setVolume(v); }
    static void  audioSetPitch(kAudio *a, float p)          { if (a) a->setPitch(p); }
    static void  audioSetPosition(kAudio *a, const kVec3 &v){ if (a) a->setPosition(v); }
    static void  audioSetVelocity(kAudio *a, const kVec3 &v){ if (a) a->setVelocity(v); }
    static void  audioSetSpatial(kAudio *a, bool s)         { if (a) a->setSpatialization(s); }
    static void  audioSetMinDist(kAudio *a, float d)        { if (a) a->setMinDistance(d); }
    static void  audioSetMaxDist(kAudio *a, float d)        { if (a) a->setMaxDistance(d); }
    static void  audioSetAttenuation(kAudio *a, int m)      { if (a) a->setAttenuationModel(m); }
    static void  audioSetRolloff(kAudio *a, float r)        { if (a) a->setRolloff(r); }
    static bool  audioIsPlaying(kAudio *a)                  { return a ? a->isPlaying() : false; }
    static bool  audioIsPaused(kAudio *a)                   { return a ? a->isPaused() : false; }
    static bool  audioIsLooping(kAudio *a)                  { return a ? a->isLooping() : false; }

    // -----------------------------------------------------------------------
    // Animation wrappers
    // -----------------------------------------------------------------------

    static kAnimator *scriptGetAnimator(kObject *o)
    {
        if (!o)
            return nullptr;
        if (o->getType() != NODE_TYPE_MESH)
            return nullptr;
        kMesh *mesh = static_cast<kMesh *>(o);
        return mesh ? mesh->getAnimator() : nullptr;
    }

    static void animatorSetSpeed(kAnimator *a, float speed)   { if (a) a->setSpeed(speed); }
    static float animatorGetSpeed(kAnimator *a)               { return a ? a->getSpeed() : 0.0f; }
    static void animatorSetTime(kAnimator *a, float time)     { if (a) a->setCurrentTime(time); }
    static float animatorGetTime(kAnimator *a)                { return a ? a->getCurrentTime() : 0.0f; }
    static int animatorGetClipCount(kAnimator *a)             { return a ? a->getAnimationCount() : 0; }
    static void animatorPlayIndex(kAnimator *a, float index)  { if (a) a->playAnimation((int)index); }
    static kSkeletalAnimation *animatorGetCurrent(kAnimator *a) { return a ? a->getCurrentAnimation() : nullptr; }
    static void animatorSetBool(kAnimator *a, const kString &name, bool value)    { if (a) a->setBool(name, value); }
    static void animatorSetFloat(kAnimator *a, const kString &name, float value)  { if (a) a->setFloat(name, value); }
    static void animatorSetInt(kAnimator *a, const kString &name, int value)      { if (a) a->setInt(name, value); }
    static void animatorSetIntFloat(kAnimator *a, const kString &name, float value) { if (a) a->setInt(name, (int)value); }
    static void animatorSetTrigger(kAnimator *a, const kString &name)             { if (a) a->setTrigger(name); }
    static float animatorGetFloat(kAnimator *a, const kString &name)              { return a ? a->getVariable(name) : 0.0f; }
    static bool  animatorGetBool(kAnimator *a, const kString &name)               { return a ? (a->getVariable(name) != 0.0f) : false; }
    static int   animatorGetInt(kAnimator *a, const kString &name)                { return a ? (int)a->getVariable(name) : 0; }

    static float skeletalAnimGetDuration(kSkeletalAnimation *a)     { return a ? a->getDuration() : 0.0f; }
    static float skeletalAnimGetTicksPerSecond(kSkeletalAnimation *a) { return a ? a->getTicksPerSecond() : 0.0f; }
    static float skeletalAnimGetSpeed(kSkeletalAnimation *a)        { return a ? a->getSpeed() : 0.0f; }
    static void  skeletalAnimSetSpeed(kSkeletalAnimation *a, float s) { if (a) a->setSpeed(s); }

    // Root-motion extraction: the returned deltas are accumulated since the
    // last query and are consumed (reset) by the call, so scripts should read
    // them once per frame and apply them to the GameObject / physics object.
    static kVec3 animatorRootMotionDeltaPos(kAnimator *a)
    {
        return a ? a->getRootMotionDeltaPosition() : kVec3(0.0f);
    }
    static kVec3 animatorRootMotionDeltaRot(kAnimator *a)
    {
        return a ? a->getRootMotionDeltaRotation() : kVec3(0.0f);
    }
    static bool animatorRootMotionXZ(kAnimator *a)  { return a ? a->getRootMotionPositionXZ() : false; }
    static bool animatorRootMotionY(kAnimator *a)   { return a ? a->getRootMotionPositionY() : false; }
    static bool animatorRootMotionRot(kAnimator *a) { return a ? a->getRootMotionRotation() : false; }

    // -----------------------------------------------------------------------
    // Physics wrappers
    // -----------------------------------------------------------------------

    static kPhysicsManager *scriptPhysicsManager()
    {
        return g_boundManager ? g_boundManager->getPhysicsManager() : nullptr;
    }

    static kPhysicsObject *objGetPhysicsObject(kObject *o)
    {
        return o ? o->getPhysicsObject() : nullptr;
    }

    static kCharacterController *objGetCharacterController(kObject *o)
    {
        return o ? o->getCharacterController() : nullptr;
    }

    static void physicsSetGravity(const kVec3 &gravity)
    {
        kPhysicsManager *pm = scriptPhysicsManager();
        if (pm)
            pm->setGravity(gravity);
    }

    static kVec3 physicsGetGravity()
    {
        kPhysicsManager *pm = scriptPhysicsManager();
        return pm ? pm->getGravity() : kVec3(0.0f, -9.81f, 0.0f);
    }

    // kPhysicsObject methods.
    static void  physSetPosition(kPhysicsObject *p, const kVec3 &v)  { if (p) p->setPosition(v); }
    static kVec3 physGetPosition(kPhysicsObject *p)                  { return p ? p->getPosition() : kVec3(0.0f); }
    static void  physSetLinearVelocity(kPhysicsObject *p, const kVec3 &v) { if (p) p->setLinearVelocity(v); }
    static void  physSetAngularVelocity(kPhysicsObject *p, const kVec3 &v){ if (p) p->setAngularVelocity(v); }
    static kVec3 physGetLinearVelocity(kPhysicsObject *p)            { return p ? p->getLinearVelocity() : kVec3(0.0f); }
    static kVec3 physGetAngularVelocity(kPhysicsObject *p)           { return p ? p->getAngularVelocity() : kVec3(0.0f); }
    static void  physApplyForce(kPhysicsObject *p, const kVec3 &v)   { if (p) p->applyForce(v); }
    static void  physApplyImpulse(kPhysicsObject *p, const kVec3 &v) { if (p) p->applyImpulse(v); }
    static void  physApplyTorque(kPhysicsObject *p, const kVec3 &v)  { if (p) p->applyTorque(v); }
    static void  physSetMass(kPhysicsObject *p, float m)             { if (p) p->setMass(m); }
    static void  physSetFriction(kPhysicsObject *p, float f)         { if (p) p->setFriction(f); }
    static void  physSetRestitution(kPhysicsObject *p, float r)      { if (p) p->setRestitution(r); }
    static void  physSetLinearDamping(kPhysicsObject *p, float d)    { if (p) p->setLinearDamping(d); }
    static void  physSetAngularDamping(kPhysicsObject *p, float d)   { if (p) p->setAngularDamping(d); }
    static void  physSetGravityFactor(kPhysicsObject *p, float g)    { if (p) p->setGravityFactor(g); }
    static bool  physIsActive(kPhysicsObject *p)                     { return p ? p->isActive() : false; }
    static int   physGetObjectType(kPhysicsObject *p)                { return p ? (int)p->getObjectType() : 0; }
    static int   physGetShapeType(kPhysicsObject *p)                 { return p ? (int)p->getShapeType() : 0; }

    // kCharacterController methods.
    static void  ccSetLinearVelocity(kCharacterController *c, const kVec3 &v) { if (c) c->setLinearVelocity(v); }
    static void  ccMove(kCharacterController *c, const kVec3 &d)             { if (c) c->move(d); }

    // -----------------------------------------------------------------------
    // Registration
    // -----------------------------------------------------------------------

    void setActiveScriptContext(kScriptManager *manager)
    {
        // Lightweight: only repoints the host-API context. No engine work.
        g_boundManager = manager;
    }

    void registerScriptBindings(kScriptManager *manager)
    {
        if (!manager)
            return;
        g_boundManager = manager;

        asIScriptEngine *e = manager->getEngine();
        if (!e)
            return;

        int r = 0;
        (void)r;

        // --- kVec3 -----------------------------------------------------------
        // asGetTypeTraits<>() supplies the required asOBJ_APP_CLASS kind bit plus
        // the correct constructor/copy traits for glm::vec3; ALLFLOATS is the
        // register-passing hint. (asOBJ_APP_CLASS_ALLFLOATS alone is rejected
        // with asINVALID_ARG because the app-class kind bit is missing.)
        r = e->RegisterObjectType("kVec3", sizeof(kVec3),
                                  asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS |
                                  asGetTypeTraits<kVec3>());
        assert(r >= 0);

        r = e->RegisterObjectBehaviour("kVec3", asBEHAVE_CONSTRUCT, "void f()",
                                       asFUNCTION(vec3DefaultCtor), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = e->RegisterObjectBehaviour("kVec3", asBEHAVE_CONSTRUCT, "void f(float, float, float)",
                                       asFUNCTION(vec3InitCtor), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = e->RegisterObjectBehaviour("kVec3", asBEHAVE_CONSTRUCT, "void f(const kVec3 &in)",
                                       asFUNCTION(vec3CopyCtor), asCALL_CDECL_OBJLAST);
        assert(r >= 0);

        r = e->RegisterObjectProperty("kVec3", "float x", asOFFSET(kVec3, x)); assert(r >= 0);
        r = e->RegisterObjectProperty("kVec3", "float y", asOFFSET(kVec3, y)); assert(r >= 0);
        r = e->RegisterObjectProperty("kVec3", "float z", asOFFSET(kVec3, z)); assert(r >= 0);

        r = e->RegisterObjectMethod("kVec3", "kVec3 opAdd(const kVec3 &in) const",
                                    asFUNCTION(vec3Add), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "kVec3 opSub(const kVec3 &in) const",
                                    asFUNCTION(vec3Sub), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "kVec3 opMul(float) const",
                                    asFUNCTION(vec3MulF), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "kVec3 opNeg() const",
                                    asFUNCTION(vec3Neg), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "bool opEquals(const kVec3 &in) const",
                                    asFUNCTION(vec3Equals), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "float length() const",
                                    asFUNCTION(vec3Length), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "kVec3 normalized() const",
                                    asFUNCTION(vec3Normalized), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "float dot(const kVec3 &in) const",
                                    asFUNCTION(vec3Dot), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kVec3", "kVec3 cross(const kVec3 &in) const",
                                    asFUNCTION(vec3Cross), asCALL_CDECL_OBJFIRST); assert(r >= 0);



        // --- kObject ---------------------------------------------------------
        // Engine-owned: no reference counting — AngelScript only holds handles.
        r = e->RegisterObjectType("kObject", 0, asOBJ_REF | asOBJ_NOCOUNT);
        assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "string getName() const",
                                    asFUNCTION(objGetName), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "void setName(const string &in)",
                                    asFUNCTION(objSetName), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "string getUuid() const",
                                    asFUNCTION(objGetUuid), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "string getTag() const",
                                    asFUNCTION(objGetTag), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "void setTag(const string &in)",
                                    asFUNCTION(objSetTag), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "bool compareTag(const string &in)",
                                    asFUNCTION(objCompareTag), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "kVec3 getPosition() const",
                                    asFUNCTION(objGetPosition), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "void setPosition(const kVec3 &in)",
                                    asFUNCTION(objSetPosition), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "kVec3 getGlobalPosition() const",
                                    asFUNCTION(objGetGlobalPosition), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "kVec3 getScale() const",
                                    asFUNCTION(objGetScale), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "void setScale(const kVec3 &in)",
                                    asFUNCTION(objSetScale), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "kVec3 getRotation() const",
                                    asFUNCTION(objGetRotation), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "void setRotation(const kVec3 &in)",
                                    asFUNCTION(objSetRotation), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "kVec3 forward() const",
                                    asFUNCTION(objForward), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "kVec3 right() const",
                                    asFUNCTION(objRight), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "kVec3 up() const",
                                    asFUNCTION(objUp), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "void rotate(const kVec3 &in, float)",
                                    asFUNCTION(objRotate), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "void translate(const kVec3 &in)",
                                    asFUNCTION(objTranslate), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        r = e->RegisterObjectMethod("kObject", "bool getActive() const",
                                    asFUNCTION(objGetActive), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "void setActive(bool)",
                                    asFUNCTION(objSetActive), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "kObject@ getParent() const",
                                    asFUNCTION(objGetParent), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        // --- Particle system access on kObject --------------------------------
        // Expose the first active particle system's isActive toggle so scripts
        // can turn their own particles on/off at runtime.
        r = e->RegisterObjectMethod("kObject", "bool getParticleActive() const",
                                    asFUNCTION(particleGetActive), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "void setParticleActive(bool)",
                                    asFUNCTION(particleSetActive), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        // Register the physics-body type before the kObject method that returns it.
        r = e->RegisterObjectType("kPhysicsObject", 0, asOBJ_REF | asOBJ_NOCOUNT);
        assert(r >= 0);

        // Character-controller type (manager-owned; scripts hold handles).
        r = e->RegisterObjectType("kCharacterController", 0, asOBJ_REF | asOBJ_NOCOUNT);
        assert(r >= 0);

        // --- kObject physics access -------------------------------------------
        r = e->RegisterObjectMethod("kObject", "kPhysicsObject@ getPhysicsObject() const",
                                    asFUNCTION(objGetPhysicsObject), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kObject", "kCharacterController@ getCharacterController() const",
                                    asFUNCTION(objGetCharacterController), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        // --- kAudio -----------------------------------------------------------
        // Manager-owned clips: no reference counting; scripts hold handles.
        r = e->RegisterObjectType("kAudio", 0, asOBJ_REF | asOBJ_NOCOUNT);
        assert(r >= 0);

        r = e->RegisterObjectMethod("kAudio", "void play()",
                                    asFUNCTION(audioPlay), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "void stop()",
                                    asFUNCTION(audioStop), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "void pause()",
                                    asFUNCTION(audioPause), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "void resume()",
                                    asFUNCTION(audioResume), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "void setLooping(bool)",
                                    asFUNCTION(audioSetLooping), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "void setVolume(float)",
                                    asFUNCTION(audioSetVolume), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "void setPitch(float)",
                                    asFUNCTION(audioSetPitch), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "void setPosition(const kVec3 &in)",
                                    asFUNCTION(audioSetPosition), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "void setVelocity(const kVec3 &in)",
                                    asFUNCTION(audioSetVelocity), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "void setSpatialization(bool)",
                                    asFUNCTION(audioSetSpatial), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "void setMinDistance(float)",
                                    asFUNCTION(audioSetMinDist), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "void setMaxDistance(float)",
                                    asFUNCTION(audioSetMaxDist), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "void setAttenuationModel(int)",
                                    asFUNCTION(audioSetAttenuation), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "void setRolloff(float)",
                                    asFUNCTION(audioSetRolloff), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "bool isPlaying() const",
                                    asFUNCTION(audioIsPlaying), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "bool isPaused() const",
                                    asFUNCTION(audioIsPaused), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAudio", "bool isLooping() const",
                                    asFUNCTION(audioIsLooping), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        // --- kAnimator / kSkeletalAnimation -----------------------------------
        r = e->RegisterObjectType("kAnimator", 0, asOBJ_REF | asOBJ_NOCOUNT);
        assert(r >= 0);
        r = e->RegisterObjectType("kSkeletalAnimation", 0, asOBJ_REF | asOBJ_NOCOUNT);
        assert(r >= 0);

        // --- Editor-asset handle types (opaque to scripts) --------------------
        // Registered so logic-graph variables of these types can be declared and
        // cast to/from generic object handles in generated AngelScript.
        r = e->RegisterObjectType("kAudioSource", 0, asOBJ_REF | asOBJ_NOCOUNT);
        assert(r >= 0);
        r = e->RegisterObjectType("kMaterial", 0, asOBJ_REF | asOBJ_NOCOUNT);
        assert(r >= 0);

        r = e->RegisterObjectMethod("kAnimator", "void setSpeed(float)",
                                    asFUNCTION(animatorSetSpeed), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "float getSpeed() const",
                                    asFUNCTION(animatorGetSpeed), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "void setCurrentTime(float)",
                                    asFUNCTION(animatorSetTime), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "float getCurrentTime() const",
                                    asFUNCTION(animatorGetTime), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "int getAnimationCount() const",
                                    asFUNCTION(animatorGetClipCount), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "void playAnimation(float index)",
                                    asFUNCTION(animatorPlayIndex), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "kSkeletalAnimation@ getCurrentAnimation() const",
                                    asFUNCTION(animatorGetCurrent), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "void setBool(const string &in, bool)",
                                    asFUNCTION(animatorSetBool), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "void setFloat(const string &in, float)",
                                    asFUNCTION(animatorSetFloat), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "void setInt(const string &in, int)",
                                    asFUNCTION(animatorSetInt), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "void setInt(const string &in, float)",
                                    asFUNCTION(animatorSetIntFloat), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "void setTrigger(const string &in)",
                                    asFUNCTION(animatorSetTrigger), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "float getFloat(const string &in)",
                                    asFUNCTION(animatorGetFloat), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "bool getBool(const string &in)",
                                    asFUNCTION(animatorGetBool), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "int getInt(const string &in)",
                                    asFUNCTION(animatorGetInt), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        r = e->RegisterObjectMethod("kSkeletalAnimation", "float getDuration() const",
                                    asFUNCTION(skeletalAnimGetDuration), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kSkeletalAnimation", "float getTicksPerSecond() const",
                                    asFUNCTION(skeletalAnimGetTicksPerSecond), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kSkeletalAnimation", "float getSpeed() const",
                                    asFUNCTION(skeletalAnimGetSpeed), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kSkeletalAnimation", "void setSpeed(float)",
                                    asFUNCTION(skeletalAnimSetSpeed), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        // Root-motion extraction deltas (kVec3, consumed by the call). Rotation
        // is returned as euler degrees (XYZ) to match kObject.getRotation().
        r = e->RegisterObjectMethod("kAnimator", "kVec3 getRootMotionDeltaPosition()",
                                    asFUNCTION(animatorRootMotionDeltaPos), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "kVec3 getRootMotionDeltaRotation()",
                                    asFUNCTION(animatorRootMotionDeltaRot), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "bool getRootMotionPositionXZ() const",
                                    asFUNCTION(animatorRootMotionXZ), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "bool getRootMotionPositionY() const",
                                    asFUNCTION(animatorRootMotionY), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kAnimator", "bool getRootMotionRotation() const",
                                    asFUNCTION(animatorRootMotionRot), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        // --- kPhysicsObject ---------------------------------------------------
        r = e->RegisterObjectMethod("kPhysicsObject", "void setPosition(const kVec3 &in)",
                                    asFUNCTION(physSetPosition), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "kVec3 getPosition() const",
                                    asFUNCTION(physGetPosition), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "void setLinearVelocity(const kVec3 &in)",
                                    asFUNCTION(physSetLinearVelocity), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "void setAngularVelocity(const kVec3 &in)",
                                    asFUNCTION(physSetAngularVelocity), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "kVec3 getLinearVelocity() const",
                                    asFUNCTION(physGetLinearVelocity), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "kVec3 getAngularVelocity() const",
                                    asFUNCTION(physGetAngularVelocity), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "void applyForce(const kVec3 &in)",
                                    asFUNCTION(physApplyForce), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "void applyImpulse(const kVec3 &in)",
                                    asFUNCTION(physApplyImpulse), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "void applyTorque(const kVec3 &in)",
                                    asFUNCTION(physApplyTorque), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "void setMass(float)",
                                    asFUNCTION(physSetMass), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "void setFriction(float)",
                                    asFUNCTION(physSetFriction), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "void setRestitution(float)",
                                    asFUNCTION(physSetRestitution), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "void setLinearDamping(float)",
                                    asFUNCTION(physSetLinearDamping), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "void setAngularDamping(float)",
                                    asFUNCTION(physSetAngularDamping), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "void setGravityFactor(float)",
                                    asFUNCTION(physSetGravityFactor), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "bool isActive() const",
                                    asFUNCTION(physIsActive), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "int getObjectType() const",
                                    asFUNCTION(physGetObjectType), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kPhysicsObject", "int getShapeType() const",
                                    asFUNCTION(physGetShapeType), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        // --- kCharacterController ---------------------------------------------
        r = e->RegisterObjectMethod("kCharacterController", "void setLinearVelocity(const kVec3 &in)",
                                    asFUNCTION(ccSetLinearVelocity), asCALL_CDECL_OBJFIRST); assert(r >= 0);
        r = e->RegisterObjectMethod("kCharacterController", "void move(const kVec3 &in)",
                                    asFUNCTION(ccMove), asCALL_CDECL_OBJFIRST); assert(r >= 0);

        // --- Global functions ------------------------------------------------
        r = e->RegisterGlobalFunction("kObject@ getSelf()",
                                      asFUNCTION(scriptGetSelf), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("float getDeltaTime()",
                                      asFUNCTION(scriptDeltaTime), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("float getFixedDeltaTime()",
                                      asFUNCTION(scriptFixedDeltaTime), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("void print(const string &in)",
                                      asFUNCTION(scriptPrint), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("void log(const string &in)",
                                      asFUNCTION(scriptPrint), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("void printConsole(const string &in)",
                                      asFUNCTION(scriptPrint), asCALL_CDECL); assert(r >= 0);

        // --- Named input ------------------------------------------------------
        r = e->RegisterGlobalFunction("bool getAction(const string &in)",
                                      asFUNCTION(scriptGetAction), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("bool getActionPressed(const string &in)",
                                      asFUNCTION(scriptGetActionPressed), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("bool getActionReleased(const string &in)",
                                      asFUNCTION(scriptGetActionReleased), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("float getAxis(const string &in)",
                                      asFUNCTION(scriptGetAxis), asCALL_CDECL); assert(r >= 0);

        // --- Audio ------------------------------------------------------------
        r = e->RegisterGlobalFunction("kAudio@ loadAudio(const string &in)",
                                      asFUNCTION(scriptLoadAudio), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("void unloadAudio(kAudio@)",
                                      asFUNCTION(scriptUnloadAudio), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("void playAudio(const string &in, bool, float, float)",
                                      asFUNCTION(scriptPlayAudio), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("void stopAllAudio()",
                                      asFUNCTION(scriptStopAllAudio), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("void setMasterVolume(float)",
                                      asFUNCTION(scriptSetMasterVolume), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("float getMasterVolume()",
                                      asFUNCTION(scriptGetMasterVolume), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("void setListenerPosition(const kVec3 &in)",
                                      asFUNCTION(scriptSetListenerPosition), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("void setListenerDirection(const kVec3 &in, const kVec3 &in)",
                                      asFUNCTION(scriptSetListenerDirection), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("void setListenerVelocity(const kVec3 &in)",
                                      asFUNCTION(scriptSetListenerVelocity), asCALL_CDECL); assert(r >= 0);

        // --- Animation --------------------------------------------------------
        r = e->RegisterGlobalFunction("kAnimator@ getAnimator(kObject@)",
                                      asFUNCTION(scriptGetAnimator), asCALL_CDECL); assert(r >= 0);

        // --- Physics ----------------------------------------------------------
        r = e->RegisterGlobalFunction("void setPhysicsGravity(const kVec3 &in)",
                                      asFUNCTION(physicsSetGravity), asCALL_CDECL); assert(r >= 0);
        r = e->RegisterGlobalFunction("kVec3 getPhysicsGravity()",
                                      asFUNCTION(physicsGetGravity), asCALL_CDECL); assert(r >= 0);
    }
}
