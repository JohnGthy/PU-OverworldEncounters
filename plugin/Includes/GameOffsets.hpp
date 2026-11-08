// Version-specific addresses for Pokemon Ultra Sun / Ultra Moon.
//
// HOW THESE WERE FOUND (see docs/RE_NOTES.md for the full method):
//  * code.bin ("static") functions: exported by name in romfs:/static.crs
//    (4916 mangled C++ symbols).  Unnamed static functions were located by
//    disassembly next to the named ones, then matched between versions with
//    a masked byte pattern (tools/pmatch.py).
//  * FieldRo.cro functions: offsets are relative to the start of the CRO file
//    image in memory.  FieldRo is loaded at a runtime address; we recover it
//    from a code.bin import stub that RO patches with an absolute FieldRo
//    address (see FieldRoBase()).
#pragma once
#include <3ds/types.h>

namespace OWE {

struct Offsets {
    const char *name;
    // ---- code.bin (absolute addresses) ----
    u32 GameManager_GetInstance;     // gfl2::base::SingletonAccessor<GameSys::GameManager>::GetInstance()
    u32 Heap_CreateHeap;             // gfl2::heap::Manager::CreateHeap(parent, id, size, type, bool, name)
    u32 Heap_DeleteHeap;             // gfl2::heap::Manager::DeleteHeap(heap)
    u32 Heap_CreateFromBuffer;       // unnamed: create a gfl2 heap inside a raw buffer (buf, id, size, type, 0, 0)
    u32 Heap_GetHeapByHeapId;        // gfl2::heap::Manager::GetHeapByHeapId(int)
    u32 BaseModel_GetPosition;       // poke_3d::model::BaseModel::GetPosition() const -> Vector3*
    u32 BaseModel_SetPositionF;      // BaseModel::SetPosition(float,float,float)
    u32 BaseModel_SetRotationF;      // BaseModel::SetRotation(float,float,float,order)
    u32 BaseModel_SetScaleF;         // BaseModel::SetScale(float,float,float)
    u32 BaseModel_SetVisible;        // BaseModel::SetVisible(int)
    u32 PokeModel_ChangeAnimation;   // PokeTool::PokeModel::ChangeAnimation(MODEL_ANIME, bool)
    u32 GetAlolaTimeZone;            // Field::EvTime::GetAlolaTimeZone(GameData const*)
    u32 MyItem_GetItemNum;           // Savedata::MyItem::GetItemNum(u16) const
    u32 PokeSet_SetupMons;           // unnamed: fills one PokeSet entry (species/level/form) from a slot
    u32 InitialSpec_Normalize;       // unnamed pml: resolves random PID / shiny mode of an InitialSpec
    u32 nngxGetPhysicalAddr;         // nngxGetPhysicalAddr(va) -> pa (0 if outside the device block)
    u32 Core_IsRare;                 // pml::pokepara::CoreParam::IsRare() const
    u32 Core_BeginAccess;            // unnamed: core data decode before field access (used by SetID)
    u32 Core_GetBlockA;              // unnamed: (coreData, writable) -> block with +4 ID, +0x10 PID
    u32 Core_EndAccess;              // unnamed: re-encode / checksum after write
    u32 RoManager_DisposeModule;     // gfl2::ro::RoManager::DisposeModule(nn::ro::Module*)
    u32 BinLinker_GetDataMax;        // gfl2::fs::BinLinkerAccessor::GetDataMax() const
    u32 BinLinker_GetData;           // gfl2::fs::BinLinkerAccessor::GetData(u32) const
    u32 Attribute_IsGrass;           // Field::Attribute::IsGrass(u32 attr)
    u32 Static_FieldRoStubWord;      // code.bin word patched by RO with FieldRo+FR_StubTarget
    // ---- FieldRo.cro (offsets from CRO image start) ----
    u32 FR_StubTarget;               // FieldRo offset that Static_FieldRoStubWord points at
    u32 FR_EncountCheck;             // Field encounter step check (this, force, encDataIdx, r3) -> bool
    u32 FR_EventCheckFrame;          // per-frame field event check (this)
    u32 FR_Raycast;                  // collision raycast (root, Vector4* from, Vector4* to, RayOut*) -> bool
    u32 FR_TMF_Initialize;           // TrialModelFactory: init system (fac, heap, heap, 1, 1)
    u32 FR_TMF_IsInitialized;        // (fac) -> bool
    u32 FR_TMF_CreateHeapPool;       // (fac, heap, 0, 0, 1)
    u32 FR_TMF_CreateModel;          // (fac, heap, SimpleParam*, SetupOption*) -> FieldTrialModel*
    u32 FR_TMF_DeleteModel;          // (fac, model)
    u32 FR_TMF_TermRequest;          // (fac)  start terminating the model system
    u32 FR_TMF_IsTermDone;           // (fac) -> bool
    u32 FR_TMF_TermFinal;            // (fac)
    u32 PokeModel_CanDelete;         // PokeTool::PokeModel::CanDelete() (code.bin)
    u32 PokeModelSystem_CreatePokeModelHeap; // PokeTool::PokeModelSystem::CreatePokeModelHeap(heap, HeapOption*)
    u32 FR_TM_IsLoaded;              // FieldTrialModel: (model) -> bool
    u32 FR_TM_Setup;                 // FieldTrialModel: (model)   create render instance
    u32 FR_TM_SetAmbient;            // FieldTrialModel: (model, int)
    u32 FR_TM_SetVisible;            // FieldTrialModel display on/off (model, bool)
    u32 PokeModel_IsAvailableAnimation; // PokeTool::PokeModel::IsAvailableAnimation(MODEL_ANIME) (code.bin)
    u32 FR_TMF_Destructor;           // FieldRo: TrialModelFactory destructor (asserts if still running)
    u32 Sound_PlayVoice;             // Sound::PlayVoice(u8 channel, MonsNo, u8 form, VoiceType, int, u32)
    u32 Sound_ChangeVoiceVolume;     // Sound::ChangeVoiceVolume(u8 channel, float, u32 frames)
    u32 Sound_VoiceTable;            // voice player table; +4 = channel count (checked by Sound::*Voice*)
    u32 PokeModel_GetMotionCollision; // PokeTool::PokeModel::GetMotionCollision(AABB*) (current motion)
    u32 Sound_ChangeVoicePitch;      // Sound::ChangeVoicePitch(u8 channel, float)
    u32 Sound_IsVoiceFinished;       // Sound::IsVoiceFinished(u8 channel)
};

// Ultra Moon / Ultra Sun v1.2 (title version 2080) - the version the user plays.
static const Offsets kUSUM_1_2 = {
    "USUM 1.2",
    0x001048B4, 0x00361724, 0x003617B0, 0x001052E8, 0x00107374,
    0x004C37BC, 0x0042B4E0, 0x0042B710, 0x0042C9D0, 0x0042B434,
    0x0044A9D4, 0x003A646C, 0x004C678C,
    0x003A8584, 0x00320C4C,
    0x00279FC0,
    0x003239F4, 0x00322CB0, 0x004AF138, 0x00322C38,
    0x0034DFCC,
    0x004B5474, 0x004B54E8, 0x003A9944,
    0x0010F528,
    0x00007F3C,
    0x00075488, 0x000256B4, 0x00073C08,
    0x00007FCC, 0x00007210, 0x00007104, 0x00007E08, 0x00007F3C, 0x000071AC, 0x000086AC, 0x00007DB0, 0x0044BC18, 0x00445298,
    0x00006F0C, 0x00006C1C, 0x00006EF0, 0x00072974,
    0x0044A940,
    0x00008754,
    0x003BF630, 0x003BDDD0, 0x00671AEC,
    0x0044B040,
    0x003BDA5C, 0x003BD848,
};

} // namespace OWE
