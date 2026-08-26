# PainEngine (Engine.dll) API reference

Auto-generated from the DLL export table (demangled). 2867 exports, 112 classes.

## Classes by method count

- **MilesEngine** -- 142
- **PhysicsObject** -- 128
- **Pathfinder2** -- 103
- **UdpMessage** -- 96
- **PhysicsWorld** -- 83
- **WaypointSet** -- 78
- **World** -- 75
- **Entity** -- 72
- **Pathfinder** -- 69
- **NetworkDevice2** -- 68
- **MenuScreen** -- 65
- **MenuItem** -- 64
- **Ragdoll** -- 60
- **Model** -- 57
- **Script** -- 47
- **WorldMesh** -- 47
- **DIInputSystem** -- 47
- **InputSystem** -- 38
- **HUD** -- 37
- **MaterialSystem** -- 35
- **String** -- 35
- **GFileManager** -- 33
- **MenuItemList** -- 32
- **Light** -- 32
- **ParticleEffect** -- 30
- **ScriptObject** -- 29
- **LineRender** -- 28
- **View** -- 28
- **MagicBoard** -- 28
- **SystemDriver** -- 27
- **AnimatedMesh** -- 27
- **Camera** -- 26
- **MenuItemTextEdit** -- 25
- **Trail** -- 24
- **GFile** -- 23
- **SceneRender** -- 22
- **Zone** -- 22
- **Decal** -- 22
- **MenuItemMapTable** -- 21
- **PCFSystem** -- 21
- **DemoRecording2** -- 21
- **WaypointGPath** -- 19
- **Billboard** -- 19
- **SimpleProfiler** -- 19
- **GMemFile** -- 19
- **ParticleSystem** -- 19
- **WaypointGPath2** -- 19
- **MenuItemSlider** -- 18
- **PainMenu** -- 18
- **MenuItemScroller** -- 18
- **Movie** -- 18
- **Sound** -- 18
- **EngineGame** -- 18
- **ObjectPackage** -- 17
- **PhysicsEngine** -- 17
- **AnimatedMeshMatPal** -- 15
- **Window** -- 15
- **MenuItemBorder** -- 15
- **RegistryManager** -- 15
- **BitSet** -- 13
- **AntiPortal** -- 13
- **GraphicsDevice** -- 13
- **Environment** -- 13
- **LoadingScreen** -- 13
- **Sky** -- 13
- **Portal** -- 13
- **WorldRegion** -- 13
- **ConvexPolygon** -- 12
- **GPack** -- 12
- **LogBuffer** -- 12
- **ConfigFile** -- 11
- **WaypointPath** -- 11
- **MenuItemCheckbox** -- 11
- **Volume** -- 11
- **ScriptTableIterator** -- 10
- **ParticleEmitter** -- 10
- **Waypoint2** -- 10
- **Object** -- 10
- **GFont** -- 10
- **SimpleMesh** -- 10
- **MessageHandler** -- 10
- **MenuItemTabGroup** -- 10
- **StringHashSystem2** -- 10
- **TrailSystem** -- 9
- **Viewport** -- 8
- **MenuItemTextButtonEx** -- 8
- **PolygonalMesh** -- 8
- **BindSkeleton** -- 8
- **MeshShape** -- 7
- **ConvexMeshPolygon** -- 7
- **GZipPack** -- 7
- **RenderWindow** -- 6
- **MenuItemPassword** -- 6
- **MenuItemNumEdit** -- 6
- **TrailEffect** -- 5
- **MeshVertex** -- 5
- **ParticlePool** -- 5
- **SimpleVertex** -- 5
- **TangentBasis** -- 4
- **RagdollData** -- 4
- **MeshPackVertNTU** -- 4
- **MeshPackVertTU2** -- 4
- **Sprite1DOF** -- 4
- **TexCoord** -- 4
- **VertexWeights** -- 3
- **SimplePackedVertex** -- 3
- **StackTracer** -- 3
- **Particle** -- 3
- **ScreenVertexNDC** -- 3
- **ScreenVertex** -- 3
- **Sprite** -- 3
- **ObjectEntry** -- 3

## AnimatedMesh
```cpp
const AnimatedMesh::`vftable'
private: virtual void __thiscall AnimatedMesh::Load(class GFile *)
private: virtual void __thiscall AnimatedMesh::Save(class GFile *)
protected: class MeshShape * __thiscall AnimatedMesh::FindMeshByName(class String const &)const 
protected: virtual void __thiscall AnimatedMesh::RenderDefault(class Model *,int)const 
protected: virtual void __thiscall AnimatedMesh::RenderDemonFX(class Model *)const 
protected: virtual void __thiscall AnimatedMesh::RenderGhostFX(class Model *)const 
protected: virtual void __thiscall AnimatedMesh::RenderShadowmap(class Model const *)const 
protected: virtual void __thiscall AnimatedMesh::RenderVolume(class Model const *)const 
protected: virtual void __thiscall AnimatedMesh::RenderWarpFX(class Model *)const 
protected: void __thiscall AnimatedMesh::ComputeBindMatrix(class Matrix,class DynamicArray<class Matrix> const &,int &)const 
public: __thiscall AnimatedMesh::AnimatedMesh(class AnimatedMesh const &)
public: __thiscall AnimatedMesh::AnimatedMesh(void)
public: class AnimatedMesh & __thiscall AnimatedMesh::operator=(class AnimatedMesh const &)
public: class DynamicArray<class Matrix> __thiscall AnimatedMesh::ComputeBindMatrix(class Matrix const &)const 
public: class Texture * __thiscall AnimatedMesh::CreateTexture(class String const &,int,int)const 
public: int __thiscall AnimatedMesh::FindAnimationByName(class String const &)const 
public: int __thiscall AnimatedMesh::LoadAnimation(class String const &,bool,int)
public: virtual __thiscall AnimatedMesh::~AnimatedMesh(void)
public: virtual char const * __thiscall AnimatedMesh::GetClassNameA(void)const 
public: virtual void __thiscall AnimatedMesh::AnimateGeometry(class DynamicArray<class Matrix> const &)
public: virtual void __thiscall AnimatedMesh::AnimateTangentSpace(class DynamicArray<class Matrix> const &)
public: virtual void __thiscall AnimatedMesh::Render(class Model *,int)const 
public: virtual void __thiscall AnimatedMesh::RenderCleanup(void)
public: virtual void __thiscall AnimatedMesh::RenderInitialize(void)
public: virtual void __thiscall AnimatedMesh::SetBindPoseFrame(class Model const *)const 
public: void __thiscall AnimatedMesh::LoadPreprocess(float,char const *)
```

## AnimatedMeshMatPal
```cpp
const AnimatedMeshMatPal::`vftable'
protected: virtual void __thiscall AnimatedMeshMatPal::RenderDefault(class Model *,int)const 
protected: virtual void __thiscall AnimatedMeshMatPal::RenderDemonFX(class Model *)const 
protected: virtual void __thiscall AnimatedMeshMatPal::RenderGhostFX(class Model *)const 
protected: virtual void __thiscall AnimatedMeshMatPal::RenderShadowmap(class Model const *)const 
protected: virtual void __thiscall AnimatedMeshMatPal::RenderVolume(class Model const *)const 
protected: virtual void __thiscall AnimatedMeshMatPal::RenderWarpFX(class Model *)const 
public: __thiscall AnimatedMeshMatPal::AnimatedMeshMatPal(class AnimatedMeshMatPal const &)
public: __thiscall AnimatedMeshMatPal::AnimatedMeshMatPal(void)
public: class AnimatedMeshMatPal & __thiscall AnimatedMeshMatPal::operator=(class AnimatedMeshMatPal const &)
public: virtual __thiscall AnimatedMeshMatPal::~AnimatedMeshMatPal(void)
public: virtual char const * __thiscall AnimatedMeshMatPal::GetClassNameA(void)const 
public: virtual void __thiscall AnimatedMeshMatPal::AnimateGeometry(class DynamicArray<class Matrix> const &)
public: virtual void __thiscall AnimatedMeshMatPal::RenderInitialize(void)
public: virtual void __thiscall AnimatedMeshMatPal::SetBindPoseFrame(class Model const *)const 
```

## AntiPortal
```cpp
const AntiPortal::`vftable'
public: __thiscall AntiPortal::AntiPortal(class AntiPortal const &)
public: __thiscall AntiPortal::AntiPortal(class WorldMesh *)
public: __thiscall AntiPortal::AntiPortal(void)
public: bool __thiscall AntiPortal::BuildTriangleAdjacencies(void)
public: class AntiPortal & __thiscall AntiPortal::operator=(class AntiPortal const &)
public: virtual __thiscall AntiPortal::~AntiPortal(void)
public: virtual char const * __thiscall AntiPortal::GetClassNameA(void)const 
public: virtual void __thiscall AntiPortal::Load(class GFile *)
public: virtual void __thiscall AntiPortal::Save(class GFile *)
public: void __thiscall AntiPortal::BuildBox(void)
public: void __thiscall AntiPortal::BuildConvex(void)
public: void __thiscall AntiPortal::Scale(float)
```

## Billboard
```cpp
const Billboard::`vftable'
public: __thiscall Billboard::Billboard(class Billboard const &)
public: __thiscall Billboard::Billboard(void)
public: class Billboard & __thiscall Billboard::operator=(class Billboard const &)
public: virtual __thiscall Billboard::~Billboard(void)
public: virtual bool __thiscall Billboard::DemoReadEntity(class GFile *)
public: virtual bool __thiscall Billboard::DemoReadFull(class GFile *)
public: virtual bool __thiscall Billboard::DemoWriteEntity(class GFile *)
public: virtual bool __thiscall Billboard::DemoWriteFull(class GFile *)
public: virtual bool __thiscall Billboard::LoadEntity(class GFile *)
public: virtual bool __thiscall Billboard::SaveEntity(class GFile *)
public: virtual void __thiscall Billboard::Draw(class RenderDevice *,int)
public: virtual void __thiscall Billboard::RenderCleanup(class RenderDevice *,class MaterialSystem *)
public: virtual void __thiscall Billboard::RenderInitialize(class RenderDevice *,class MaterialSystem *,char const *)
public: virtual void __thiscall Billboard::Tick(float)
public: void __thiscall Billboard::FadeTick(int,float)
public: void __thiscall Billboard::SetCorona(bool)
public: void __thiscall Billboard::SetTexture(char const *,int)
public: void __thiscall Billboard::UpdateBBox(void)
```

## BindSkeleton
```cpp
protected: void __thiscall BindSkeleton::ProcessRelationsRecursive(int &,int)
public: __thiscall BindSkeleton::~BindSkeleton(void)
public: __thiscall BindSkeleton::BindSkeleton(class BindSkeleton const &)
public: __thiscall BindSkeleton::BindSkeleton(void)
public: class BindSkeleton & __thiscall BindSkeleton::operator=(class BindSkeleton const &)
public: int __thiscall BindSkeleton::JointIndex(char const *)const 
public: void __thiscall BindSkeleton::NormalizeJoints(float)
public: void __thiscall BindSkeleton::ProcessRelations(void)
```

## BitSet
```cpp
public: __thiscall BitSet::~BitSet(void)
public: __thiscall BitSet::BitSet(void)
public: class BitSet & __thiscall BitSet::operator=(class BitSet const &)
public: int __thiscall BitSet::GetSize(void)
public: int __thiscall BitSet::Test(int)const 
public: unsigned long * __thiscall BitSet::GetPtrToData(void)
public: void __thiscall BitSet::Clear(int)
public: void __thiscall BitSet::ClearAll(void)
public: void __thiscall BitSet::CopyFrom(class BitSet &)
public: void __thiscall BitSet::MakeCompleteCopy(class BitSet &)
public: void __thiscall BitSet::Release(void)
public: void __thiscall BitSet::Resize(int)
public: void __thiscall BitSet::Set(int)
```

## Camera
```cpp
public: __thiscall Camera::Camera(class Camera const &)
public: __thiscall Camera::Camera(void)
public: class Camera & __thiscall Camera::operator=(class Camera const &)
public: class Matrix __thiscall Camera::RawWorldToCamera(void)const 
public: class Matrix __thiscall Camera::WorldToCamera(void)const 
public: class Matrix const __thiscall Camera::GetViewMatrix(void)const 
public: class Vector __thiscall Camera::GetCubemapForwardVector(void)const 
public: class Vector __thiscall Camera::GetCubemapRightVector(void)const 
public: class Vector __thiscall Camera::GetCubemapUpVector(void)const 
public: class Vector __thiscall Camera::GetForwardVector(void)const 
public: class Vector __thiscall Camera::GetRightVector(void)const 
public: class Vector __thiscall Camera::GetUpVector(void)const 
public: class Vector const __thiscall Camera::GetPosition(void)const 
public: class Vector const __thiscall Camera::GetRawPosition(void)const 
public: class Vector const __thiscall Camera::GetRawRotation(void)const 
public: class Vector const __thiscall Camera::GetRotation(void)const 
public: float const __thiscall Camera::GetFOV(void)const 
public: void __thiscall Camera::InverseTransform(void)
public: void __thiscall Camera::LookAt(class Vector const &)
public: void __thiscall Camera::SetDispPosition(class Vector const &)
public: void __thiscall Camera::SetDispRotation(class Vector const &)
public: void __thiscall Camera::SetFOV(float)
public: void __thiscall Camera::SetPosition(class Vector const &)
public: void __thiscall Camera::SetRotation(class Vector const &)
public: void __thiscall Camera::SetViewMatrix(class Matrix const &)
public: void __thiscall Camera::UpdateCubemapViewMatrix(void)
```

## ConfigFile
```cpp
private: int __thiscall ConfigFile::Load(char const *)
private: int __thiscall ConfigFile::Save(char const *)
public: __thiscall ConfigFile::~ConfigFile(void)
public: __thiscall ConfigFile::ConfigFile(class ConfigFile const &)
public: __thiscall ConfigFile::ConfigFile(void)
public: char const * __thiscall ConfigFile::GetString(char const *,char const *)const 
public: class ConfigFile & __thiscall ConfigFile::operator=(class ConfigFile const &)
public: int __thiscall ConfigFile::GetInt(char const *,char const *)const 
public: int __thiscall ConfigFile::Load(class String const &)
public: int __thiscall ConfigFile::Save(class String const &)
public: void __thiscall ConfigFile::AddOption(char const *,class String const &,class String const &)
```

## ConvexMeshPolygon
```cpp
public: __thiscall ConvexMeshPolygon::ConvexMeshPolygon(class ConvexMeshPolygon const &)
public: __thiscall ConvexMeshPolygon::ConvexMeshPolygon(void)
public: class ConvexMeshPolygon & __thiscall ConvexMeshPolygon::operator=(class ConvexMeshPolygon const &)
public: int __thiscall ConvexMeshPolygon::Classify(class Plane const &,float)const 
public: int __thiscall ConvexMeshPolygon::Split(class Plane const &,class ConvexMeshPolygon &,class ConvexMeshPolygon &,float)const 
public: void __thiscall ConvexMeshPolygon::operator+=(struct MeshVertex const &)
public: void __thiscall ConvexMeshPolygon::Reverse(void)
```

## ConvexPolygon
```cpp
public: __thiscall ConvexPolygon::ConvexPolygon(class ConvexPolygon const &)
public: __thiscall ConvexPolygon::ConvexPolygon(void)
public: bool __thiscall ConvexPolygon::BoxTest(class BoundingBox const &,float)const 
public: bool __thiscall ConvexPolygon::BoxTestForBoxTypeZonePortalInclusion(class BoundingBox const &,float)const 
public: bool __thiscall ConvexPolygon::ConvexVolumeTest(class ConvexVolumeResizable const &,float)const 
public: class ConvexPolygon & __thiscall ConvexPolygon::operator=(class ConvexPolygon const &)
public: class ConvexVolume __thiscall ConvexPolygon::Extrude(float)
public: int __thiscall ConvexPolygon::Classify(class Plane const &,float)const 
public: int __thiscall ConvexPolygon::CreateFromPlane(class Plane const &,float)
public: int __thiscall ConvexPolygon::Split(class Plane const &,class ConvexPolygon &,class ConvexPolygon &,float)const 
public: void __thiscall ConvexPolygon::operator+=(class Vector const &)
public: void __thiscall ConvexPolygon::Reverse(void)
```

## Decal
```cpp
const Decal::`vftable'
public: __thiscall Decal::Decal(class Decal const &)
public: __thiscall Decal::Decal(class DecalEffect *)
public: bool __thiscall Decal::Spawn(class Entity *,class Matrix const &)
public: bool __thiscall Decal::Spawn(class Entity *,class Vector const &,class Vector const &)
public: bool __thiscall Decal::Spawn(class Entity *,class Vector const &,class Vector const &,class Vector const &,class Vector const &)
public: class Decal & __thiscall Decal::operator=(class Decal const &)
public: class Matrix const & __thiscall Decal::GetSpawnMatrix(void)const 
public: virtual __thiscall Decal::~Decal(void)
public: virtual bool __thiscall Decal::DemoReadEntity(class GFile *)
public: virtual bool __thiscall Decal::DemoReadFull(class GFile *)
public: virtual bool __thiscall Decal::DemoWriteEntity(class GFile *)
public: virtual bool __thiscall Decal::DemoWriteFull(class GFile *)
public: virtual bool __thiscall Decal::LoadEntity(class GFile *)
public: virtual bool __thiscall Decal::SaveEntity(class GFile *)
public: virtual void __thiscall Decal::Draw(class RenderDevice *,int)
public: virtual void __thiscall Decal::SetScale(float)
public: virtual void __thiscall Decal::Tick(float)
public: void __thiscall Decal::SetAlpha(int)
public: void __thiscall Decal::SetState(class RenderDevice *)
public: void __thiscall Decal::SetTexture(class String const &)
public: void __thiscall Decal::SetTexture(class Texture *)
```

## DemoRecording2
```cpp
private: bool __thiscall DemoRecording2::ReadHeader(class String &,class String &,long &)
private: bool __thiscall DemoRecording2::WriteHeader(class String,class String,long)
private: void __thiscall DemoRecording2::UpdatePlaying(void)
private: void __thiscall DemoRecording2::UpdateRecording(void)
private: void __thiscall DemoRecording2::UpdateView(class Vector &,float,float)
public: __thiscall DemoRecording2::~DemoRecording2(void)
public: __thiscall DemoRecording2::DemoRecording2(class DemoRecording2 const &)
public: __thiscall DemoRecording2::DemoRecording2(void)
public: bool __thiscall DemoRecording2::IsIdle(void)const 
public: bool __thiscall DemoRecording2::IsPlaying(void)const 
public: bool __thiscall DemoRecording2::IsRecording(void)const 
public: bool __thiscall DemoRecording2::StartPlaying(char const *,class String *,bool,bool)
public: class DemoRecording2 & __thiscall DemoRecording2::operator=(class DemoRecording2 const &)
public: float __thiscall DemoRecording2::GetFPS(void)const 
public: float __thiscall DemoRecording2::GetSpeed(void)const 
public: void __thiscall DemoRecording2::AddToDelete(int)
public: void __thiscall DemoRecording2::RecordPlayer(int,unsigned short,int,int)
public: void __thiscall DemoRecording2::SetSpeed(float)
public: void __thiscall DemoRecording2::StartRecording(char const *)
public: void __thiscall DemoRecording2::Stop(void)
public: void __thiscall DemoRecording2::Update(void)
```

## DIInputSystem
```cpp
const DIInputSystem::`vftable'
private: bool __thiscall DIInputSystem::BindUIKey(unsigned int)
private: bool __thiscall DIInputSystem::CheckMessagesKeys(int)
private: bool __thiscall DIInputSystem::SetupDInput(void)
private: bool __thiscall DIInputSystem::SetupKeyboard(void)
private: bool __thiscall DIInputSystem::SetupMouse(void)
private: bool __thiscall DIInputSystem::UnbindUIKey(unsigned int)
private: unsigned int __thiscall DIInputSystem::TranslateKeyCode(unsigned int)
private: unsigned int __thiscall DIInputSystem::TranslateKeyCode(unsigned int,long)
private: virtual void __thiscall DIInputSystem::FillKeyNamesTable(void)
private: void __thiscall DIInputSystem::FillDIKeyTable(void)
private: void __thiscall DIInputSystem::KeyboardTick(void)
private: void __thiscall DIInputSystem::MouseTick(void)
private: void __thiscall DIInputSystem::SwitchToDInput(void)
private: void __thiscall DIInputSystem::SwitchToWin32(void)
public: __thiscall DIInputSystem::DIInputSystem(class DIInputSystem const &)
public: __thiscall DIInputSystem::DIInputSystem(struct HWND__ *)
public: class DIInputSystem & __thiscall DIInputSystem::operator=(class DIInputSystem const &)
public: virtual __thiscall DIInputSystem::~DIInputSystem(void)
public: virtual bool __thiscall DIInputSystem::GetMouseSmooth(void)const 
public: virtual bool __thiscall DIInputSystem::GetUseDInput(void)const 
public: virtual bool __thiscall DIInputSystem::IsKeyPrintable(unsigned char)
public: virtual class Vector __thiscall DIInputSystem::GetMouseDelta(void)const 
public: virtual float __thiscall DIInputSystem::GetDIDeltaScale(void)const 
public: virtual float __thiscall DIInputSystem::GetMouseSensitivity(void)const 
public: virtual float __thiscall DIInputSystem::GetWheelSensitivity(void)const 
public: virtual int __thiscall DIInputSystem::GetKBType(void)
public: virtual int __thiscall DIInputSystem::IsMouseLocked(void)const 
public: virtual int __thiscall DIInputSystem::IsMouseVisible(void)const 
public: virtual long __thiscall DIInputSystem::HandleMessages(struct HWND__ *,unsigned int,unsigned int,long)
public: virtual struct tagPOINT __thiscall DIInputSystem::GetMousePosition(void)const 
public: virtual unsigned char __thiscall DIInputSystem::KeyToPrintable(unsigned char)
public: virtual void __thiscall DIInputSystem::CenterMouse(void)
public: virtual void __thiscall DIInputSystem::LockMouse(int)
public: virtual void __thiscall DIInputSystem::Release(void)
public: virtual void __thiscall DIInputSystem::Reset(void)
public: virtual void __thiscall DIInputSystem::SetDIDeltaScale(float)
public: virtual void __thiscall DIInputSystem::SetMousePosition(int,int)
public: virtual void __thiscall DIInputSystem::SetMouseSensitivity(float)
public: virtual void __thiscall DIInputSystem::SetMouseSmooth(bool)
public: virtual void __thiscall DIInputSystem::SetUseDInput(bool)
public: virtual void __thiscall DIInputSystem::SetWheelSensitivity(float)
public: virtual void __thiscall DIInputSystem::SetWindow(struct HWND__ *)
public: virtual void __thiscall DIInputSystem::ShowMouse(int)
public: virtual void __thiscall DIInputSystem::Tick(void)
public: virtual void __thiscall DIInputSystem::UpdateMousePosition(void)
public: void __thiscall DIInputSystem::CaptureMouse(int)
```

## EngineGame
```cpp
const EngineGame::`vftable'
public: __thiscall EngineGame::EngineGame(class EngineGame const &)
public: __thiscall EngineGame::EngineGame(void)
public: class EngineGame & __thiscall EngineGame::operator=(class EngineGame const &)
public: class Entity * __thiscall EngineGame::CreatePlayer(char const *,bool)
public: virtual __thiscall EngineGame::~EngineGame(void)
public: void __thiscall EngineGame::Close(void)
public: void __thiscall EngineGame::HideMenu(void)
public: void __thiscall EngineGame::HideMPStats(void)
public: void __thiscall EngineGame::Initialize(void)
public: void __thiscall EngineGame::RecordingUpdateViewFromPlayer(class Entity *,float,float)
public: void __thiscall EngineGame::ShowMenu(void)
public: void __thiscall EngineGame::ShowMPStats(void)
public: void __thiscall EngineGame::SwitchConsole(void)
public: void __thiscall EngineGame::SwitchMagicBoard(bool)
public: void __thiscall EngineGame::SwitchMapSelect(bool)
public: void __thiscall EngineGame::SwitchMenu(bool)
public: void __thiscall EngineGame::Tick(bool,float)
```

## Entity
```cpp
const Entity::`vftable'
public: __thiscall Entity::Entity(class Entity const &)
public: __thiscall Entity::Entity(int)
public: bool __thiscall Entity::KillAllChildrenByName(char const *)
public: bool __thiscall Entity::SeesEntity(class Entity &)
public: bool __thiscall Entity::SeesPoint(class Vector &)
public: char * __thiscall Entity::GetSynchroString(void)
public: class Billboard * __thiscall Entity::AsBillboard(void)
public: class Decal * __thiscall Entity::AsDecal(void)
public: class Entity & __thiscall Entity::operator=(class Entity const &)
public: class Entity * __thiscall Entity::GetChildByName(char const *)
public: class Environment * __thiscall Entity::AsEnvironment(void)
public: class Light * __thiscall Entity::AsLight(void)
public: class Light * __thiscall Entity::GetEnvironmentDirLight(void)
public: class Model * __thiscall Entity::AsModel(void)
public: class ParticleEffect * __thiscall Entity::AsParticleFx(void)
public: class Quaternion __thiscall Entity::GetRotation(void)const 
public: class Quaternion __thiscall Entity::GetWorldRotation(void)const 
public: class Trail * __thiscall Entity::AsTrail(void)
public: class Vector __thiscall Entity::GetForwardVector(void)const 
public: class Vector __thiscall Entity::GetPosition(void)const 
public: class Vector __thiscall Entity::GetWorldPosition(void)const 
public: class Volume * __thiscall Entity::AsVolume(void)
public: class WorldMesh * __thiscall Entity::AsMesh(void)
public: class WorldRegion * __thiscall Entity::AsRegion(void)
public: float __thiscall Entity::GetCamDistance(void)const 
public: float __thiscall Entity::GetOrientation(void)const 
public: float __thiscall Entity::GetScale(void)const 
public: int __thiscall Entity::ComputeVSLights(int)
public: union Color const __thiscall Entity::GetEnvironmentAmbient(void)
public: unsigned char __thiscall Entity::SynchroState(void)
public: virtual __thiscall Entity::~Entity(void)
public: virtual bool __thiscall Entity::DemoCalcSize(int &,unsigned long &)
public: virtual bool __thiscall Entity::DemoReadEntity(class GFile *)
public: virtual bool __thiscall Entity::DemoReadFull(class GFile *)
public: virtual bool __thiscall Entity::DemoWriteEntity(class GFile *)
public: virtual bool __thiscall Entity::DemoWriteFull(class GFile *)
public: virtual bool __thiscall Entity::LoadEntity(class GFile *)
public: virtual bool __thiscall Entity::OnUnregister(void)
public: virtual bool __thiscall Entity::SaveEntity(class GFile *)
public: virtual class Vector __thiscall Entity::GetCenter(void)const 
public: virtual float __thiscall Entity::GetWorldScale(void)const 
public: virtual void __thiscall Entity::Draw(class RenderDevice *,int)
public: virtual void __thiscall Entity::EnableDraw(bool)
public: virtual void __thiscall Entity::Load(class GFile *)
public: virtual void __thiscall Entity::RenderCleanup(class RenderDevice *,class MaterialSystem *)
public: virtual void __thiscall Entity::RenderInitialize(class RenderDevice *,class MaterialSystem *,char const *)
public: virtual void __thiscall Entity::Reset(void)
public: virtual void __thiscall Entity::Save(class GFile *)
public: virtual void __thiscall Entity::SetObjectTransform(class Matrix const &)
public: virtual void __thiscall Entity::SetOrientation(float)
public: virtual void __thiscall Entity::SetPosition(class Vector const &)
public: virtual void __thiscall Entity::SetPositionAndRotation(class Vector const &,class Quaternion const &)
public: virtual void __thiscall Entity::SetRotation(class Quaternion const &)
public: virtual void __thiscall Entity::SetScale(float)
public: virtual void __thiscall Entity::Tick(float)
public: virtual void __thiscall Entity::UpdateTransform(bool)
public: virtual void __thiscall Entity::UpdateVolEntity(void)
public: void __thiscall Entity::AddLight(class Light *)
public: void __thiscall Entity::CreatePhysicsObject(unsigned long,float,int,bool)
public: void __thiscall Entity::KillAllChildren(int)
public: void __thiscall Entity::RegisterChild(class Entity *,bool,char)
public: void __thiscall Entity::RemovePhysicsObject(void)
public: void __thiscall Entity::RenderBoundingBox(int)
public: void __thiscall Entity::RenderLocalBoundingBox(int)
public: void __thiscall Entity::ResetLights(void)
public: void __thiscall Entity::SetEnvironmentAmbient(bool,union Color const &)
public: void __thiscall Entity::SetIdentity(void)
public: void __thiscall Entity::SetSynchroState(unsigned char,unsigned char,unsigned short,unsigned char)
public: void __thiscall Entity::SetSynchroString(char const *)
public: void __thiscall Entity::UnregisterAllChildren(int)
public: void __thiscall Entity::UnregisterChild(class Entity *)
```

## Environment
```cpp
const Environment::`vftable'
public: __thiscall Environment::Environment(class Environment const &)
public: __thiscall Environment::Environment(void)
public: class Environment & __thiscall Environment::operator=(class Environment const &)
public: int __thiscall Environment::PointTest(class Vector const &)const 
public: virtual __thiscall Environment::~Environment(void)
public: virtual void __thiscall Environment::EnableDraw(bool)
public: virtual void __thiscall Environment::UpdateTransform(bool)
public: void __thiscall Environment::AddEntity(class Entity *)
public: void __thiscall Environment::AddLight(class Light *)
public: void __thiscall Environment::RemoveEntity(class Entity *)
public: void __thiscall Environment::RemoveLight(class Light *)
public: void __thiscall Environment::RemoveLights(void)
```

## GFile
```cpp
const GFile::`vftable'
public: __thiscall GFile::GFile(class GFile const &)
public: __thiscall GFile::GFile(void)
public: bool __thiscall GFile::PutToken(unsigned short)
public: bool __thiscall GFile::TestToken(unsigned short)
public: char const * __thiscall GFile::GetName(void)
public: class GFile & __thiscall GFile::operator=(class GFile const &)
public: unsigned long __thiscall GFile::GetCRC(void)
public: virtual __int64 __thiscall GFile::Tell(void)
public: virtual __thiscall GFile::~GFile(void)
public: virtual bool __thiscall GFile::IsFile(void)
public: virtual bool __thiscall GFile::IsMemFile(void)
public: virtual bool __thiscall GFile::Open(char const *,char const *)
public: virtual int __thiscall GFile::GetC(void)
public: virtual int __thiscall GFile::GetS(char *,int)
public: virtual int __thiscall GFile::Read(void *,int)
public: virtual int __thiscall GFile::ReadA(void *,int)
public: virtual int __thiscall GFile::Seek(__int64,int)
public: virtual int __thiscall GFile::Write(void const *,int)
public: virtual long __thiscall GFile::GetSize(void)
public: virtual struct _iobuf * __thiscall GFile::GetFILE(void)
public: virtual void __thiscall GFile::Close(void)
public: virtual void __thiscall GFile::Flush(void)
```

## GFileManager
```cpp
public: __thiscall GFileManager::~GFileManager(void)
public: __thiscall GFileManager::GFileManager(class GFileManager const &)
public: __thiscall GFileManager::GFileManager(void)
public: bool __thiscall GFileManager::Copy(char const *,char const *)
public: bool __thiscall GFileManager::CreateDirectoryA(char const *)
public: bool __thiscall GFileManager::FF_IsDirectory(long)
public: bool __thiscall GFileManager::FileExist(char const *)
public: bool __thiscall GFileManager::TestDirectory(char const *)
public: char const * __thiscall GFileManager::FF_GetName(long)
public: class GFile * __thiscall GFileManager::Add(class GFile *)
public: class GFile * __thiscall GFileManager::CreateFileReader(char const *,char const *)
public: class GFile * __thiscall GFileManager::CreateFileWriter(char const *,char const *)
public: class GFileManager & __thiscall GFileManager::operator=(class GFileManager const &)
public: class GPack * __thiscall GFileManager::FindPackByPath(char const *)
public: class GPack * __thiscall GFileManager::RegisterPack(class GPack *,char const *,char const *,bool)
public: class String __thiscall GFileManager::FileTime(char const *)
public: long __thiscall GFileManager::FF_Next(long)
public: long __thiscall GFileManager::FF_Open(char const *)
public: struct _iobuf * __thiscall GFileManager::GetPAKFile(void)
public: unsigned long __thiscall GFileManager::FileSize(char const *)
public: void * __thiscall GFileManager::GetZIPFile(void)
public: void __thiscall GFileManager::Close(class GFile *)
public: void __thiscall GFileManager::ClosePAK(void)
public: void __thiscall GFileManager::CloseZIP(void)
public: void __thiscall GFileManager::CreatePAK(char const *)
public: void __thiscall GFileManager::CreateZIP(char const *)
public: void __thiscall GFileManager::DetachFile(class GFile *)
public: void __thiscall GFileManager::FF_Close(long)
public: void __thiscall GFileManager::FF_MarkPackedFileAsFound(long,char const *)
public: void __thiscall GFileManager::FindFiles(char const *,class DynamicArray<class String> &,unsigned long)
public: void __thiscall GFileManager::RegisterFile(char const *,struct FIdx *)
public: void __thiscall GFileManager::RemoveFilesInDirectory(char const *)
public: void __thiscall GFileManager::UnregisterPack(class GPack *)
```

## GFont
```cpp
private: bool __thiscall GFont::CalcTextureSize(struct FT_FaceRec_ *,int *,int *)
private: bool __thiscall GFont::CheckTexSize(struct FT_FaceRec_ *,int,int)
private: unsigned long __thiscall GFont::CharToUnicode(void *,char)
private: void __thiscall GFont::RenderGlyphs(struct FT_FaceRec_ *)
public: __thiscall GFont::~GFont(void)
public: __thiscall GFont::GFont(class GFont const &)
public: __thiscall GFont::GFont(void)
public: class GFont & __thiscall GFont::operator=(class GFont const &)
public: int __thiscall GFont::Load(class RenderDevice *,struct FT_LibraryRec_ *,char const *,int,char const *)
public: int __thiscall GFont::VertShift(void)const 
```

## GMemFile
```cpp
const GMemFile::`vftable'
public: __thiscall GMemFile::GMemFile(class GMemFile const &)
public: __thiscall GMemFile::GMemFile(void)
public: class GMemFile & __thiscall GMemFile::operator=(class GMemFile const &)
public: virtual __int64 __thiscall GMemFile::Tell(void)
public: virtual __thiscall GMemFile::~GMemFile(void)
public: virtual bool __thiscall GMemFile::IsFile(void)
public: virtual bool __thiscall GMemFile::IsMemFile(void)
public: virtual bool __thiscall GMemFile::Open(char const *,char const *)
public: virtual bool __thiscall GMemFile::Open(void *,long,bool)
public: virtual int __thiscall GMemFile::GetC(void)
public: virtual int __thiscall GMemFile::Read(void *,int)
public: virtual int __thiscall GMemFile::ReadA(void *,int)
public: virtual int __thiscall GMemFile::Seek(__int64,int)
public: virtual int __thiscall GMemFile::Write(void const *,int)
public: virtual long __thiscall GMemFile::GetSize(void)
public: virtual void * __thiscall GMemFile::GetCurPtr(void)
public: virtual void * __thiscall GMemFile::GetPtr(void)
public: virtual void __thiscall GMemFile::Close(void)
```

## GPack
```cpp
const GPack::`vftable'
public: __thiscall GPack::GPack(class GPack const &)
public: __thiscall GPack::GPack(void)
public: char const * __thiscall GPack::GetFName(void)
public: char const * __thiscall GPack::GetPath(void)
public: class GPack & __thiscall GPack::operator=(class GPack const &)
public: static long __cdecl GPack::Extract(char const *,char const *)
public: static long __cdecl GPack::Make(char const *,char const *,bool)
public: virtual __thiscall GPack::~GPack(void)
public: virtual bool __thiscall GPack::OpenPack(char const *,char const *)
public: virtual class GFile * __thiscall GPack::GetFile(struct FIdx *)
public: void __thiscall GPack::Release(void)
```

## GraphicsDevice
```cpp
public: __thiscall GraphicsDevice::~GraphicsDevice(void)
public: __thiscall GraphicsDevice::GraphicsDevice(class GraphicsDevice const &)
public: __thiscall GraphicsDevice::GraphicsDevice(void)
public: class GraphicsDevice & __thiscall GraphicsDevice::operator=(class GraphicsDevice const &)
public: class View * __thiscall GraphicsDevice::CreateView(void)
public: class View * __thiscall GraphicsDevice::GetActiveView(void)
public: int __thiscall GraphicsDevice::Init(int,int,int,unsigned long)
public: int __thiscall GraphicsDevice::SetRes(int,int,int,int,int)
public: struct HWND__ * __thiscall GraphicsDevice::FullScreenHWND(void)const 
public: void __thiscall GraphicsDevice::AddView(class View *)
public: void __thiscall GraphicsDevice::DeleteView(class View *)
public: void __thiscall GraphicsDevice::RemoveView(class View *)
public: void __thiscall GraphicsDevice::SetViewAsActive(class View *)
```

## GZipPack
```cpp
const GZipPack::`vftable'
public: __thiscall GZipPack::GZipPack(class GZipPack const &)
public: __thiscall GZipPack::GZipPack(void)
public: class GZipPack & __thiscall GZipPack::operator=(class GZipPack const &)
public: virtual __thiscall GZipPack::~GZipPack(void)
public: virtual bool __thiscall GZipPack::OpenPack(char const *,char const *)
public: virtual class GFile * __thiscall GZipPack::GetFile(struct FIdx *)
```

## HUD
```cpp
private: void __thiscall HUD::ClearAlphaLine(unsigned char *,int,int,int,int,int)
private: void __thiscall HUD::ClearAlphaQuad(unsigned char *,int,int,int,int,int)
public: __thiscall HUD::~HUD(void)
public: __thiscall HUD::HUD(class HUD const &)
public: __thiscall HUD::HUD(void)
public: class HUD & __thiscall HUD::operator=(class HUD const &)
public: class String __thiscall HUD::ColorSubstr(class String,int)
public: int __thiscall HUD::GetTextHeight(char const *)const 
public: int __thiscall HUD::GetTextHeight(void)const 
public: int __thiscall HUD::GetTextWidth(char const *)const 
public: int __thiscall HUD::Init(void)
public: int __thiscall HUD::LoadFont(char const *,int)
public: int __thiscall HUD::LoadFont(char const *,int,char const *)
public: static class String __cdecl HUD::StripColorInfo(class String)
public: unsigned char __thiscall HUD::GetTransparency(void)const 
public: void __thiscall HUD::Destroy(void)
public: void __thiscall HUD::DrawBorder(int,int,int,int)
public: void __thiscall HUD::DrawBossHealth(int)
public: void __thiscall HUD::DrawImageAndWait(char const *,long)
public: void __thiscall HUD::DrawQuad(class Texture *,float,float,float,float,unsigned long,float,float,float,float)
public: void __thiscall HUD::DrawQuad(struct HUD::QuadDef const &)
public: void __thiscall HUD::DrawQuadAlphaTest(class Texture *,float,float,float,float,unsigned char,float,float,float,float)
public: void __thiscall HUD::DrawQuadOverlay(class Texture *,float,float,float,float,unsigned long,float,float,float,float)
public: void __thiscall HUD::DrawQuadRotated(class Texture *,float,float,float,float,float,struct tagPOINT &,unsigned long)
public: void __thiscall HUD::DrawQuadShader(class Material *,float,float,float,float,struct MaterialInput *,unsigned long,float,float,float,float)
public: void __thiscall HUD::DrawRect(float,float,float,float,unsigned long)
public: void __thiscall HUD::DrawTiles(class Texture *,float,float,float,float,bool)
public: void __thiscall HUD::Print(int,int,char const *,unsigned long,bool,float,int,int)
public: void __thiscall HUD::Print(struct tagRECT &,char const *,unsigned long,bool,float,int,int)
public: void __thiscall HUD::RenderCleanup(void)
public: void __thiscall HUD::SetFont(char const *)
public: void __thiscall HUD::SetFont(char const *,char const *,int)
public: void __thiscall HUD::SetFont(char const *,int)
public: void __thiscall HUD::SetFont(int)
public: void __thiscall HUD::SetRenderState(void)
public: void __thiscall HUD::SetTransparency(unsigned char)
public: void __thiscall HUD::UseMultiColor(bool)
```

## InputSystem
```cpp
const InputSystem::`vftable'
protected: virtual void __thiscall InputSystem::FillKeyNamesTable(void)
protected: void __thiscall InputSystem::ProcessEvents(void)
protected: void __thiscall InputSystem::SendEvent(struct InputSystem::InputEvent const &)
public: __thiscall InputSystem::InputSystem(class InputSystem const &)
public: __thiscall InputSystem::InputSystem(struct HWND__ *)
public: bool __thiscall InputSystem::IsFireSwitched(void)const 
public: class DynamicArray<int> & __thiscall InputSystem::GetActiveKeyList(void)
public: class InputSystem & __thiscall InputSystem::operator=(class InputSystem const &)
public: class String __thiscall InputSystem::GetKeyName(int)
public: class String __thiscall InputSystem::GetKeyNameByEngName(class String)
public: class String __thiscall InputSystem::GetKeyNameEng(int)
public: class String __thiscall InputSystem::GetShortNameByEngName(class String)
public: int __thiscall InputSystem::GetKeyAction(int)
public: int __thiscall InputSystem::GetKeyByName(class String)
public: int __thiscall InputSystem::GetKeyByShortName(class String)
public: int __thiscall InputSystem::LoadBindings(void)
public: int __thiscall InputSystem::SaveBindings(void)
public: struct InputSystem::KeyInfo * __thiscall InputSystem::GetKeyTable(void)
public: unsigned int __thiscall InputSystem::GetAltPauseKey(void)const 
public: unsigned int __thiscall InputSystem::GetPauseKey(void)const 
public: unsigned long __thiscall InputSystem::GetActionStatus(void)const 
public: unsigned long __thiscall InputSystem::GetKeyStatus(int)const 
public: unsigned long __thiscall InputSystem::GetMouseStatus(void)const 
public: unsigned long __thiscall InputSystem::GetUIActionStatus(void)const 
public: virtual __thiscall InputSystem::~InputSystem(void)
public: virtual long __thiscall InputSystem::HandleMessages(struct HWND__ *,unsigned int,unsigned int,long)
public: virtual void __thiscall InputSystem::Release(void)
public: virtual void __thiscall InputSystem::Reset(void)
public: virtual void __thiscall InputSystem::SetWindow(struct HWND__ *)
public: void __thiscall InputSystem::BindKey(int,int,class String)
public: void __thiscall InputSystem::BindMessageKey(int,int)
public: void __thiscall InputSystem::ClearMessageBinding(int)
public: void __thiscall InputSystem::ClearUIBinding(unsigned int)
public: void __thiscall InputSystem::SetActionStatus(unsigned long)
public: void __thiscall InputSystem::SetLuaToDo(class String)
public: void __thiscall InputSystem::SetUIActionStatus(unsigned long)
public: void __thiscall InputSystem::ZeroBindings(void)
```

## Light
```cpp
const Light::`vftable'
public: __thiscall Light::Light(class Light const &)
public: __thiscall Light::Light(void)
public: class Light & __thiscall Light::operator=(class Light const &)
public: class Matrix __thiscall Light::GetProjMatrix(void)const 
public: class Vector __thiscall Light::GetDirection(void)const 
public: float __thiscall Light::GetAttIntensity(class Entity const *)const 
public: float __thiscall Light::GetConeAngleCos(void)const 
public: float __thiscall Light::GetRadius(void)const 
public: int __thiscall Light::GetType(void)const 
public: int __thiscall Light::Intersect(class BoundingBox const &)
public: virtual __thiscall Light::~Light(void)
public: virtual bool __thiscall Light::DemoReadEntity(class GFile *)
public: virtual bool __thiscall Light::DemoReadFull(class GFile *)
public: virtual bool __thiscall Light::DemoWriteEntity(class GFile *)
public: virtual bool __thiscall Light::DemoWriteFull(class GFile *)
public: virtual bool __thiscall Light::LoadEntity(class GFile *)
public: virtual bool __thiscall Light::SaveEntity(class GFile *)
public: virtual void __thiscall Light::Draw(class RenderDevice *,int)
public: virtual void __thiscall Light::EnableDraw(bool)
public: virtual void __thiscall Light::UpdateTransform(bool)
public: void __thiscall Light::EnableDynamic(bool)
public: void __thiscall Light::EnableFakeSpecular(bool)
public: void __thiscall Light::GetDirAndAttCol(class Entity const *,class Plane &,class Plane &)const 
public: void __thiscall Light::SetConeAngleCos(float)
public: void __thiscall Light::SetDirection(class Vector const &)
public: void __thiscall Light::SetFalloff(float)
public: void __thiscall Light::SetImportantDynamic(bool)
public: void __thiscall Light::SetProjectorTexture(class String const &)
public: void __thiscall Light::SetRadius(float)
public: void __thiscall Light::SetType(int)
public: void __thiscall Light::UpdateProj(void)
```

## LineRender
```cpp
private: void __thiscall LineRender::SetState(class RenderDevice *)
public: __thiscall LineRender::~LineRender(void)
public: __thiscall LineRender::LineRender(void)
public: class LineRender & __thiscall LineRender::operator=(class LineRender const &)
public: int __thiscall LineRender::Init(void)
public: void __thiscall LineRender::DrawAntiPortals(class DynamicArray<class AntiPortal *> const &,unsigned long)
public: void __thiscall LineRender::DrawBox(class BoundingBox const &,unsigned long)
public: void __thiscall LineRender::DrawDirLight(unsigned long,class Vector const &,class Vector const &)
public: void __thiscall LineRender::DrawEllipse(unsigned long,class Vector const &,class Vector const &,class Vector const &)
public: void __thiscall LineRender::DrawFilledBox(class BoundingBox const &,unsigned long,bool)
public: void __thiscall LineRender::DrawFilledPoly(class ConvexPolygon const &,unsigned long)
public: void __thiscall LineRender::DrawFloor(class Pathfinder2Floor const &,unsigned long)
public: void __thiscall LineRender::DrawFloor(class PathfinderFloor const &,unsigned long)
public: void __thiscall LineRender::DrawGlobalWaypointPath(class WaypointGPath const &)
public: void __thiscall LineRender::DrawLine(class Vector const &,class Vector const &,unsigned long,bool)
public: void __thiscall LineRender::DrawMesh(class WorldMesh const *,unsigned long)
public: void __thiscall LineRender::DrawPlane(class Plane const &)
public: void __thiscall LineRender::DrawPoly(class ConvexPolygon const &,unsigned long)
public: void __thiscall LineRender::DrawSphere(float,unsigned long,class Vector *,bool)
public: void __thiscall LineRender::DrawSphereCone(float,unsigned long,float,class Matrix *)
public: void __thiscall LineRender::DrawTranslucentBox(class BoundingBox const &,unsigned long)
public: void __thiscall LineRender::DrawTranslucentZones(class DynamicArray<class Zone *> const &,unsigned long)
public: void __thiscall LineRender::DrawTri(class Vector const * const,unsigned long)
public: void __thiscall LineRender::DrawWaypointPath(class WaypointPath const &)
public: void __thiscall LineRender::DrawWaypointSet(class WaypointSet const &)
public: void __thiscall LineRender::DrawWaypointSet2(void)
public: void __thiscall LineRender::DrawWireframe(class DynamicArray<struct SimpleVertex> const &,class Matrix const &,unsigned long)
public: void __thiscall LineRender::RenderCleanup(void)
```

## LoadingScreen
```cpp
private: void __thiscall LoadingScreen::DrawProgress(void)
public: __thiscall LoadingScreen::~LoadingScreen(void)
public: __thiscall LoadingScreen::LoadingScreen(class LoadingScreen const &)
public: __thiscall LoadingScreen::LoadingScreen(void)
public: bool __thiscall LoadingScreen::Active(void)const 
public: class LoadingScreen & __thiscall LoadingScreen::operator=(class LoadingScreen const &)
public: int __thiscall LoadingScreen::GetOverall(void)const 
public: int __thiscall LoadingScreen::GetProgress(void)const 
public: void __thiscall LoadingScreen::Activate(bool,int,class String,class String)
public: void __thiscall LoadingScreen::Progress(int)
public: void __thiscall LoadingScreen::Render(void)
public: void __thiscall LoadingScreen::SetIcon(int,int,int,class String)
public: void __thiscall LoadingScreen::SetOverall(int,float)
```

## LogBuffer
```cpp
public: __thiscall LogBuffer::~LogBuffer(void)
public: __thiscall LogBuffer::LogBuffer(char const *)
public: class LogBuffer & __thiscall LogBuffer::operator=(class LogBuffer const &)
public: void __cdecl LogBuffer::Print(char const *,...)
public: void __thiscall LogBuffer::`default constructor closure'(void)
public: void __thiscall LogBuffer::Close(void)
public: void __thiscall LogBuffer::Flush(void)
public: void __thiscall LogBuffer::Open(char const *)
public: void __thiscall LogBuffer::Print(class BoundingBox const &)
public: void __thiscall LogBuffer::Print(class Matrix const &)
public: void __thiscall LogBuffer::Print(class Vector const &)
public: void __thiscall LogBuffer::SetCallbackFunc(void (__cdecl*)(char const *))
```

## MagicBoard
```cpp
public: __thiscall MagicBoard::~MagicBoard(void)
public: __thiscall MagicBoard::MagicBoard(class MagicBoard const &)
public: __thiscall MagicBoard::MagicBoard(void)
public: bool __thiscall MagicBoard::Active(void)const 
public: bool __thiscall MagicBoard::AddOnEnabled(void)const 
public: bool __thiscall MagicBoard::CanPutDivineAway(void)
public: bool __thiscall MagicBoard::FindCardInGroup(int,int)
public: bool __thiscall MagicBoard::GetAllFree(void)const 
public: bool __thiscall MagicBoard::IsCardInSlot(int,int)
public: bool __thiscall MagicBoard::SendEvent(enum MenuEvent,int,void *)
public: class MagicBoard & __thiscall MagicBoard::operator=(class MagicBoard const &)
public: class MagicCard * __thiscall MagicBoard::GetZoomCard(void)const 
public: unsigned int __thiscall MagicBoard::GetCash(void)const 
public: void __thiscall MagicBoard::Activate(bool)
public: void __thiscall MagicBoard::AddCard(int,class String,class String,class String,int,bool,bool,class String)
public: void __thiscall MagicBoard::Clear(void)
public: void __thiscall MagicBoard::DrawCounter(void)
public: void __thiscall MagicBoard::MakeAllFree(bool)
public: void __thiscall MagicBoard::Render(void)
public: void __thiscall MagicBoard::SetAllFree(bool)
public: void __thiscall MagicBoard::SetBackground(class String,unsigned long)
public: void __thiscall MagicBoard::SetCash(unsigned int)
public: void __thiscall MagicBoard::SetCashCheat(unsigned int)
public: void __thiscall MagicBoard::SetMouseTexture(int,int,class String,unsigned long)
public: void __thiscall MagicBoard::SetSlotPosition(int,int,int)
public: void __thiscall MagicBoard::Setup(void)
public: void __thiscall MagicBoard::SetupSlots(int,int,int,int,int,int,int)
public: void __thiscall MagicBoard::ShowMouse(bool)
```

## MaterialSystem
```cpp
private: bool __thiscall MaterialSystem::TextureOnDisk(class String const &,class String &)const 
private: bool __thiscall MaterialSystem::TextureOnDiskExt(class String const &,class String &)const 
private: class RenderEffect * __thiscall MaterialSystem::LoadEffect(char const *)
private: int __thiscall MaterialSystem::LoadShader(char const *,int,struct StreamDef *)
private: void __thiscall MaterialSystem::CreateWarpTexture(void)
private: void __thiscall MaterialSystem::FreeTexture(class Texture *)
private: void __thiscall MaterialSystem::LoadEffects(void)
private: void __thiscall MaterialSystem::LoadScripts(void)
private: void __thiscall MaterialSystem::ReleaseEffects(void)
public: __thiscall MaterialSystem::~MaterialSystem(void)
public: __thiscall MaterialSystem::MaterialSystem(class MaterialSystem const &)
public: __thiscall MaterialSystem::MaterialSystem(void)
public: bool __thiscall MaterialSystem::IsTextureOnDisk(class String const &)const 
public: class Material * __thiscall MaterialSystem::CreateMaterial(char const *)
public: class Material * __thiscall MaterialSystem::FindMaterial(char const *)const 
public: class MaterialSystem & __thiscall MaterialSystem::operator=(class MaterialSystem const &)
public: class Texture * __thiscall MaterialSystem::CreateCubeMap(char const *,unsigned long,int)
public: class Texture * __thiscall MaterialSystem::CreateEmptyTexture(char const *,int,int,int,unsigned long)
public: class Texture * __thiscall MaterialSystem::CreateTexture(char const *,unsigned long,int)
public: class Texture * __thiscall MaterialSystem::FindTexture(char const *)const 
public: class Texture * __thiscall MaterialSystem::ReplaceTexture(char const *,char const *)
public: int __thiscall MaterialSystem::CreateShader(char const *,int)
public: int __thiscall MaterialSystem::Init(void)
public: int __thiscall MaterialSystem::ReloadTextures(void)
public: int __thiscall MaterialSystem::SetTextureLOD(void)
public: struct FXEntry * __thiscall MaterialSystem::FindEffect(char const *)const 
public: void __thiscall MaterialSystem::CleanupFrozenStuff(void)
public: void __thiscall MaterialSystem::EnumerateTextures(class DynamicArray<class String> &)
public: void __thiscall MaterialSystem::PreLoadAll(void)const 
public: void __thiscall MaterialSystem::ReleaseMaterial(class Material *,bool)
public: void __thiscall MaterialSystem::ReleaseTexture(class Texture *,bool)
public: void __thiscall MaterialSystem::SetTexFiltering(void)
public: void __thiscall MaterialSystem::SetTextureAnimAdvance(char const *,float)
public: void __thiscall MaterialSystem::SetTextureAnimNone(char const *)
public: void __thiscall MaterialSystem::Tick(float)
```

## MenuItem
```cpp
const MenuItem::`vftable'
protected: virtual void __thiscall MenuItem::DrawAt(int,int,unsigned long)
protected: virtual void __thiscall MenuItem::DrawBackground(void)
public: __thiscall MenuItem::MenuItem(class MenuItem const &)
public: __thiscall MenuItem::MenuItem(void)
public: bool __thiscall MenuItem::ApplyRequired(void)const 
public: bool __thiscall MenuItem::GetWarning(void)const 
public: bool __thiscall MenuItem::IsDisabled(void)const 
public: bool __thiscall MenuItem::IsPressed(void)const 
public: bool __thiscall MenuItem::IsVisible(void)const 
public: bool __thiscall MenuItem::PlayExitMovie(void)const 
public: bool __thiscall MenuItem::UnderMouse(void)const 
public: class MenuItem & __thiscall MenuItem::operator=(class MenuItem const &)
public: class String __thiscall MenuItem::GetAcceptSound(void)const 
public: class String __thiscall MenuItem::GetAction(void)const 
public: class String __thiscall MenuItem::GetBigFont(void)const 
public: class String __thiscall MenuItem::GetName(void)const 
public: class String __thiscall MenuItem::GetText(void)const 
public: enum MenuItemType __thiscall MenuItem::GetType(void)const 
public: int __thiscall MenuItem::ExitMovieEnd(void)const 
public: int __thiscall MenuItem::ExitMovieStart(void)const 
public: int __thiscall MenuItem::GetBigFontSize(void)const 
public: int __thiscall MenuItem::GetHeight(void)const 
public: int __thiscall MenuItem::GetPosX(void)const 
public: int __thiscall MenuItem::GetPosY(void)const 
public: int __thiscall MenuItem::GetWidth(void)const 
public: unsigned long __thiscall MenuItem::CalcTransColor(unsigned long)
public: unsigned long __thiscall MenuItem::GetTextColor(void)const 
public: virtual __thiscall MenuItem::~MenuItem(void)
public: virtual bool __thiscall MenuItem::CheckIfUnderMouse(void)
public: virtual bool __thiscall MenuItem::SendEvent(enum MenuEvent,int,void *)
public: virtual void __thiscall MenuItem::CalcPosition(void)
public: virtual void __thiscall MenuItem::CalcSize(void)
public: virtual void __thiscall MenuItem::Render(void)
public: virtual void __thiscall MenuItem::RenderDesc(void)
public: virtual void __thiscall MenuItem::SetAcceptSound(class String)
public: virtual void __thiscall MenuItem::SetBigFont(class String,int)
public: virtual void __thiscall MenuItem::SetBigFontTex(class String)
public: virtual void __thiscall MenuItem::SetColors(unsigned long,unsigned long,unsigned long,unsigned long)
public: virtual void __thiscall MenuItem::SetLightOnSound(class String)
public: virtual void __thiscall MenuItem::SetPosition(int,int)
public: virtual void __thiscall MenuItem::SetSmallFont(class String,int)
public: virtual void __thiscall MenuItem::SetSmallFontTex(class String)
public: virtual void __thiscall MenuItem::UpdateScale(void)
public: void __thiscall MenuItem::ApplyRequired(bool)
public: void __thiscall MenuItem::Disable(void)
public: void __thiscall MenuItem::DrawShadow(bool)
public: void __thiscall MenuItem::Enable(void)
public: void __thiscall MenuItem::PlayExitMovie(int,int)
public: void __thiscall MenuItem::SetAction(class String)
public: void __thiscall MenuItem::SetAlign(enum MenuAlign)
public: void __thiscall MenuItem::SetBackground(class String)
public: void __thiscall MenuItem::SetBGWidth(int)
public: void __thiscall MenuItem::SetConfigOption(class String)
public: void __thiscall MenuItem::SetDesc(class String)
public: void __thiscall MenuItem::SetDescPos(int,int)
public: void __thiscall MenuItem::SetHeight(int)
public: void __thiscall MenuItem::SetName(class String)
public: void __thiscall MenuItem::SetText(class String)
public: void __thiscall MenuItem::SetTransparency(unsigned char)
public: void __thiscall MenuItem::SetUnderMouse(bool)
public: void __thiscall MenuItem::SetVisibility(bool)
public: void __thiscall MenuItem::SetWarning(bool)
public: void __thiscall MenuItem::SetWidth(int)
```

## MenuItemBorder
```cpp
const MenuItemBorder::`vftable'
public: __thiscall MenuItemBorder::MenuItemBorder(class MenuItemBorder const &)
public: __thiscall MenuItemBorder::MenuItemBorder(void)
public: class MenuItemBorder & __thiscall MenuItemBorder::operator=(class MenuItemBorder const &)
public: class MenuItemScroller * __thiscall MenuItemBorder::GetScroller(void)const 
public: virtual __thiscall MenuItemBorder::~MenuItemBorder(void)
public: virtual void __thiscall MenuItemBorder::Clear(void)
public: virtual void __thiscall MenuItemBorder::Render(void)
public: void __thiscall MenuItemBorder::AddScroller(class MenuItemScroller *)
public: void __thiscall MenuItemBorder::RemoveScroller(void)
public: void __thiscall MenuItemBorder::SetColumnsCount(int)
public: void __thiscall MenuItemBorder::SetColumnWidth(int,int)
public: void __thiscall MenuItemBorder::SetDark(bool)
public: void __thiscall MenuItemBorder::SetHeader(int)
public: void __thiscall MenuItemBorder::SetSize(int,int)
```

## MenuItemCheckbox
```cpp
const MenuItemCheckbox::`vftable'
public: __thiscall MenuItemCheckbox::MenuItemCheckbox(class MenuItemCheckbox const &)
public: __thiscall MenuItemCheckbox::MenuItemCheckbox(void)
public: class MenuItemCheckbox & __thiscall MenuItemCheckbox::operator=(class MenuItemCheckbox const &)
public: virtual __thiscall MenuItemCheckbox::~MenuItemCheckbox(void)
public: virtual bool __thiscall MenuItemCheckbox::SendEvent(enum MenuEvent,int,void *)
public: virtual void __thiscall MenuItemCheckbox::CalcPosition(void)
public: virtual void __thiscall MenuItemCheckbox::CalcSize(void)
public: virtual void __thiscall MenuItemCheckbox::Render(void)
public: void __thiscall MenuItemCheckbox::SetTextureChecked(class String,unsigned long,unsigned long)
public: void __thiscall MenuItemCheckbox::SetTextureUnchecked(class String,unsigned long,unsigned long)
```

## MenuItemList
```cpp
const MenuItemList::`vftable'
public: __thiscall MenuItemList::MenuItemList(class MenuItemList const &)
public: __thiscall MenuItemList::MenuItemList(void)
public: bool __thiscall MenuItemList::GetHeaderUse(void)const 
public: class MenuItemBorder * __thiscall MenuItemList::GetBorder(void)const 
public: class MenuItemList & __thiscall MenuItemList::operator=(class MenuItemList const &)
public: class MenuItemScroller * __thiscall MenuItemList::GetScroller(void)const 
public: int __thiscall MenuItemList::GetDoubleClickedIndex(void)const 
public: int __thiscall MenuItemList::GetMaxHeight(void)const 
public: int __thiscall MenuItemList::GetNumItems(void)const 
public: int __thiscall MenuItemList::GetSelectedIndex(void)const 
public: struct MenuItemList::MenuListItem * * __thiscall MenuItemList::GetItems(void)const 
public: struct MenuItemList::MenuListItem * __thiscall MenuItemList::GetDoubleClicked(void)const 
public: struct MenuItemList::MenuListItem * __thiscall MenuItemList::GetFirst(void)const 
public: struct MenuItemList::MenuListItem * __thiscall MenuItemList::GetItem(int)const 
public: struct MenuItemList::MenuListItem * __thiscall MenuItemList::GetSelected(void)const 
public: virtual __thiscall MenuItemList::~MenuItemList(void)
public: virtual bool __thiscall MenuItemList::SendEvent(enum MenuEvent,int,void *)
public: virtual void __thiscall MenuItemList::AddItem(class String,void *)
public: virtual void __thiscall MenuItemList::CalcPosition(void)
public: virtual void __thiscall MenuItemList::CalcSize(void)
public: virtual void __thiscall MenuItemList::DrawElem(struct MenuItemList::MenuListItem *,int,int,bool)
public: virtual void __thiscall MenuItemList::MoveItemDown(int)
public: virtual void __thiscall MenuItemList::MoveItemUp(int)
public: virtual void __thiscall MenuItemList::RemoveItem(int)
public: virtual void __thiscall MenuItemList::Render(void)
public: virtual void __thiscall MenuItemList::SetColors(unsigned long,unsigned long,unsigned long,unsigned long)
public: virtual void __thiscall MenuItemList::SetMaxHeight(int)
public: virtual void __thiscall MenuItemList::SetPosition(int,int)
public: void __thiscall MenuItemList::RemoveAll(void)
public: void __thiscall MenuItemList::SetBorderWidth(int)
public: void __thiscall MenuItemList::SetHeaderUse(bool)
```

## MenuItemMapTable
```cpp
const MenuItemMapTable::`vftable'
public: __thiscall MenuItemMapTable::MenuItemMapTable(class MenuItemMapTable const &)
public: __thiscall MenuItemMapTable::MenuItemMapTable(void)
public: class MenuItemList * __thiscall MenuItemMapTable::GetServerMapList(void)const 
public: class MenuItemMapTable & __thiscall MenuItemMapTable::operator=(class MenuItemMapTable const &)
public: class String __thiscall MenuItemMapTable::GetSelectedMap(void)
public: virtual __thiscall MenuItemMapTable::~MenuItemMapTable(void)
public: virtual bool __thiscall MenuItemMapTable::SendEvent(enum MenuEvent,int,void *)
public: virtual void __thiscall MenuItemMapTable::Render(void)
public: virtual void __thiscall MenuItemMapTable::SetBigFont(class String,int)
public: virtual void __thiscall MenuItemMapTable::SetColors(unsigned long,unsigned long,unsigned long,unsigned long)
public: virtual void __thiscall MenuItemMapTable::SetMaxHeight(int)
public: virtual void __thiscall MenuItemMapTable::SetPosition(int,int)
public: virtual void __thiscall MenuItemMapTable::SetSmallFont(class String,int)
public: void __thiscall MenuItemMapTable::AddAllMapsToServer(void)
public: void __thiscall MenuItemMapTable::AddMapToServer(struct MenuItemList::MenuListItem *)
public: void __thiscall MenuItemMapTable::FillMapList(void)
public: void __thiscall MenuItemMapTable::RemoveAllMapsFromServer(void)
public: void __thiscall MenuItemMapTable::RemoveMap(class String)
public: void __thiscall MenuItemMapTable::RemoveMapFromServer(int)
public: void __thiscall MenuItemMapTable::Update(class String)
```

## MenuItemNumEdit
```cpp
const MenuItemNumEdit::`vftable'
public: __thiscall MenuItemNumEdit::MenuItemNumEdit(class MenuItemNumEdit const &)
public: __thiscall MenuItemNumEdit::MenuItemNumEdit(void)
public: class MenuItemNumEdit & __thiscall MenuItemNumEdit::operator=(class MenuItemNumEdit const &)
public: virtual __thiscall MenuItemNumEdit::~MenuItemNumEdit(void)
public: virtual bool __thiscall MenuItemNumEdit::SendEvent(enum MenuEvent,int,void *)
```

## MenuItemPassword
```cpp
const MenuItemPassword::`vftable'
public: __thiscall MenuItemPassword::MenuItemPassword(class MenuItemPassword const &)
public: __thiscall MenuItemPassword::MenuItemPassword(void)
public: class MenuItemPassword & __thiscall MenuItemPassword::operator=(class MenuItemPassword const &)
public: virtual __thiscall MenuItemPassword::~MenuItemPassword(void)
public: virtual void __thiscall MenuItemPassword::Render(void)
```

## MenuItemScroller
```cpp
const MenuItemScroller::`vftable'
protected: int __thiscall MenuItemScroller::CheckMouse(void)
public: __thiscall MenuItemScroller::MenuItemScroller(class MenuItemScroller const &)
public: __thiscall MenuItemScroller::MenuItemScroller(void)
public: class MenuItemScroller & __thiscall MenuItemScroller::operator=(class MenuItemScroller const &)
public: int __thiscall MenuItemScroller::GetMax(void)const 
public: int __thiscall MenuItemScroller::GetScrollerHeight(void)const 
public: int __thiscall MenuItemScroller::GetValue(void)const 
public: virtual __thiscall MenuItemScroller::~MenuItemScroller(void)
public: virtual bool __thiscall MenuItemScroller::SendEvent(enum MenuEvent,int,void *)
public: virtual void __thiscall MenuItemScroller::CalcPosition(void)
public: virtual void __thiscall MenuItemScroller::CalcSize(void)
public: virtual void __thiscall MenuItemScroller::Render(void)
public: void __thiscall MenuItemScroller::ScrollDown(void)
public: void __thiscall MenuItemScroller::ScrollUp(void)
public: void __thiscall MenuItemScroller::SetMinMax(int,int)
public: void __thiscall MenuItemScroller::SetScrollerHeight(int)
public: void __thiscall MenuItemScroller::SetValue(int)
```

## MenuItemSlider
```cpp
const MenuItemSlider::`vftable'
protected: int __thiscall MenuItemSlider::CheckMouse(void)
public: __thiscall MenuItemSlider::MenuItemSlider(class MenuItemSlider const &)
public: __thiscall MenuItemSlider::MenuItemSlider(void)
public: bool __thiscall MenuItemSlider::GetFullWidth(void)const 
public: bool __thiscall MenuItemSlider::IsFloat(void)const 
public: class MenuItemSlider & __thiscall MenuItemSlider::operator=(class MenuItemSlider const &)
public: int __thiscall MenuItemSlider::GetValue(void)const 
public: virtual __thiscall MenuItemSlider::~MenuItemSlider(void)
public: virtual bool __thiscall MenuItemSlider::SendEvent(enum MenuEvent,int,void *)
public: virtual void __thiscall MenuItemSlider::CalcPosition(void)
public: virtual void __thiscall MenuItemSlider::CalcSize(void)
public: virtual void __thiscall MenuItemSlider::Render(void)
public: void __thiscall MenuItemSlider::SetControlWidth(int)
public: void __thiscall MenuItemSlider::SetFullWidth(bool)
public: void __thiscall MenuItemSlider::SetMinMax(int,int,bool)
public: void __thiscall MenuItemSlider::SetSliderWidth(int)
public: void __thiscall MenuItemSlider::SetValue(int)
```

## MenuItemTabGroup
```cpp
const MenuItemTabGroup::`vftable'
public: __thiscall MenuItemTabGroup::MenuItemTabGroup(class MenuItemTabGroup const &)
public: __thiscall MenuItemTabGroup::MenuItemTabGroup(void)
public: bool __thiscall MenuItemTabGroup::IsActive(void)const 
public: class MenuItemTabGroup & __thiscall MenuItemTabGroup::operator=(class MenuItemTabGroup const &)
public: virtual __thiscall MenuItemTabGroup::~MenuItemTabGroup(void)
public: virtual void __thiscall MenuItemTabGroup::Clear(void)
public: virtual void __thiscall MenuItemTabGroup::Render(void)
public: void __thiscall MenuItemTabGroup::Activate(bool)
public: void __thiscall MenuItemTabGroup::AddItem(class MenuItem *)
```

## MenuItemTextButtonEx
```cpp
const MenuItemTextButtonEx::`vftable'
public: __thiscall MenuItemTextButtonEx::MenuItemTextButtonEx(class MenuItemTextButtonEx const &)
public: __thiscall MenuItemTextButtonEx::MenuItemTextButtonEx(void)
public: class MenuItemTextButtonEx & __thiscall MenuItemTextButtonEx::operator=(class MenuItemTextButtonEx const &)
public: virtual __thiscall MenuItemTextButtonEx::~MenuItemTextButtonEx(void)
public: virtual bool __thiscall MenuItemTextButtonEx::SendEvent(enum MenuEvent,int,void *)
public: void __thiscall MenuItemTextButtonEx::ChangeValue(class String)
public: void __thiscall MenuItemTextButtonEx::SetOriginalText(class String)
```

## MenuItemTextEdit
```cpp
const MenuItemTextEdit::`vftable'
public: __thiscall MenuItemTextEdit::MenuItemTextEdit(class MenuItemTextEdit const &)
public: __thiscall MenuItemTextEdit::MenuItemTextEdit(void)
public: bool __thiscall MenuItemTextEdit::IsInEditMode(void)const 
public: class MenuItemTextEdit & __thiscall MenuItemTextEdit::operator=(class MenuItemTextEdit const &)
public: class String __thiscall MenuItemTextEdit::GetEditText(void)const 
public: class String __thiscall MenuItemTextEdit::GetEnterAction(void)const 
public: int __thiscall MenuItemTextEdit::GetTextLength(void)
public: virtual __thiscall MenuItemTextEdit::~MenuItemTextEdit(void)
public: virtual bool __thiscall MenuItemTextEdit::SendEvent(enum MenuEvent,int,void *)
public: virtual void __thiscall MenuItemTextEdit::CalcSize(void)
public: virtual void __thiscall MenuItemTextEdit::Render(void)
public: void __thiscall MenuItemTextEdit::FindUnderCursorColor(void)
public: void __thiscall MenuItemTextEdit::OnBackspace(void)
public: void __thiscall MenuItemTextEdit::OnCursorLeft(void)
public: void __thiscall MenuItemTextEdit::OnCursorRight(void)
public: void __thiscall MenuItemTextEdit::OnDelete(void)
public: void __thiscall MenuItemTextEdit::OnEnd(void)
public: void __thiscall MenuItemTextEdit::OnEnter(void)
public: void __thiscall MenuItemTextEdit::OnHome(void)
public: void __thiscall MenuItemTextEdit::OnPrintable(unsigned char,bool)
public: void __thiscall MenuItemTextEdit::SetCurrColor(int)
public: void __thiscall MenuItemTextEdit::SetEditText(class String)
public: void __thiscall MenuItemTextEdit::SetEnterAction(class String)
public: void __thiscall MenuItemTextEdit::SetMaxLength(int)
```

## MenuScreen
```cpp
public: __thiscall MenuScreen::~MenuScreen(void)
public: __thiscall MenuScreen::MenuScreen(class MenuScreen const &)
public: __thiscall MenuScreen::MenuScreen(void)
public: bool __thiscall MenuScreen::GetActiveTextEdit(void)const 
public: bool __thiscall MenuScreen::IsKeyInUse(int)
public: bool __thiscall MenuScreen::PlayingExitMovie(void)const 
public: bool __thiscall MenuScreen::PlayingMovie(void)
public: bool __thiscall MenuScreen::SendEvent(enum MenuEvent,int,void *)
public: class MenuItem * __thiscall MenuScreen::AddBorder(class String)
public: class MenuItem * __thiscall MenuScreen::AddCharPicker(class String,class String,class String)
public: class MenuItem * __thiscall MenuScreen::AddCheckbox(class String,class String,class String)
public: class MenuItem * __thiscall MenuScreen::AddColorPicker(class String,class String,class String)
public: class MenuItem * __thiscall MenuScreen::AddImageButton(class String,class String,class String,class String,class String,class String)
public: class MenuItem * __thiscall MenuScreen::AddImageButtonEx(class String,class String,class String,int)
public: class MenuItem * __thiscall MenuScreen::AddKeyControl(class String,class String,class String,class String,class String,class String,class String,class String)
public: class MenuItem * __thiscall MenuScreen::AddKeyList(class String,bool)
public: class MenuItem * __thiscall MenuScreen::AddList(class String,bool)
public: class MenuItem * __thiscall MenuScreen::AddLoadSave(class String,bool)
public: class MenuItem * __thiscall MenuScreen::AddMapTable(class String)
public: class MenuItem * __thiscall MenuScreen::AddNumEdit(class String,class String,class String,int,class String)
public: class MenuItem * __thiscall MenuScreen::AddNumRange(class String,class String,class String,int,int,int)
public: class MenuItem * __thiscall MenuScreen::AddPassword(class String,class String,class String,int,class String)
public: class MenuItem * __thiscall MenuScreen::AddPlayerModel(class String)
public: class MenuItem * __thiscall MenuScreen::AddScroller(class String,class String,class String,int,int,int,int)
public: class MenuItem * __thiscall MenuScreen::AddServerList(class String,class String,class String,bool,bool)
public: class MenuItem * __thiscall MenuScreen::AddSimpleKeyConf(class String,class String,class String,int)
public: class MenuItem * __thiscall MenuScreen::AddSlider(class String,class String,class String,int,int,bool,int,int,int)
public: class MenuItem * __thiscall MenuScreen::AddSliderImage(class String,class String,class String,int,int,bool,int,int,int)
public: class MenuItem * __thiscall MenuScreen::AddStaticText(class String,class String)
public: class MenuItem * __thiscall MenuScreen::AddTabGroup(class String)
public: class MenuItem * __thiscall MenuScreen::AddTextButton(class String,class String,class String)
public: class MenuItem * __thiscall MenuScreen::AddTextButtonEx(class String,class String,class String,class String)
public: class MenuItem * __thiscall MenuScreen::AddTextEdit(class String,class String,class String,int,class String)
public: class MenuItem * __thiscall MenuScreen::AddWeaponList(class String,bool)
public: class MenuItem * __thiscall MenuScreen::FindItem(class String)
public: class MenuItemScroller * __thiscall MenuScreen::GetScroller(void)const 
public: class MenuScreen & __thiscall MenuScreen::operator=(class MenuScreen const &)
public: class Texture * __thiscall MenuScreen::GetBackground(void)const 
public: int __thiscall MenuScreen::GetFirstShown(void)const 
public: int __thiscall MenuScreen::GetMenuWidth(void)const 
public: int __thiscall MenuScreen::GetTopPos(void)const 
public: void * __thiscall MenuScreen::GetSelectedServer(void)const 
public: void __thiscall MenuScreen::AcceptActiveTextEdit(void)
public: void __thiscall MenuScreen::Activate(void)
public: void __thiscall MenuScreen::Clear(bool)
public: void __thiscall MenuScreen::ClearKey(int)
public: void __thiscall MenuScreen::ClearKeyConfig(class String)
public: void __thiscall MenuScreen::PlayExitMovie(int,int,class String)
public: void __thiscall MenuScreen::Render(void)
public: void __thiscall MenuScreen::SetActiveTextEdit(bool)
public: void __thiscall MenuScreen::SetBackground(char const *,enum MenuBGType)
public: void __thiscall MenuScreen::SetFirstShown(int)
public: void __thiscall MenuScreen::SetItemAction(class String)
public: void __thiscall MenuScreen::SetItemsDrawShadow(bool)
public: void __thiscall MenuScreen::SetItemsFadeLength(int)
public: void __thiscall MenuScreen::SetMenuWidth(int)
public: void __thiscall MenuScreen::SetMovieLoop(int,int)
public: void __thiscall MenuScreen::SetScroller(class MenuItemScroller *)
public: void __thiscall MenuScreen::SetSelectedServer(void *)
public: void __thiscall MenuScreen::SetShowItemsFrame(int)
public: void __thiscall MenuScreen::SetTopPos(int)
public: void __thiscall MenuScreen::SetWaitTime(float)
public: void __thiscall MenuScreen::StopMovie(void)
public: void __thiscall MenuScreen::UpdateKeyConfig(void)
public: void __thiscall MenuScreen::UpdateMovie(void)
```

## MeshPackVertNTU
```cpp
public: __thiscall MeshPackVertNTU::MeshPackVertNTU(struct MeshPackVertNTU const &)
public: __thiscall MeshPackVertNTU::MeshPackVertNTU(struct MeshVertex const &,class Plane const &)
public: __thiscall MeshPackVertNTU::MeshPackVertNTU(void)
public: struct MeshPackVertNTU & __thiscall MeshPackVertNTU::operator=(struct MeshPackVertNTU const &)
```

## MeshPackVertTU2
```cpp
public: __thiscall MeshPackVertTU2::MeshPackVertTU2(struct MeshPackVertTU2 const &)
public: __thiscall MeshPackVertTU2::MeshPackVertTU2(struct MeshVertex const &,class Vector const &)
public: __thiscall MeshPackVertTU2::MeshPackVertTU2(void)
public: struct MeshPackVertTU2 & __thiscall MeshPackVertTU2::operator=(struct MeshPackVertTU2 const &)
```

## MeshShape
```cpp
const MeshShape::`vftable'
protected: unsigned short __thiscall MeshShape::RedirectBone(unsigned short)
public: __thiscall MeshShape::MeshShape(class MeshShape const &)
public: __thiscall MeshShape::MeshShape(void)
public: class MeshShape & __thiscall MeshShape::operator=(class MeshShape const &)
public: virtual __thiscall MeshShape::~MeshShape(void)
public: void __thiscall MeshShape::SetupVertexWeights(void)
```

## MeshVertex
```cpp
public: __thiscall MeshVertex::MeshVertex(class Vector const &,class Vector const &,class TexCoord const &)
public: __thiscall MeshVertex::MeshVertex(class Vector const &,unsigned long,class TexCoord const &,class TexCoord const &)
public: __thiscall MeshVertex::MeshVertex(struct MeshVertex const &)
public: __thiscall MeshVertex::MeshVertex(void)
public: struct MeshVertex & __thiscall MeshVertex::operator=(struct MeshVertex const &)
```

## MessageHandler
```cpp
const MessageHandler::`vftable'
protected: static long __stdcall MessageHandler::GWindowProc(struct HWND__ *,unsigned int,unsigned int,long)
protected: static void __cdecl MessageHandler::InsertHandler(class MessageHandler *)
protected: static void __cdecl MessageHandler::RemoveHandler(class MessageHandler *)
public: __thiscall MessageHandler::~MessageHandler(void)
public: __thiscall MessageHandler::MessageHandler(class MessageHandler const &)
public: __thiscall MessageHandler::MessageHandler(struct HWND__ *)
public: class MessageHandler & __thiscall MessageHandler::operator=(class MessageHandler const &)
public: struct HWND__ * __thiscall MessageHandler::GetHandle(void)const 
public: void __thiscall MessageHandler::`default constructor closure'(void)
```

## MilesEngine
```cpp
private: bool __thiscall MilesEngine::Sound3D_HasAlwaysSameSpeed(int)
private: bool __thiscall MilesEngine::Sound3D_IsObstructed(int)
private: bool __thiscall MilesEngine::Start2DSample(class Miles2DSound *,int)
private: bool __thiscall MilesEngine::Start3DSample(class Miles3DSound *,int)
private: bool __thiscall MilesEngine::StreamingSound_IsPlaying(int)
private: class Miles2DSound * __thiscall MilesEngine::Find2DSoundToStart(void)
private: class Miles3DSound * __thiscall MilesEngine::Find3DSoundToStart(class MilesLoadedFile *)
private: class MilesLoadedFile * __thiscall MilesEngine::GetEmptyFileByName(char const *)
private: class MilesLoadedFile * __thiscall MilesEngine::GetFileByName(char const *)
private: class MilesLoadedFile * __thiscall MilesEngine::PrepareSample(char const *)
private: class MilesLoadedFile * __thiscall MilesEngine::SureGetFile(char const *)
private: class Vector __thiscall MilesEngine::GetPlayerPosition(void)
private: class Vector __thiscall MilesEngine::GetPlayerVelocity(void)
private: float __thiscall MilesEngine::Get3DDigitalEffectsVolume(void)
private: float __thiscall MilesEngine::GetMasterVolumeLevel(void)
private: float __thiscall MilesEngine::Sound2D_GetVolume(int)
private: float __thiscall MilesEngine::Sound3D_GetVolume(int)
private: int __thiscall MilesEngine::Find2DSoundToStop(void)
private: int __thiscall MilesEngine::Find3DSoundToStop(float &,class MilesLoadedFile *)
private: int __thiscall MilesEngine::GetLongestPlaying2DSoundForFile(class MilesLoadedFile *)
private: int __thiscall MilesEngine::StreamingSound_GetLoopCount(int)
private: void * __thiscall MilesEngine::LoadAndUnpackFile(char const *)
private: void __thiscall MilesEngine::Enumerate3DSoundProviders(void)
private: void __thiscall MilesEngine::GetPlayerOrientation(class Vector &,class Vector &)
private: void __thiscall MilesEngine::GetStreamingVolume(float &)
private: void __thiscall MilesEngine::Release2DSample(int,class Miles2DSound *,bool)
private: void __thiscall MilesEngine::Release3DSample(int,class Miles3DSound *,bool)
private: void __thiscall MilesEngine::Release3DSoundProvider(void)
private: void __thiscall MilesEngine::ResetFlagsFromPauseStack(void)
private: void __thiscall MilesEngine::SetDefaultSoundProperties(int,unsigned long)
private: void __thiscall MilesEngine::SetDopplerFactor(float)
private: void __thiscall MilesEngine::SetPrivilegedSoundsVolumeMultiplier(float)
private: void __thiscall MilesEngine::TryToPlayRealSound(class Miles3DSound *)
private: void __thiscall MilesEngine::TryToPlayRealSound2D(class Miles2DSound *)
private: void __thiscall MilesEngine::UpdateAllSoundsToPrivileged(void)
private: void __thiscall MilesEngine::UpdateSpeed(void)
public: __thiscall MilesEngine::~MilesEngine(void)
public: __thiscall MilesEngine::MilesEngine(class MilesEngine const &)
public: __thiscall MilesEngine::MilesEngine(void)
public: bool __thiscall MilesEngine::GetDefaultObstruction(void)
public: bool __thiscall MilesEngine::LoadAudio(class GFile *)
public: bool __thiscall MilesEngine::SaveAudio(class GFile *)
public: bool __thiscall MilesEngine::Set3DSoundProvider(class String)
public: bool __thiscall MilesEngine::Set3DSoundProvider(int)
public: bool __thiscall MilesEngine::SetFirstSuccesfulProvider(int)
public: bool __thiscall MilesEngine::SetMostSuitableProvider(void)
public: bool __thiscall MilesEngine::Sound2D_IsPlaying(int)
public: bool __thiscall MilesEngine::Sound2D_Save(int,class GFile *)
public: bool __thiscall MilesEngine::Sound3D_IsPlaying(int)
public: bool __thiscall MilesEngine::Sound3D_Save(int,class GFile *)
public: bool __thiscall MilesEngine::StartedOk(void)
public: class MilesEngine & __thiscall MilesEngine::operator=(class MilesEngine const &)
public: class String __thiscall MilesEngine::GetProviderName(int)
public: float __thiscall MilesEngine::GetCurrent2DIntensity(void)const 
public: float __thiscall MilesEngine::GetCurrent3DIntensity(void)const 
public: float __thiscall MilesEngine::GetFalloffSpeed(void)
public: float __thiscall MilesEngine::Sound2D_GetLowPass(int)
public: float __thiscall MilesEngine::Sound3D_GetLowPass(int)
public: float __thiscall MilesEngine::StreamingSound_GetLowPass(int)
public: int __thiscall MilesEngine::GetCurrentRoomType(void)const 
public: int __thiscall MilesEngine::GetCurrentSoundProvider(void)
public: int __thiscall MilesEngine::NumOfProviders(void)
public: int __thiscall MilesEngine::PauseCurrentlyPlayingSounds(void)
public: int __thiscall MilesEngine::Sound2D_Create(char const *,unsigned char)
public: int __thiscall MilesEngine::Sound2D_Create(class GFile *)
public: int __thiscall MilesEngine::Sound2D_CreateEx(char const *,unsigned char,float,bool,bool,bool,float,bool)
public: int __thiscall MilesEngine::Sound3D_Create(char const *,unsigned char)
public: int __thiscall MilesEngine::Sound3D_Create(class GFile *)
public: int __thiscall MilesEngine::Sound3D_CreateEx(char const *,unsigned char,bool,float,class Vector,float,float,float,bool,bool,bool,float)
public: int __thiscall MilesEngine::StreamingSound_Create(char const *)
public: struct _DIG_DRIVER * __thiscall MilesEngine::GetDigitalDriver(void)const 
public: void __thiscall MilesEngine::Dump(void)
public: void __thiscall MilesEngine::GetSoundCounts(int &,int &,int &,int &,int &,int &)
public: void __thiscall MilesEngine::LoadFilesOffMemory(void)
public: void __thiscall MilesEngine::LoadReset(void)
public: void __thiscall MilesEngine::PreloadFile(char const *)
public: void __thiscall MilesEngine::Reset(void)
public: void __thiscall MilesEngine::ResumeSounds(int)
public: void __thiscall MilesEngine::SaveGame_PauseSounds(void)
public: void __thiscall MilesEngine::SaveGame_ResumeSounds(void)
public: void __thiscall MilesEngine::Set3DDigitalEffectsVolume(float)
public: void __thiscall MilesEngine::SetAllSoundsToLowPass(float)
public: void __thiscall MilesEngine::SetAudioRecorder(class AudioRecorder *)
public: void __thiscall MilesEngine::SetDefaultObstruction(bool)
public: void __thiscall MilesEngine::SetFalloffSpeed(float)
public: void __thiscall MilesEngine::SetLowPass(float)
public: void __thiscall MilesEngine::SetMasterVolumeLevel(float)
public: void __thiscall MilesEngine::SetObstructionProperties(float,float,float,float,float,float,float)
public: void __thiscall MilesEngine::SetPlayerOrientation(class Vector &,class Vector &)
public: void __thiscall MilesEngine::SetPlayerPosition(class Vector &)
public: void __thiscall MilesEngine::SetPlayerVelocity(class Vector &)
public: void __thiscall MilesEngine::SetRoom(int,bool)
public: void __thiscall MilesEngine::SetRoomIntensity2D(float,float)
public: void __thiscall MilesEngine::SetRoomIntensity3D(float,float)
public: void __thiscall MilesEngine::SetSoundProperties(class String,int,int)
public: void __thiscall MilesEngine::SetSoundSpeedRandomizer(float)
public: void __thiscall MilesEngine::SetSpeakersFlip(bool)
public: void __thiscall MilesEngine::SetSpeakerType(int)
public: void __thiscall MilesEngine::SetSpeed(float,float)
public: void __thiscall MilesEngine::SetStreamingVolume(float)
public: void __thiscall MilesEngine::Sound2D_Delete(int)
public: void __thiscall MilesEngine::Sound2D_Forget(int)
public: void __thiscall MilesEngine::Sound2D_NoSpeedRandomize(int)
public: void __thiscall MilesEngine::Sound2D_Play(int)
public: void __thiscall MilesEngine::Sound2D_PlayAndForget(int)
public: void __thiscall MilesEngine::Sound2D_Resume(int)
public: void __thiscall MilesEngine::Sound2D_SetAlwaysSameSpeed(int,bool)
public: void __thiscall MilesEngine::Sound2D_SetDontSave(int)
public: void __thiscall MilesEngine::Sound2D_SetLoopCount(int,int)
public: void __thiscall MilesEngine::Sound2D_SetLowPass(int,float)
public: void __thiscall MilesEngine::Sound2D_SetSoundSpeed(int,float)
public: void __thiscall MilesEngine::Sound2D_SetVolume(int,float,float)
public: void __thiscall MilesEngine::Sound2D_Stop(int)
public: void __thiscall MilesEngine::Sound3D_Delete(int)
public: void __thiscall MilesEngine::Sound3D_Forget(int)
public: void __thiscall MilesEngine::Sound3D_Play(int)
public: void __thiscall MilesEngine::Sound3D_PlayAndForget(int)
public: void __thiscall MilesEngine::Sound3D_Resume(int)
public: void __thiscall MilesEngine::Sound3D_SetDontSave(int)
public: void __thiscall MilesEngine::Sound3D_SetHearingDistance(int,float,float)
public: void __thiscall MilesEngine::Sound3D_SetIntensity(int,float,float)
public: void __thiscall MilesEngine::Sound3D_SetLoopCount(int,int)
public: void __thiscall MilesEngine::Sound3D_SetLowPass(int,float)
public: void __thiscall MilesEngine::Sound3D_SetNoSpeedRandomize(int)
public: void __thiscall MilesEngine::Sound3D_SetObstructed(int,bool)
public: void __thiscall MilesEngine::Sound3D_SetOrientation(int,class Vector &,class Vector &)
public: void __thiscall MilesEngine::Sound3D_SetPosition(int,class Vector &)
public: void __thiscall MilesEngine::Sound3D_SetSoundSpeed(int,float)
public: void __thiscall MilesEngine::Sound3D_SetVelocity(int,class Vector &)
public: void __thiscall MilesEngine::Sound3D_SetVolume(int,float,float)
public: void __thiscall MilesEngine::Sound3D_Stop(int)
public: void __thiscall MilesEngine::StopAllSounds(void)
public: void __thiscall MilesEngine::StopFinishedSounds(void)
public: void __thiscall MilesEngine::StreamingSound_Delete(int)
public: void __thiscall MilesEngine::StreamingSound_GetVolume(int,float &)
public: void __thiscall MilesEngine::StreamingSound_Play(int)
public: void __thiscall MilesEngine::StreamingSound_Resume(int)
public: void __thiscall MilesEngine::StreamingSound_SetLoopCount(int,int)
public: void __thiscall MilesEngine::StreamingSound_SetLowPass(int,float)
public: void __thiscall MilesEngine::StreamingSound_SetVolume(int,float)
public: void __thiscall MilesEngine::StreamingSound_Stop(int)
public: void __thiscall MilesEngine::Tick(float)
```

## Model
```cpp
const Model::`vftable'
protected: void __thiscall Model::ProcessRagdoll(void)
protected: void __thiscall Model::RecomputeLocalBB(void)
protected: void __thiscall Model::RenderJoint(unsigned long,class Vector const &,class Vector const &,float,float)
protected: void __thiscall Model::RenderSphere(unsigned long,class Matrix const &,float)
public: __thiscall Model::Model(class Model const &)
public: __thiscall Model::Model(void)
public: bool __thiscall Model::ComputeFrameMatrix(class DynamicArray<class Matrix> &,class Matrix const &)
public: bool __thiscall Model::IsRagdoll(void)
public: class Matrix __thiscall Model::GetJointTransform(int)
public: class Model & __thiscall Model::operator=(class Model const &)
public: class String __thiscall Model::GetJointName(int)const 
public: class Vector __thiscall Model::GetAnimationMovement(int,float)const 
public: float __thiscall Model::GetAnimationFrameTime(int)const 
public: float __thiscall Model::GetAnimationTimeScale(int)
public: float __thiscall Model::GetAnimationTotalTime(int)const 
public: int __thiscall Model::ActivateAnimation(int,bool,float,float,bool)
public: int __thiscall Model::FindAnimation(class String const &)const 
public: int __thiscall Model::GetJointIndex(char const *)const 
public: int __thiscall Model::IsPerPixel(void)const 
public: int __thiscall Model::Load(char const *,float,class AnimatedMeshManager &)
public: int __thiscall Model::LoadAnimation(char const *,bool,int)
public: int __thiscall Model::SetAnimationMovementCurve(int,class String const &,unsigned long)
public: virtual __thiscall Model::~Model(void)
public: virtual bool __thiscall Model::DemoCalcSize(int &,unsigned long &)
public: virtual bool __thiscall Model::DemoReadEntity(class GFile *)
public: virtual bool __thiscall Model::DemoReadFull(class GFile *)
public: virtual bool __thiscall Model::DemoWriteEntity(class GFile *)
public: virtual bool __thiscall Model::DemoWriteFull(class GFile *)
public: virtual bool __thiscall Model::LoadEntity(class GFile *)
public: virtual bool __thiscall Model::SaveEntity(class GFile *)
public: virtual class Vector __thiscall Model::GetCenter(void)const 
public: virtual void __thiscall Model::Draw(class RenderDevice *,int)
public: virtual void __thiscall Model::RenderCleanup(class RenderDevice *,class MaterialSystem *)
public: virtual void __thiscall Model::SetScale(float)
public: virtual void __thiscall Model::Tick(float)
public: virtual void __thiscall Model::UpdateTransform(bool)
public: virtual void __thiscall Model::UpdateVolEntity(void)
public: void __thiscall Model::ApplyJointRotation(int,float,float,float)
public: void __thiscall Model::CreateRagdollIfNone(void)
public: void __thiscall Model::CreateShadowMap(bool)
public: void __thiscall Model::DrawJointNames(unsigned long)
public: void __thiscall Model::DrawSkeleton(class RenderDevice *)
public: void __thiscall Model::ResetMaterialSpecular(void)
public: void __thiscall Model::SetAnimationFrameTime(int,float)
public: void __thiscall Model::SetAnimationTimeScale(int,float)
public: void __thiscall Model::SetBlendAlpha(float)
public: void __thiscall Model::SetMaterial(class String const &)
public: void __thiscall Model::SetMaterialRefractFresnel(class String const &,float,float,class Vector const &,class Vector const &)
public: void __thiscall Model::SetMaterialSpecular(class String const &,class Vector const &,float)
public: void __thiscall Model::SetMeshLighting(class String const &,bool,class Vector const &)
public: void __thiscall Model::SetMeshVisibility(class String const &,bool)
public: void __thiscall Model::SetRagdoll(bool,int)
public: void __thiscall Model::SetTexture(class String const &,class String const &)
public: void __thiscall Model::SetupGib(class Model *)
public: void __thiscall Model::SetWaterImpact(int,class Vector const &,float,float,float,float)
public: void __thiscall Model::UpdateWaterImpact(int,class Vector const &,float,float,float,float)
```

## Movie
```cpp
public: __thiscall Movie::~Movie(void)
public: __thiscall Movie::Movie(void)
public: bool __thiscall Movie::Draw(void)
public: bool __thiscall Movie::IsPlaying(void)const 
public: bool __thiscall Movie::LoopEnabled(void)const 
public: bool __thiscall Movie::Open(char const *,bool,int)
public: bool __thiscall Movie::Play(bool)
public: bool __thiscall Movie::PlayFrames(int,int,bool)
public: bool __thiscall Movie::Seek(int,bool)
public: bool __thiscall Movie::Update(void)
public: class Movie & __thiscall Movie::operator=(class Movie const &)
public: class Texture * __thiscall Movie::GetTexture(void)const 
public: int __thiscall Movie::GetCurrentFrame(void)const 
public: void __thiscall Movie::Close(void)
public: void __thiscall Movie::Loop(bool)
public: void __thiscall Movie::Pause(bool)
public: void __thiscall Movie::SetPosition(int,int)
public: void __thiscall Movie::Stop(void)
```

## NetworkDevice2
```cpp
private: bool __thiscall NetworkDevice2::LowLayer_Open(unsigned short,bool,class String)
private: bool __thiscall NetworkDevice2::LowLayer_Read(class IpAddress &,class UdpMessage *)
private: bool __thiscall NetworkDevice2::LowLayer_SendMaintenanceMessage(class IpAddress &,class UdpMessage *)
private: bool __thiscall NetworkDevice2::LowLayer_Write(class IpAddress const &,class UdpMessage *)
private: bool __thiscall NetworkDevice2::ReadyForNetOpen(class IpAddress *)
private: bool __thiscall NetworkDevice2::TestBandwidthControl(void)
private: class NetworkPeer2 * __thiscall NetworkDevice2::ClientPeer(void)
private: class NetworkPeer2 * __thiscall NetworkDevice2::LowLayer_FindPeerByAddress(class IpAddress &)
private: void __thiscall NetworkDevice2::ClearDispatchedMessages(void)
private: void __thiscall NetworkDevice2::ClearScriptMessagesList(void)
private: void __thiscall NetworkDevice2::DistributeScriptMessages(void)
private: void __thiscall NetworkDevice2::HandleCommand_Challenge(class IpAddress &,class UdpMessage *)
private: void __thiscall NetworkDevice2::HandleCommand_ClientConnect(class IpAddress &,class UdpMessage *)
private: void __thiscall NetworkDevice2::HandleCommand_Connect(class IpAddress &,class UdpMessage *)
private: void __thiscall NetworkDevice2::HandleCommand_Disconnect(class IpAddress &)
private: void __thiscall NetworkDevice2::HandleCommand_Error(class IpAddress &,class UdpMessage *)
private: void __thiscall NetworkDevice2::HandleCommand_GetChallenge(class IpAddress &)
private: void __thiscall NetworkDevice2::LowLayer_Close(void)
private: void __thiscall NetworkDevice2::LowLayer_ConnectAsClient(class IpAddress &)
private: void __thiscall NetworkDevice2::LowLayer_ConnectAsClient(void *)
private: void __thiscall NetworkDevice2::LowLayer_Disconnect(void)
private: void __thiscall NetworkDevice2::LowLayer_GameSpyCDKEYCallback(int,int,char *)
private: void __thiscall NetworkDevice2::LowLayer_HandleNetworkDevicePackage(class UdpMessage *,class IpAddress &)
private: void __thiscall NetworkDevice2::LowLayer_StartServer(void)
private: void __thiscall NetworkDevice2::UpdateBandwidthControl(unsigned long)
public: __thiscall NetworkDevice2::~NetworkDevice2(void)
public: __thiscall NetworkDevice2::NetworkDevice2(class NetworkDevice2 const &)
public: __thiscall NetworkDevice2::NetworkDevice2(void)
public: bool __thiscall NetworkDevice2::CanConnectNewClient(bool)
public: bool __thiscall NetworkDevice2::IsClient(void)
public: bool __thiscall NetworkDevice2::IsServer(void)
public: bool __thiscall NetworkDevice2::IsSpectator(unsigned char)
public: bool __thiscall NetworkDevice2::NetworkClientAndConnect(char const *,class String,unsigned short,bool)
public: bool __thiscall NetworkDevice2::NetworkClientAndConnect(void *,class String,bool)
public: bool __thiscall NetworkDevice2::NetworkServer(class String,class String,class String,unsigned short)
public: bool __thiscall NetworkDevice2::ScriptsSecure(unsigned long &)
public: class Entity * __thiscall NetworkDevice2::GetEntityFromNumber(unsigned long)
public: class Entity * __thiscall NetworkDevice2::GetLocalPlayer(void)
public: class Entity * __thiscall NetworkDevice2::GetPlayerByIndex(int)
public: class NetworkDevice2 & __thiscall NetworkDevice2::operator=(class NetworkDevice2 const &)
public: class String __thiscall NetworkDevice2::GetMyIP(int *)
public: class String __thiscall NetworkDevice2::GetNetstat(void)
public: double __thiscall NetworkDevice2::Ping(unsigned char)
public: float __thiscall NetworkDevice2::PacketLoss(unsigned char)
public: long __thiscall NetworkDevice2::GetBandwidthControl(void)
public: unsigned char __thiscall NetworkDevice2::GetClientID(void)
public: unsigned long __thiscall NetworkDevice2::GetEntityNumber(class Entity *)
public: unsigned long __thiscall NetworkDevice2::GetLastFrameLatency(void)
public: void __thiscall NetworkDevice2::BanClient(unsigned char)
public: void __thiscall NetworkDevice2::DisconnectClient(unsigned char)
public: void __thiscall NetworkDevice2::GetLastUpdateStats(unsigned long &,unsigned long &)
public: void __thiscall NetworkDevice2::InterpretVariable(unsigned char,class String &,class String &)
public: void __thiscall NetworkDevice2::LoadNewMap(class String)
public: void __thiscall NetworkDevice2::NetworkClose(void)
public: void __thiscall NetworkDevice2::OnActivateAnimation(class Model *,int,bool,float,float,bool)
public: void __thiscall NetworkDevice2::OnLoadAnimation(class Model *,class String,bool,int)
public: void __thiscall NetworkDevice2::PingReset(unsigned char)
public: void __thiscall NetworkDevice2::PostTick(void)
public: void __thiscall NetworkDevice2::PreTick(float)
public: void __thiscall NetworkDevice2::SendScriptMessage(class UdpMessage *,bool,bool,unsigned char)
public: void __thiscall NetworkDevice2::SetBandwidthControl(long)
public: void __thiscall NetworkDevice2::SetCDKeyAndIsPublic(class String,bool)
public: void __thiscall NetworkDevice2::SetServerFramerate(int)
public: void __thiscall NetworkDevice2::SetSpectator(unsigned char,bool)
public: void __thiscall NetworkDevice2::SetStatsNumberToAverage(int)
public: void __thiscall NetworkDevice2::SetStatsUpdateDelay(unsigned long)
public: void __thiscall NetworkDevice2::SetSynchroState(class Entity *,unsigned char)
public: void __thiscall NetworkDevice2::TransferVariable(unsigned char,class String &,class String &)
```

## Object
```cpp
const Object::`vftable'
protected: __thiscall Object::Object(char const *)
protected: __thiscall Object::Object(void)
public: __thiscall Object::Object(class Object const &)
public: class Object & __thiscall Object::operator=(class Object const &)
public: virtual __thiscall Object::~Object(void)
public: virtual char const * __thiscall Object::GetClassNameA(void)const 
public: virtual void __thiscall Object::Load(class GFile *)
public: virtual void __thiscall Object::Save(class GFile *)
public: void __thiscall Object::RemoveFromPackage(void)
```

## ObjectEntry
```cpp
public: __thiscall ObjectEntry::ObjectEntry(void)
public: bool __thiscall ObjectEntry::InThisPackage(void)const 
public: struct ObjectEntry & __thiscall ObjectEntry::operator=(struct ObjectEntry const &)
```

## ObjectPackage
```cpp
private: int __thiscall ObjectPackage::AddName(char const *)
public: __thiscall ObjectPackage::~ObjectPackage(void)
public: __thiscall ObjectPackage::ObjectPackage(char const *)
public: __thiscall ObjectPackage::ObjectPackage(class ObjectPackage const &)
public: char const * __thiscall ObjectPackage::GetName(void)const 
public: class Object * __thiscall ObjectPackage::CreateObject(char const *,char const *)
public: class Object * __thiscall ObjectPackage::GetInstance(char const *,char const *,char const *)
public: class Object * __thiscall ObjectPackage::GetInstance(int,char const *)
public: class ObjectPackage & __thiscall ObjectPackage::operator=(class ObjectPackage const &)
public: class String __thiscall ObjectPackage::GetObjectName(int)const 
public: int __thiscall ObjectPackage::GetInstanceNum(void)const 
public: void __thiscall ObjectPackage::AddRefObject(class Object *)
public: void __thiscall ObjectPackage::Delete(class Object *)
public: void __thiscall ObjectPackage::Dump(class LogBuffer &)
public: void __thiscall ObjectPackage::RemoveInstance(class Object *)
public: void __thiscall ObjectPackage::RemoveObject(class Object *)
public: void __thiscall ObjectPackage::Save(char const *)
```

## PainMenu
```cpp
public: __thiscall PainMenu::~PainMenu(void)
public: __thiscall PainMenu::PainMenu(class PainMenu const &)
public: __thiscall PainMenu::PainMenu(void)
public: bool __thiscall PainMenu::Active(void)const 
public: bool __thiscall PainMenu::SendEvent(enum MenuEvent,int,void *)
public: class PainMenu & __thiscall PainMenu::operator=(class PainMenu const &)
public: void __thiscall PainMenu::Activate(bool)
public: void __thiscall PainMenu::Clear(void)
public: void __thiscall PainMenu::EnableSplash(bool)
public: void __thiscall PainMenu::PauseSounds(void)
public: void __thiscall PainMenu::PlayMenuSound(char const *,bool)
public: void __thiscall PainMenu::Render(void)
public: void __thiscall PainMenu::ResetRoomType(void)
public: void __thiscall PainMenu::ResumeSounds(void)
public: void __thiscall PainMenu::SetMouseTexture(int,int,class String,unsigned long)
public: void __thiscall PainMenu::ShowCredits(bool)
public: void __thiscall PainMenu::ShowMouse(bool)
public: void __thiscall PainMenu::StopSound(void)
```

## Particle
```cpp
public: __thiscall Particle::Particle(struct Particle const &)
public: __thiscall Particle::Particle(void)
public: struct Particle & __thiscall Particle::operator=(struct Particle const &)
```

## ParticleEffect
```cpp
const ParticleEffect::`vftable'
public: __thiscall ParticleEffect::EmitterDef::EmitterDef(struct ParticleEffect::EmitterDef const &)
public: __thiscall ParticleEffect::EmitterDef::EmitterDef(void)
public: __thiscall ParticleEffect::ParticleEffect(class ParticleEffect const &)
public: __thiscall ParticleEffect::ParticleEffect(void)
public: class ParticleEffect & __thiscall ParticleEffect::operator=(class ParticleEffect const &)
public: int __thiscall ParticleEffect::AddEmitter(class ParticleEmitter *)
public: int __thiscall ParticleEffect::IsActive(void)const 
public: struct ParticleEffect::EmitterDef & __thiscall ParticleEffect::EmitterDef::operator=(struct ParticleEffect::EmitterDef const &)
public: virtual __thiscall ParticleEffect::~ParticleEffect(void)
public: virtual bool __thiscall ParticleEffect::DemoReadEntity(class GFile *)
public: virtual bool __thiscall ParticleEffect::DemoReadFull(class GFile *)
public: virtual bool __thiscall ParticleEffect::DemoWriteEntity(class GFile *)
public: virtual bool __thiscall ParticleEffect::DemoWriteFull(class GFile *)
public: virtual bool __thiscall ParticleEffect::LoadEntity(class GFile *)
public: virtual bool __thiscall ParticleEffect::OnUnregister(void)
public: virtual bool __thiscall ParticleEffect::SaveEntity(class GFile *)
public: virtual void __thiscall ParticleEffect::Draw(class RenderDevice *,int)
public: virtual void __thiscall ParticleEffect::RenderCleanup(class RenderDevice *,class MaterialSystem *)
public: virtual void __thiscall ParticleEffect::RenderInitialize(class RenderDevice *,class MaterialSystem *,char const *)
public: virtual void __thiscall ParticleEffect::SetPosition(class Vector const &)
public: virtual void __thiscall ParticleEffect::SetRotation(class Quaternion const &)
public: virtual void __thiscall ParticleEffect::SetScale(float)
public: virtual void __thiscall ParticleEffect::Tick(float)
public: void __thiscall ParticleEffect::Deactivate(int)
public: void __thiscall ParticleEffect::EmitterDef::SetupTransform(void)
public: void __thiscall ParticleEffect::RemoveEmitter(int)
public: void __thiscall ParticleEffect::Restart(void)
public: void __thiscall ParticleEffect::SetParentOffset(class Vector const &,int,class Vector const &)
public: void __thiscall ParticleEffect::SetupEmitters(bool)
```

## ParticleEmitter
```cpp
public: __thiscall ParticleEmitter::~ParticleEmitter(void)
public: __thiscall ParticleEmitter::ParticleEmitter(class ParticleEmitter const &)
public: __thiscall ParticleEmitter::ParticleEmitter(void)
public: class ParticleEmitter & __thiscall ParticleEmitter::operator=(class ParticleEmitter const &)
public: int __thiscall ParticleEmitter::IsActive(void)const 
public: void __thiscall ParticleEmitter::Deactivate(int)
public: void __thiscall ParticleEmitter::InitParticle(struct Particle *)
public: void __thiscall ParticleEmitter::Restart(void)
public: void __thiscall ParticleEmitter::SetScale(float)
public: void __thiscall ParticleEmitter::Tick(float)
```

## ParticlePool
```cpp
public: __thiscall ParticlePool::ParticlePool(class ParticlePool const &)
public: __thiscall ParticlePool::ParticlePool(void)
public: class ParticlePool & __thiscall ParticlePool::operator=(class ParticlePool const &)
public: struct Particle * __thiscall ParticlePool::Alloc(void)
public: void __thiscall ParticlePool::Free(struct Particle *)
```

## ParticleSystem
```cpp
public: __thiscall ParticleSystem::~ParticleSystem(void)
public: __thiscall ParticleSystem::ParticleSystem(class ParticleSystem const &)
public: __thiscall ParticleSystem::ParticleSystem(void)
public: class ParticleEmitter * __thiscall ParticleSystem::CreateEmitter(char const *,bool)
public: class ParticleEmitter * __thiscall ParticleSystem::GetMasterEmitter(char const *)
public: class ParticleSystem & __thiscall ParticleSystem::operator=(class ParticleSystem const &)
public: class VertexBuffer<struct SimpleVertex,262,20000> & __thiscall ParticleSystem::GetVBuf(void)
public: int __thiscall ParticleSystem::Init(void)
public: int __thiscall ParticleSystem::LoadEmitter(class ParticleEmitter &,class String const &)
public: int __thiscall ParticleSystem::SaveEmitter(class ParticleEmitter const &,class String const &)
public: unsigned short __thiscall ParticleSystem::GetEBuf(void)
public: void __thiscall ParticleSystem::Load(class GFile *)
public: void __thiscall ParticleSystem::RenderCleanup(void)
public: void __thiscall ParticleSystem::RenderSprites(class Camera const &,struct Sprite const &,bool)
public: void __thiscall ParticleSystem::RenderSprites(class Camera const &,struct Sprite1DOF const &)
public: void __thiscall ParticleSystem::ResetState(void)
public: void __thiscall ParticleSystem::Save(class GFile *)
public: void __thiscall ParticleSystem::SetState(void)
public: void __thiscall ParticleSystem::UpdateAllEmittersWithMaster(class ParticleEmitter const *)
```

## Pathfinder
```cpp
protected: class PathfinderFloorSet * __thiscall Pathfinder::GetCurrentFloorSet(void)
protected: float __thiscall Pathfinder::GetShortestPortalPath(class PathfinderPortalNode *,class PathfinderPortalNode *,class DynamicArray<class PathfinderPortalNode *> &,int,int)
protected: void __thiscall Pathfinder::DeterminePortalNodesWideness(void)
protected: void __thiscall Pathfinder::RemoveWaypointsOutsideOfZones(float)
public: __thiscall Pathfinder::~Pathfinder(void)
public: __thiscall Pathfinder::Pathfinder(class Pathfinder const &)
public: __thiscall Pathfinder::Pathfinder(void)
public: class Pathfinder & __thiscall Pathfinder::operator=(class Pathfinder const &)
public: class PathfinderPortalNode & __thiscall Pathfinder::GetNodeForPortal(int)
public: class Vector __thiscall Pathfinder::GetWaypointPosition(int,int)
public: class WaypointSet & __thiscall Pathfinder::GetWaypointsInZone(int)
public: class WaypointSet * __thiscall Pathfinder::GetCurrentWaypointSet(void)
public: class World * __thiscall Pathfinder::GetWorld(void)
public: float __thiscall Pathfinder::GetShortestPath(class Vector,class Vector,float (__cdecl*)(class Waypoint &,class Waypoint &,class Waypoint &),class WaypointGPath &,int)
public: float __thiscall Pathfinder::LocalGetShortestPath(int,int *,int,float (__cdecl*)(class Waypoint &,class Waypoint &,class Waypoint &),class WaypointPath &)
public: int __thiscall Pathfinder::AddWaypoint(class Waypoint &)
public: int __thiscall Pathfinder::GetWaypointPaths(int,int)
public: int __thiscall Pathfinder::HowMany(void)
public: int __thiscall Pathfinder::LoadContents(char const *,float,bool)
public: int __thiscall Pathfinder::LoadFloors(char const *,float)
public: int __thiscall Pathfinder::PickWaypoint(int,int,float)
public: int __thiscall Pathfinder::PreparePortalNodes(float)
public: int __thiscall Pathfinder::SaveContents(char const *,float)
public: int __thiscall Pathfinder::SaveFloors(char const *,float)
public: int __thiscall Pathfinder::Selection(float,struct tagRECT *,int)
public: int __thiscall Pathfinder::TestStructuresCorectness(void)
public: void __thiscall Pathfinder::AddGridOnSelectedFloors(float)
public: void __thiscall Pathfinder::CleanStructures(void)
public: void __thiscall Pathfinder::ClearContents(void)
public: void __thiscall Pathfinder::ClearPortalNodes(void)
public: void __thiscall Pathfinder::ClearWaypointSets(void)
public: void __thiscall Pathfinder::ConnectDisconnectSelected(float,float,int,int)
public: void __thiscall Pathfinder::ConnectSelected(void)
public: void __thiscall Pathfinder::CopySelected(void)
public: void __thiscall Pathfinder::DisconnectSelected(void)
public: void __thiscall Pathfinder::DisplaceSelected(class Vector const &)
public: void __thiscall Pathfinder::FlipZ(void)
public: void __thiscall Pathfinder::FloorSelectedSetClearAll(int)
public: void __thiscall Pathfinder::FloorSelection(struct tagRECT *,int)
public: void __thiscall Pathfinder::GetClosestWaypoint(class Vector,int &,int &,float &)
public: void __thiscall Pathfinder::GetWaypointPath(int,int,int,int &,float &)
public: void __thiscall Pathfinder::Init(class World *)
public: void __thiscall Pathfinder::InvertSelection(void)
public: void __thiscall Pathfinder::LevelWaypointsWithFloors(float)
public: void __thiscall Pathfinder::MoveSelectedToZoneTheyreIn(void)
public: void __thiscall Pathfinder::MoveWaypoint(class Waypoint &,class Vector,int)
public: void __thiscall Pathfinder::MoveWaypoint(int,class Vector,int)
public: void __thiscall Pathfinder::PrepareFloors(void)
public: void __thiscall Pathfinder::RecalculateDistances(int)
public: void __thiscall Pathfinder::RecalculateSelected(void)
public: void __thiscall Pathfinder::RemoveConnectionsCollidingWithGeometryInSelected(void)
public: void __thiscall Pathfinder::RemoveSelected(void)
public: void __thiscall Pathfinder::RemoveSelectedFloors(void)
public: void __thiscall Pathfinder::RemoveWaypoint(class Waypoint &)
public: void __thiscall Pathfinder::RemoveWaypoint(int)
public: void __thiscall Pathfinder::Render(int,int)
public: void __thiscall Pathfinder::ScaleContents(float)
public: void __thiscall Pathfinder::ScaleSelected(class Vector const &,float)
public: void __thiscall Pathfinder::SelectUnselectFloorsOfAreaLowerHigherThan(int,int,float)
public: void __thiscall Pathfinder::SelectUnselectWaypointsOnSelectedFloors(int)
public: void __thiscall Pathfinder::SelectWaypointsNotConnectedToAnything(void)
public: void __thiscall Pathfinder::SelectWaypointsNotConnectedToAnythingInCurrentRoom(void)
public: void __thiscall Pathfinder::SelectWaypointsOutsideOfCurrentZone(float)
public: void __thiscall Pathfinder::SetSelectedAsForAllMonsters(void)
public: void __thiscall Pathfinder::SetSelectedAsForSmallMonstersOnly(void)
public: void __thiscall Pathfinder::SetViewport(class Viewport *)
public: void __thiscall Pathfinder::Test_InitWithWaypointGrid(int)
public: void __thiscall Pathfinder::UnselectAllWaypointsInPathfinder(void)
public: void __thiscall Pathfinder::WaypointSelectedSetClearAll(int)
```

## Pathfinder2
```cpp
protected: class Vector __thiscall Pathfinder2::CenterCalculation(class Pathfinder2Portal *,class Vector &,class Vector &)
protected: class Waypoint2 * __thiscall Pathfinder2::GetNextAvailableFrom(int,int &,int &,float &)
protected: float __thiscall Pathfinder2::GetShortestPortalPath(int,int,class DynamicArray<int> &,int,float)
protected: int __thiscall Pathfinder2::GetClosestInPortal(class Pathfinder2Portal *,class Vector &,int)
protected: void __thiscall Pathfinder2::AssertTwoWayPaths(void)
protected: void __thiscall Pathfinder2::CalculateBorder(int)
protected: void __thiscall Pathfinder2::CalculateBorders(void)
protected: void __thiscall Pathfinder2::ClearUndo(int)
protected: void __thiscall Pathfinder2::GeneratePortal(int,int,class DynamicArray<int> &)
protected: void __thiscall Pathfinder2::ImportFromOldPathfinderFromWpSet(class WaypointSet *,int)
protected: void __thiscall Pathfinder2::MakeUndo(int,class Vector &)
protected: void __thiscall Pathfinder2::SortWaypoints(void)
public: __thiscall Pathfinder2::~Pathfinder2(void)
public: __thiscall Pathfinder2::Pathfinder2(class Pathfinder2 const &)
public: __thiscall Pathfinder2::Pathfinder2(void)
public: bool __thiscall Pathfinder2::Connected(int,int)
public: bool __thiscall Pathfinder2::LoadContents(char const *,float,bool)
public: bool __thiscall Pathfinder2::LoadFloors(char const *,float)
public: bool __thiscall Pathfinder2::LoadPathfinding(class GFile *)
public: bool __thiscall Pathfinder2::MergeContents(char const *,float)
public: bool __thiscall Pathfinder2::SaveContents(char const *,float,bool)
public: bool __thiscall Pathfinder2::SaveFloors(char const *,float)
public: bool __thiscall Pathfinder2::SavePathfinding(class GFile *)
public: bool __thiscall Pathfinder2::SetEnabled(int)
public: bool __thiscall Pathfinder2::WaypointEnabled(int)
public: class Pathfinder2 & __thiscall Pathfinder2::operator=(class Pathfinder2 const &)
public: class Waypoint2 & __thiscall Pathfinder2::operator[](int)const 
public: float __thiscall Pathfinder2::Dist(int,int)
public: float __thiscall Pathfinder2::GetShortestPath(class Vector,class Vector,class WaypointGPath2 &)
public: float __thiscall Pathfinder2::GetShortestPath(int,int *,int,class WaypointPath2 *,bool,class Vector *,float,int)
public: float __thiscall Pathfinder2::GetViewLimits(void)
public: int __thiscall Pathfinder2::AddWaypoint(class Waypoint2)
public: int __thiscall Pathfinder2::ConnectionCollidingWithGeometry(class Vector const &,class Vector const &)
public: int __thiscall Pathfinder2::FastGetWaypointIndex(class Waypoint2 *)
public: int __thiscall Pathfinder2::GetIndexOfWaypointClosestTo(class Vector const &,float)
public: int __thiscall Pathfinder2::GetWaypointByPosition(class Vector &)
public: int __thiscall Pathfinder2::GetWaypointIndex(class Waypoint2 &)
public: int __thiscall Pathfinder2::GetWaypointPaths(int)
public: int __thiscall Pathfinder2::HowManySets(void)
public: int __thiscall Pathfinder2::NumberOfWaypoints(void)
public: int __thiscall Pathfinder2::PickWaypoint(class Viewport &,int,int,float,bool)
public: int __thiscall Pathfinder2::Selection(class Viewport &,float,struct tagRECT *,bool)
public: short __thiscall Pathfinder2::GetCurrentSet(void)
public: short __thiscall Pathfinder2::GetValidCurrentSet(void)
public: void __thiscall Pathfinder2::AddConnection(int,int)
public: void __thiscall Pathfinder2::ApplyUndo(void)
public: void __thiscall Pathfinder2::ChangeViewLimits(bool)
public: void __thiscall Pathfinder2::ClearAutomaticStructures(void)
public: void __thiscall Pathfinder2::ClearContents(void)
public: void __thiscall Pathfinder2::ContractCurrentSet(void)
public: void __thiscall Pathfinder2::EnableAllWaypoints(bool)
public: void __thiscall Pathfinder2::EnableInBox(class BoundingBox &,bool)
public: void __thiscall Pathfinder2::EnableSet(unsigned short,bool)
public: void __thiscall Pathfinder2::EnableSets(bool)
public: void __thiscall Pathfinder2::EnableWaypoint(int,bool)
public: void __thiscall Pathfinder2::ExpandCurrentSet(void)
public: void __thiscall Pathfinder2::FastPickCurrentSet(class Viewport &,int,int,float)
public: void __thiscall Pathfinder2::FlipZ(void)
public: void __thiscall Pathfinder2::FloorSelection(class Viewport const &,struct tagRECT *,bool)
public: void __thiscall Pathfinder2::GenerateAutomaticStructures(void)
public: void __thiscall Pathfinder2::GetClosestWaypoint(class Vector,int &,float &)
public: void __thiscall Pathfinder2::GetColorsFromScript(void)
public: void __thiscall Pathfinder2::GetCurrentSetFromSelected(void)
public: void __thiscall Pathfinder2::GetWaypointPath(int,int,int &,float &)
public: void __thiscall Pathfinder2::ImportFromOldPathfinder(void)
public: void __thiscall Pathfinder2::MakeNewSetFromSelected(void)
public: void __thiscall Pathfinder2::MassAddConnection(class DynamicArray<int> &,class DynamicArray<int> &)
public: void __thiscall Pathfinder2::MassRemoveConnection(class DynamicArray<int> &,class DynamicArray<int> &)
public: void __thiscall Pathfinder2::MergeSetsFromSelected(void)
public: void __thiscall Pathfinder2::MergeWaypointsBelowDistance(float)
public: void __thiscall Pathfinder2::MoveWaypoint(class Waypoint2 &,class Vector,bool)
public: void __thiscall Pathfinder2::MoveWaypoint(int,class Vector,bool)
public: void __thiscall Pathfinder2::PrepareFloors(void)
public: void __thiscall Pathfinder2::RandomizeSets(void)
public: void __thiscall Pathfinder2::RecalculateAllDistances(void)
public: void __thiscall Pathfinder2::RecalculateDistances(int)
public: void __thiscall Pathfinder2::RemoveConnection(int,int)
public: void __thiscall Pathfinder2::RemoveWaypoint(class Waypoint2 &)
public: void __thiscall Pathfinder2::RemoveWaypoint(int)
public: void __thiscall Pathfinder2::Render(int,int)
public: void __thiscall Pathfinder2::ScaleContents(float)
public: void __thiscall Pathfinder2::Select(int,bool)
public: void __thiscall Pathfinder2::Select_All(bool)
public: void __thiscall Pathfinder2::Select_AllInSet(bool,unsigned short)
public: void __thiscall Pathfinder2::Select_FloorsOfAreaLowerHigherThan(bool,bool,float)
public: void __thiscall Pathfinder2::Select_Invert(void)
public: void __thiscall Pathfinder2::Select_NotConnectedToAnything(void)
public: void __thiscall Pathfinder2::Select_OnSelectedFloors(bool)
public: void __thiscall Pathfinder2::Selected_Connect(void)
public: void __thiscall Pathfinder2::Selected_ConnectDisconnect(float,float,int,int)
public: void __thiscall Pathfinder2::Selected_Copy(class Vector const &)
public: void __thiscall Pathfinder2::Selected_Disconnect(void)
public: void __thiscall Pathfinder2::Selected_Displace(class Vector const &)
public: void __thiscall Pathfinder2::Selected_MoveToCurrentSet(void)
public: void __thiscall Pathfinder2::Selected_Recalculate(void)
public: void __thiscall Pathfinder2::Selected_Remove(void)
public: void __thiscall Pathfinder2::Selected_RemoveConnectionsCollidingWithGeometry(float)
public: void __thiscall Pathfinder2::Selected_Scale(class Vector const &,float)
public: void __thiscall Pathfinder2::SelectedFloors_AddGrid(float)
public: void __thiscall Pathfinder2::SelectedFloors_Remove(void)
public: void __thiscall Pathfinder2::SwitchSetEnabling(class Viewport &,int,int,float)
public: void __thiscall Pathfinder2::ValidateSet(unsigned short)
public: void __thiscall Pathfinder2::ValidateSets(void)
```

## PCFSystem
```cpp
private: void __thiscall PCFSystem::ParseCommandLine(char const *)
public: __thiscall PCFSystem::~PCFSystem(void)
public: __thiscall PCFSystem::PCFSystem(class PCFSystem const &)
public: __thiscall PCFSystem::PCFSystem(void)
public: bool __thiscall PCFSystem::InitVideo(void)
public: bool __thiscall PCFSystem::InServerMode(void)
public: bool __thiscall PCFSystem::LoadGame(class GFile *)
public: bool __thiscall PCFSystem::SaveGame(class GFile *)
public: bool __thiscall PCFSystem::ZoneAlarmNetworkAccessPopUp(void)
public: class PCFSystem & __thiscall PCFSystem::operator=(class PCFSystem const &)
public: class RenderDevice * __thiscall PCFSystem::GetRenderDevice(void)const 
public: int __thiscall PCFSystem::GetGameState(void)
public: int __thiscall PCFSystem::Initialize(struct HINSTANCE__ *,char const *)
public: struct HINSTANCE__ * __thiscall PCFSystem::GetInstance(void)const 
public: void __thiscall PCFSystem::Close(void)
public: void __thiscall PCFSystem::ResetTimer(void)
public: void __thiscall PCFSystem::RunLoop(void)
public: void __thiscall PCFSystem::SetTimeMultiplier(double)
public: void __thiscall PCFSystem::SwitchToState(int)
public: void __thiscall PCFSystem::TakeScreenshot(void)const 
public: void __thiscall PCFSystem::TickEngine(bool)
```

## PhysicsEngine
```cpp
public: __thiscall PhysicsEngine::~PhysicsEngine(void)
public: __thiscall PhysicsEngine::PhysicsEngine(class PhysicsEngine const &)
public: __thiscall PhysicsEngine::PhysicsEngine(void)
public: bool __thiscall PhysicsEngine::LoadPhysics(class GFile *)
public: bool __thiscall PhysicsEngine::SavePhysics(class GFile *)
public: bool __thiscall PhysicsEngine::Tick(float,bool)
public: class PhysicsEngine & __thiscall PhysicsEngine::operator=(class PhysicsEngine const &)
public: class PhysicsWorld * __thiscall PhysicsEngine::GetWorldFromRigidBody(void *)
public: class PhysicsWorld * __thiscall PhysicsEngine::MakePhysicsWorld(class World *,float,float)
public: class PhysicsWorld * __thiscall PhysicsEngine::MakePhysicsWorldFromHKE(class World *,char const *)
public: int __thiscall PhysicsEngine::BarrierInfo(void *)
public: int __thiscall PhysicsEngine::RigidBodyInfo(void *,void * *,int &)
public: void __thiscall PhysicsEngine::GetTweaksFromMessage(class UdpMessage *)
public: void __thiscall PhysicsEngine::GetTweaksFromScript(void)
public: void __thiscall PhysicsEngine::Init(void)
public: void __thiscall PhysicsEngine::PackTweaksIntoMessage(class UdpMessage *)
public: void __thiscall PhysicsEngine::UpdatePredictionTick(float)
```

## PhysicsObject
```cpp
const PhysicsObject::`vftable'
protected: bool __thiscall PhysicsObject::PositionBugFixed(void)
protected: class Vector __thiscall PhysicsObject::GetPivotOffset(void)const 
protected: int __thiscall PhysicsObject::StepCheck(class Vector,float)
protected: void * __thiscall PhysicsObject::MeshUnder(float)
protected: void __thiscall PhysicsObject::FixHavokPositionBug(void)
protected: void __thiscall PhysicsObject::MovePlayerOutOfWall(void)
protected: void __thiscall PhysicsObject::MPPredRemoveFromBatch(bool)
protected: void __thiscall PhysicsObject::PlayerActionLadder(class Vector &,class Vector &)
protected: void __thiscall PhysicsObject::PlayerActionUnderwater(class Vector &,class Vector &)
public: __thiscall PhysicsObject::PhysicsObject(class PhysicsObject const &)
public: __thiscall PhysicsObject::PhysicsObject(void)
public: bool __thiscall PhysicsObject::CanLineTraceCollision(void)
public: bool __thiscall PhysicsObject::FloorCheck(float,float,float *,bool,bool)
public: bool __thiscall PhysicsObject::FloorCheckMP(float)
public: bool __thiscall PhysicsObject::FloorCheckRandom(float,float)
public: bool __thiscall PhysicsObject::GetCollisionCallbacks(void)
public: bool __thiscall PhysicsObject::GetEntitySteered(void)
public: bool __thiscall PhysicsObject::IsActive(void)
public: bool __thiscall PhysicsObject::IsEnabled(void)
public: bool __thiscall PhysicsObject::IsFixed(void)
public: bool __thiscall PhysicsObject::IsMovedByExplosions(void)
public: bool __thiscall PhysicsObject::IsPhys(void)
public: bool __thiscall PhysicsObject::IsPinned(void)
public: bool __thiscall PhysicsObject::IsPlayer(void)
public: bool __thiscall PhysicsObject::JumpedInLastAction(void)
public: bool __thiscall PhysicsObject::LineTrace(class Vector &,class Vector &,struct HitData &)
public: bool __thiscall PhysicsObject::LoadPO(class GFile *)
public: bool __thiscall PhysicsObject::MonsterFloorCheck(float,float,float *,bool)
public: bool __thiscall PhysicsObject::SavePO(class GFile *)
public: char __thiscall PhysicsObject::GetFreedomOfRotation(void)
public: class Entity * __thiscall PhysicsObject::GetEntity(void)
public: class Glass * __thiscall PhysicsObject::GetGlass(void)
public: class PhysicsObject & __thiscall PhysicsObject::operator=(class PhysicsObject const &)
public: class Quaternion __thiscall PhysicsObject::GetRotQuaternion(void)
public: class Vector __thiscall PhysicsObject::GetAngularVel(void)
public: class Vector __thiscall PhysicsObject::GetCenterOfMass(void)const 
public: class Vector __thiscall PhysicsObject::GetForwardVec(void)const 
public: class Vector __thiscall PhysicsObject::GetGravity(void)
public: class Vector __thiscall PhysicsObject::GetPawnFloorPos(void)const 
public: class Vector __thiscall PhysicsObject::GetPawnHeadPos(void)const 
public: class Vector __thiscall PhysicsObject::GetPosition(void)const 
public: class Vector __thiscall PhysicsObject::GetRightVec(void)const 
public: class Vector __thiscall PhysicsObject::GetVel(void)const 
public: float __thiscall PhysicsObject::GetAngularDamping(void)
public: float __thiscall PhysicsObject::GetCameraFix(void)
public: float __thiscall PhysicsObject::GetDistanceFromPoint(class Vector)
public: float __thiscall PhysicsObject::GetFriction(void)
public: float __thiscall PhysicsObject::GetLinearDamping(void)
public: float __thiscall PhysicsObject::GetMass(void)
public: float __thiscall PhysicsObject::GetMaxSphereRay(void)
public: float __thiscall PhysicsObject::GetOrientation(void)const 
public: float __thiscall PhysicsObject::GetRestitution(void)
public: float __thiscall PhysicsObject::GetVolumeMass(void)
public: int __thiscall PhysicsObject::GetCollisionGroup(void)
public: int __thiscall PhysicsObject::GetType(void)
public: short __thiscall PhysicsObject::GetActiveMeshIndex(void)
public: short __thiscall PhysicsObject::GetPlayerPitch(void)
public: unsigned char __thiscall PhysicsObject::GetMPByte(void)
public: virtual __thiscall PhysicsObject::~PhysicsObject(void)
public: virtual void __thiscall PhysicsObject::Enable(bool)
public: void * __thiscall PhysicsObject::GetHavokBody(void)const 
public: void __thiscall PhysicsObject::Activate(bool)
public: void __thiscall PhysicsObject::AddMPExternalInfluence(void)
public: void __thiscall PhysicsObject::AddRotateActor(class Vector const &,class Vector const &)
public: void __thiscall PhysicsObject::AttachPlayerToUnderbody(void)
public: void __thiscall PhysicsObject::DetachPlayerFromUnderbody(void)
public: void __thiscall PhysicsObject::EffectForce(class Vector,class Vector const *)
public: void __thiscall PhysicsObject::EffectForces(void)
public: void __thiscall PhysicsObject::EffectRotateActor(void)
public: void __thiscall PhysicsObject::EnableGravity(bool)
public: void __thiscall PhysicsObject::EnableLineTraceCollision(bool)
public: void __thiscall PhysicsObject::EnableSpeedDamping(bool,float,float,float)
public: void __thiscall PhysicsObject::FixGrenadeFlight(class Entity *)
public: void __thiscall PhysicsObject::FixPlayerMovement(class Entity *)
public: void __thiscall PhysicsObject::Hit(class Vector const &,class Vector const &)
public: void __thiscall PhysicsObject::Impulse(class Vector const &,class Vector const &)
public: void __thiscall PhysicsObject::IsMovedByExplosions(bool)
public: void __thiscall PhysicsObject::MaintainAngularVelocity(bool,class Vector,float)
public: void __thiscall PhysicsObject::MaintainLinearMovement(bool,class Vector,bool)
public: void __thiscall PhysicsObject::MaintainPosition(bool,class Vector,float)
public: void __thiscall PhysicsObject::MaintainVelocity(bool,class Vector,float)
public: void __thiscall PhysicsObject::MoveCenterOfMass(class Vector &)const 
public: void __thiscall PhysicsObject::MultiPlayerAction(class Vector &,class Vector &,bool)
public: void __thiscall PhysicsObject::MultiPlayerActionNonPhysic(class PlayerInput *,bool,bool)
public: void __thiscall PhysicsObject::MultiPlayerActionNonPhysic(float,float,float)
public: void __thiscall PhysicsObject::MultiPlayerActionSetupPredictionTime(unsigned long)
public: void __thiscall PhysicsObject::MultiPlayerGetOrientFromController(void)
public: void __thiscall PhysicsObject::PlayerAction(class Vector &,class Vector &)
public: void __thiscall PhysicsObject::RemoveForces(void)
public: void __thiscall PhysicsObject::Rotate(class Vector &)
public: void __thiscall PhysicsObject::ScaleInertiaTensor(float)
public: void __thiscall PhysicsObject::ScaleMass(float)
public: void __thiscall PhysicsObject::SetAngularDamping(float)
public: void __thiscall PhysicsObject::SetAngularVel(class Vector const &)
public: void __thiscall PhysicsObject::SetAsGlass(class Glass *)
public: void __thiscall PhysicsObject::SetAsTransporter(bool,class Vector,float)
public: void __thiscall PhysicsObject::SetCollisionCallbacks(bool,float,float,float)
public: void __thiscall PhysicsObject::SetCollisionGroup(int)
public: void __thiscall PhysicsObject::SetEntitySteered(bool)
public: void __thiscall PhysicsObject::SetFlying(bool)
public: void __thiscall PhysicsObject::SetFreedomOfRotation(int,float)
public: void __thiscall PhysicsObject::SetFriction(float)
public: void __thiscall PhysicsObject::SetGravity(class Vector)
public: void __thiscall PhysicsObject::SetGrenade(bool)
public: void __thiscall PhysicsObject::SetHardDeactivator(void)
public: void __thiscall PhysicsObject::SetLinearDamping(float)
public: void __thiscall PhysicsObject::SetMass(float)
public: void __thiscall PhysicsObject::SetMonsterVisionProperties(float,float,float,float)
public: void __thiscall PhysicsObject::SetMoveVector(class Vector &)
public: void __thiscall PhysicsObject::SetMPByte(unsigned char)
public: void __thiscall PhysicsObject::SetMPMissileController(int)
public: void __thiscall PhysicsObject::SetNotPredictionAffected(void)
public: void __thiscall PhysicsObject::SetOrientation(float)
public: void __thiscall PhysicsObject::SetPawnHeadPos(class Vector const &)
public: void __thiscall PhysicsObject::SetPinned(bool)
public: void __thiscall PhysicsObject::SetPlayerShocked(float)
public: void __thiscall PhysicsObject::SetPlayerShocked(void)
public: void __thiscall PhysicsObject::SetPosition(class Vector const &,bool)
public: void __thiscall PhysicsObject::SetPositionAndRotation(class Vector const &,class Quaternion const &)
public: void __thiscall PhysicsObject::SetRestitution(float)
public: void __thiscall PhysicsObject::SetRotQuaternion(class Quaternion const &)
public: void __thiscall PhysicsObject::SetSynchronizedProp(void)
public: void __thiscall PhysicsObject::SetVel(class Vector const &)
public: void __thiscall PhysicsObject::Tick(float)
public: void __thiscall PhysicsObject::UpdateEntity(float)
public: void __thiscall PhysicsObject::ZeroAngularVelocity(void)
public: void __thiscall PhysicsObject::ZeroVelocity(void)
```

## PhysicsWorld
```cpp
private: __thiscall PhysicsWorld::PhysicsWorld(float,float)
private: bool __thiscall PhysicsWorld::VisibilityWalkPortals(class RaySegment &,class Zone *,class Zone *)
private: class PhysicsObject * __thiscall PhysicsWorld::CreatePhysicsObjectFromMesh(class Vector const &,class Quaternion const &,unsigned long,class WorldMesh *,int,bool)
private: class PhysicsObject * __thiscall PhysicsWorld::CreatePhysicsObjectFromRagdoll(class Vector const &,class Quaternion const &,class Model *,float,int,bool)
private: int __thiscall PhysicsWorld::ResetWorldPortalsCounter(void)
private: void __thiscall PhysicsWorld::AddMesh(class WorldMesh *,float,float)
private: void __thiscall PhysicsWorld::SetWorldPortalsCounter(int)
public: __thiscall PhysicsWorld::~PhysicsWorld(void)
public: bool __thiscall PhysicsWorld::CalculatePawnToEntityVisibility(class PhysicsObject *,class PhysicsObject *)
public: bool __thiscall PhysicsWorld::CalculatePawnToPointVisibility(class PhysicsObject *,class Vector &)
public: bool __thiscall PhysicsWorld::CheckStartGlass(void *,class Vector,float,class Vector)
public: bool __thiscall PhysicsWorld::EnableCollisionToMesh(bool,char const *,float,float)
public: bool __thiscall PhysicsWorld::IsHavokBodyInWorld(void *)
public: bool __thiscall PhysicsWorld::IsHavokBodyPinned(void *)
public: bool __thiscall PhysicsWorld::IsUnderwaterWorld(void)
public: bool __thiscall PhysicsWorld::LineTraceStaticMesh(class WorldMesh *,class Vector &,class Vector &,struct HitData &)
public: bool __thiscall PhysicsWorld::LoadPhysicsWorld(class GFile *)
public: bool __thiscall PhysicsWorld::SavePhysicsWorld(class GFile *)
public: bool __thiscall PhysicsWorld::SetEntityCollisionGroup(class Entity *,bool,int)
public: bool __thiscall PhysicsWorld::Tick(float)
public: bool __thiscall PhysicsWorld::TickWillMovePhysics(float)
public: class Entity * __thiscall PhysicsWorld::GetEntityByActiveMeshIndex(short)
public: class Model * __thiscall PhysicsWorld::GetModelByConstraint(void *)
public: class PhysicsObject * __thiscall PhysicsWorld::CreatePhysicsObject(class Vector const &,class Quaternion const &,unsigned long,class Entity *,float,int,bool)
public: class PhysicsWorld & __thiscall PhysicsWorld::operator=(class PhysicsWorld const &)
public: class Quaternion __thiscall PhysicsWorld::GetHavokBodyRotation(void *)
public: class Vector __thiscall PhysicsWorld::GetGravity(void)
public: class Vector __thiscall PhysicsWorld::GetHavokBodyPosition(void *)
public: class Vector __thiscall PhysicsWorld::GetHavokBodyVelocity(void *)
public: class Vector __thiscall PhysicsWorld::GetRegionPos(void *)
public: float __thiscall PhysicsWorld::ClosestPointsDistanceBetween(class PhysicsObject *,class PhysicsObject *)
public: float __thiscall PhysicsWorld::GetTickPart(void)
public: int __thiscall PhysicsWorld::LineTrace(class Vector const &,class Vector const &,struct HitData &)
public: int __thiscall PhysicsWorld::LineTraceFirstHit(class Vector const &,class Vector const &)
public: int __thiscall PhysicsWorld::LineTraceFixedGeom(class Vector const &,class Vector const &,struct HitData &)
public: int __thiscall PhysicsWorld::LineTraceHitPlayer(class Vector const &,class Vector const &,struct HitData &)
public: int __thiscall PhysicsWorld::SetCollisionToAll(bool,float,float,float,float,float,int)
public: unsigned char __thiscall PhysicsWorld::GetHavokBodyActiveGroup(void *)
public: unsigned long __thiscall PhysicsWorld::GetPhysicsFrame(void)
public: void * __thiscall PhysicsWorld::CreateRegionFromPoints(class DynamicArray<class Vector> &,void *,float)
public: void * __thiscall PhysicsWorld::GetStaticMesh(class Entity *)
public: void __thiscall PhysicsWorld::ActiveMeshGroupActivate(unsigned char)
public: void __thiscall PhysicsWorld::ActiveMeshGroupEnable(unsigned char,bool)
public: void __thiscall PhysicsWorld::ActiveMeshGroupRecurrentActivationEnable(unsigned char,bool)
public: void __thiscall PhysicsWorld::ActiveMeshGroupSetActivationSettings(unsigned char,bool,float,float,float,float,float,float,float)
public: void __thiscall PhysicsWorld::ActiveMeshGroupSetCollisionGroup(unsigned char,int)
public: void __thiscall PhysicsWorld::ActiveMeshGroupSetTimeToRemove(unsigned char,float,float)
public: void __thiscall PhysicsWorld::ActiveMeshGroupStaticMeshEnable(unsigned char,bool)
public: void __thiscall PhysicsWorld::AdvancePhysicsFrame(void)
public: void __thiscall PhysicsWorld::DestroyRegion(void *)
public: void __thiscall PhysicsWorld::EnableUnderwaterWorld(bool)
public: void __thiscall PhysicsWorld::Explosion(class Vector,float,float,int,int,float)
public: void __thiscall PhysicsWorld::ExplosionParabolic(class Vector,float,float,class Vector)
public: void __thiscall PhysicsWorld::ExplosionUp(class Vector,float,float,float,float)
public: void __thiscall PhysicsWorld::GetFromHKE(char const *)
public: void __thiscall PhysicsWorld::GetIntersectingEntities(class BoundingBox &,class DynamicArray<class Entity *> &,int)
public: void __thiscall PhysicsWorld::HitEntity(void *,class Vector const &,class Vector const &)
public: void __thiscall PhysicsWorld::MPPredAddRestToWorld(void)
public: void __thiscall PhysicsWorld::MPPredRemoveAllFromWorld(void)
public: void __thiscall PhysicsWorld::MPPredRestore(void)
public: void __thiscall PhysicsWorld::MPPredSaveRagdolls(void)
public: void __thiscall PhysicsWorld::MultiplayerExplosion(class Vector,float,float,int,int,float,float)
public: void __thiscall PhysicsWorld::Pause(void)
public: void __thiscall PhysicsWorld::PinHavokBody(void *,bool)
public: void __thiscall PhysicsWorld::PrecacheRagdoll(class Model *,char const *)
public: void __thiscall PhysicsWorld::ReleaseStaticMeshes(void)
public: void __thiscall PhysicsWorld::ReloadWorld(void)
public: void __thiscall PhysicsWorld::RemoveHavokBodyFromIS(void *,bool)
public: void __thiscall PhysicsWorld::RemoveStaticMesh(void *)
public: void __thiscall PhysicsWorld::SelfExplosion(class Vector,float,float,class DynamicArray<class PhysicsObject *> *)
public: void __thiscall PhysicsWorld::SetActiveMeshMaxRecursiveActivationDistance(float)
public: void __thiscall PhysicsWorld::SetHavokBodyPosition(void *,class Vector)
public: void __thiscall PhysicsWorld::SetHavokBodyRotation(void *,class Quaternion &)
public: void __thiscall PhysicsWorld::SetHavokBodyVelocity(void *,class Vector)
public: void __thiscall PhysicsWorld::SetPhysicsFrame(unsigned long)
public: void __thiscall PhysicsWorld::SetRegionPos(void *,class Vector const &)
public: void __thiscall PhysicsWorld::Start(bool)
public: void __thiscall PhysicsWorld::StaticMeshEnable(class Entity *,bool)
public: void __thiscall PhysicsWorld::StaticMeshEntityCheck(void)
public: void __thiscall PhysicsWorld::StaticMeshesEnableByGroup(unsigned char,bool)
public: void __thiscall PhysicsWorld::UpdateEntities(unsigned long)
public: void __thiscall PhysicsWorld::UpdatePredictionTick(float)
public: void __thiscall PhysicsWorld::WarmUp(float)
```

## PolygonalMesh
```cpp
public: __thiscall PolygonalMesh::~PolygonalMesh(void)
public: __thiscall PolygonalMesh::PolygonalMesh(class PolygonalMesh const &)
public: __thiscall PolygonalMesh::PolygonalMesh(class SimpleMesh const &)
public: __thiscall PolygonalMesh::PolygonalMesh(void)
public: class PolygonalMesh & __thiscall PolygonalMesh::operator=(class PolygonalMesh const &)
public: int __thiscall PolygonalMesh::Split(class Plane const &,class PolygonalMesh &,class PolygonalMesh &)const 
public: void __thiscall PolygonalMesh::CreateMesh(class WorldMesh &)const 
public: void __thiscall PolygonalMesh::RecomputeBox(void)
```

## Portal
```cpp
const Portal::`vftable'
public: __thiscall Portal::Portal(class Portal const &)
public: __thiscall Portal::Portal(void)
public: class Portal & __thiscall Portal::operator=(class Portal const &)
public: virtual __thiscall Portal::~Portal(void)
public: virtual char const * __thiscall Portal::GetClassNameA(void)const 
public: virtual void __thiscall Portal::Load(class GFile *)
public: virtual void __thiscall Portal::Save(class GFile *)
public: void __thiscall Portal::BuildBox(void)
public: void __thiscall Portal::BuildFromMesh(class World *,class WorldMesh const *)
public: void __thiscall Portal::BuildOtherPoly(void)
public: void __thiscall Portal::FixVertexOrder(void)
public: void __thiscall Portal::Scale(float)
```

## Ragdoll
```cpp
private: void __thiscall Ragdoll::FillRagdollData(class RagdollData &)
private: void __thiscall Ragdoll::GetLimbsPositionsAndRotations(class DynamicArray<class Vector> &,class DynamicArray<class Quaternion> &)
private: void __thiscall Ragdoll::SetFromRagdollData(class RagdollData &)
private: void __thiscall Ragdoll::SetLimbsPositionsAndRotations(class DynamicArray<class Vector> &,class DynamicArray<class Quaternion> &)
public: __thiscall Ragdoll::~Ragdoll(void)
public: __thiscall Ragdoll::Ragdoll(void)
public: bool __thiscall Ragdoll::CanLineTraceCollision(void)
public: bool __thiscall Ragdoll::Init(class Model *,class RagdollSkeleton *,char const *,float,int)
public: bool __thiscall Ragdoll::IsActive(void)
public: bool __thiscall Ragdoll::IsPinned(void)
public: bool __thiscall Ragdoll::Joint_AreLinked(int,int)
public: bool __thiscall Ragdoll::Joint_InRagdoll(int)
public: bool __thiscall Ragdoll::Joint_IsPinned(int)
public: bool __thiscall Ragdoll::LineTrace(class Vector &,class Vector &,struct HitData &)
public: bool __thiscall Ragdoll::LoadRagdoll(class GFile *)
public: bool __thiscall Ragdoll::LoadRagdollCollisionGroups(class GFile *)
public: bool __thiscall Ragdoll::SaveRagdoll(class GFile *)
public: bool __thiscall Ragdoll::SaveRagdollCollisionGroups(class GFile *)
public: class Ragdoll & __thiscall Ragdoll::operator=(class Ragdoll const &)
public: class Vector __thiscall Ragdoll::GetGravity(void)
public: class Vector __thiscall Ragdoll::Joint_GetAngularVelocity(int)
public: class Vector __thiscall Ragdoll::Joint_GetLinearVelocity(int)
public: int __thiscall Ragdoll::GetCollisionGroup(void)
public: int __thiscall Ragdoll::GetJointFromHavokBody(void *)
public: void __thiscall Ragdoll::Activate(class DynamicArray<class Matrix> const &,int)
public: void __thiscall Ragdoll::Animate(class DynamicArray<class Matrix> const &)
public: void __thiscall Ragdoll::ApplyPointImpulse(class Vector,class Vector)
public: void __thiscall Ragdoll::Deactivate(void)
public: void __thiscall Ragdoll::EnableGravity(bool)
public: void __thiscall Ragdoll::EnableLineTraceCollision(bool)
public: void __thiscall Ragdoll::EternalFreeze(void)
public: void __thiscall Ragdoll::ForceActivate(void)
public: void __thiscall Ragdoll::IsMovedByExplosions(bool)
public: void __thiscall Ragdoll::Joint_BreakConstraints(int)
public: void __thiscall Ragdoll::Joint_Enable(int,bool)
public: void __thiscall Ragdoll::Joint_GetPosition(int,class Vector &)
public: void __thiscall Ragdoll::Joint_GetRotation(int,class Quaternion &)
public: void __thiscall Ragdoll::Joint_SetCollisionCallbacks(int,float,float,float)
public: void __thiscall Ragdoll::Joint_SetMatrix(int,class Matrix const &)
public: void __thiscall Ragdoll::Joint_SetPinned(int,bool)
public: void __thiscall Ragdoll::Joint_SetPosition(int,class Vector const &)
public: void __thiscall Ragdoll::Joint_SetPositionLL(int,class Vector const &)
public: void __thiscall Ragdoll::Joint_SetRotation(int,class Quaternion const &)
public: void __thiscall Ragdoll::Joint_SetVelocities(int,class Vector const &,class Vector const &)
public: void __thiscall Ragdoll::Joint_SetVelocitiesForLinked(int,class Vector const &,class Vector const &)
public: void __thiscall Ragdoll::Move(class Vector const &)
public: void __thiscall Ragdoll::ScaleInertiaTensor(float)
public: void __thiscall Ragdoll::SelfExplosion(class Vector,float,float)
public: void __thiscall Ragdoll::SetAngularDamping(float)
public: void __thiscall Ragdoll::SetBreakablesThreshold(float)
public: void __thiscall Ragdoll::SetCollisionGroup(int)
public: void __thiscall Ragdoll::SetDeactivationHardness(float)
public: void __thiscall Ragdoll::SetFriction(float)
public: void __thiscall Ragdoll::SetGravity(class Vector &)
public: void __thiscall Ragdoll::SetHardDeactivator(void)
public: void __thiscall Ragdoll::SetLinearDamping(float)
public: void __thiscall Ragdoll::SetMass(float)
public: void __thiscall Ragdoll::SetPinned(bool)
public: void __thiscall Ragdoll::SetRestitution(float)
public: void __thiscall Ragdoll::SetVelocities(class Vector const &,class Vector const &)
```

## RagdollData
```cpp
public: __thiscall RagdollData::~RagdollData(void)
public: __thiscall RagdollData::RagdollData(class RagdollData const &)
public: __thiscall RagdollData::RagdollData(void)
public: class RagdollData & __thiscall RagdollData::operator=(class RagdollData const &)
```

## RegistryManager
```cpp
protected: bool __thiscall RegistryManager::ReadBool(struct HKEY__ *,class String,bool &)
protected: bool __thiscall RegistryManager::ReadCDKey(struct HKEY__ *,class String &)
protected: bool __thiscall RegistryManager::ReadString(struct HKEY__ *,class String,class String &)
protected: bool __thiscall RegistryManager::ReadUInt(struct HKEY__ *,class String,unsigned int &)
protected: bool __thiscall RegistryManager::WriteBool(struct HKEY__ *,class String,bool)
protected: bool __thiscall RegistryManager::WriteCDKey(struct HKEY__ *,class String)
protected: bool __thiscall RegistryManager::WriteString(struct HKEY__ *,class String,class String)
protected: bool __thiscall RegistryManager::WriteUInt(struct HKEY__ *,class String,unsigned int)
protected: void __thiscall RegistryManager::OutputErrorString(void)
protected: void __thiscall RegistryManager::ReadFromRegistry(void)
public: __thiscall RegistryManager::~RegistryManager(void)
public: __thiscall RegistryManager::RegistryManager(class RegistryManager const &)
public: __thiscall RegistryManager::RegistryManager(void)
public: class RegistryManager & __thiscall RegistryManager::operator=(class RegistryManager const &)
public: void __thiscall RegistryManager::Update(void)
```

## RenderWindow
```cpp
const RenderWindow::`vftable'
public: __thiscall RenderWindow::~RenderWindow(void)
public: __thiscall RenderWindow::RenderWindow(class RenderWindow const &)
public: __thiscall RenderWindow::RenderWindow(void)
public: class RenderWindow & __thiscall RenderWindow::operator=(class RenderWindow const &)
public: virtual long __thiscall RenderWindow::HandleMessages(struct HWND__ *,unsigned int,unsigned int,long)
```

## SceneRender
```cpp
private: bool __thiscall SceneRender::CutPortalPolygonWithAntiPortalVolumes(class ConvexPolygon,class DynamicArray<class ConvexVolumeResizable *> &)
private: static bool __cdecl SceneRender::CreateEntityVisibilityVolume(class Vector const &,class ConvexVolumeResizable const &,class Portal const *,class ConvexVolumeResizable &)
private: static bool __cdecl SceneRender::EntityVisible(class Vector const &,class Entity *,class ConvexVolumeResizable &,int,unsigned char *)
private: static void __cdecl SceneRender::CreatePortalToPortalVolume(int,class Portal *,class ConvexPolygon &,class ConvexVolumeResizable &)
private: static void __cdecl SceneRender::RecalculateZonePVS(int)
private: static void __cdecl SceneRender::RecalculateZonePVSRecurrent(int,class Portal *,class ConvexVolumeResizable &,int,class Portal *)
private: void __thiscall SceneRender::CreateAntiportalVolume(class AntiPortal const &,class ConvexVolumeResizable &)
private: void __thiscall SceneRender::CreatePortalVolume(class ConvexVolume const &,class Portal const *,class ConvexVolume &)
private: void __thiscall SceneRender::CutPortalPolygonWithFrustum(class ConvexVolume const &,class ConvexPolygon &)
private: void __thiscall SceneRender::RecursiveWalkPortals(class Portal *,class Zone *,class ConvexVolume const &,bool)
public: __thiscall SceneRender::~SceneRender(void)
public: __thiscall SceneRender::SceneRender(class SceneRender const &)
public: __thiscall SceneRender::SceneRender(class Viewport &)
public: bool __thiscall SceneRender::IsVisible(class Entity const *)const 
public: class Zone * __thiscall SceneRender::GetEyeZone(void)
public: static bool __cdecl SceneRender::EntityVisible(class Entity *,class Vector &)
public: static void __cdecl SceneRender::CreateBoxVolume(class BoundingBox const &,class Vector const &,class ConvexVolumeResizable &)
public: static void __cdecl SceneRender::RecalculateWorldPVS(void)
public: void __thiscall SceneRender::Dump(void)
public: void __thiscall SceneRender::Occlude(bool,bool)
public: void __thiscall SceneRender::OccludeReflection(class DynamicArray<class WorldMesh *> const &,class Plane const &,class BoundingBox const &)
public: void __thiscall SceneRender::UpdateDistanceFromCam(void)
```

## ScreenVertex
```cpp
public: __thiscall ScreenVertex::ScreenVertex(float,float,float,float,unsigned long)
public: __thiscall ScreenVertex::ScreenVertex(struct ScreenVertex const &)
public: struct ScreenVertex & __thiscall ScreenVertex::operator=(struct ScreenVertex const &)
```

## ScreenVertexNDC
```cpp
public: __thiscall ScreenVertexNDC::ScreenVertexNDC(float,float,float,float)
public: __thiscall ScreenVertexNDC::ScreenVertexNDC(struct ScreenVertexNDC const &)
public: struct ScreenVertexNDC & __thiscall ScreenVertexNDC::operator=(struct ScreenVertexNDC const &)
```

## Script
```cpp
public: __thiscall Script::~Script(void)
public: __thiscall Script::Script(struct lua_State *,int)
public: __thiscall Script::Script(void)
public: bool __thiscall Script::DoFile(char const *,bool)
public: bool __thiscall Script::GetBool(int,bool)
public: bool __thiscall Script::Init(void)
public: bool __thiscall Script::Initialized(void)
public: bool __thiscall Script::IsFunction(void)
public: bool __thiscall Script::IsNil(int)
public: bool __thiscall Script::IsNumber(void)
public: bool __thiscall Script::IsString(void)
public: bool __thiscall Script::IsTable(void)
public: char const * __thiscall Script::GetString(int,char const *)
public: class Script & __thiscall Script::operator=(class Script const &)
public: class Script __thiscall Script::GetTable(int)
public: class Script __thiscall Script::operator[](char const *)
public: class Script __thiscall Script::operator[](int)
public: class ScriptObject __thiscall Script::Globals(void)
public: float __thiscall Script::GetFloat(int,float)
public: int __thiscall Script::GetCount(int)
public: int __thiscall Script::GetGCCount(void)
public: int __thiscall Script::GetGCThreshold(void)
public: int __thiscall Script::GetInt(int,int)
public: int __thiscall Script::GetMetatable(int)
public: int __thiscall Script::GetTop(void)
public: int __thiscall Script::GetType(int)
public: int __thiscall Script::NewTable(void)
public: void * __thiscall Script::GetLightUD(int,void *)
public: void __cdecl Script::Call(char const *,int,char const *,...)
public: void __cdecl Script::DoString(char const *,...)
public: void __cdecl Script::QueueMessage(char const *,char const *,...)
public: void __thiscall Script::Call(int,int)
public: void __thiscall Script::DoBuffer(char const *,char const *)
public: void __thiscall Script::GetInfo(char const *,struct lua_Debug *)
public: void __thiscall Script::PushBool(bool)
public: void __thiscall Script::PushFloat(float)
public: void __thiscall Script::PushInt(int)
public: void __thiscall Script::PushLightUD(void *)
public: void __thiscall Script::PushNil(void)
public: void __thiscall Script::PushString(char const *)
public: void __thiscall Script::RegisterFunction(char const *,int (__cdecl*)(struct lua_State *))
public: void __thiscall Script::RegisterLibrary(char const *,struct luaL_reg const *,bool)
public: void __thiscall Script::Release(void)
public: void __thiscall Script::SetGCThreshold(int)
public: void __thiscall Script::SetHook(void (__cdecl*)(struct lua_State *,struct lua_Debug *),unsigned long)
public: void __thiscall Script::SetTable(int)
public: void __thiscall Script::SetTop(int)
```

## ScriptObject
```cpp
private: void __thiscall ScriptObject::UpdateValue(void)
public: __thiscall ScriptObject::~ScriptObject(void)
public: __thiscall ScriptObject::ScriptObject(struct lua_State *,struct lua_TObject const *)
public: bool __thiscall ScriptObject::GetBool(void)const 
public: bool __thiscall ScriptObject::IsBool(void)const 
public: bool __thiscall ScriptObject::IsFunction(void)const 
public: bool __thiscall ScriptObject::IsLightUD(void)const 
public: bool __thiscall ScriptObject::IsNil(void)const 
public: bool __thiscall ScriptObject::IsNumber(void)const 
public: bool __thiscall ScriptObject::IsString(void)const 
public: bool __thiscall ScriptObject::IsTable(void)const 
public: char const * __thiscall ScriptObject::GetString(void)const 
public: class ScriptObject & __thiscall ScriptObject::NewKey(char const *)
public: class ScriptObject & __thiscall ScriptObject::NewKey(int)
public: class ScriptObject & __thiscall ScriptObject::operator=(class ScriptObject const &)
public: class ScriptObject __thiscall ScriptObject::GetMetatable(void)const 
public: class ScriptObject __thiscall ScriptObject::operator[](char const *)
public: class ScriptObject __thiscall ScriptObject::operator[](int)
public: float __thiscall ScriptObject::GetFloat(void)const 
public: int __thiscall ScriptObject::GetCount(void)const 
public: int __thiscall ScriptObject::GetInt(void)const 
public: int __thiscall ScriptObject::GetType(void)const 
public: void * __thiscall ScriptObject::GetLightUD(void)const 
public: void __thiscall ScriptObject::SetBool(bool)
public: void __thiscall ScriptObject::SetFloat(float)
public: void __thiscall ScriptObject::SetInteger(int)
public: void __thiscall ScriptObject::SetNil(void)
public: void __thiscall ScriptObject::SetString(char const *)
public: void __thiscall ScriptObject::SetTObject(struct lua_TObject *)
```

## ScriptTableIterator
```cpp
public: __thiscall ScriptTableIterator::~ScriptTableIterator(void)
public: __thiscall ScriptTableIterator::operator bool(void)const 
public: __thiscall ScriptTableIterator::ScriptTableIterator(class ScriptObject &,bool)
public: bool __thiscall ScriptTableIterator::IsValid(void)const 
public: bool __thiscall ScriptTableIterator::Next(void)
public: class ScriptObject & __thiscall ScriptTableIterator::GetKey(void)
public: class ScriptObject & __thiscall ScriptTableIterator::GetValue(void)
public: class ScriptTableIterator & __thiscall ScriptTableIterator::operator++(void)
public: void __thiscall ScriptTableIterator::Invalidate(void)
public: void __thiscall ScriptTableIterator::Reset(void)
```

## SimpleMesh
```cpp
const SimpleMesh::`vftable'
protected: void __thiscall SimpleMesh::CalcTangents(class DynamicArray<class Plane> &)
public: __thiscall SimpleMesh::SimpleMesh(class SimpleMesh const &)
public: __thiscall SimpleMesh::SimpleMesh(void)
public: class SimpleMesh & __thiscall SimpleMesh::operator=(class SimpleMesh const &)
public: virtual __thiscall SimpleMesh::~SimpleMesh(void)
public: void __thiscall SimpleMesh::Init(void)
public: void __thiscall SimpleMesh::Render(class Material *)
public: void __thiscall SimpleMesh::SetTextureName(int,class String const &)
public: void __thiscall SimpleMesh::SetTexturesPriority(unsigned long)
```

## SimplePackedVertex
```cpp
public: __thiscall SimplePackedVertex::SimplePackedVertex(class Vector const &,unsigned long,float const * const,float)
public: __thiscall SimplePackedVertex::SimplePackedVertex(struct SimplePackedVertex const &)
public: struct SimplePackedVertex & __thiscall SimplePackedVertex::operator=(struct SimplePackedVertex const &)
```

## SimpleProfiler
```cpp
public: __thiscall SimpleProfiler::~SimpleProfiler(void)
public: __thiscall SimpleProfiler::SimpleProfiler(bool,int)
public: char const * __thiscall SimpleProfiler::GetStateName(int)
public: class SimpleProfiler & __thiscall SimpleProfiler::operator=(class SimpleProfiler const &)
public: class String __thiscall SimpleProfiler::GetTextInfo(void)
public: double __thiscall SimpleProfiler::GiveAvgTimeForState(int)
public: double __thiscall SimpleProfiler::GiveLastFramesAverageForState(int)
public: double __thiscall SimpleProfiler::GiveLastFramesTimeForState(int)
public: double __thiscall SimpleProfiler::GiveMaxTimeForState(int)
public: double __thiscall SimpleProfiler::GiveSummarizedAverageOfLastFrames(void)
public: double __thiscall SimpleProfiler::GiveSummarizedTimeOfLastFrames(void)
public: double __thiscall SimpleProfiler::GiveTimeSinceViewUpdate(void)
public: int __thiscall SimpleProfiler::GetState(void)
public: int __thiscall SimpleProfiler::GiveNumberOfFrames(void)
public: int __thiscall SimpleProfiler::GiveNumberOfStates(void)
public: void __thiscall SimpleProfiler::`default constructor closure'(void)
public: void __thiscall SimpleProfiler::AdvanceFrame(void)
public: void __thiscall SimpleProfiler::SetState(int)
public: void __thiscall SimpleProfiler::ViewUpdate(void)
```

## SimpleVertex
```cpp
public: __thiscall SimpleVertex::SimpleVertex(class Vector const &,unsigned long,class TexCoord const &)
public: __thiscall SimpleVertex::SimpleVertex(float,float,float,unsigned long,float,float)
public: __thiscall SimpleVertex::SimpleVertex(struct SimpleVertex const &)
public: __thiscall SimpleVertex::SimpleVertex(void)
public: struct SimpleVertex & __thiscall SimpleVertex::operator=(struct SimpleVertex const &)
```

## Sky
```cpp
public: __thiscall Sky::~Sky(void)
public: __thiscall Sky::Sky(class Sky const &)
public: __thiscall Sky::Sky(void)
public: class Sky & __thiscall Sky::operator=(class Sky const &)
public: class SkyLayer * __thiscall Sky::GetLayer(int)
public: int __thiscall Sky::GetLayerNum(void)
public: int __thiscall Sky::Load(char const *)
public: void __thiscall Sky::Draw(void)
public: void __thiscall Sky::FlashTexture(float,int)
public: void __thiscall Sky::RenderCleanup(class RenderDevice *,class MaterialSystem *)
public: void __thiscall Sky::RenderInitialize(class RenderDevice *,class MaterialSystem *)
public: void __thiscall Sky::Tick(float)
public: void __thiscall Sky::Unload(void)
```

## Sound
```cpp
const Sound::`vftable'
protected: void __thiscall Sound::Release(bool)
public: __thiscall Sound::Sound(class Sound const &)
public: __thiscall Sound::Sound(void)
public: class Sound & __thiscall Sound::operator=(class Sound const &)
public: int __thiscall Sound::GetSound3DIndex(void)
public: virtual __thiscall Sound::~Sound(void)
public: virtual bool __thiscall Sound::LoadEntity(class GFile *)
public: virtual bool __thiscall Sound::SaveEntity(class GFile *)
public: virtual void __thiscall Sound::EnableDraw(bool)
public: virtual void __thiscall Sound::SetPosition(class Vector const &)
public: virtual void __thiscall Sound::Tick(float)
public: virtual void __thiscall Sound::UpdateTransform(bool)
public: void __thiscall Sound::Play(float)
public: void __thiscall Sound::Setup2D(char const *,float,float)
public: void __thiscall Sound::Setup3D(char const *,float,float,float,float,bool)
public: void __thiscall Sound::SetVelocityScaleFactor(float)
public: void __thiscall Sound::Stop(void)
```

## Sprite
```cpp
public: __thiscall Sprite::Sprite(struct Sprite const &)
public: __thiscall Sprite::Sprite(void)
public: struct Sprite & __thiscall Sprite::operator=(struct Sprite const &)
```

## Sprite1DOF
```cpp
public: __thiscall Sprite1DOF::~Sprite1DOF(void)
public: __thiscall Sprite1DOF::Sprite1DOF(struct Sprite1DOF const &)
public: __thiscall Sprite1DOF::Sprite1DOF(void)
public: struct Sprite1DOF & __thiscall Sprite1DOF::operator=(struct Sprite1DOF const &)
```

## StackTracer
```cpp
public: static void __cdecl StackTracer::ErrorMessageBox(void)
public: static void __cdecl StackTracer::Push(char const *)
public: struct StackTracer & __thiscall StackTracer::operator=(struct StackTracer const &)
```

## String
```cpp
public: __thiscall String::~String(void)
public: __thiscall String::String(char const *)
public: __thiscall String::String(char,int)
public: __thiscall String::String(class String const &)
public: __thiscall String::String(void)
public: bool __thiscall String::EqualsNoCase(class String const &)const 
public: bool __thiscall String::GreaterNoCase(class String const &)const 
public: bool __thiscall String::IsPrefix(class String const &)const 
public: bool __thiscall String::operator!=(class String const &)const 
public: bool __thiscall String::operator==(class String const &)const 
public: bool __thiscall String::operator>(class String const &)const 
public: bool __thiscall String::TestPrintable(void)
public: bool __thiscall String::ValidAsGameSpyCDKey(void)
public: char const * __thiscall String::AsChar(void)const 
public: char const * __thiscall String::Find(char const *)const 
public: char const * __thiscall String::Find(class String const &)const 
public: class String & __thiscall String::operator+=(char const *)
public: class String & __thiscall String::operator+=(char)
public: class String & __thiscall String::operator+=(class String const &)
public: class String & __thiscall String::operator=(class String const &)
public: class String & __thiscall String::ToLower(void)
public: class String __thiscall String::LowerCase(void)const 
public: class String __thiscall String::operator+(char const *)const 
public: class String __thiscall String::operator+(char)const 
public: class String __thiscall String::operator+(class String const &)const 
public: class String __thiscall String::SubStr(int,int)const 
public: int __thiscall String::Len(void)const 
public: static class String __cdecl String::BaseName(class String const &)
public: static class String __cdecl String::GenerateRandom(void)
public: static class String __cdecl String::GetExtension(class String const &)
public: static class String __cdecl String::GetPath(class String const &)
public: static class String __cdecl String::Sprintf(char const *,...)
public: static class String __cdecl String::StripExtension(class String const &)
public: static void __cdecl String::Tokenize(char const *,char const *,class DynamicArray<class String> &)
public: static void __cdecl String::TokenizeWithLiteral(char const *,char const *,class DynamicArray<class String> &,char)
```

## StringHashSystem2
```cpp
public: __thiscall StringHashSystem2::~StringHashSystem2(void)
public: __thiscall StringHashSystem2::StringHashSystem2(class StringHashSystem2 const &)
public: __thiscall StringHashSystem2::StringHashSystem2(void)
public: class String __thiscall StringHashSystem2::ReadString(class UdpMessage *)
public: class StringHashSystem2 & __thiscall StringHashSystem2::operator=(class StringHashSystem2 const &)
public: unsigned short __thiscall StringHashSystem2::GetFirstUnusedStringNumber(void)
public: void __thiscall StringHashSystem2::Clear(void)
public: void __thiscall StringHashSystem2::SetFirstUnsureStringNumberToReceive(unsigned short)
public: void __thiscall StringHashSystem2::SetFirstUnsureStringNumberToSend(unsigned short)
public: void __thiscall StringHashSystem2::WriteString(class UdpMessage *,class String &)
```

## SystemDriver
```cpp
private: void __thiscall SystemDriver::GetCPUInfo(void)
private: void __thiscall SystemDriver::GetWindowsInfo(void)
public: __thiscall SystemDriver::~SystemDriver(void)
public: __thiscall SystemDriver::SystemDriver(class SystemDriver const &)
public: __thiscall SystemDriver::SystemDriver(void)
public: bool __thiscall SystemDriver::Test(void)
public: bool __thiscall SystemDriver::TestTimer(int)
public: char const * __thiscall SystemDriver::GetBaseDir(void)const 
public: char const * __thiscall SystemDriver::GetTimeAsString(void)const 
public: class String __thiscall SystemDriver::GetExePath(void)const 
public: class String __thiscall SystemDriver::GetOSName(void)const 
public: class SystemDriver & __thiscall SystemDriver::operator=(class SystemDriver const &)
public: double __thiscall SystemDriver::GetLastTimeStep(void)
public: double __thiscall SystemDriver::GetTickCount(void)const 
public: double __thiscall SystemDriver::GetTimeFromTimerReset(void)
public: double __thiscall SystemDriver::GetTimeStep(void)
public: double __thiscall SystemDriver::GetTSCLowDword(void)
public: double const __thiscall SystemDriver::GetSecondsPerCycle(void)const 
public: int __thiscall SystemDriver::CreateTimer(unsigned long,bool)
public: unsigned long __thiscall SystemDriver::GetCurrentTimeMS(void)const 
public: void __thiscall SystemDriver::Dump(class LogBuffer &)
public: void __thiscall SystemDriver::ExactSleep(unsigned long)
public: void __thiscall SystemDriver::GetPureTickCount(unsigned long &,unsigned long &)
public: void __thiscall SystemDriver::Initialize(struct HINSTANCE__ *)
public: void __thiscall SystemDriver::MilisecondsToTickCount(unsigned long &,unsigned long &,unsigned long &)
public: void __thiscall SystemDriver::ResetTimer(void)
public: void __thiscall SystemDriver::SetTimerPeriod(int,unsigned long)
```

## TangentBasis
```cpp
public: __thiscall TangentBasis::TangentBasis(class Vector const &,class Vector const &,class TexCoord const &)
public: __thiscall TangentBasis::TangentBasis(struct TangentBasis const &)
public: __thiscall TangentBasis::TangentBasis(void)
public: struct TangentBasis & __thiscall TangentBasis::operator=(struct TangentBasis const &)
```

## TexCoord
```cpp
public: __thiscall TexCoord::TexCoord(float,float)
public: __thiscall TexCoord::TexCoord(void)
public: class TexCoord & __thiscall TexCoord::operator=(class TexCoord const &)
public: int __thiscall TexCoord::operator==(class TexCoord const &)const 
```

## Trail
```cpp
const Trail::`vftable'
protected: void __thiscall Trail::AddNode(void)
protected: void __thiscall Trail::CutTail(void)
public: __thiscall Trail::Trail(class Trail const &)
public: __thiscall Trail::Trail(class TrailEffect *)
public: class Trail & __thiscall Trail::operator=(class Trail const &)
public: virtual __thiscall Trail::~Trail(void)
public: virtual bool __thiscall Trail::DemoCalcSize(int &,unsigned long &)
public: virtual bool __thiscall Trail::DemoReadEntity(class GFile *)
public: virtual bool __thiscall Trail::DemoReadFull(class GFile *)
public: virtual bool __thiscall Trail::DemoWriteEntity(class GFile *)
public: virtual bool __thiscall Trail::DemoWriteFull(class GFile *)
public: virtual bool __thiscall Trail::LoadEntity(class GFile *)
public: virtual bool __thiscall Trail::SaveEntity(class GFile *)
public: virtual void __thiscall Trail::Draw(class RenderDevice *,int)
public: virtual void __thiscall Trail::SetScale(float)
public: virtual void __thiscall Trail::Tick(float)
public: virtual void __thiscall Trail::UpdateTransform(bool)
public: void __thiscall Trail::AttachToBones(class Entity *,class DynamicArray<class String> const &)
public: void __thiscall Trail::Free(void)
public: void __thiscall Trail::Init(int,int)
public: void __thiscall Trail::SetState(class RenderDevice *)
public: void __thiscall Trail::SetTexture(class String const &)
public: void __thiscall Trail::SetTexture(class Texture *)
```

## TrailEffect
```cpp
public: __thiscall TrailEffect::~TrailEffect(void)
public: __thiscall TrailEffect::TrailEffect(class TrailEffect const &)
public: __thiscall TrailEffect::TrailEffect(void)
public: class TrailEffect & __thiscall TrailEffect::operator=(class TrailEffect const &)
public: void __thiscall TrailEffect::Load(class String const &)
```

## TrailSystem
```cpp
private: class TrailEffect * __thiscall TrailSystem::LoadTrailEffect(class String const &)
public: __thiscall TrailSystem::~TrailSystem(void)
public: __thiscall TrailSystem::TrailSystem(class TrailSystem const &)
public: __thiscall TrailSystem::TrailSystem(void)
public: class Trail * __thiscall TrailSystem::CreateTrail(char const *,char const *)
public: class TrailSystem & __thiscall TrailSystem::operator=(class TrailSystem const &)
public: int __thiscall TrailSystem::Init(void)
public: void __thiscall TrailSystem::Reload(void)
public: void __thiscall TrailSystem::RenderCleanup(void)
```

## UdpMessage
```cpp
private: float __thiscall UdpMessage::ReadFloatOptimized(unsigned long,float,unsigned char,unsigned char)
private: unsigned long __thiscall UdpMessage::WriteFloatOptimized(float,float,unsigned char,unsigned char)
public: __thiscall UdpMessage::~UdpMessage(void)
public: __thiscall UdpMessage::UdpMessage(char *,int)
public: __thiscall UdpMessage::UdpMessage(class UdpMessage const &)
public: __thiscall UdpMessage::UdpMessage(int)
public: bool __thiscall UdpMessage::CheckRemoveCRCStamp(void)
public: bool __thiscall UdpMessage::ReadBool(void)
public: bool __thiscall UdpMessage::ReadCheckToken(unsigned char)
public: bool __thiscall UdpMessage::ReadFinished(void)
public: bool __thiscall UdpMessage::ReadFromFile(class String)
public: bool __thiscall UdpMessage::WriteToFile(class String)
public: char * __thiscall UdpMessage::GetBuffer(void)const 
public: char __thiscall UdpMessage::PeekChar(void)
public: char __thiscall UdpMessage::ReadChar(void)
public: char __thiscall UdpMessage::WriteULongOptimized(unsigned long)
public: class Entity * __thiscall UdpMessage::ReadEntityPtr(void)
public: class Quaternion __thiscall UdpMessage::ReadQuaternion(void)
public: class Quaternion __thiscall UdpMessage::ReadQuaternionOptimized(void)
public: class Quaternion __thiscall UdpMessage::ReadQuaternionOptimized2(void)
public: class String __thiscall UdpMessage::ReadString(void)
public: class String __thiscall UdpMessage::ReadStringNL(void)
public: class UdpMessage & __thiscall UdpMessage::operator+=(class UdpMessage const &)
public: class UdpMessage & __thiscall UdpMessage::operator=(class UdpMessage const &)
public: class Vector __thiscall UdpMessage::ReadAngularVelocityOptimized(void)
public: class Vector __thiscall UdpMessage::ReadLinearVelocityOptimized(void)
public: class Vector __thiscall UdpMessage::ReadNormalOptimized(void)
public: class Vector __thiscall UdpMessage::ReadPositionOptimized(void)
public: class Vector __thiscall UdpMessage::ReadVector(void)
public: double __thiscall UdpMessage::PeekDouble(void)
public: double __thiscall UdpMessage::ReadDouble(void)
public: float __thiscall UdpMessage::PeekFloat(void)
public: float __thiscall UdpMessage::ReadFloat(void)
public: float __thiscall UdpMessage::ReadFloatSignedOptimized(float)
public: float __thiscall UdpMessage::ReadFloatUnsignedOptimized(float)
public: int __thiscall UdpMessage::GetPos(void)const 
public: int __thiscall UdpMessage::GetSize(void)const 
public: long __thiscall UdpMessage::PeekLong(void)
public: long __thiscall UdpMessage::ReadLong(void)
public: short __thiscall UdpMessage::PeekShort(void)
public: short __thiscall UdpMessage::ReadShort(void)
public: unsigned char __thiscall UdpMessage::PeekByte(void)
public: unsigned char __thiscall UdpMessage::PeekUChar(void)
public: unsigned char __thiscall UdpMessage::ReadBits(unsigned char)
public: unsigned char __thiscall UdpMessage::ReadByte(void)
public: unsigned char __thiscall UdpMessage::ReadUChar(void)
public: unsigned long __thiscall UdpMessage::PeekULong(void)
public: unsigned long __thiscall UdpMessage::ReadStateOptimized(void)
public: unsigned long __thiscall UdpMessage::ReadULong(void)
public: unsigned long __thiscall UdpMessage::ReadULongOptimized(void)
public: unsigned short __thiscall UdpMessage::PeekUShort(void)
public: unsigned short __thiscall UdpMessage::ReadUpTo32767Optimized(void)
public: unsigned short __thiscall UdpMessage::ReadUShort(void)
public: void __thiscall UdpMessage::`default constructor closure'(void)
public: void __thiscall UdpMessage::DumpToLogfile(class String)
public: void __thiscall UdpMessage::EndWriting(void)
public: void __thiscall UdpMessage::FillWith(unsigned char,unsigned short)
public: void __thiscall UdpMessage::MakeCRCStamp(void)
public: void __thiscall UdpMessage::ReadData(char *,int)
public: void __thiscall UdpMessage::ResetBitPos(void)
public: void __thiscall UdpMessage::ScrapData(int)
public: void __thiscall UdpMessage::Seek(int)
public: void __thiscall UdpMessage::SeekToBegin(void)
public: void __thiscall UdpMessage::Test(void)
public: void __thiscall UdpMessage::Trim(void)
public: void __thiscall UdpMessage::WriteAngularVelocityOptimized(class Vector &)
public: void __thiscall UdpMessage::WriteBits(unsigned char,unsigned char)
public: void __thiscall UdpMessage::WriteBool(bool)
public: void __thiscall UdpMessage::WriteByte(unsigned char)
public: void __thiscall UdpMessage::WriteChar(char)
public: void __thiscall UdpMessage::WriteCheckToken(unsigned char)
public: void __thiscall UdpMessage::WriteData(char const *,int)
public: void __thiscall UdpMessage::WriteData(class UdpMessage *)
public: void __thiscall UdpMessage::WriteData(class UdpMessage *,int)
public: void __thiscall UdpMessage::WriteData(class UdpMessage *,int,int)
public: void __thiscall UdpMessage::WriteDouble(double)
public: void __thiscall UdpMessage::WriteEntityPtr(class Entity *)
public: void __thiscall UdpMessage::WriteFloat(float)
public: void __thiscall UdpMessage::WriteFloatSignedOptimized(float,float)
public: void __thiscall UdpMessage::WriteFloatUnsignedOptimized(float,float)
public: void __thiscall UdpMessage::WriteLinearVelocityOptimized(class Vector &)
public: void __thiscall UdpMessage::WriteLong(long)
public: void __thiscall UdpMessage::WriteNormalOptimized(class Vector &)
public: void __thiscall UdpMessage::WritePositionOptimized(class Vector &)
public: void __thiscall UdpMessage::WriteQuaternion(class Quaternion &)
public: void __thiscall UdpMessage::WriteQuaternionOptimized(class Quaternion &)
public: void __thiscall UdpMessage::WriteQuaternionOptimized2(class Quaternion &)
public: void __thiscall UdpMessage::WriteShort(short)
public: void __thiscall UdpMessage::WriteStateOptimized(unsigned long)
public: void __thiscall UdpMessage::WriteString(char const *)
public: void __thiscall UdpMessage::WriteStringNL(char const *)
public: void __thiscall UdpMessage::WriteUChar(unsigned char)
public: void __thiscall UdpMessage::WriteULong(unsigned long)
public: void __thiscall UdpMessage::WriteUpTo32767Optimized(unsigned short)
public: void __thiscall UdpMessage::WriteUShort(unsigned short)
public: void __thiscall UdpMessage::WriteVector(class Vector &)
```

## VertexWeights
```cpp
public: __thiscall VertexWeights::VertexWeights(float,float,float,unsigned short,unsigned short,unsigned short,unsigned short)
public: __thiscall VertexWeights::VertexWeights(void)
public: struct VertexWeights & __thiscall VertexWeights::operator=(struct VertexWeights const &)
```

## View
```cpp
const View::`vftable'
protected: void __thiscall View::RecalcWindowAspect(void)
protected: void __thiscall View::RenderBoxes(class SceneRender const &)
protected: void __thiscall View::RenderCubemap(void)
protected: void __thiscall View::RenderDemonFXWorld(class SceneRender const &,bool)
protected: void __thiscall View::RenderFakeReflection(void)
protected: void __thiscall View::RenderGhostFXWorld(class SceneRender const &)
protected: void __thiscall View::RenderHUD(class SceneRender const &)
protected: void __thiscall View::RenderPathfinder(void)
protected: void __thiscall View::RenderPostProcess(void)
protected: void __thiscall View::RenderReflection(int)
protected: void __thiscall View::RenderRefraction(void)
protected: void __thiscall View::RenderShadowmaps(class SceneRender const &)
protected: void __thiscall View::RenderWorld(class SceneRender const &,unsigned long)
protected: void __thiscall View::RenderWorldToTexture(class SceneRender const &)
protected: void __thiscall View::RenderZ(struct View::Sortable *)
protected: void __thiscall View::RenderZones(class SceneRender const &)
public: __thiscall View::View(class View const &)
public: __thiscall View::View(void)
public: virtual __thiscall View::~View(void)
public: virtual struct HWND__ * __thiscall View::GetActiveWindow(void)
public: virtual struct HWND__ * __thiscall View::GetTopmostWindow(void)
public: void __thiscall View::FakeStuff(void)
public: void __thiscall View::Init(void)
public: void __thiscall View::PlayMovie(class String)
public: void __thiscall View::PrecacheMeshes(void)
public: void __thiscall View::Render(float)
public: void __thiscall View::SetWorld(class World *)
```

## Viewport
```cpp
public: __thiscall Viewport::Viewport(class Viewport const &)
public: __thiscall Viewport::Viewport(void)
public: class Matrix __thiscall Viewport::GetCameraToScreen(void)const 
public: class Viewport & __thiscall Viewport::operator=(class Viewport const &)
public: void __thiscall Viewport::Resize(int,int)
public: void __thiscall Viewport::Resize(struct HWND__ *)
public: void __thiscall Viewport::SetCameraToScreen(float,bool)
public: void __thiscall Viewport::Update(class Camera const &)
```

## Volume
```cpp
const Volume::`vftable'{for `Entity'}
const Volume::`vftable'{for `SimpleMesh'}
public: __thiscall Volume::Volume(class Volume const &)
public: __thiscall Volume::Volume(class WorldMesh const &,int)
public: class Volume & __thiscall Volume::operator=(class Volume const &)
public: virtual __thiscall Volume::~Volume(void)
public: virtual void __thiscall Volume::Draw(class RenderDevice *,int)
public: virtual void __thiscall Volume::RenderCleanup(class RenderDevice *,class MaterialSystem *)
public: virtual void __thiscall Volume::RenderInitialize(class RenderDevice *,class MaterialSystem *,char const *)
public: void __thiscall Volume::DrawBackPass(class RenderDevice *)
public: void __thiscall Volume::DrawFrontPass(class RenderDevice *)
```

## Waypoint2
```cpp
public: __thiscall Waypoint2::Waypoint2(class Waypoint2 const &)
public: __thiscall Waypoint2::Waypoint2(void)
public: bool __thiscall Waypoint2::InBox(class BoundingBox const &)const 
public: bool __thiscall Waypoint2::LoadFrom(class GFile *)
public: bool __thiscall Waypoint2::operator==(class Waypoint2 const &)const 
public: bool __thiscall Waypoint2::SaveTo(class GFile *,float)
public: class Waypoint2 & __thiscall Waypoint2::operator=(class Waypoint2 const &)
public: float __thiscall Waypoint2::Dist(class Waypoint2 &)const 
public: float __thiscall Waypoint2::DistSquared(class Waypoint2 &)const 
public: void __thiscall Waypoint2::MakeBox(class BoundingBox &,float)const 
```

## WaypointGPath
```cpp
protected: class WaypointPath * __thiscall WaypointGPath::CalculatePreciselyNextZone(void)
protected: int __thiscall WaypointGPath::InternalGetNext(class Vector &,int &,int &)
protected: int __thiscall WaypointGPath::Peek(int,class Vector &,class Vector &)
protected: int __thiscall WaypointGPath::PeekZoneAndWaypointIndex(int,int &,int &)
protected: void __thiscall WaypointGPath::GetPosLast(class Vector &)
protected: void __thiscall WaypointGPath::PeekDir(int,class Vector &)
public: __thiscall WaypointGPath::~WaypointGPath(void)
public: __thiscall WaypointGPath::WaypointGPath(class WaypointGPath const &)
public: __thiscall WaypointGPath::WaypointGPath(void)
public: class Vector __thiscall WaypointGPath::Next(class Vector &)
public: class WaypointGPath & __thiscall WaypointGPath::operator=(class WaypointGPath const &)
public: int __thiscall WaypointGPath::DisableWaypoint(int)
public: int __thiscall WaypointGPath::Finished(void)
public: int __thiscall WaypointGPath::PeekPos(int,class Vector &)
public: void __thiscall WaypointGPath::Empty(void)
public: void __thiscall WaypointGPath::EnableAllWaypointsInZone(int)
public: void __thiscall WaypointGPath::Init(class Pathfinder *,float (__cdecl*)(class Waypoint &,class Waypoint &,class Waypoint &),char)
public: void __thiscall WaypointGPath::RecalculatePath(void)
public: void __thiscall WaypointGPath::Step(void)
```

## WaypointGPath2
```cpp
protected: class WaypointPath2 * __thiscall WaypointGPath2::CalculatePreciselyNextSet(void)
protected: int __thiscall WaypointGPath2::InternalGetNext(class Vector &,int &,int &)
protected: int __thiscall WaypointGPath2::Peek(int,class Vector &,class Vector &)
protected: void __thiscall WaypointGPath2::GetPosLast(class Vector &)
protected: void __thiscall WaypointGPath2::PeekDir(int,class Vector &)
public: __thiscall WaypointGPath2::~WaypointGPath2(void)
public: __thiscall WaypointGPath2::WaypointGPath2(class WaypointGPath2 const &)
public: __thiscall WaypointGPath2::WaypointGPath2(void)
public: bool __thiscall WaypointGPath2::Load(class GFile *)
public: bool __thiscall WaypointGPath2::Save(class GFile *)
public: class Vector __thiscall WaypointGPath2::Next(class Vector &)
public: class WaypointGPath2 & __thiscall WaypointGPath2::operator=(class WaypointGPath2 const &)
public: int __thiscall WaypointGPath2::Finished(void)
public: int __thiscall WaypointGPath2::PeekPos(int,class Vector &)
public: void __thiscall WaypointGPath2::Empty(void)
public: void __thiscall WaypointGPath2::Init(class Pathfinder2 *)
public: void __thiscall WaypointGPath2::RecalculatePath(void)
public: void __thiscall WaypointGPath2::SetMoveConstraints(float,float)
public: void __thiscall WaypointGPath2::Step(void)
```

## WaypointPath
```cpp
public: __thiscall WaypointPath::~WaypointPath(void)
public: __thiscall WaypointPath::WaypointPath(class WaypointPath const &)
public: __thiscall WaypointPath::WaypointPath(void)
public: class Vector __thiscall WaypointPath::GetNextFromPath(void)
public: class Vector __thiscall WaypointPath::PeekNextFromPath(void)
public: class WaypointPath & __thiscall WaypointPath::operator=(class WaypointPath const &)
public: int __thiscall WaypointPath::PeekIndex(class Pathfinder *,int,int)
public: int __thiscall WaypointPath::PeekNextIndex(class Pathfinder *,int)
public: int __thiscall WaypointPath::Size(void)
public: void __thiscall WaypointPath::AddAtEnd(class Waypoint *)
public: void __thiscall WaypointPath::Empty(void)
```

## WaypointSet
```cpp
protected: float __thiscall WaypointSet::DistanceBetween(int,int)
protected: int __thiscall WaypointSet::Connected(int,int)
protected: int __thiscall WaypointSet::ConnectionCollidingWithGeometry(class Vector const &,class Vector const &,class World *,class Zone &)
protected: void __thiscall WaypointSet::AddConnection(int,int)
protected: void __thiscall WaypointSet::PackDistances(void)
protected: void __thiscall WaypointSet::RemoveConnection(int,int)
protected: void __thiscall WaypointSet::UnpackDistances(void)
protected: void __thiscall WaypointSet::UnselectUnique(int)
public: __thiscall WaypointSet::~WaypointSet(void)
public: __thiscall WaypointSet::WaypointSet(class WaypointSet const &)
public: __thiscall WaypointSet::WaypointSet(void)
public: class PathfinderFloorSet * __thiscall WaypointSet::GetFloorSet(void)
public: class PathfinderPortalNode * __thiscall WaypointSet::GetConnectionFromPortal(class PathfinderPortalNode *,int,float &,int)
public: class PathfinderPortalNode * __thiscall WaypointSet::GetPortalNode(int)
public: class Waypoint & __thiscall WaypointSet::operator[](int)const 
public: class Waypoint * __thiscall WaypointSet::GetNextAvailableFrom(int,int &,float &)
public: class WaypointSet & __thiscall WaypointSet::operator=(class WaypointSet const &)
public: float __thiscall WaypointSet::GetShortestPath(int,int *,int,float (__cdecl*)(class Waypoint &,class Waypoint &,class Waypoint &),class WaypointPath &,int,class Vector *,float)
public: int __thiscall WaypointSet::AddWaypoint(class Waypoint)
public: int __thiscall WaypointSet::FastGetWaypointIndex(class Waypoint *)
public: int __thiscall WaypointSet::GetIndexOfWaypointClosestTo(class Vector const &,float)
public: int __thiscall WaypointSet::GetWaypointIndex(class Waypoint &)
public: int __thiscall WaypointSet::HasFloors(void)
public: int __thiscall WaypointSet::HowManyPortals(void)
public: int __thiscall WaypointSet::IsDisabled(int)
public: int __thiscall WaypointSet::LoadContents(class GFile *,class DynamicArray<int> *,float)
public: int __thiscall WaypointSet::LoadFloors(struct _iobuf *,float)
public: int __thiscall WaypointSet::NumberOfWaypoints(void)
public: int __thiscall WaypointSet::PickWaypoint(class Viewport &,int,int,float)
public: int __thiscall WaypointSet::SaveContents(class Pathfinder *,class GFile *,float)
public: int __thiscall WaypointSet::SaveFloors(struct _iobuf *,float)
public: int __thiscall WaypointSet::Selection(class Viewport &,float,struct tagRECT *,int)
public: void __thiscall WaypointSet::AddGridOnSelectedFloors(float)
public: void __thiscall WaypointSet::ClearConnections(void)
public: void __thiscall WaypointSet::ClearContents(void)
public: void __thiscall WaypointSet::ClearDistances(void)
public: void __thiscall WaypointSet::ClearFloors(void)
public: void __thiscall WaypointSet::ConnectDisconnectSelected(float,float,int,int)
public: void __thiscall WaypointSet::ConnectSelected(void)
public: void __thiscall WaypointSet::CopySelected(class Vector)
public: void __thiscall WaypointSet::CopyTo(class WaypointSet &)
public: void __thiscall WaypointSet::DisableWaypoint(int)
public: void __thiscall WaypointSet::DisconnectSelected(void)
public: void __thiscall WaypointSet::DisplaceSelected(class Vector const &)
public: void __thiscall WaypointSet::EnableAllWaypoints(void)
public: void __thiscall WaypointSet::EnableWaypoint(int)
public: void __thiscall WaypointSet::FlipFloorZ(void)
public: void __thiscall WaypointSet::FlipZ(void)
public: void __thiscall WaypointSet::GenerateFloors(class World *,int)
public: void __thiscall WaypointSet::GeneratePortalConnections(class DynamicArray<class PathfinderPortalNode *> &,class DynamicArray<int> &)
public: void __thiscall WaypointSet::GetPortalNodesOrderedByDistanceFromPoint(class Vector const &,class DynamicArray<class PathfinderPortalNode *> &,int)
public: void __thiscall WaypointSet::GetWaypointIndicesInBox(class BoundingBox,class DynamicArray<int> &)
public: void __thiscall WaypointSet::InLoadTransformNodeIndicesIntoPointers(class Pathfinder *)
public: void __thiscall WaypointSet::InvertSelection(void)
public: void __thiscall WaypointSet::LevelWaypointsWithFloors(float)
public: void __thiscall WaypointSet::MergeWaypointsAtSamePosition(float)
public: void __thiscall WaypointSet::MoveSelectedToTheZoneTheyreIn(void)
public: void __thiscall WaypointSet::MoveWaypoint(class Waypoint &,class Vector,int)
public: void __thiscall WaypointSet::MoveWaypoint(int,class Vector,int)
public: void __thiscall WaypointSet::RecalculateDistances(int)
public: void __thiscall WaypointSet::RecalculateSelected(void)
public: void __thiscall WaypointSet::RemoveConnectionsCollidingWithGeometryInSelected(class World *,class Zone &)
public: void __thiscall WaypointSet::RemoveSelected(void)
public: void __thiscall WaypointSet::RemoveWaypoint(class Waypoint &)
public: void __thiscall WaypointSet::RemoveWaypoint(int)
public: void __thiscall WaypointSet::RemoveWaypointsNotConnectedToAnything(void)
public: void __thiscall WaypointSet::RemoveWaypointsOutsideOfZoneOrItsPortals(class World *,int,float)
public: void __thiscall WaypointSet::ScaleContents(float)
public: void __thiscall WaypointSet::ScaleFloors(float)
public: void __thiscall WaypointSet::ScaleSelected(class Vector const &,float)
public: void __thiscall WaypointSet::SelectedForAllMonsters(void)
public: void __thiscall WaypointSet::SelectedForSmallMonstersOnly(void)
public: void __thiscall WaypointSet::SelectedSetClearAll(int)
public: void __thiscall WaypointSet::SelectUnique(int)
public: void __thiscall WaypointSet::SelectUnselectWaypointsOnSelectedFloors(int)
public: void __thiscall WaypointSet::SelectWaypointsNotConnectedToAnything(void)
public: void __thiscall WaypointSet::SelectWaypointsOutsideOfZoneOrItsPortals(class World *,int,float)
public: void __thiscall WaypointSet::Test_InitWithWaypointGrid(int)
```

## Window
```cpp
const Window::`vftable'
public: __thiscall Window::~Window(void)
public: __thiscall Window::Window(class Window const &)
public: __thiscall Window::Window(void)
public: class Window & __thiscall Window::operator=(class Window const &)
public: int __thiscall Window::Create(char const *,int,int,int,int,int)
public: int __thiscall Window::Initialize(struct HINSTANCE__ * const,class Window::Resources const &,char const *)
public: int __thiscall Window::IsActive(void)const 
public: void __thiscall Window::AdjustSize(int,int)
public: void __thiscall Window::Destroy(void)
public: void __thiscall Window::GetSizes(int &,int &)const 
public: void __thiscall Window::Hide(void)
public: void __thiscall Window::SetPosition(int,int)
public: void __thiscall Window::SetSize(int,int)
public: void __thiscall Window::Show(void)
```

## World
```cpp
private: int __thiscall World::CreatePlane(class Vector const &,class Vector const &,class Vector const &)
private: void __thiscall World::BuildVolumetrics(void)
private: void __thiscall World::BuildZones(void)
public: __thiscall World::~World(void)
public: __thiscall World::World(class World const &)
public: __thiscall World::World(void)
public: bool __thiscall World::IsAntiPortalEnabled(char const *)
public: bool __thiscall World::IsPortalEnabled(char const *)
public: bool __thiscall World::LoadEntities(class GFile *)
public: bool __thiscall World::LoadGlasses(class GFile *)
public: bool __thiscall World::LoadPortalState(class GFile *)
public: bool __thiscall World::LoadZoneState(class GFile *)
public: bool __thiscall World::NearLadder(class BoundingBox const &)const 
public: bool __thiscall World::NearLadder(class Vector const &)const 
public: bool __thiscall World::OnIce(class BoundingBox const &)const 
public: bool __thiscall World::OnIce(class Vector const &)const 
public: bool __thiscall World::SaveEntities(class GFile *)
public: bool __thiscall World::SaveGlasses(class GFile *)
public: bool __thiscall World::SavePortalState(class GFile *)
public: bool __thiscall World::SaveZoneState(class GFile *)
public: char const * __thiscall World::InDeathZone(class BoundingBox const &)const 
public: char const * __thiscall World::InDeathZone(class Vector const &)const 
public: class Entity * __thiscall World::CreateEntity(int,char const *,char const *,float,bool)
public: class Entity * __thiscall World::GetEntityByIndex(int,int)const 
public: class Entity * __thiscall World::GetEntityByName(char const *)
public: class Entity * __thiscall World::PickEntity(class Viewport &,int,int,int)
public: class Model * __thiscall World::GibModel(class Model *,char const *,float,float,class Entity *,int,int,char const *)
public: class String __thiscall World::GetFirstPackageName(void)
public: class World & __thiscall World::operator=(class World const &)
public: class WorldMesh * __thiscall World::DamageItem(class WorldMesh *,char const *,char const *)
public: int __thiscall World::ExplodeItem(class Entity *,char const *,float,float,float,float,bool,char const *,class Vector)
public: int __thiscall World::FindZone(class Vector const &)
public: int __thiscall World::GetEntities(int,class BoundingBox const &,class Entity * *,int)
public: int __thiscall World::LoadFloors(char const *,float)
public: int __thiscall World::LoadMeshPak(char const *)
public: int __thiscall World::LoadMeshPakFile(char const *)
public: int __thiscall World::LoadWaypoints(char const *,float,bool)
public: int __thiscall World::SaveFloors(char const *,float)
public: int __thiscall World::SaveWaypoints(char const *,float)
public: static class DynamicArray<class WorldMesh *> __cdecl World::LoadMeshPakArray(char const *)
public: static int __cdecl World::SaveMeshPakArray(char const *,class DynamicArray<class WorldMesh *>)
public: void __thiscall World::AddEntity(class Entity *)
public: void __thiscall World::BlurTexture(class Texture *,class Texture *,float,bool)const 
public: void __thiscall World::BuildGlasses(void)
public: void __thiscall World::ClearRenderTargets(void)
public: void __thiscall World::ComposeTextures(class Texture *,class Texture * *,float *,int)const 
public: void __thiscall World::DeleteAntiPortal(char const *)
public: void __thiscall World::DeleteDelayedEntities(void)
public: void __thiscall World::DeleteDyingEntities(void)
public: void __thiscall World::DeleteEntity(class Entity *)
public: void __thiscall World::DeleteEntityDelayed(class Entity *)
public: void __thiscall World::DrawTexture(class Texture *,class Material *)const 
public: void __thiscall World::Dump(void)
public: void __thiscall World::EnableAntiPortal(char const *,bool)
public: void __thiscall World::EnablePortal(char const *,bool)
public: void __thiscall World::Init(float)
public: void __thiscall World::MeshesActiveGroupEnableDraw(unsigned char,bool)
public: void __thiscall World::MeshesActiveGroupRemove(unsigned char)
public: void __thiscall World::MeshesActiveGroupSetCollisionGroup(unsigned char,int)
public: void __thiscall World::MeshesActiveGroupSetTimeToRemove(unsigned char,float,float)
public: void __thiscall World::MeshesSetDefaultCubeMaps(class String const &)
public: void __thiscall World::MeshesSetDefaultDetailMaps(class String const &,float,float)
public: void __thiscall World::MeshesSetDefaultNormalMaps(class String const &)
public: void __thiscall World::MeshesTableToSlotAdd(class WorldMesh *)
public: void __thiscall World::OnSettingsChanged(void)
public: void __thiscall World::ProcessLateVBs(void)
public: void __thiscall World::Release(char const *)
public: void __thiscall World::ReleaseWithoutMap(void)
public: void __thiscall World::RemoveEntity(class Entity *)
public: void __thiscall World::SortEnvironmentByNames(void)
public: void __thiscall World::TestSwitchZones(class Vector const &)
public: void __thiscall World::Tick(float)
public: void __thiscall World::UpdateAllEntities(void)
public: void __thiscall World::UpdateEntity(class Entity *)
public: void __thiscall World::UseSwitchZones(bool)
```

## WorldMesh
```cpp
const WorldMesh::`vftable'{for `Entity'}
const WorldMesh::`vftable'{for `SimpleMesh'}
private: void __thiscall WorldMesh::RenderLightPass(class Light const *)
private: void __thiscall WorldMesh::RenderNTU(class Material *)
private: void __thiscall WorldMesh::RenderShadowPass(class Model *)
private: void __thiscall WorldMesh::RenderShadows(class Model * *,int)
private: void __thiscall WorldMesh::RenderTU2Specular(void)
private: void __thiscall WorldMesh::RenderUberLightPass(class Light const * * const,int)
private: void __thiscall WorldMesh::RenderWater(struct TWater const &)
private: void __thiscall WorldMesh::SetupMaterials(char const *)
public: __thiscall WorldMesh::WorldMesh(class WorldMesh const &)
public: __thiscall WorldMesh::WorldMesh(void)
public: class Plane __thiscall WorldMesh::GetReflectionPlane(void)const 
public: class Vector __thiscall WorldMesh::CenterGeometry(void)
public: class Vector __thiscall WorldMesh::GetRandomPoint(void)
public: class WorldMesh & __thiscall WorldMesh::operator=(class WorldMesh const &)
public: int __thiscall WorldMesh::LoadMesh(class GFile *)
public: int __thiscall WorldMesh::SaveMesh(class GFile *)
public: virtual __thiscall WorldMesh::~WorldMesh(void)
public: virtual bool __thiscall WorldMesh::LoadEntity(class GFile *)
public: virtual bool __thiscall WorldMesh::SaveEntity(class GFile *)
public: virtual char const * __thiscall WorldMesh::GetClassNameA(void)const 
public: virtual void __thiscall WorldMesh::Draw(class RenderDevice *,int)
public: virtual void __thiscall WorldMesh::Load(class GFile *)
public: virtual void __thiscall WorldMesh::RenderCleanup(class RenderDevice *,class MaterialSystem *)
public: virtual void __thiscall WorldMesh::RenderInitialize(class RenderDevice *,class MaterialSystem *,char const *)
public: virtual void __thiscall WorldMesh::Save(class GFile *)
public: virtual void __thiscall WorldMesh::Tick(float)
public: virtual void __thiscall WorldMesh::UpdateVolEntity(void)
public: void __thiscall WorldMesh::AddSpecularLight(class Light *)
public: void __thiscall WorldMesh::CleanupTextures(class RenderDevice *,class MaterialSystem *)
public: void __thiscall WorldMesh::DrawShadow(class Model *)
public: void __thiscall WorldMesh::DrawShadows(void)
public: void __thiscall WorldMesh::DrawZ(class Matrix const &)
public: void __thiscall WorldMesh::GetClosestPoint(class Vector,float &,class Vector &)
public: void __thiscall WorldMesh::InitializeTextures(class RenderDevice *,class MaterialSystem *,char const *)
public: void __thiscall WorldMesh::InitializeVBs(class RenderDevice *)
public: void __thiscall WorldMesh::PreScale(float)
public: void __thiscall WorldMesh::RecomputeBox(void)
public: void __thiscall WorldMesh::ReloadTextures(void)
public: void __thiscall WorldMesh::SetDefaultMaterial(char const *)
public: void __thiscall WorldMesh::SetSpecular(float)
public: void __thiscall WorldMesh::SetupFlags(void)
public: void __thiscall WorldMesh::SetupGlassShader(void)
public: void __thiscall WorldMesh::SetupShaders(void)
public: void __thiscall WorldMesh::SetupWaterShader(void)
public: void __thiscall WorldMesh::TransformToObjectSpace(void)
```

## WorldRegion
```cpp
const WorldRegion::`vftable'
public: __thiscall WorldRegion::WorldRegion(class WorldRegion const &)
public: __thiscall WorldRegion::WorldRegion(void)
public: bool __thiscall WorldRegion::CreateRegionFromPoints(class DynamicArray<class Vector> &,float)
public: class WorldRegion & __thiscall WorldRegion::operator=(class WorldRegion const &)
public: virtual __thiscall WorldRegion::~WorldRegion(void)
public: virtual class Vector & __thiscall WorldRegion::GetPosition(void)
public: virtual void __thiscall WorldRegion::Draw(class RenderDevice *,int)
public: virtual void __thiscall WorldRegion::EnableDraw(bool)
public: virtual void __thiscall WorldRegion::SetPosition(class Vector const &)
public: virtual void __thiscall WorldRegion::Tick(float)
public: void __thiscall WorldRegion::EventTrigger(bool,class Entity *)
public: void __thiscall WorldRegion::SetRegionName(class String)
```

## Zone
```cpp
const Zone::`vftable'
public: __thiscall Zone::Zone(class Zone const &)
public: __thiscall Zone::Zone(void)
public: bool __thiscall Zone::BoxTest(class BoundingBox const &)
public: bool __thiscall Zone::HasPlane(class Plane &)
public: bool __thiscall Zone::IsABoxReally(void)
public: bool __thiscall Zone::PointTest(class Vector const &)
public: bool __thiscall Zone::PortalTest(class Portal const &,float)
public: class Entity * __thiscall Zone::NextEntity(class Entity *)const 
public: class String __thiscall Zone::GetSwitchZoneName(void)
public: class Zone & __thiscall Zone::operator=(class Zone const &)
public: int __thiscall Zone::BoxTestExact(class BoundingBox const &)
public: virtual __thiscall Zone::~Zone(void)
public: virtual char const * __thiscall Zone::GetClassNameA(void)const 
public: virtual void __thiscall Zone::Load(class GFile *)
public: virtual void __thiscall Zone::Save(class GFile *)
public: void __thiscall Zone::AddEntity(class Entity *)
public: void __thiscall Zone::BuildBox(void)
public: void __thiscall Zone::BuildConvex(void)
public: void __thiscall Zone::ClearEntityList(void)
public: void __thiscall Zone::RemoveEntity(class Entity *)
public: void __thiscall Zone::Scale(float)
```

## Free functions / globals
```cpp
bool __cdecl IsBlackEdition(void)
bool __cdecl IsBooHInstalled(void)
bool __cdecl IsPKInstalled(void)
class AbstractFactory<class Object,class String> `public: static class AbstractFactory<class Object,class String> & __cdecl AbstractFactory<class Object,class String>::GetInstance(void)'::`2'::instance
class DemoRecording2 * gDemoRec
class EngineGame * (__cdecl* OurGame)(void)
class GFileManager GFileMan
class GFileManager GFileManAudio
class IdentityMatrix GIdentityMatrix
class LogBuffer GLog
class PCFSystem * GEngine
class Script GScript
class SimpleProfiler GProfiler
class View * (__cdecl* OurView)(void)
float __cdecl GAStarDistanceHeuristic(class Waypoint &,class Waypoint &,class Waypoint &)
m_alloc_free
m_alloc_realloc
m_deallocator
m_reallocator
private: __thiscall AbstractFactory<class Object,class String>::AbstractFactory<class Object,class String>(void)
private: static char const * * StackTracer::NameStack
private: static int const Decal::MAX_ELEMS
private: static int const Trail::MAX_BONES
private: static int const World::MaxPackages
private: static int StackTracer::Count
private: void __thiscall DynamicArray<char>::AppendElements(class DynamicArray<char> const &,int,struct Int2Type<0>)
private: void __thiscall DynamicArray<char>::AppendElements(class DynamicArray<char> const &,int,struct Int2Type<1>)
private: void __thiscall DynamicArray<char>::CopyElements(class DynamicArray<char> const &,struct Int2Type<0>)
private: void __thiscall DynamicArray<char>::CopyElements(class DynamicArray<char> const &,struct Int2Type<1>)
private: void __thiscall DynamicArray<char>::FreeElements(struct Int2Type<0>)
private: void __thiscall DynamicArray<char>::FreeElements(struct Int2Type<1>)
private: void __thiscall DynamicArray<char>::InitElements(struct Int2Type<0>)
private: void __thiscall DynamicArray<char>::InitElements(struct Int2Type<1>)
private: void __thiscall DynamicArray<char>::LoadContents(class GFile &,struct Int2Type<0>)
private: void __thiscall DynamicArray<char>::LoadContents(class GFile &,struct Int2Type<1>)
private: void __thiscall DynamicArray<char>::RemoveElement(int,struct Int2Type<0>)
private: void __thiscall DynamicArray<char>::RemoveElement(int,struct Int2Type<1>)
private: void __thiscall DynamicArray<char>::ResizeNoInit(int)
private: void __thiscall DynamicArray<char>::SaveContents(class GFile &,struct Int2Type<0>)const 
private: void __thiscall DynamicArray<char>::SaveContents(class GFile &,struct Int2Type<1>)const 
protected: static class MessageHandler * * MessageHandler::GHandlerList
protected: static int const InputSystem::MaxEventQueueSize
protected: void __thiscall DynamicArray<char>::ResizeBuffer(void)
public: __thiscall AbstractFactory<class Object,class String>::~AbstractFactory<class Object,class String>(void)
public: __thiscall DynamicArray<char>::~DynamicArray<char>(void)
public: __thiscall DynamicArray<char>::DynamicArray<char>(class DynamicArray<char> const &)
public: __thiscall DynamicArray<char>::DynamicArray<char>(int)
public: __thiscall DynamicArray<char>::DynamicArray<char>(void)
public: char & __thiscall DynamicArray<char>::operator[](int)const 
public: char * __thiscall DynamicArray<char>::Create(void)
public: char * __thiscall DynamicArray<char>::Elements(void)const 
public: class DynamicArray<char> & __thiscall DynamicArray<char>::operator=(class DynamicArray<char> const &)
public: class Object * __thiscall AbstractFactory<class Object,class String>::Create(class String const &)const 
public: int __thiscall DynamicArray<char>::Add(char const &)
public: int __thiscall DynamicArray<char>::AddUnique(char const &)
public: int __thiscall DynamicArray<char>::Find(char const &)
public: int __thiscall DynamicArray<char>::Grow(int)
public: int __thiscall DynamicArray<char>::Size(void)const 
public: static char const * const AnimatedMesh::ClassName
public: static char const * const AnimatedMesh::DefaultExt
public: static char const * const AnimatedMeshMatPal::ClassName
public: static char const * const AntiPortal::ClassName
public: static char const * const Portal::ClassName
public: static char const * const WorldMesh::ClassName
public: static char const * const Zone::ClassName
public: static class AbstractFactory<class Object,class String> & __cdecl AbstractFactory<class Object,class String>::GetInstance(void)
public: static float ScreenVertex::FX
public: static float ScreenVertex::FY
public: static int AntiPortal::Counter
public: static int const AnimatedMeshMatPal::MAX_BONES_PER_MESH
public: static int const ConvexMeshPolygon::MaxPolyVerts
public: static int const ConvexPolygon::MaxPolyVerts
public: static int const Entity::MaxInfluences
public: static int const Entity::MaxLights
public: static int const Entity::MaxZoneLinks
public: static int const Model::MAX_IMPACTS
public: static int const ParticleEffect::MaxEmitters
public: static int const ParticlePool::MaxParticleCount
public: static int const SceneRender::MaxCoronas
public: static int const View::TEX_MBLUR
public: static int const World::TEX_MBLUR
public: static struct FactoryRegistration<class Object,class AnimatedMesh,class String> AnimatedMesh::registration
public: static struct FactoryRegistration<class Object,class AnimatedMeshMatPal,class String> AnimatedMeshMatPal::registration
public: static struct FactoryRegistration<class Object,class AntiPortal,class String> AntiPortal::registration
public: static struct FactoryRegistration<class Object,class Portal,class String> Portal::registration
public: static struct FactoryRegistration<class Object,class WorldMesh,class String> WorldMesh::registration
public: static struct FactoryRegistration<class Object,class Zone,class String> Zone::registration
public: void __thiscall AbstractFactory<class Object,class String>::RegisterCreateFn(class String const &,class Object * (__cdecl*)(void))
public: void __thiscall DynamicArray<char>::Append(class DynamicArray<char> const &)
public: void __thiscall DynamicArray<char>::Clear(int)
public: void __thiscall DynamicArray<char>::FastClear(void)
public: void __thiscall DynamicArray<char>::FastRemove(int)
public: void __thiscall DynamicArray<char>::Insert(char const &,int)
public: void __thiscall DynamicArray<char>::Remove(int)
public: void __thiscall DynamicArray<char>::RemoveLast(void)
public: void __thiscall DynamicArray<char>::Resize(int)
public: void __thiscall DynamicArray<char>::Shrink(int)
public: void __thiscall DynamicArray<char>::Swap(int,int)
S102<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S14<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S17<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S20<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S21<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S23<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S24<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S27<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S28<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S30<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S31<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S33<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S34<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S35<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S36<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S37<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S39<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S4<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S40<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S42<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S43<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S44<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S45<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S46<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S47<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S48<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S49<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S51<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S52<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S53<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S54<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S56<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S57<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S58<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S60<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S61<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S64<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S65<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S77<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S82<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S93<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S97<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
S99<`template-parameter-2',AbstractFactory<class Object,class String>::tInstance, ?? :: ?? ::IA::XZ::XZ::AV2 * const volatile>
struct PFNAPI_DATA_TYPE CDAPFN0506_luaMenu_ActivateMap
struct PFNAPI_DATA_TYPE CDAPFN0506_luaMenu_SetBorderScroller
struct PFNAPI_DATA_TYPE CDAPFN0506_luaMenu_SwitchToLevelSel
struct PFNAPI_DATA_TYPE CDAPFN0506_luaMenu_SwitchToMap
unsigned int const m_alloc_calloc
unsigned int const m_alloc_delete
unsigned int const m_alloc_delete_array
unsigned int const m_alloc_malloc
unsigned int const m_alloc_new
unsigned int const m_alloc_new_array
unsigned int const m_alloc_unknown
unsigned long * GCRCTable
void * __cdecl m_allocator(char const *,unsigned int,char const *,unsigned int,unsigned int)
void * __cdecl m_calloc(unsigned int)
void * __cdecl m_malloc(unsigned int)
void * __cdecl m_malloc_a16(unsigned int)
void * __cdecl m_realloc(void *,unsigned int)
void * __cdecl m_realloc_a16(void *,unsigned int)
void __cdecl m_free(void *)
void __cdecl m_free_a16(void *)
void __cdecl m_setOwner(char const *,unsigned int,char const *)
```

