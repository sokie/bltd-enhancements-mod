#include "bots-internal.hpp"
#include "events.hpp"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

#define EVENTS_LOG(level, format, ...) Log::Write(level, "[Events] " format, __VA_ARGS__)

namespace Bots {
namespace Events {

using UE3::UObject;
using UE3::FVector;
using UE3::FRotator;
using UE3::Field;

struct FColor {
	uint8_t B;
	uint8_t G;
	uint8_t R;
	uint8_t A;
};

struct FLinearColor {
	float R;
	float G;
	float B;
	float A;
};

struct FQuat {
	float X;
	float Y;
	float Z;
	float W;
};

struct Matrix3 {
	float M[3][3];
};

static Matrix3 MatrixOf(const FQuat& q)
{
	float x2 = q.X + q.X, y2 = q.Y + q.Y, z2 = q.Z + q.Z;
	float xx = q.X * x2, xy = q.X * y2, xz = q.X * z2;
	float yy = q.Y * y2, yz = q.Y * z2, zz = q.Z * z2;
	float wx = q.W * x2, wy = q.W * y2, wz = q.W * z2;
	Matrix3 m = { { { 1.0f - (yy + zz), xy + wz, xz - wy }, { xy - wz, 1.0f - (xx + zz), yz + wx }, { xz + wy, yz - wx, 1.0f - (xx + yy) } } };
	return m;
}

static Matrix3 MatrixOf(const FRotator& r)
{
	const float toRadians = 3.14159265f / 32768.0f;
	float sp = sinf(r.Pitch * toRadians), cp = cosf(r.Pitch * toRadians);
	float sy = sinf(r.Yaw * toRadians), cy = cosf(r.Yaw * toRadians);
	float sr = sinf(r.Roll * toRadians), cr = cosf(r.Roll * toRadians);
	Matrix3 m = { { { cp * cy, cp * sy, sp }, { sr * sp * cy - cr * sy, sr * sp * sy + cr * cy, -sr * cp }, { -(cr * sp * cy + sr * sy), cy * sr - cr * sp * sy, cr * cp } } };
	return m;
}

static Matrix3 Multiply(const Matrix3& a, const Matrix3& b)
{
	Matrix3 m = {};
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			m.M[i][j] = a.M[i][0] * b.M[0][j] + a.M[i][1] * b.M[1][j] + a.M[i][2] * b.M[2][j];
		}
	}
	return m;
}

static Matrix3 Transpose(const Matrix3& a)
{
	Matrix3 m = {};
	for (int i = 0; i < 3; i++) {
		for (int j = 0; j < 3; j++) {
			m.M[i][j] = a.M[j][i];
		}
	}
	return m;
}

static FVector Transform(const FVector& v, const Matrix3& m)
{
	FVector out = { v.X * m.M[0][0] + v.Y * m.M[1][0] + v.Z * m.M[2][0], v.X * m.M[0][1] + v.Y * m.M[1][1] + v.Z * m.M[2][1], v.X * m.M[0][2] + v.Y * m.M[1][2] + v.Z * m.M[2][2] };
	return out;
}

static FRotator RotatorOf(const Matrix3& m)
{
	const float toUnits = 32768.0f / 3.14159265f;
	FRotator r = {};
	r.Pitch = (int32_t)(atan2f(m.M[0][2], sqrtf(m.M[0][0] * m.M[0][0] + m.M[0][1] * m.M[0][1])) * toUnits);
	r.Yaw = (int32_t)(atan2f(m.M[0][1], m.M[0][0]) * toUnits);
	r.Roll = 0;
	Matrix3 yawPitch = MatrixOf(r);
	float syY = m.M[2][0] * yawPitch.M[1][0] + m.M[2][1] * yawPitch.M[1][1] + m.M[2][2] * yawPitch.M[1][2];
	float syX = m.M[1][0] * yawPitch.M[1][0] + m.M[1][1] * yawPitch.M[1][1] + m.M[1][2] * yawPitch.M[1][2];
	r.Roll = (int32_t)(atan2f(syY, syX) * toUnits);
	return r;
}

static const char* const SKY_MASTER_MATERIAL = "MT_SkyMaster";
static const char* const SPHERE_MESH = "EngineMeshes.Sphere";
static const char* const GLOW_MATERIAL = "FX_Impact.MT.MT_SphereGlow";
static const char* const HOLO_MATERIAL = "FX_Characters.MT.MT_DeathEffect_Holo";
static const float HEAD_DISSOLVE = 0.15f;
static const FLinearColor HEAD_COLOR = { 1.0f, 0.42f, 0.04f, 1.0f };
static const char* const GLOW_SKIN_MATERIAL = "RefillStations.Material.MT_RefilStations";
static const char* const PUMPKIN_SKIN = "Halloween.TX_Pumpkin_Dif";
static const char* const PUMPKIN_MASK = "Halloween.TX_Pumpkin_Msk";
static const float PUMPKIN_MIN_GLOW = 1.5f;
static const float PUMPKIN_MAX_GLOW = 5.0f;
static const float PUMPKIN_SQUASH = 0.85f;
static const char* const GHOST_MESH = "CH_FoxBLA.Pawn.CH_FoxBL_A";
static const char* const GHOST_ANIM_SET = "AN_Animations.AnimSet.AN_3P_Default_Rifle";
static const char* const GHOST_ANIM = "Idle_A";
static const int GHOST_COUNT = 3;
static const float GHOST_RADIUS = 1500.0f;
static const float GHOST_HEIGHT = 600.0f;
static const float GHOST_TURN_PER_S = 0.11f;
static const float GHOST_BOB = 80.0f;
static const FLinearColor GHOST_COLOR = { 1.2f, 1.8f, 2.6f, 1.0f };
static const float GHOST_DISSOLVE = 0.3f;
static const float GHOST_SPAWN = 1.0f;
static const float GHOST_SCALE = 2.6f;
static const float FLYBY_SCALE = 1.8f;
static const float RISE_SCALE = 1.3f;
static const float PI_F = 3.14159265f;
static const char* const CUBE_MESH = "EngineMeshes.Cube";
static const char* const TOMB_SKIN = "Halloween.TX_Tomb_Dif";
static const char* const TOMB_MASK = "Halloween.TX_Tomb_Msk";
static const char* const STONE_SKIN = "Halloween.TX_Stone_Dif";
static const FVector PLAQUE_SCALE = { 0.235f, 0.004f, 0.34f };
static const int PROP_COUNT = 80;
static const float PROP_SPACING = 400.0f;
static const float FLOOR_TRACE = 400.0f;
static const FVector TOMB_SCALE = { 0.25f, 0.07f, 0.36f };
static const float GROUND_PUMPKIN_SCALE = 0.16f;
static const float SPHERE_RADIUS = 160.0f;
static const float CUBE_HALF = 128.0f;
static const float PAWN_HALF_HEIGHT = 96.0f;
static const float RISE_HEIGHT = 320.0f;
static const DWORD RISE_MS = 4500;
static const DWORD FLYBY_MIN_MS = 50000;
static const DWORD FLYBY_SPREAD_MS = 70000;
static const DWORD FLYBY_MS = 1700;
static const float FLYBY_BEHIND = 500.0f;
static const float FLYBY_AHEAD = 1000.0f;
static const float FLYBY_HEIGHT = 230.0f;
static const char* const FLYBY_SOUND = "A_Common.HRV.Play_HRV_Start";
static const float BURST_RANGE = 150.0f;
static const char* const BURST_EFFECT = "WPN_HE_Frag.FX.PS_HE_Frag_Exp";
static const char* const BURST_SOUND = "WPN_HE_Frag.Audio.Play_Explo_Frag";
static const char* const EMITTER_CLASS = "Engine.EmitterSpawnable";
static const char* const MENU_LEVEL = "FoxEntry";
static const char* const BLACK_OPS_PREFIX = "BO";
static const DWORD HEADS_INTERVAL_MS = 500;
static const size_t HEAD_LIMIT = 24;
static const DWORD FLASH_MIN_MS = 15000;
static const DWORD FLASH_SPREAD_MS = 35000;
static const DWORD FLASH_STEP_MS[4] = { 70, 60, 110, 0 };
static const FLinearColor SKY_TINT = { 0.42f, 0.30f, 0.70f, 1.0f };
static const float SKY_DESATURATION = 0.25f;
static const float SUN_SCALE = 0.55f;
static const FColor SUN_COLOR = { 175, 105, 125, 255 };
static const FColor FOG_COLOR = { 95, 35, 55, 255 };
static const float FOG_MIN_DENSITY = 0.00045f;
static const float FOG_SCALE = 3.0f;
static const float FOG_HEIGHT_ABOVE = 20000.0f;
static const float FOG_BRIGHTNESS = 0.5f;
static const FVector MOON_OFFSET = { 3200.0f, 3200.0f, 3300.0f };
static const float MOON_SCALE = 9.0f;
static const float VIEW_DESATURATION = 0.45f;
static const float VIEW_BLOOM = 1.4f;
static const float HEAD_SCALE = 0.16f;
static const float HEAD_LIFT = 12.0f;
static const float HEAD_MIN_GLOW = 4.0f;
static const float HEAD_MAX_GLOW = 9.0f;
static const char* const HELMET_MATERIAL_TEXT = "helmet";
static const char* const TRAIL_MATERIAL = "FX_Characters.MT.MT_SpawnTrl_CH";
static const char* const GHOST_SKIN = "Halloween.TX_Ghost_Dif";
static const char* const GHOST_MASK = "Halloween.TX_Ghost_Msk";
static const FLinearColor TRAIL_COLOR = { 1.5f, 0.7f, 0.15f, 1.0f };
static const float TRAIL_INTERP = 0.75f;
static const wchar_t* const SOUND_PACKAGE = L"Halloween";
static const char* const SOUND_BANKS[] = { "SB_Floodgate", "SB_SkyFire", "SB_Brutalism" };
static const char* const THUNDER_SOUND = "Halloween.Play_Thunder_ST";
static const char* const BELL_SOUND = "Halloween.Play_Church_Bell_01";
static const char* const AMBIENT_SOUNDS[] = { "Halloween.Play_Dog_Bark_Sequence", "Halloween.Play_Dist_Metal_Rumble", "Halloween.Play_Water_Drips", "Halloween.Play_Dog_Bark_Sequence" };
static const DWORD THUNDER_DELAY_MIN_MS = 50;
static const DWORD THUNDER_DELAY_SPREAD_MS = 250;
static const DWORD BELL_DELAY_MS = 6000;
static const DWORD AMBIENT_MIN_MS = 35000;
static const DWORD AMBIENT_SPREAD_MS = 60000;
struct Retexture {
	const char* Instance;
	bool Prefix;
	const char* Parameter;
	const char* Texture;
};
static const Retexture RETEXTURES[] = {
	{ "MI_Retail_", true, "Diffuse", "Halloween.TX_Retail_Dif" },
	{ "MI_SignNeonPanel", false, "Diffuse", "Halloween.TX_Neon_Dif" },
	{ "MI_SignNeonPanel", false, "Mask", "Halloween.TX_Neon_Msk" },
	{ "MI_SignNeonPanel_D", false, "Diffuse", "Halloween.TX_Neon_Dif" },
	{ "MI_SignNeonPanel_D", false, "Mask", "Halloween.TX_Neon_Msk" },
	{ "MI_ProjectorSign_1x1_D", false, "Diffuse", "Halloween.TX_Holo_Square_Dif" },
	{ "MI_ProjectorSign_2x1_D", false, "Diffuse", "Halloween.TX_Holo_Wide_Dif" },
};
static const char* const GRAFFITI_MATERIAL = "DM_Graff_Channel";
static const char* const GRAFFITI_PREFIX = "MI_Graff";
static const char* const GRAFFITI[] = { "Halloween.TX_Graff_Pumpkin", "Halloween.TX_Graff_Bats", "Halloween.TX_Graff_Boo", "Halloween.TX_Graff_Skull",
	"Halloween.TX_Graff_Web", "Halloween.TX_Graff_Ghost", "Halloween.TX_Graff_TrickOrTreat", "Halloween.TX_Graff_Happy" };
struct GlowKind {
	const char* Parent;
	const char* Parameter;
	bool Vector;
};
static const GlowKind GLOW_KINDS[] = {
	{ "MT_SpecConstant_Emissive_Opaque_Panner", "Emissive_Mult", false },
	{ "MT_ProjectorSigns", "Emissive_Mult", false },
	{ "MT_HoloSigns", "Emissive_Mult", false },
	{ "MT_Neon_Signs", "Emmisive_Mult", false },
	{ "MT_BackgroundBldg_Night_Mask", "Window Brightness", false },
	{ "MT_Glow", "Emissive_Color", true },
	{ "MT_Standard_Emissive_Opaque", "Emiss_Mult", true },
};
struct SurgeStep {
	DWORD Ms;
	float Level;
};
static const SurgeStep SURGE[] = { { 80, 0.1f }, { 60, 1.0f }, { 190, 0.05f }, { 70, 0.7f }, { 120, 0.15f }, { 0, 1.0f } };
static const int SURGE_PERCENT = 50;
static const int32_t BLOOD_MOON_SECONDS = 60;
static const int32_t LAST_BELL_SECONDS = 10;
static const DWORD BLOOD_FADE_MS = 6000;
static const DWORD BLOOD_UPDATE_MS = 100;
static const FLinearColor BLOOD_SKY = { 0.75f, 0.08f, 0.08f, 1.0f };
static const FColor BLOOD_FOG = { 18, 10, 120, 255 };
static const FLinearColor BLOOD_MOON_COLOR = { 4.0f, 0.15f, 0.06f, 1.0f };
static const float BLOOD_MOON_GROWTH = 0.3f;
static const FLinearColor BLOOD_GHOST_COLOR = { 2.6f, 0.35f, 0.30f, 1.0f };

struct Refs {
	UObject* LocalPlayerClass;
	UObject* MaterialInstanceClass;
	UObject* DominantLightComponentClass;
	UObject* HeightFogComponentClass;
	UObject* InterpActorClass;
	UObject* SetVectorParameter;
	UObject* SetScalarParameter;
	UObject* SetTextureParameter;
	UObject* SetLightProperties;
	UObject* ReattachComponent;
	UObject* SetStaticMesh;
	UObject* SetMaterial;
	UObject* CreateMaterialInstance;
	UObject* SetBase;
	UObject* SetHardAttach;
	UObject* SetCollision;
	UObject* SetDrawScale3D;
	UObject* SetTemplate;
	UObject* DestroyOnFinishProperty;
	UObject* Trace;
	UObject* GetBoneQuaternion;
	UObject* PostAkEvent;
	UObject* LoadBank;
	UObject* GlobalProperty;
	UObject* HardAttachProperty;
	UObject* ColActorsProperty;
	UObject* BlockActorsProperty;
	UObject* IgnoreEncroachersProperty;
	UObject* OverrideProperty;
	UObject* RecoveryProperty;
	UObject* ClearOverride;
	UObject* OverrideDesaturation;
	UObject* OverrideHighLights;
	UObject* OverrideMidTones;
	UObject* OverrideShadows;
	UObject* OverrideEnableBloom;
	UObject* OverrideBloomScale;
	UObject* EnableBloom;
	UObject* EnableDOF;
};

struct EventOffsets {
	int32_t SetVector_Name;
	int32_t SetVector_Value;
	int32_t SetScalar_Name;
	int32_t SetScalar_Value;
	int32_t SetTexture_Name;
	int32_t SetTexture_Value;
	int32_t SetLight_Brightness;
	int32_t SetLight_Color;
	int32_t Reattach_Component;
	int32_t SetMesh_Mesh;
	int32_t SetMesh_Scale;
	int32_t SetMaterial_Material;
	int32_t SetMaterial_Index;
	int32_t CreateInstance_Index;
	int32_t CreateInstance_Return;
	int32_t SetBase_NewBase;
	int32_t SetBase_SkelComp;
	int32_t SetBase_AttachName;
	int32_t SetScale3D_Scale;
	int32_t SetTemplate_Template;
	int32_t Trace_HitLocation;
	int32_t Trace_TraceEnd;
	int32_t Trace_TraceStart;
	int32_t Trace_ReturnValue;
	int32_t BoneQuat_Name;
	int32_t BoneQuat_Space;
	int32_t BoneQuat_Return;
	int32_t PostAkEvent_Event;
	int32_t LoadBank_Name;
	int32_t PC_Player;
	int32_t MaterialInstance_Parent;
	int32_t Light_Brightness;
	int32_t Light_Color;
	int32_t Fog_Density;
	int32_t Fog_Color;
	int32_t Fog_StartDistance;
	int32_t DynamicSM_Component;
	int32_t Mesh_Materials;
	int32_t Actor_RemoteRole;
	int32_t Actor_RelativeLocation;
	int32_t Actor_RelativeRotation;
	int32_t LP_Settings;
	int32_t PP_Desaturation;
	int32_t PP_HighLights;
	int32_t PP_MidTones;
	int32_t PP_Shadows;
	int32_t PP_BloomScale;
};

struct GhostRefs {
	UObject* ActorClass;
	UObject* SetSkeletalMesh;
	UObject* KeepSpaceBasesProperty;
	UObject* UpdateAnimations;
	UObject* PlayAnim;
	UObject* LoopProperty;
	UObject* RestartProperty;
	UObject* SetRotation;
	int32_t SetMesh_Mesh;
	int32_t PlayAnim_Name;
	int32_t SetRotation_Rotation;
	int32_t Actor_Component;
	int32_t Component_AnimSets;
	int32_t Mesh_Materials;
};

struct Ghost {
	UObject* Actor;
	float Phase;
	std::vector<UObject*> Instances;
};

struct ExtraRefs {
	UObject* GetScalarParameter;
	UObject* GetScalarReturn;
	UObject* GetVectorParameter;
	UObject* GetVectorReturn;
	UObject* MatchIsOverProperty;
	int32_t GetScalar_Name;
	int32_t GetScalar_Value;
	int32_t GetVector_Name;
	int32_t GetVector_Value;
	int32_t World_GRI;
	int32_t GRI_TimeLimit;
	int32_t GRI_RemainingTime;
};

struct Glow {
	UObject* Instance;
	int32_t Name;
	bool Vector;
	float Scalar;
	FLinearColor Color;
};

struct Transient {
	UObject* Actor;
	std::vector<UObject*> Instances;
	DWORD StartTick;
	DWORD DurationMs;
	FVector Start;
	FVector End;
};

struct GroundPumpkin {
	UObject* Actor;
	FVector Location;
};

struct Light {
	UObject* Component;
	float Brightness;
	FColor Color;
};


struct Head {
	UObject* Pawn;
	UObject* Actor;
	int32_t HelmetElement;
	UObject* HelmetInstance;
};

static Refs ref;
static EventOffsets eoff;
static int32_t name_final_rgb = -1;
static int32_t name_final_desaturation = -1;
static int32_t name_head = -1;
static int32_t name_holo_color = -1;
static int32_t name_emissive_color = -1;
static int32_t name_dissolve = -1;
static int32_t name_diffuse = -1;
static int32_t name_mask = -1;
static int32_t name_min_emis = -1;
static int32_t name_max_emis = -1;
static int32_t name_htv_mode = -1;
static bool resolved = false;
static bool resolve_failed = false;
static EventsConfig events_config = { HALLOWEEN_AUTO };
static std::string level;
static bool active = false;
static std::vector<Light> lights;
static std::vector<Head> heads;
static UObject* moon = 0;
static UObject* local_player = 0;
static DWORD next_flash_tick = 0;
static DWORD flash_step_tick = 0;
static int flash_step = -1;
static DWORD next_heads_tick = 0;
static GhostRefs gref;
static int ghosts_resolved = 0;
static std::vector<Ghost> ghosts;
static std::vector<Transient> transients;
static std::vector<UObject*> props;
static std::vector<GroundPumpkin> ground_pumpkins;
static DWORD next_flyby_tick = 0;
static DWORD next_burst_check_tick = 0;
static FVector ghost_center;
static DWORD ghost_start_tick = 0;
static bool sounds_loaded = false;
static UObject* local_controller = 0;
static DWORD thunder_tick = 0;
static DWORD bell_tick = 0;
static DWORD next_ambient_tick = 0;
static ExtraRefs xref;
static int extra_resolved = 0;
static std::vector<UObject*> sky_materials;
static std::vector<UObject*> fog_components;
static std::vector<Glow> glows;
static int surge_step = -1;
static DWORD surge_step_tick = 0;
static float blood = 0.0f;
static bool blood_wanted = false;
static bool blood_forced = false;
static bool last_bell_rung = false;
static DWORD blood_update_tick = 0;
static UObject* blood_moon = 0;
static UObject* blood_moon_instance = 0;
static bool moon_here = false;

static bool Resolve()
{
	if (resolved || resolve_failed) {
		return resolved;
	}
	struct ObjectRef {
		UObject** Target;
		const char* Path;
	};
	struct OffsetRef {
		int32_t* Target;
		const char* Path;
	};
	static const ObjectRef OBJECTS[] = {
		{ &ref.LocalPlayerClass, "Engine.LocalPlayer" },
		{ &ref.MaterialInstanceClass, "Engine.MaterialInstanceConstant" },
		{ &ref.DominantLightComponentClass, "Engine.DominantDirectionalLightComponent" },
		{ &ref.HeightFogComponentClass, "Engine.HeightFogComponent" },
		{ &ref.InterpActorClass, "Engine.DynamicSMActor_Spawnable" },
		{ &ref.SetVectorParameter, "Engine.MaterialInstanceConstant.SetVectorParameterValue" },
		{ &ref.SetScalarParameter, "Engine.MaterialInstanceConstant.SetScalarParameterValue" },
		{ &ref.SetTextureParameter, "Engine.MaterialInstanceConstant.SetTextureParameterValue" },
		{ &ref.SetLightProperties, "Engine.LightComponent.SetLightProperties" },
		{ &ref.ReattachComponent, "Engine.Actor.ReattachComponent" },
		{ &ref.SetStaticMesh, "Engine.DynamicSMActor.SetStaticMesh" },
		{ &ref.SetMaterial, "Engine.MeshComponent.SetMaterial" },
		{ &ref.CreateMaterialInstance, "Engine.MeshComponent.CreateAndSetMaterialInstanceConstant" },
		{ &ref.SetBase, "Engine.Actor.SetBase" },
		{ &ref.SetHardAttach, "Engine.Actor.SetHardAttach" },
		{ &ref.SetCollision, "Engine.Actor.SetCollision" },
		{ &ref.SetDrawScale3D, "Engine.Actor.SetDrawScale3D" },
		{ &ref.SetTemplate, "Engine.Emitter.SetTemplate" },
		{ &ref.DestroyOnFinishProperty, "Engine.Emitter.SetTemplate.bDestroyOnFinish" },
		{ &ref.Trace, "Engine.Actor.Trace" },
		{ &ref.GetBoneQuaternion, "Engine.SkeletalMeshComponent.GetBoneQuaternion" },
		{ &ref.PostAkEvent, "Engine.Actor.PostAkEvent" },
		{ &ref.LoadBank, "Engine.PlayerController.WwiseClientLoadBank" },
		{ &ref.GlobalProperty, "Engine.Actor.PostAkEvent.bGlobal" },
		{ &ref.HardAttachProperty, "Engine.Actor.SetHardAttach.bNewHardAttach" },
		{ &ref.ColActorsProperty, "Engine.Actor.SetCollision.bNewColActors" },
		{ &ref.BlockActorsProperty, "Engine.Actor.SetCollision.bNewBlockActors" },
		{ &ref.IgnoreEncroachersProperty, "Engine.Actor.SetCollision.bNewIgnoreEncroachers" },
		{ &ref.OverrideProperty, "Engine.LocalPlayer.bOverridePostProcessSettings" },
		{ &ref.RecoveryProperty, "Engine.LocalPlayer.bEnablePostProcessOverrideRecovery" },
		{ &ref.ClearOverride, "Engine.LocalPlayer.ClearPostProcessSettingsOverride" },
		{ &ref.OverrideDesaturation, "Engine.PostProcessVolume.PostProcessSettings.bOverride_Scene_Desaturation" },
		{ &ref.OverrideHighLights, "Engine.PostProcessVolume.PostProcessSettings.bOverride_Scene_HighLights" },
		{ &ref.OverrideMidTones, "Engine.PostProcessVolume.PostProcessSettings.bOverride_Scene_MidTones" },
		{ &ref.OverrideShadows, "Engine.PostProcessVolume.PostProcessSettings.bOverride_Scene_Shadows" },
		{ &ref.OverrideEnableBloom, "Engine.PostProcessVolume.PostProcessSettings.bOverride_EnableBloom" },
		{ &ref.OverrideBloomScale, "Engine.PostProcessVolume.PostProcessSettings.bOverride_Bloom_Scale" },
		{ &ref.EnableBloom, "Engine.PostProcessVolume.PostProcessSettings.bEnableBloom" },
		{ &ref.EnableDOF, "Engine.PostProcessVolume.PostProcessSettings.bEnableDOF" },
	};
	static const OffsetRef OFFSETS[] = {
		{ &eoff.SetVector_Name, "Engine.MaterialInstanceConstant.SetVectorParameterValue.ParameterName" },
		{ &eoff.SetVector_Value, "Engine.MaterialInstanceConstant.SetVectorParameterValue.Value" },
		{ &eoff.SetScalar_Name, "Engine.MaterialInstanceConstant.SetScalarParameterValue.ParameterName" },
		{ &eoff.SetScalar_Value, "Engine.MaterialInstanceConstant.SetScalarParameterValue.Value" },
		{ &eoff.SetTexture_Name, "Engine.MaterialInstanceConstant.SetTextureParameterValue.ParameterName" },
		{ &eoff.SetTexture_Value, "Engine.MaterialInstanceConstant.SetTextureParameterValue.Value" },
		{ &eoff.SetLight_Brightness, "Engine.LightComponent.SetLightProperties.NewBrightness" },
		{ &eoff.SetLight_Color, "Engine.LightComponent.SetLightProperties.NewLightColor" },
		{ &eoff.Reattach_Component, "Engine.Actor.ReattachComponent.ComponentToReattach" },
		{ &eoff.SetMesh_Mesh, "Engine.DynamicSMActor.SetStaticMesh.NewMesh" },
		{ &eoff.SetMesh_Scale, "Engine.DynamicSMActor.SetStaticMesh.NewScale3D" },
		{ &eoff.SetMaterial_Material, "Engine.MeshComponent.SetMaterial.Material" },
		{ &eoff.SetMaterial_Index, "Engine.MeshComponent.SetMaterial.ElementIndex" },
		{ &eoff.CreateInstance_Index, "Engine.MeshComponent.CreateAndSetMaterialInstanceConstant.ElementIndex" },
		{ &eoff.CreateInstance_Return, "Engine.MeshComponent.CreateAndSetMaterialInstanceConstant.ReturnValue" },
		{ &eoff.SetBase_NewBase, "Engine.Actor.SetBase.NewBase" },
		{ &eoff.SetBase_SkelComp, "Engine.Actor.SetBase.SkelComp" },
		{ &eoff.SetBase_AttachName, "Engine.Actor.SetBase.AttachName" },
		{ &eoff.SetScale3D_Scale, "Engine.Actor.SetDrawScale3D.NewScale3D" },
		{ &eoff.SetTemplate_Template, "Engine.Emitter.SetTemplate.NewTemplate" },
		{ &eoff.Trace_HitLocation, "Engine.Actor.Trace.HitLocation" },
		{ &eoff.Trace_TraceEnd, "Engine.Actor.Trace.TraceEnd" },
		{ &eoff.Trace_TraceStart, "Engine.Actor.Trace.TraceStart" },
		{ &eoff.Trace_ReturnValue, "Engine.Actor.Trace.ReturnValue" },
		{ &eoff.BoneQuat_Name, "Engine.SkeletalMeshComponent.GetBoneQuaternion.BoneName" },
		{ &eoff.BoneQuat_Space, "Engine.SkeletalMeshComponent.GetBoneQuaternion.Space" },
		{ &eoff.BoneQuat_Return, "Engine.SkeletalMeshComponent.GetBoneQuaternion.ReturnValue" },
		{ &eoff.PostAkEvent_Event, "Engine.Actor.PostAkEvent.InAkEvent" },
		{ &eoff.LoadBank_Name, "Engine.PlayerController.WwiseClientLoadBank.BankName" },
		{ &eoff.PC_Player, "Engine.PlayerController.Player" },
		{ &eoff.MaterialInstance_Parent, "Engine.MaterialInstance.Parent" },
		{ &eoff.Light_Brightness, "Engine.LightComponent.Brightness" },
		{ &eoff.Light_Color, "Engine.LightComponent.LightColor" },
		{ &eoff.Fog_Density, "Engine.HeightFogComponent.Density" },
		{ &eoff.Fog_Color, "Engine.HeightFogComponent.LightColor" },
		{ &eoff.Fog_StartDistance, "Engine.HeightFogComponent.StartDistance" },
		{ &eoff.DynamicSM_Component, "Engine.DynamicSMActor.StaticMeshComponent" },
		{ &eoff.Mesh_Materials, "Engine.MeshComponent.Materials" },
		{ &eoff.Actor_RemoteRole, "Engine.Actor.RemoteRole" },
		{ &eoff.Actor_RelativeLocation, "Engine.Actor.RelativeLocation" },
		{ &eoff.Actor_RelativeRotation, "Engine.Actor.RelativeRotation" },
		{ &eoff.LP_Settings, "Engine.LocalPlayer.PostProcessSettingsOverride" },
		{ &eoff.PP_Desaturation, "Engine.PostProcessVolume.PostProcessSettings.Scene_Desaturation" },
		{ &eoff.PP_HighLights, "Engine.PostProcessVolume.PostProcessSettings.Scene_HighLights" },
		{ &eoff.PP_MidTones, "Engine.PostProcessVolume.PostProcessSettings.Scene_MidTones" },
		{ &eoff.PP_Shadows, "Engine.PostProcessVolume.PostProcessSettings.Scene_Shadows" },
		{ &eoff.PP_BloomScale, "Engine.PostProcessVolume.PostProcessSettings.Bloom_Scale" },
	};
	for (const ObjectRef& entry : OBJECTS) {
		*entry.Target = UE3::FindObject(entry.Path);
		if (!*entry.Target) {
			EVENTS_LOG(LOG_LEVEL_ERROR, "%s was not found, the events are off.", entry.Path);
			resolve_failed = true;
			return false;
		}
	}
	for (const OffsetRef& entry : OFFSETS) {
		*entry.Target = UE3::FindPropertyOffset(entry.Path);
		if (*entry.Target < 0) {
			EVENTS_LOG(LOG_LEVEL_ERROR, "%s was not found, the events are off.", entry.Path);
			resolve_failed = true;
			return false;
		}
	}
	resolved = true;
	return true;
}

static bool ResolveExtra()
{
	if (extra_resolved) {
		return extra_resolved > 0;
	}
	struct ObjectRef {
		UObject** Target;
		const char* Path;
	};
	struct OffsetRef {
		int32_t* Target;
		const char* Path;
	};
	static const ObjectRef OBJECTS[] = {
		{ &xref.GetScalarParameter, "Engine.MaterialInterface.GetScalarParameterValue" },
		{ &xref.GetScalarReturn, "Engine.MaterialInterface.GetScalarParameterValue.ReturnValue" },
		{ &xref.GetVectorParameter, "Engine.MaterialInterface.GetVectorParameterValue" },
		{ &xref.GetVectorReturn, "Engine.MaterialInterface.GetVectorParameterValue.ReturnValue" },
		{ &xref.MatchIsOverProperty, "Engine.GameReplicationInfo.bMatchIsOver" },
	};
	static const OffsetRef OFFSETS[] = {
		{ &xref.GetScalar_Name, "Engine.MaterialInterface.GetScalarParameterValue.ParameterName" },
		{ &xref.GetScalar_Value, "Engine.MaterialInterface.GetScalarParameterValue.OutValue" },
		{ &xref.GetVector_Name, "Engine.MaterialInterface.GetVectorParameterValue.ParameterName" },
		{ &xref.GetVector_Value, "Engine.MaterialInterface.GetVectorParameterValue.OutValue" },
		{ &xref.World_GRI, "Engine.WorldInfo.GRI" },
		{ &xref.GRI_TimeLimit, "Engine.GameReplicationInfo.TimeLimit" },
		{ &xref.GRI_RemainingTime, "Engine.GameReplicationInfo.RemainingTime" },
	};
	extra_resolved = 1;
	for (const ObjectRef& entry : OBJECTS) {
		*entry.Target = UE3::FindObject(entry.Path);
		if (!*entry.Target) {
			EVENTS_LOG(LOG_LEVEL_WARN, "%s was not found, the theme has no power surge or blood moon.", entry.Path);
			extra_resolved = -1;
		}
	}
	for (const OffsetRef& entry : OFFSETS) {
		*entry.Target = UE3::FindPropertyOffset(entry.Path);
		if (*entry.Target < 0) {
			EVENTS_LOG(LOG_LEVEL_WARN, "%s was not found, the theme has no power surge or blood moon.", entry.Path);
			extra_resolved = -1;
		}
	}
	return extra_resolved > 0;
}

static void Call(UObject* object, UObject* function, std::vector<uint8_t>& parms)
{
	UE3::ProcessNativeEvent(object, function, parms.data());
}

static std::vector<uint8_t> Parms(UObject* function)
{
	return std::vector<uint8_t>(UE3::FunctionParmsSize(function) + 16, 0);
}

static void SetBit(void* base, UObject* property, bool value)
{
	UE3::SetBoolProperty((UObject*)base, property, value);
}

static UE3::FName Name(int32_t index)
{
	UE3::FName name = { index, 0 };
	return name;
}

static UObject* Outer(UObject* object)
{
	return object ? Field<UObject*>(object, UE3::Offset::ObjectOuter) : 0;
}

static const char* LevelName(UObject* controller)
{
	UObject* package = Outer(Outer(Outer(controller)));
	return package ? UE3::ObjectName(package) : "";
}

static bool IsHalloweenToday()
{
	SYSTEMTIME now;
	GetLocalTime(&now);
	return now.wMonth == 10 && now.wDay == 31;
}

static bool ThemeWanted()
{
	LoadEventsConfig(events_config);
	if (GetEnvironmentVariableA("BLTD_HALLOWEEN", 0, 0) > 0) {
		return true;
	}
	return events_config.Halloween == HALLOWEEN_ON || (events_config.Halloween == HALLOWEEN_AUTO && IsHalloweenToday());
}

static void SetVectorParameter(UObject* material, int32_t name, const FLinearColor& value)
{
	std::vector<uint8_t> parms = Parms(ref.SetVectorParameter);
	Field<UE3::FName>(parms.data(), eoff.SetVector_Name) = Name(name);
	Field<FLinearColor>(parms.data(), eoff.SetVector_Value) = value;
	Call(material, ref.SetVectorParameter, parms);
}

static void SetScalarParameter(UObject* material, int32_t name, float value)
{
	std::vector<uint8_t> parms = Parms(ref.SetScalarParameter);
	Field<UE3::FName>(parms.data(), eoff.SetScalar_Name) = Name(name);
	Field<float>(parms.data(), eoff.SetScalar_Value) = value;
	Call(material, ref.SetScalarParameter, parms);
}

static void SetTextureParameter(UObject* material, int32_t name, UObject* texture)
{
	std::vector<uint8_t> parms = Parms(ref.SetTextureParameter);
	Field<UE3::FName>(parms.data(), eoff.SetTexture_Name) = Name(name);
	Field<UObject*>(parms.data(), eoff.SetTexture_Value) = texture;
	Call(material, ref.SetTextureParameter, parms);
}

static bool GetScalarParameter(UObject* material, int32_t name, float& value)
{
	std::vector<uint8_t> parms = Parms(xref.GetScalarParameter);
	Field<UE3::FName>(parms.data(), xref.GetScalar_Name) = Name(name);
	Call(material, xref.GetScalarParameter, parms);
	value = Field<float>(parms.data(), xref.GetScalar_Value);
	return UE3::GetBoolProperty((UObject*)parms.data(), xref.GetScalarReturn);
}

static bool GetVectorParameter(UObject* material, int32_t name, FLinearColor& value)
{
	std::vector<uint8_t> parms = Parms(xref.GetVectorParameter);
	Field<UE3::FName>(parms.data(), xref.GetVector_Name) = Name(name);
	Call(material, xref.GetVectorParameter, parms);
	value = Field<FLinearColor>(parms.data(), xref.GetVector_Value);
	return UE3::GetBoolProperty((UObject*)parms.data(), xref.GetVectorReturn);
}

static float Mix(float a, float b, float t)
{
	return a + (b - a) * t;
}

static FLinearColor Mix(const FLinearColor& a, const FLinearColor& b, float t)
{
	FLinearColor out = { Mix(a.R, b.R, t), Mix(a.G, b.G, t), Mix(a.B, b.B, t), Mix(a.A, b.A, t) };
	return out;
}

static FColor Mix(const FColor& a, const FColor& b, float t)
{
	FColor out = { (uint8_t)Mix(a.B, b.B, t), (uint8_t)Mix(a.G, b.G, t), (uint8_t)Mix(a.R, b.R, t), (uint8_t)Mix(a.A, b.A, t) };
	return out;
}

static FLinearColor Scaled(const FLinearColor& color, float scale)
{
	FLinearColor out = { color.R * scale, color.G * scale, color.B * scale, color.A };
	return out;
}

static void SetLight(UObject* component, float brightness, const FColor& color)
{
	std::vector<uint8_t> parms = Parms(ref.SetLightProperties);
	Field<float>(parms.data(), eoff.SetLight_Brightness) = brightness;
	Field<FColor>(parms.data(), eoff.SetLight_Color) = color;
	Call(component, ref.SetLightProperties, parms);
}

static void Reattach(UObject* actor, UObject* component)
{
	std::vector<uint8_t> parms = Parms(ref.ReattachComponent);
	Field<UObject*>(parms.data(), eoff.Reattach_Component) = component;
	Call(actor, ref.ReattachComponent, parms);
}

static void SetScale(UObject* actor, float scale)
{
	std::vector<uint8_t> parms = Parms(ref.SetDrawScale3D);
	Field<FVector>(parms.data(), eoff.SetScale3D_Scale) = FVector{ scale, scale, scale };
	Call(actor, ref.SetDrawScale3D, parms);
}

static void SetCollisionOff(UObject* actor)
{
	std::vector<uint8_t> parms = Parms(ref.SetCollision);
	SetBit(parms.data(), ref.ColActorsProperty, false);
	SetBit(parms.data(), ref.BlockActorsProperty, false);
	SetBit(parms.data(), ref.IgnoreEncroachersProperty, true);
	Call(actor, ref.SetCollision, parms);
}

static void SetMaterial(UObject* component, int32_t element, UObject* material);
static bool ColourElement(UObject* component, int32_t element, const FLinearColor& color, float dissolve);
static bool SkinMesh(UObject* actor, UObject* skin, UObject* mask, float min_glow, float max_glow);
static bool SkinsReady();
static bool GhostsReady();
static UObject* SpawnGhostActor(UObject* controller, const FVector& location, int32_t yaw, float scale, std::vector<UObject*>* instances);
static UObject* InstanceMaterial(UObject* component, int32_t element);
static bool PostSound(UObject* controller, const char* path);
static bool PostSoundOn(UObject* actor, const char* path, bool global);
static float EnvFloat(const char* name, float fallback);
static bool TrailStyle();
static bool SkinMeshTrail(UObject* actor, UObject* skin, const FLinearColor& color, float interp);
static UObject* HeadSkin(bool mask);

static UObject* SpawnMeshActor(UObject* spawner, const FVector& location, const FVector& scale3D, UObject* mesh, UObject* material, int32_t yaw = 0)
{
	FRotator rotation = { 0, yaw & 0xFFFF, 0 };
	UObject* actor = SpawnActor(spawner, ref.InterpActorClass, location, rotation);
	if (!actor) {
		EVENTS_LOG(LOG_LEVEL_WARN, "No mesh actor spawned at (%.0f %.0f %.0f).", location.X, location.Y, location.Z);
		return 0;
	}
	Field<uint8_t>(actor, eoff.Actor_RemoteRole) = 0;
	SetCollisionOff(actor);
	std::vector<uint8_t> parms = Parms(ref.SetStaticMesh);
	Field<UObject*>(parms.data(), eoff.SetMesh_Mesh) = mesh;
	Field<FVector>(parms.data(), eoff.SetMesh_Scale) = scale3D;
	Call(actor, ref.SetStaticMesh, parms);
	UObject* component = Field<UObject*>(actor, eoff.DynamicSM_Component);
	if (component && material) {
		SetMaterial(component, 0, material);
	}
	return actor;
}

static UObject* SpawnMeshActor(UObject* spawner, const FVector& location, float scale, UObject* mesh, UObject* material)
{
	FVector scale3D = { scale, scale, scale };
	return SpawnMeshActor(spawner, location, scale3D, mesh, material);
}

static bool FloorBelow(UObject* tracer, const FVector& point, FVector& floor)
{
	std::vector<uint8_t> parms = Parms(ref.Trace);
	Field<FVector>(parms.data(), eoff.Trace_TraceStart) = point;
	Field<FVector>(parms.data(), eoff.Trace_TraceEnd) = FVector{ point.X, point.Y, point.Z - FLOOR_TRACE };
	Call(tracer, ref.Trace, parms);
	if (!Field<UObject*>(parms.data(), eoff.Trace_ReturnValue)) {
		return false;
	}
	floor = Field<FVector>(parms.data(), eoff.Trace_HitLocation);
	return true;
}

static UObject* SpawnTombstone(UObject* controller, const FVector& at, int32_t yaw)
{
	UObject* cube = UE3::FindObject(CUBE_MESH);
	UObject* skin = UE3::FindObject(GLOW_SKIN_MATERIAL);
	UObject* mask = UE3::FindObject(TOMB_MASK);
	UObject* slab = SpawnMeshActor(controller, at, TOMB_SCALE, cube, skin, yaw);
	if (!slab) {
		return 0;
	}
	SkinMesh(slab, UE3::FindObject(STONE_SKIN), mask, 0.0f, 0.0f);
	float angle = (float)(yaw & 0xFFFF) / 65536.0f * 2.0f * PI_F;
	float gap = CUBE_HALF * TOMB_SCALE.Y + CUBE_HALF * PLAQUE_SCALE.Y + 0.5f;
	for (float side = -1.0f; side <= 1.0f; side += 2.0f) {
		FVector plaqueAt = { at.X - sinf(angle) * gap * side, at.Y + cosf(angle) * gap * side, at.Z };
		UObject* plaque = SpawnMeshActor(controller, plaqueAt, PLAQUE_SCALE, cube, skin, yaw);
		if (plaque) {
			SkinMesh(plaque, UE3::FindObject(TOMB_SKIN), mask, 0.0f, 0.0f);
			props.push_back(plaque);
		}
	}
	return slab;
}

static uint32_t HashText(const char* text)
{
	uint32_t hash = 2166136261u;
	for (; *text; text++) {
		hash = (hash ^ (uint8_t)*text) * 16777619u;
	}
	return hash;
}

static void SpawnProps(UObject* controller)
{
	std::vector<NavMesh::CoverSpot> spots;
	if (!NavMesh::ReadCover(level, spots) || spots.empty()) {
		EVENTS_LOG(LOG_LEVEL_INFO, "%s has no cover file, the theme has no props.", level.c_str());
		return;
	}
	UObject* cube = UE3::FindObject(CUBE_MESH);
	UObject* sphere = UE3::FindObject(SPHERE_MESH);
	UObject* skin = UE3::FindObject(GLOW_SKIN_MATERIAL);
	if (!cube || !sphere || !SkinsReady() || !UE3::FindObject(TOMB_SKIN) || !UE3::FindObject(TOMB_MASK) || !UE3::FindObject(STONE_SKIN)) {
		EVENTS_LOG(LOG_LEVEL_WARN, "%s", "The prop meshes or skins are missing, the theme has no props.");
		return;
	}
	uint32_t seed = HashText(level.c_str());
	for (size_t i = spots.size() - 1; i > 0; i--) {
		seed = seed * 1664525u + 1013904223u;
		size_t j = (seed >> 8) % (i + 1);
		NavMesh::CoverSpot swap = spots[i];
		spots[i] = spots[j];
		spots[j] = swap;
	}
	std::vector<FVector> taken;
	uint32_t tombs = 0;
	uint32_t pumpkins = 0;
	for (size_t i = 0; i < spots.size() && tombs + pumpkins < (uint32_t)PROP_COUNT; i++) {
		const NavMesh::CoverSpot& spot = spots[i];
		bool crowded = false;
		for (const FVector& other : taken) {
			crowded = crowded || Distance(other, spot.Location) < PROP_SPACING;
		}
		if (crowded) {
			continue;
		}
		FVector ground;
		if (!FloorBelow(controller, spot.Location, ground)) {
			continue;
		}
		taken.push_back(spot.Location);
		float floor = ground.Z;
		UObject* actor;
		if (i % 2 != 0) {
			FVector at = { spot.Location.X, spot.Location.Y, floor + CUBE_HALF * TOMB_SCALE.Z };
			actor = SpawnTombstone(controller, at, spot.Yaw + 32768);
			if (actor) {
				tombs++;
			}
		}
		else {
			FVector scale3D = { GROUND_PUMPKIN_SCALE, GROUND_PUMPKIN_SCALE, GROUND_PUMPKIN_SCALE * PUMPKIN_SQUASH };
			FVector at = { spot.Location.X, spot.Location.Y, floor + SPHERE_RADIUS * scale3D.Z };
			actor = SpawnMeshActor(controller, at, scale3D, sphere, skin, spot.Yaw + 32768);
			if (actor) {
				SkinMesh(actor, UE3::FindObject(PUMPKIN_SKIN), UE3::FindObject(PUMPKIN_MASK), PUMPKIN_MIN_GLOW, PUMPKIN_MAX_GLOW);
				GroundPumpkin pumpkin = { actor, at };
				ground_pumpkins.push_back(pumpkin);
				pumpkins++;
			}
		}
		if (actor) {
			props.push_back(actor);
			if (props.size() <= 4) {
				EVENTS_LOG(LOG_LEVEL_DEBUG, "%s at (%.0f %.0f %.0f), yaw %d, spot %.0f.", i % 2 != 0 ? "Tombstone" : "Pumpkin", spot.Location.X, spot.Location.Y, floor, spot.Yaw & 0xFFFF, spot.Location.Z);
			}
		}
	}
	EVENTS_LOG(LOG_LEVEL_INFO, "%u tombstones and %u pumpkins stand at %u of the map's %u cover spots.", tombs, pumpkins, (uint32_t)taken.size(), (uint32_t)spots.size());
}

static void PlaceTestProps(UObject* controller)
{
	static bool placed = false;
	UObject* pawn = Field<UObject*>(controller, off.Controller_Pawn);
	if (placed || !pawn || !IsAlive(pawn) || GetEnvironmentVariableA("BLTD_PROPS_HERE", 0, 0) == 0) {
		return;
	}
	placed = true;
	UObject* cube = UE3::FindObject(CUBE_MESH);
	UObject* sphere = UE3::FindObject(SPHERE_MESH);
	UObject* skin = UE3::FindObject(GLOW_SKIN_MATERIAL);
	if (cube && sphere && SkinsReady() && UE3::FindObject(TOMB_SKIN) && UE3::FindObject(TOMB_MASK) && UE3::FindObject(STONE_SKIN)) {
		FVector origin = Field<FVector>(pawn, off.Actor_Location);
		int32_t yaw = Field<FRotator>(controller, off.Actor_Rotation).Yaw;
		float angle = (float)(yaw & 0xFFFF) / 65536.0f * 2.0f * PI_F;
		FVector forward = { cosf(angle), sinf(angle), 0.0f };
		FVector right = { -forward.Y, forward.X, 0.0f };
		float floor = origin.Z - PAWN_HALF_HEIGHT;
		FVector tombAt = { origin.X + forward.X * 260.0f - right.X * 90.0f, origin.Y + forward.Y * 260.0f - right.Y * 90.0f, floor + CUBE_HALF * TOMB_SCALE.Z };
		UObject* tomb = SpawnTombstone(controller, tombAt, yaw);
		if (tomb) {
			props.push_back(tomb);
		}
		FVector scale3D = { GROUND_PUMPKIN_SCALE, GROUND_PUMPKIN_SCALE, GROUND_PUMPKIN_SCALE * PUMPKIN_SQUASH };
		FVector pumpkinAt = { origin.X + forward.X * 220.0f + right.X * 90.0f, origin.Y + forward.Y * 220.0f + right.Y * 90.0f, floor + SPHERE_RADIUS * scale3D.Z };
		UObject* pumpkin = SpawnMeshActor(controller, pumpkinAt, scale3D, sphere, skin, yaw + 32768);
		if (pumpkin) {
			SkinMesh(pumpkin, UE3::FindObject(PUMPKIN_SKIN), UE3::FindObject(PUMPKIN_MASK), PUMPKIN_MIN_GLOW, PUMPKIN_MAX_GLOW);
			props.push_back(pumpkin);
		}
		FVector headScale = { HEAD_SCALE, HEAD_SCALE, HEAD_SCALE * PUMPKIN_SQUASH };
		FVector headAt = { origin.X + forward.X * 230.0f, origin.Y + forward.Y * 230.0f, origin.Z + 30.0f };
		bool trail = TrailStyle();
		UObject* head = SpawnMeshActor(controller, headAt, headScale, sphere, trail ? UE3::FindObject(TRAIL_MATERIAL) : skin, yaw + 32768);
		if (head) {
			if (trail) {
				SkinMeshTrail(head, UE3::FindObject(PUMPKIN_SKIN), TRAIL_COLOR, EnvFloat("BLTD_HEAD_INTERP", TRAIL_INTERP));
			}
			else {
				SkinMesh(head, HeadSkin(false), HeadSkin(true), HEAD_MIN_GLOW, HEAD_MAX_GLOW);
			}
			props.push_back(head);
		}
		if (GhostsReady()) {
			FVector ghostAt = { origin.X + forward.X * 340.0f + right.X * 60.0f, origin.Y + forward.Y * 340.0f + right.Y * 60.0f, floor + 8.0f };
			UObject* ghost = SpawnGhostActor(controller, ghostAt, yaw + 32768, 1.0f, 0);
			if (ghost) {
				props.push_back(ghost);
				UObject* component = Field<UObject*>(ghost, gref.Actor_Component);
				UObject* meshNow = component ? Field<UObject*>(component, look.SkeletalMeshComponent_SkeletalMesh) : 0;
				EVENTS_LOG(LOG_LEVEL_DEBUG, "Test ghost %s at (%.0f %.0f %.0f), mesh %s.", UE3::ObjectName(ghost), ghostAt.X, ghostAt.Y, ghostAt.Z, meshNow ? UE3::ObjectName(meshNow) : "none");
			}
		}
		EVENTS_LOG(LOG_LEVEL_DEBUG, "Test props in front of the pawn at (%.0f %.0f %.0f), yaw %d.", origin.X, origin.Y, origin.Z, yaw & 0xFFFF);
	}
}

static bool ResolveGhosts()
{
	if (ghosts_resolved) {
		return ghosts_resolved > 0;
	}
	struct ObjectRef {
		UObject** Target;
		const char* Path;
	};
	struct OffsetRef {
		int32_t* Target;
		const char* Path;
	};
	static const ObjectRef OBJECTS[] = {
		{ &gref.ActorClass, "Engine.SkeletalMeshActorSpawnable" },
		{ &gref.SetSkeletalMesh, "Engine.SkeletalMeshComponent.SetSkeletalMesh" },
		{ &gref.KeepSpaceBasesProperty, "Engine.SkeletalMeshComponent.SetSkeletalMesh.bKeepSpaceBases" },
		{ &gref.UpdateAnimations, "Engine.SkeletalMeshComponent.UpdateAnimations" },
		{ &gref.PlayAnim, "Engine.SkeletalMeshComponent.PlayAnim" },
		{ &gref.LoopProperty, "Engine.SkeletalMeshComponent.PlayAnim.bLoop" },
		{ &gref.RestartProperty, "Engine.SkeletalMeshComponent.PlayAnim.bRestartIfAlreadyPlaying" },
		{ &gref.SetRotation, "Engine.Actor.SetRotation" },
	};
	static const OffsetRef OFFSETS[] = {
		{ &gref.SetMesh_Mesh, "Engine.SkeletalMeshComponent.SetSkeletalMesh.NewMesh" },
		{ &gref.PlayAnim_Name, "Engine.SkeletalMeshComponent.PlayAnim.AnimName" },
		{ &gref.SetRotation_Rotation, "Engine.Actor.SetRotation.NewRotation" },
		{ &gref.Actor_Component, "Engine.SkeletalMeshActor.SkeletalMeshComponent" },
		{ &gref.Component_AnimSets, "Engine.SkeletalMeshComponent.AnimSets" },
		{ &gref.Mesh_Materials, "Engine.SkeletalMesh.Materials" },
	};
	ghosts_resolved = 1;
	for (const ObjectRef& entry : OBJECTS) {
		*entry.Target = UE3::FindObject(entry.Path);
		if (!*entry.Target) {
			EVENTS_LOG(LOG_LEVEL_WARN, "%s was not found, the theme has no ghosts.", entry.Path);
			ghosts_resolved = -1;
		}
	}
	for (const OffsetRef& entry : OFFSETS) {
		*entry.Target = UE3::FindPropertyOffset(entry.Path);
		if (*entry.Target < 0) {
			EVENTS_LOG(LOG_LEVEL_WARN, "%s was not found, the theme has no ghosts.", entry.Path);
			ghosts_resolved = -1;
		}
	}
	return ghosts_resolved > 0;
}

static void PlaceGhost(UObject* actor, float angle, float seconds, float phase)
{
	FVector at = { ghost_center.X + cosf(angle) * GHOST_RADIUS, ghost_center.Y + sinf(angle) * GHOST_RADIUS, ghost_center.Z + GHOST_HEIGHT + sinf(seconds * 0.9f + phase) * GHOST_BOB };
	std::vector<uint8_t> parms = Parms(fn.Actor_SetLocation);
	Field<FVector>(parms.data(), off.SetLocation_NewLocation) = at;
	Call(actor, fn.Actor_SetLocation, parms);
	FRotator facing = { 0, (int32_t)((angle + PI_F * 0.5f) / (2.0f * PI_F) * 65536.0f) & 0xFFFF, 0 };
	std::vector<uint8_t> rotationParms = Parms(gref.SetRotation);
	Field<FRotator>(rotationParms.data(), gref.SetRotation_Rotation) = facing;
	Call(actor, gref.SetRotation, rotationParms);
}

static bool GhostsReady()
{
	return ResolveGhosts() && UE3::FindObject(GHOST_MESH) && UE3::FindObject(GHOST_ANIM_SET) && UE3::FindObject(HOLO_MATERIAL)
		&& UE3::FindNameIndex(GHOST_ANIM) >= 0 && name_holo_color >= 0 && name_emissive_color >= 0 && name_dissolve >= 0;
}

static UObject* SpawnGhostActor(UObject* controller, const FVector& location, int32_t yaw, float scale, std::vector<UObject*>* instances)
{
	UObject* mesh = UE3::FindObject(GHOST_MESH);
	UObject* animSet = UE3::FindObject(GHOST_ANIM_SET);
	UObject* holo = UE3::FindObject(HOLO_MATERIAL);
	FRotator rotation = { 0, yaw & 0xFFFF, 0 };
	UObject* actor = SpawnActor(controller, gref.ActorClass, location, rotation);
	if (!actor) {
		EVENTS_LOG(LOG_LEVEL_WARN, "%s", "No ghost actor spawned.");
		return 0;
	}
	Field<uint8_t>(actor, eoff.Actor_RemoteRole) = 0;
	SetCollisionOff(actor);
	std::vector<uint8_t> scaleParms = Parms(ref.SetDrawScale3D);
	Field<FVector>(scaleParms.data(), eoff.SetScale3D_Scale) = FVector{ scale, scale, scale };
	Call(actor, ref.SetDrawScale3D, scaleParms);
	UObject* component = Field<UObject*>(actor, gref.Actor_Component);
	if (!component) {
		return actor;
	}
	std::vector<uint8_t> meshParms = Parms(gref.SetSkeletalMesh);
	Field<UObject*>(meshParms.data(), gref.SetMesh_Mesh) = mesh;
	SetBit(meshParms.data(), gref.KeepSpaceBasesProperty, false);
	Call(component, gref.SetSkeletalMesh, meshParms);
	UE3::TArray<UObject*>& sets = Field<UE3::TArray<UObject*> >(component, gref.Component_AnimSets);
	if (sets.Max < 1) {
		sets.Data = (UObject**)UE3::Malloc(sizeof(UObject*) * 4, 8);
		sets.Max = sets.Data ? 4 : 0;
	}
	if (sets.Data) {
		sets.Data[0] = animSet;
		sets.Num = 1;
	}
	std::vector<uint8_t> updateParms = Parms(gref.UpdateAnimations);
	Call(component, gref.UpdateAnimations, updateParms);
	char override[256] = {};
	GetEnvironmentVariableA("BLTD_GHOST_MATERIAL", override, sizeof(override));
	bool plain = strcmp(override, "plain") == 0;
	UObject* material = *override && !plain && UE3::FindObject(override) ? UE3::FindObject(override) : holo;
	FLinearColor color = GHOST_COLOR;
	char text[128] = {};
	if (GetEnvironmentVariableA("BLTD_GHOST_COLOR", text, sizeof(text)) > 0) {
		sscanf_s(text, "%f,%f,%f", &color.R, &color.G, &color.B);
	}
	char scalars[256] = {};
	GetEnvironmentVariableA("BLTD_GHOST_SCALARS", scalars, sizeof(scalars));
	int32_t elements = Field<UE3::TArray<UObject*> >(mesh, gref.Mesh_Materials).Num;
	for (int32_t element = 0; element < elements && element < 16 && !plain; element++) {
		SetMaterial(component, element, material);
		UObject* instance = InstanceMaterial(component, element);
		if (!instance) {
			continue;
		}
		for (const char* parameter : { "HoloColor", "EmissiveColor", "SpawnColor" }) {
			int32_t name = UE3::FindNameIndex(parameter);
			if (name >= 0) {
				SetVectorParameter(instance, name, color);
			}
		}
		SetScalarParameter(instance, name_dissolve, GHOST_DISSOLVE);
		if (UE3::FindNameIndex("SpawnInterp") >= 0) {
			SetScalarParameter(instance, UE3::FindNameIndex("SpawnInterp"), GHOST_SPAWN);
		}
		for (const char* at = scalars; *at; ) {
			char key[64] = {};
			float value = 0.0f;
			int used = 0;
			if (sscanf_s(at, "%63[^=]=%f%n", key, (unsigned)sizeof(key), &value, &used) < 2 || used <= 0) {
				break;
			}
			int32_t name = UE3::FindNameIndex(key);
			if (name >= 0) {
				SetScalarParameter(instance, name, value);
			}
			at += used;
			if (*at == ';') {
				at++;
			}
		}
		if (instances) {
			instances->push_back(instance);
		}
	}
	std::vector<uint8_t> animParms = Parms(gref.PlayAnim);
	Field<UE3::FName>(animParms.data(), gref.PlayAnim_Name) = Name(UE3::FindNameIndex(GHOST_ANIM));
	SetBit(animParms.data(), gref.LoopProperty, true);
	SetBit(animParms.data(), gref.RestartProperty, true);
	Call(component, gref.PlayAnim, animParms);
	return actor;
}

static void SpawnGhosts(UObject* controller, const FVector& origin)
{
	if (!GhostsReady()) {
		EVENTS_LOG(LOG_LEVEL_WARN, "%s", "The ghost mesh, animations or hologram are missing, the theme has no ghosts.");
		return;
	}
	ghost_center = origin;
	ghost_start_tick = GetTickCount();
	for (int i = 0; i < GHOST_COUNT; i++) {
		float phase = (float)i * 2.0f * PI_F / (float)GHOST_COUNT;
		Ghost ghost = { 0, phase };
		ghost.Actor = SpawnGhostActor(controller, origin, 0, GHOST_SCALE, &ghost.Instances);
		if (!ghost.Actor) {
			return;
		}
		PlaceGhost(ghost.Actor, phase, 0.0f, phase);
		ghosts.push_back(ghost);
	}
	EVENTS_LOG(LOG_LEVEL_INFO, "%u ghosts circle (%.0f %.0f %.0f).", (uint32_t)ghosts.size(), origin.X, origin.Y, origin.Z);
}

static void MoveGhosts(DWORD now)
{
	float seconds = (float)(now - ghost_start_tick) / 1000.0f;
	for (const Ghost& ghost : ghosts) {
		if (UE3::IsValidObject(ghost.Actor)) {
			PlaceGhost(ghost.Actor, ghost.Phase + seconds * GHOST_TURN_PER_S, seconds, ghost.Phase);
		}
	}
}

static void MoveActor(UObject* actor, const FVector& location)
{
	std::vector<uint8_t> parms = Parms(fn.Actor_SetLocation);
	Field<FVector>(parms.data(), off.SetLocation_NewLocation) = location;
	Call(actor, fn.Actor_SetLocation, parms);
}

static int32_t YawBetween(const FVector& from, const FVector& to)
{
	return (int32_t)(atan2f(to.Y - from.Y, to.X - from.X) / (2.0f * PI_F) * 65536.0f) & 0xFFFF;
}

static void StartTransient(UObject* controller, const FVector& start, const FVector& end, DWORD duration_ms, float scale, DWORD now)
{
	if (!GhostsReady()) {
		return;
	}
	Transient transient = { 0, std::vector<UObject*>(), now, duration_ms, start, end };
	transient.Actor = SpawnGhostActor(controller, start, YawBetween(start, end), scale, &transient.Instances);
	if (transient.Actor) {
		transients.push_back(transient);
	}
}

static void MoveTransients(DWORD now)
{
	for (size_t i = transients.size(); i-- > 0; ) {
		Transient& transient = transients[i];
		float t = (float)(now - transient.StartTick) / (float)transient.DurationMs;
		if (!UE3::IsValidObject(transient.Actor) || t >= 1.0f) {
			if (UE3::IsValidObject(transient.Actor)) {
				DestroyActor(transient.Actor);
			}
			transients.erase(transients.begin() + i);
			continue;
		}
		FVector at = { transient.Start.X + (transient.End.X - transient.Start.X) * t, transient.Start.Y + (transient.End.Y - transient.Start.Y) * t, transient.Start.Z + (transient.End.Z - transient.Start.Z) * t };
		MoveActor(transient.Actor, at);
		float dissolve = GHOST_DISSOLVE + (1.0f - GHOST_DISSOLVE) * t * t;
		for (UObject* instance : transient.Instances) {
			if (UE3::IsValidObject(instance)) {
				SetScalarParameter(instance, name_dissolve, dissolve);
			}
		}
	}
}

static void RiseFrom(UObject* controller, UObject* pawn, DWORD now)
{
	FVector start = Field<FVector>(pawn, off.Actor_Location);
	FVector end = { start.X, start.Y, start.Z + RISE_HEIGHT };
	StartTransient(controller, start, end, RISE_MS, RISE_SCALE, now);
}

static void Flyby(UObject* controller, DWORD now)
{
	if ((int32_t)(now - next_flyby_tick) < 0) {
		return;
	}
	next_flyby_tick = now + FLYBY_MIN_MS + (DWORD)(rand() % (int)FLYBY_SPREAD_MS);
	UObject* pawn = Field<UObject*>(controller, off.Controller_Pawn);
	if (!IsAlive(pawn)) {
		return;
	}
	FVector origin = Field<FVector>(pawn, off.Actor_Location);
	float angle = (float)(Field<FRotator>(pawn, off.Actor_Rotation).Yaw & 0xFFFF) / 65536.0f * 2.0f * PI_F;
	float side = (rand() % 2) ? 1.0f : -1.0f;
	FVector forward = { cosf(angle), sinf(angle), 0.0f };
	FVector right = { -forward.Y * side, forward.X * side, 0.0f };
	FVector start = { origin.X - forward.X * FLYBY_BEHIND + right.X * 180.0f, origin.Y - forward.Y * FLYBY_BEHIND + right.Y * 180.0f, origin.Z + FLYBY_HEIGHT };
	FVector end = { origin.X + forward.X * FLYBY_AHEAD - right.X * 120.0f, origin.Y + forward.Y * FLYBY_AHEAD - right.Y * 120.0f, origin.Z + FLYBY_HEIGHT * 0.6f };
	StartTransient(controller, start, end, FLYBY_MS, FLYBY_SCALE, now);
	PostSound(controller, FLYBY_SOUND);
}

static void BurstPumpkins(UObject* controller, DWORD now)
{
	if ((int32_t)(now - next_burst_check_tick) < 0) {
		return;
	}
	next_burst_check_tick = now + 100;
	UObject* pawn = Field<UObject*>(controller, off.Controller_Pawn);
	if (!IsAlive(pawn)) {
		return;
	}
	FVector at = Field<FVector>(pawn, off.Actor_Location);
	for (size_t i = ground_pumpkins.size(); i-- > 0; ) {
		GroundPumpkin& pumpkin = ground_pumpkins[i];
		if (!UE3::IsValidObject(pumpkin.Actor)) {
			ground_pumpkins.erase(ground_pumpkins.begin() + i);
			continue;
		}
		if (Distance(at, pumpkin.Location) > BURST_RANGE) {
			continue;
		}
		UObject* effect = UE3::FindObject(BURST_EFFECT);
		UObject* emitterClass = UE3::FindObject(EMITTER_CLASS);
		FRotator rotation = {};
		UObject* emitter = effect && emitterClass ? SpawnActor(controller, emitterClass, pumpkin.Location, rotation) : 0;
		if (emitter) {
			Field<uint8_t>(emitter, eoff.Actor_RemoteRole) = 0;
			std::vector<uint8_t> parms = Parms(ref.SetTemplate);
			Field<UObject*>(parms.data(), eoff.SetTemplate_Template) = effect;
			SetBit(parms.data(), ref.DestroyOnFinishProperty, true);
			Call(emitter, ref.SetTemplate, parms);
		}
		PostSoundOn(pumpkin.Actor, BURST_SOUND, false);
		DestroyActor(pumpkin.Actor);
		ground_pumpkins.erase(ground_pumpkins.begin() + i);
		EVENTS_LOG(LOG_LEVEL_DEBUG, "A pumpkin burst at (%.0f %.0f %.0f), effect %s.", pumpkin.Location.X, pumpkin.Location.Y, pumpkin.Location.Z, emitter ? "spawned" : "missing");
	}
}

static void AttachToBone(UObject* actor, UObject* pawn, UObject* mesh, int32_t bone)
{
	std::vector<uint8_t> hardParms = Parms(ref.SetHardAttach);
	SetBit(hardParms.data(), ref.HardAttachProperty, true);
	Call(actor, ref.SetHardAttach, hardParms);
	std::vector<uint8_t> parms = Parms(ref.SetBase);
	Field<UObject*>(parms.data(), eoff.SetBase_NewBase) = pawn;
	Field<UObject*>(parms.data(), eoff.SetBase_SkelComp) = mesh;
	Field<UE3::FName>(parms.data(), eoff.SetBase_AttachName) = Name(bone);
	Call(actor, ref.SetBase, parms);
	std::vector<uint8_t> quatParms = Parms(ref.GetBoneQuaternion);
	Field<UE3::FName>(quatParms.data(), eoff.BoneQuat_Name) = Name(bone);
	Field<int32_t>(quatParms.data(), eoff.BoneQuat_Space) = 0;
	Call(mesh, ref.GetBoneQuaternion, quatParms);
	Matrix3 boneInverse = Transpose(MatrixOf(Field<FQuat>(quatParms.data(), eoff.BoneQuat_Return)));
	FRotator facing = { 0, Field<FRotator>(pawn, off.Actor_Rotation).Yaw, 0 };
	char text[64] = {};
	if (GetEnvironmentVariableA("BLTD_HEAD_ROTATION", text, sizeof(text)) > 0) {
		int pitch = 0, yaw = 0, roll = 0;
		sscanf_s(text, "%d,%d,%d", &pitch, &yaw, &roll);
		facing.Pitch += pitch;
		facing.Yaw += yaw;
		facing.Roll += roll;
	}
	FVector lift = { 0.0f, 0.0f, EnvFloat("BLTD_HEAD_LIFT", HEAD_LIFT) };
	Field<FRotator>(actor, eoff.Actor_RelativeRotation) = RotatorOf(Multiply(MatrixOf(facing), boneInverse));
	Field<FVector>(actor, eoff.Actor_RelativeLocation) = Transform(lift, boneInverse);
}

static UObject* HeadMaterial(UObject* fallback)
{
	char path[256] = {};
	if (GetEnvironmentVariableA("BLTD_HEAD_MATERIAL", path, sizeof(path)) == 0) {
		return fallback;
	}
	UObject* material = UE3::FindObject(path);
	if (!material) {
		EVENTS_LOG(LOG_LEVEL_WARN, "Head material %s not found, the glow stays.", path);
	}
	return material ? material : fallback;
}

static float EnvFloat(const char* name, float fallback)
{
	char text[32] = {};
	if (GetEnvironmentVariableA(name, text, sizeof(text)) == 0) {
		return fallback;
	}
	return (float)atof(text);
}

static float HeadScale()
{
	float scale = EnvFloat("BLTD_HEAD_SCALE", HEAD_SCALE);
	return scale > 0.0f ? scale : HEAD_SCALE;
}

static FLinearColor HeadColor()
{
	char text[64] = {};
	FLinearColor color = HEAD_COLOR;
	if (GetEnvironmentVariableA("BLTD_HEAD_COLOR", text, sizeof(text)) > 0) {
		sscanf_s(text, "%f,%f,%f", &color.R, &color.G, &color.B);
	}
	return color;
}

static UObject* InstanceMaterial(UObject* component, int32_t element)
{
	if (!component) {
		return 0;
	}
	std::vector<uint8_t> parms = Parms(ref.CreateMaterialInstance);
	Field<int32_t>(parms.data(), eoff.CreateInstance_Index) = element;
	Call(component, ref.CreateMaterialInstance, parms);
	return Field<UObject*>(parms.data(), eoff.CreateInstance_Return);
}

static void SetMaterial(UObject* component, int32_t element, UObject* material)
{
	std::vector<uint8_t> parms = Parms(ref.SetMaterial);
	Field<UObject*>(parms.data(), eoff.SetMaterial_Material) = material;
	Field<int32_t>(parms.data(), eoff.SetMaterial_Index) = element;
	Call(component, ref.SetMaterial, parms);
}

static bool ColourElement(UObject* component, int32_t element, const FLinearColor& color, float dissolve)
{
	UObject* instance = InstanceMaterial(component, element);
	if (!instance) {
		return false;
	}
	SetVectorParameter(instance, name_holo_color, color);
	SetVectorParameter(instance, name_emissive_color, color);
	SetScalarParameter(instance, name_dissolve, dissolve);
	return true;
}

static bool ColourMesh(UObject* actor, const FLinearColor& color, float dissolve)
{
	return ColourElement(Field<UObject*>(actor, eoff.DynamicSM_Component), 0, color, dissolve);
}

static bool SkinMesh(UObject* actor, UObject* skin, UObject* mask, float min_glow, float max_glow)
{
	UObject* instance = InstanceMaterial(Field<UObject*>(actor, eoff.DynamicSM_Component), 0);
	if (!instance) {
		return false;
	}
	SetTextureParameter(instance, name_diffuse, skin);
	SetTextureParameter(instance, name_mask, mask);
	SetScalarParameter(instance, name_min_emis, min_glow);
	SetScalarParameter(instance, name_max_emis, max_glow);
	SetScalarParameter(instance, name_htv_mode, 0.0f);
	return true;
}

static bool SkinMeshTrail(UObject* actor, UObject* skin, const FLinearColor& color, float interp)
{
	UObject* instance = InstanceMaterial(Field<UObject*>(actor, eoff.DynamicSM_Component), 0);
	if (!instance) {
		return false;
	}
	SetTextureParameter(instance, name_diffuse, skin);
	if (UE3::FindNameIndex("SpawnColor") >= 0) {
		SetVectorParameter(instance, UE3::FindNameIndex("SpawnColor"), color);
	}
	if (UE3::FindNameIndex("SpawnInterp") >= 0) {
		SetScalarParameter(instance, UE3::FindNameIndex("SpawnInterp"), interp);
	}
	return true;
}

static bool TrailStyle()
{
	char text[16] = {};
	return GetEnvironmentVariableA("BLTD_HEAD_STYLE", text, sizeof(text)) > 0 && _stricmp(text, "trail") == 0 && UE3::FindObject(TRAIL_MATERIAL);
}

static UObject* HeadSkin(bool mask)
{
	char text[16] = {};
	bool plain = GetEnvironmentVariableA("BLTD_HEAD_STYLE", text, sizeof(text)) > 0 && _stricmp(text, "pumpkin") == 0;
	bool ghost = !plain && UE3::FindObject(GHOST_SKIN) && UE3::FindObject(GHOST_MASK);
	return UE3::FindObject(ghost ? (mask ? GHOST_MASK : GHOST_SKIN) : (mask ? PUMPKIN_MASK : PUMPKIN_SKIN));
}

static bool SkinsReady()
{
	return name_diffuse >= 0 && name_mask >= 0 && name_min_emis >= 0 && name_max_emis >= 0 && name_htv_mode >= 0
		&& UE3::FindObject(GLOW_SKIN_MATERIAL) && UE3::FindObject(PUMPKIN_SKIN) && UE3::FindObject(PUMPKIN_MASK);
}

static void LoadBank(UObject* controller, const char* bank)
{
	size_t length = strlen(bank) + 1;
	std::vector<uint8_t> parms = Parms(ref.LoadBank);
	UE3::FString& name = Field<UE3::FString>(parms.data(), eoff.LoadBank_Name);
	name.Data = (wchar_t*)UE3::Malloc((uint32_t)(length * sizeof(wchar_t)), 8);
	if (!name.Data) {
		return;
	}
	for (size_t i = 0; i < length; i++) {
		name.Data[i] = (wchar_t)(unsigned char)bank[i];
	}
	name.Num = name.Max = (int32_t)length;
	Call(controller, ref.LoadBank, parms);
	UE3::FreeArray(&name);
}

static bool PostSoundOn(UObject* actor, const char* path, bool global)
{
	UObject* event = UE3::FindObject(path);
	if (!event || !actor || !UE3::IsValidObject(actor)) {
		return false;
	}
	std::vector<uint8_t> parms = Parms(ref.PostAkEvent);
	Field<UObject*>(parms.data(), eoff.PostAkEvent_Event) = event;
	SetBit(parms.data(), ref.GlobalProperty, global);
	Call(actor, ref.PostAkEvent, parms);
	return true;
}

static bool PostSound(UObject* controller, const char* path)
{
	return PostSoundOn(controller, path, true);
}

static void LoadSounds(UObject* controller)
{
	if (!sounds_loaded) {
		sounds_loaded = UE3::LoadPackage(SOUND_PACKAGE);
		if (!sounds_loaded) {
			EVENTS_LOG(LOG_LEVEL_WARN, "%ls.upk did not load, the theme has no sounds.", SOUND_PACKAGE);
			return;
		}
	}
	for (const char* bank : SOUND_BANKS) {
		LoadBank(controller, bank);
	}
	EVENTS_LOG(LOG_LEVEL_INFO, "Sound package loaded, thunder %s, bell %s.", UE3::FindObject(THUNDER_SOUND) ? "found" : "missing", UE3::FindObject(BELL_SOUND) ? "found" : "missing");
}

static uint8_t* Settings()
{
	return local_player ? (uint8_t*)local_player + eoff.LP_Settings : 0;
}

static void SetTones(const FLinearColor& highlights, const FLinearColor& midtones, const FLinearColor& shadows)
{
	uint8_t* settings = Settings();
	if (!settings) {
		return;
	}
	Field<FVector>(settings, eoff.PP_HighLights) = FVector{ highlights.R, highlights.G, highlights.B };
	Field<FVector>(settings, eoff.PP_MidTones) = FVector{ midtones.R, midtones.G, midtones.B };
	Field<FVector>(settings, eoff.PP_Shadows) = FVector{ shadows.R, shadows.G, shadows.B };
}

static const FLinearColor NIGHT_HIGHLIGHTS = { 0.80f, 0.72f, 1.00f, 1.0f };
static const FLinearColor NIGHT_MIDTONES = { 1.00f, 0.92f, 1.25f, 1.0f };
static const FLinearColor NIGHT_SHADOWS = { 0.00f, 0.00f, 0.03f, 1.0f };
static const FLinearColor BOLT_HIGHLIGHTS = { 1.30f, 1.30f, 1.60f, 1.0f };
static const FLinearColor BOLT_MIDTONES = { 2.10f, 2.10f, 2.50f, 1.0f };
static const FLinearColor BOLT_SHADOWS = { 0.10f, 0.10f, 0.14f, 1.0f };
static const FLinearColor BLOOD_HIGHLIGHTS = { 1.30f, 0.62f, 0.58f, 1.0f };
static const FLinearColor BLOOD_MIDTONES = { 1.60f, 0.82f, 0.86f, 1.0f };
static const FLinearColor BLOOD_SHADOWS = { 0.03f, 0.00f, 0.00f, 1.0f };

static void NightTones()
{
	SetTones(Mix(NIGHT_HIGHLIGHTS, BLOOD_HIGHLIGHTS, blood), Mix(NIGHT_MIDTONES, BLOOD_MIDTONES, blood), Mix(NIGHT_SHADOWS, BLOOD_SHADOWS, blood));
}

static bool ViewIsOurs()
{
	uint8_t* settings = Settings();
	return settings && Field<float>(settings, eoff.PP_Desaturation) == VIEW_DESATURATION && Field<float>(settings, eoff.PP_BloomScale) == VIEW_BLOOM;
}

static void ApplyPostProcess()
{
	uint8_t* settings = Settings();
	if (!settings) {
		return;
	}
	UE3::SetBoolProperty(local_player, ref.RecoveryProperty, false);
	SetBit(settings, ref.EnableDOF, false);
	SetBit(settings, ref.OverrideDesaturation, true);
	SetBit(settings, ref.OverrideHighLights, true);
	SetBit(settings, ref.OverrideMidTones, true);
	SetBit(settings, ref.OverrideShadows, true);
	SetBit(settings, ref.OverrideEnableBloom, true);
	SetBit(settings, ref.OverrideBloomScale, true);
	SetBit(settings, ref.EnableBloom, true);
	Field<float>(settings, eoff.PP_Desaturation) = VIEW_DESATURATION;
	Field<float>(settings, eoff.PP_BloomScale) = VIEW_BLOOM;
	NightTones();
	UE3::SetBoolProperty(local_player, ref.OverrideProperty, true);
}

static void ResetPostProcess()
{
	if (local_player && UE3::IsValidObject(local_player)) {
		UE3::SetBoolProperty(local_player, ref.OverrideProperty, false);
	}
}

static FVector ViewOrigin(UObject* controller)
{
	UObject* pawn = Field<UObject*>(controller, off.Controller_Pawn);
	return Field<FVector>(pawn ? pawn : controller, off.Actor_Location);
}

static FVector MoonAt(const FVector& origin)
{
	return FVector{ origin.X + MOON_OFFSET.X, origin.Y + MOON_OFFSET.Y, origin.Z + MOON_OFFSET.Z };
}

static FVector MoonFor(UObject* controller)
{
	FVector origin = ViewOrigin(controller);
	if (!moon_here) {
		return MoonAt(origin);
	}
	float angle = (float)(Field<FRotator>(controller, off.Actor_Rotation).Yaw & 0xFFFF) / 65536.0f * 2.0f * PI_F;
	float reach = sqrtf(MOON_OFFSET.X * MOON_OFFSET.X + MOON_OFFSET.Y * MOON_OFFSET.Y);
	return FVector{ origin.X + cosf(angle) * reach, origin.Y + sinf(angle) * reach, origin.Z + MOON_OFFSET.Z * 0.35f };
}

static uint32_t RetextureSigns(const std::vector<UObject*>& instances, uint32_t& graffiti)
{
	uint32_t changed = 0;
	graffiti = 0;
	for (UObject* instance : instances) {
		const char* name = UE3::ObjectName(instance);
		UObject* parent = Field<UObject*>(instance, eoff.MaterialInstance_Parent);
		if (parent && strcmp(UE3::ObjectName(parent), GRAFFITI_MATERIAL) == 0 && strncmp(name, GRAFFITI_PREFIX, strlen(GRAFFITI_PREFIX)) == 0) {
			UObject* stencil = UE3::FindObject(GRAFFITI[HashText(name) % (sizeof(GRAFFITI) / sizeof(GRAFFITI[0]))]);
			if (stencil) {
				SetTextureParameter(instance, name_mask, stencil);
				graffiti++;
			}
			continue;
		}
		bool hit = false;
		for (const Retexture& entry : RETEXTURES) {
			bool match = entry.Prefix ? strncmp(name, entry.Instance, strlen(entry.Instance)) == 0 : strcmp(name, entry.Instance) == 0;
			UObject* texture = match ? UE3::FindObject(entry.Texture) : 0;
			int32_t parameter = texture ? UE3::FindNameIndex(entry.Parameter) : -1;
			if (parameter >= 0) {
				SetTextureParameter(instance, parameter, texture);
				hit = true;
			}
		}
		changed += hit ? 1 : 0;
	}
	return changed;
}

static void FindGlows(const std::vector<UObject*>& instances)
{
	for (UObject* instance : instances) {
		UObject* parent = Field<UObject*>(instance, eoff.MaterialInstance_Parent);
		const char* parentName = parent ? UE3::ObjectName(parent) : "";
		for (const GlowKind& kind : GLOW_KINDS) {
			int32_t name = UE3::FindNameIndex(kind.Parameter);
			if (strcmp(parentName, kind.Parent) != 0 || name < 0) {
				continue;
			}
			Glow glow = { instance, name, kind.Vector, 0.0f, {} };
			bool known = kind.Vector ? GetVectorParameter(instance, name, glow.Color) : GetScalarParameter(instance, name, glow.Scalar);
			if (known) {
				glows.push_back(glow);
			}
		}
	}
}

static void SetGlow(float level)
{
	for (const Glow& glow : glows) {
		if (!UE3::IsValidObject(glow.Instance)) {
			continue;
		}
		if (glow.Vector) {
			SetVectorParameter(glow.Instance, glow.Name, Scaled(glow.Color, level));
		}
		else {
			SetScalarParameter(glow.Instance, glow.Name, glow.Scalar * level);
		}
	}
}

static void StartSurge(DWORD now)
{
	if (glows.empty() || surge_step >= 0) {
		return;
	}
	surge_step = 0;
	surge_step_tick = now;
	SetGlow(SURGE[0].Level);
}

static void Surge(DWORD now)
{
	if (surge_step < 0 || now - surge_step_tick < SURGE[surge_step].Ms) {
		return;
	}
	surge_step++;
	surge_step_tick = now;
	SetGlow(SURGE[surge_step].Level);
	if (SURGE[surge_step].Ms == 0) {
		surge_step = -1;
	}
}

static UObject* GameState(UObject* controller)
{
	UObject* world = Field<UObject*>(controller, off.Actor_WorldInfo);
	return world ? Field<UObject*>(world, xref.World_GRI) : 0;
}

static void RaiseBloodMoon(UObject* controller)
{
	UObject* sphere = UE3::FindObject(SPHERE_MESH);
	UObject* holo = UE3::FindObject(HOLO_MATERIAL);
	if (blood_moon || !sphere || !holo || name_dissolve < 0) {
		return;
	}
	blood_moon = SpawnMeshActor(controller, MoonFor(controller), 0.01f, sphere, holo);
	blood_moon_instance = blood_moon ? InstanceMaterial(Field<UObject*>(blood_moon, eoff.DynamicSM_Component), 0) : 0;
	if (blood_moon_instance) {
		SetScalarParameter(blood_moon_instance, name_dissolve, GHOST_DISSOLVE);
		if (UE3::FindNameIndex("SpawnInterp") >= 0) {
			SetScalarParameter(blood_moon_instance, UE3::FindNameIndex("SpawnInterp"), GHOST_SPAWN);
		}
	}
}

static void ShowBlood(UObject* controller)
{
	for (UObject* material : sky_materials) {
		if (UE3::IsValidObject(material)) {
			SetVectorParameter(material, name_final_rgb, Mix(SKY_TINT, BLOOD_SKY, blood));
		}
	}
	for (UObject* component : fog_components) {
		if (UE3::IsValidObject(component)) {
			Field<FColor>(component, eoff.Fog_Color) = Mix(FOG_COLOR, BLOOD_FOG, blood);
			Reattach(Outer(component), component);
		}
	}
	if (ViewIsOurs() && flash_step < 0) {
		NightTones();
	}
	if (moon && UE3::IsValidObject(moon)) {
		SetScale(moon, MOON_SCALE * (blood < 0.99f ? 1.0f - blood : 0.01f));
	}
	if (blood > 0.0f) {
		RaiseBloodMoon(controller);
	}
	if (blood_moon && UE3::IsValidObject(blood_moon)) {
		SetScale(blood_moon, blood > 0.01f ? MOON_SCALE * (1.0f + BLOOD_MOON_GROWTH * blood) : 0.01f);
		if (blood_moon_instance && UE3::IsValidObject(blood_moon_instance)) {
			SetVectorParameter(blood_moon_instance, name_holo_color, Scaled(BLOOD_MOON_COLOR, blood));
			SetVectorParameter(blood_moon_instance, name_emissive_color, Scaled(BLOOD_MOON_COLOR, blood));
		}
	}
	FLinearColor ghostColor = Mix(GHOST_COLOR, BLOOD_GHOST_COLOR, blood);
	for (const Ghost& ghost : ghosts) {
		for (UObject* instance : ghost.Instances) {
			if (UE3::IsValidObject(instance)) {
				SetVectorParameter(instance, name_holo_color, ghostColor);
				SetVectorParameter(instance, name_emissive_color, ghostColor);
			}
		}
	}
}

static void BloodMoon(UObject* controller, DWORD now)
{
	UObject* gri = GameState(controller);
	int32_t left = gri && Field<int32_t>(gri, xref.GRI_TimeLimit) > 0 ? Field<int32_t>(gri, xref.GRI_RemainingTime) : -1;
	bool over = gri && UE3::GetBoolProperty(gri, xref.MatchIsOverProperty);
	bool want = blood_forced || (left > 0 && left <= BLOOD_MOON_SECONDS) || (blood_wanted && (over || left == 0));
	if (want && !blood_wanted) {
		EVENTS_LOG(LOG_LEVEL_INFO, "The blood moon rises, %d s left.", left);
		PostSound(local_controller, BELL_SOUND);
		last_bell_rung = false;
	}
	if (want && !last_bell_rung && left > 0 && left <= LAST_BELL_SECONDS) {
		last_bell_rung = true;
		PostSound(local_controller, BELL_SOUND);
	}
	blood_wanted = want;
	if (blood_moon && UE3::IsValidObject(blood_moon)) {
		MoveActor(blood_moon, MoonFor(controller));
	}
	DWORD elapsed = now - blood_update_tick;
	float target = want ? 1.0f : 0.0f;
	if (blood == target || elapsed < BLOOD_UPDATE_MS) {
		return;
	}
	blood_update_tick = now;
	float step = (float)elapsed / (float)BLOOD_FADE_MS;
	blood = want ? (blood + step < 1.0f ? blood + step : 1.0f) : (blood - step > 0.0f ? blood - step : 0.0f);
	ShowBlood(controller);
}

static UObject* SpawnHeightFog(UObject* controller)
{
	UObject* fogClass = UE3::FindObject("Engine.HeightFog");
	UObject* noDelete = UE3::FindObject("Engine.Actor.bNoDelete");
	int32_t componentOffset = UE3::FindPropertyOffset("Engine.HeightFog.Component");
	int32_t brightnessOffset = UE3::FindPropertyOffset("Engine.HeightFogComponent.LightBrightness");
	UObject* defaults = fogClass ? DefaultObject(fogClass) : 0;
	if (!defaults || !noDelete || componentOffset < 0 || brightnessOffset < 0) {
		EVENTS_LOG(LOG_LEVEL_WARN, "%s", "The height fog class is missing, this map has no fog.");
		return 0;
	}
	FVector origin = ViewOrigin(controller);
	FVector at = { origin.X, origin.Y, origin.Z + FOG_HEIGHT_ABOVE };
	UE3::SetBoolProperty(defaults, noDelete, false);
	UObject* actor = SpawnActor(controller, fogClass, at, FRotator{});
	UE3::SetBoolProperty(defaults, noDelete, true);
	UObject* component = actor ? Field<UObject*>(actor, componentOffset) : 0;
	if (!component) {
		EVENTS_LOG(LOG_LEVEL_WARN, "%s", "No height fog spawned, this map has no fog.");
		return 0;
	}
	Field<uint8_t>(actor, eoff.Actor_RemoteRole) = 0;
	Field<float>(component, brightnessOffset) = FOG_BRIGHTNESS;
	return component;
}

static void Dress(UObject* controller)
{
	int32_t count = UE3::ObjectCount();
	uint32_t fogs = 0;
	std::vector<UObject*>& skyMaterials = sky_materials;
	std::vector<UObject*> sunLights;
	std::vector<UObject*>& fogComponents = fog_components;
	std::vector<UObject*> instances;
	for (int32_t i = 0; i < count; i++) {
		UObject* object = UE3::ObjectAt(i);
		if (!object || !UE3::IsValidObject(object) || strncmp(UE3::ObjectName(object), "Default__", 9) == 0 || !Outer(object) || strncmp(UE3::ObjectName(Outer(object)), "Default__", 9) == 0) {
			continue;
		}
		if (UE3::IsA(object, ref.MaterialInstanceClass)) {
			UObject* parent = Field<UObject*>(object, eoff.MaterialInstance_Parent);
			if (parent && strcmp(UE3::ObjectName(parent), SKY_MASTER_MATERIAL) == 0) {
				skyMaterials.push_back(object);
			}
			instances.push_back(object);
		}
		else if (UE3::IsA(object, ref.DominantLightComponentClass)) {
			sunLights.push_back(object);
		}
		else if (UE3::IsA(object, ref.HeightFogComponentClass)) {
			fogComponents.push_back(object);
		}
	}
	EVENTS_LOG(LOG_LEVEL_DEBUG, "Found %u sky materials, %u sun lights, %u fog components.", (uint32_t)skyMaterials.size(), (uint32_t)sunLights.size(), (uint32_t)fogComponents.size());
	for (UObject* material : skyMaterials) {
		EVENTS_LOG(LOG_LEVEL_DEBUG, "Tinting sky material %s.", UE3::ObjectName(material));
		SetVectorParameter(material, name_final_rgb, SKY_TINT);
		SetScalarParameter(material, name_final_desaturation, SKY_DESATURATION);
	}
	for (UObject* component : sunLights) {
		Light light = { component, Field<float>(component, eoff.Light_Brightness), Field<FColor>(component, eoff.Light_Color) };
		EVENTS_LOG(LOG_LEVEL_DEBUG, "Dimming sun light %s of %s, brightness %.2f.", UE3::ObjectName(component), UE3::ObjectName(Outer(component)), light.Brightness);
		lights.push_back(light);
		SetLight(component, light.Brightness * SUN_SCALE, SUN_COLOR);
	}
	UObject* fogEnabled = UE3::FindObject("Engine.HeightFogComponent.bEnabled");
	bool fogShown = false;
	for (UObject* component : fogComponents) {
		fogShown = fogShown || (fogEnabled && UE3::GetBoolProperty(component, fogEnabled));
	}
	if (!fogShown) {
		UObject* spawned = SpawnHeightFog(controller);
		if (spawned) {
			EVENTS_LOG(LOG_LEVEL_DEBUG, "%s", "The map has no height fog switched on, the theme brings one.");
			fogComponents.push_back(spawned);
		}
	}
	for (UObject* component : fogComponents) {
		float& density = Field<float>(component, eoff.Fog_Density);
		EVENTS_LOG(LOG_LEVEL_DEBUG, "Thickening fog %s of %s, density %g.", UE3::ObjectName(component), UE3::ObjectName(Outer(component)), density);
		density = density * FOG_SCALE > FOG_MIN_DENSITY ? density * FOG_SCALE : FOG_MIN_DENSITY;
		Field<FColor>(component, eoff.Fog_Color) = FOG_COLOR;
		Field<float>(component, eoff.Fog_StartDistance) = 0.0f;
		Reattach(Outer(component), component);
		fogs++;
	}
	EVENTS_LOG(LOG_LEVEL_DEBUG, "%s", "Darkening the view.");
	ApplyPostProcess();
	EVENTS_LOG(LOG_LEVEL_DEBUG, "%s", "Loading the sounds.");
	LoadSounds(controller);
	uint32_t graffiti = 0;
	uint32_t signs = RetextureSigns(instances, graffiti);
	if (ResolveExtra()) {
		FindGlows(instances);
	}
	EVENTS_LOG(LOG_LEVEL_INFO, "%u graffiti and %u sign materials show the theme's art, %u glowing materials can surge.", graffiti, signs, (uint32_t)glows.size());
	EVENTS_LOG(LOG_LEVEL_DEBUG, "%s", "Raising the moon.");
	FVector origin = ViewOrigin(controller);
	UObject* sphere = UE3::FindObject(SPHERE_MESH);
	UObject* glow = UE3::FindObject(GLOW_MATERIAL);
	if (sphere) {
		moon = SpawnMeshActor(controller, MoonAt(origin), MOON_SCALE, sphere, glow);
	}
	EVENTS_LOG(LOG_LEVEL_DEBUG, "%s", "Raising the ghosts.");
	SpawnGhosts(controller, origin);
	EVENTS_LOG(LOG_LEVEL_DEBUG, "%s", "Placing the props.");
	SpawnProps(controller);
	blood_forced = GetEnvironmentVariableA("BLTD_BLOOD_MOON", 0, 0) > 0;
	moon_here = GetEnvironmentVariableA("BLTD_MOON_HERE", 0, 0) > 0;
	blood_update_tick = GetTickCount();
	EVENTS_LOG(LOG_LEVEL_INFO, "Halloween on %s: %u sky materials tinted, %u sun lights dimmed, %u fogs thickened, moon %s, glow material %s, pumpkin skins %s.", level.c_str(), (uint32_t)skyMaterials.size(), (uint32_t)lights.size(), fogs, moon ? "up" : "missing", glow ? "found" : "missing", SkinsReady() ? "ready" : "missing");
}

static void KeepView()
{
	if (UE3::IsValidObject(local_player) && !UE3::GetBoolProperty(local_player, ref.OverrideProperty)) {
		EVENTS_LOG(LOG_LEVEL_DEBUG, "%s", "The view override was off, the theme's view is back.");
		ApplyPostProcess();
	}
}

static void Flash(DWORD now)
{
	if (flash_step < 0) {
		if ((int32_t)(now - next_flash_tick) < 0) {
			return;
		}
		flash_step = 0;
		flash_step_tick = now;
	}
	else if (now - flash_step_tick < FLASH_STEP_MS[flash_step]) {
		return;
	}
	else {
		flash_step++;
		flash_step_tick = now;
	}
	bool bright = flash_step == 0 || flash_step == 2;
	if (ViewIsOurs()) {
		if (bright) {
			SetTones(BOLT_HIGHLIGHTS, BOLT_MIDTONES, BOLT_SHADOWS);
		}
		else {
			NightTones();
		}
	}
	if (flash_step == 0) {
		thunder_tick = (now + THUNDER_DELAY_MIN_MS + (DWORD)(rand() % (int)THUNDER_DELAY_SPREAD_MS)) | 1;
		if (rand() % 100 < SURGE_PERCENT) {
			StartSurge(now);
		}
	}
	if (flash_step == 3) {
		flash_step = -1;
		DWORD wait = FLASH_MIN_MS + (DWORD)(rand() % (int)FLASH_SPREAD_MS);
		next_flash_tick = now + (blood_wanted ? wait / 2 : wait);
	}
}

static int32_t HelmetElement(UObject* mesh_component)
{
	UObject* mesh = mesh_component && look.SkeletalMeshComponent_SkeletalMesh >= 0 ? Field<UObject*>(mesh_component, look.SkeletalMeshComponent_SkeletalMesh) : 0;
	if (!mesh || !ResolveGhosts()) {
		return -1;
	}
	UE3::TArray<UObject*>& materials = Field<UE3::TArray<UObject*> >(mesh, gref.Mesh_Materials);
	UE3::TArray<UObject*>& overrides = Field<UE3::TArray<UObject*> >(mesh_component, eoff.Mesh_Materials);
	for (int32_t element = 0; element < materials.Num && element < 16; element++) {
		UObject* candidates[2] = { materials.Data[element], element < overrides.Num ? overrides.Data[element] : 0 };
		for (UObject* material : candidates) {
			const char* name = material ? UE3::ObjectName(material) : "";
			for (const char* at = name; *at; at++) {
				if (_strnicmp(at, HELMET_MATERIAL_TEXT, strlen(HELMET_MATERIAL_TEXT)) == 0) {
					return element;
				}
			}
		}
	}
	static bool listed = false;
	if (!listed) {
		listed = true;
		char list[512] = {};
		for (int32_t element = 0; element < materials.Num && element < 16; element++) {
			UObject* material = element < overrides.Num && overrides.Data[element] ? overrides.Data[element] : materials.Data[element];
			strncat_s(list, sizeof(list), material ? UE3::ObjectName(material) : "none", _TRUNCATE);
			strncat_s(list, sizeof(list), " ", _TRUNCATE);
		}
		EVENTS_LOG(LOG_LEVEL_DEBUG, "Elements of %s: %s(%d overrides).", UE3::ObjectName(mesh), list, overrides.Num);
	}
	return -1;
}

static UObject* HideHelmet(UObject* mesh_component, int32_t element, UObject* holo)
{
	SetMaterial(mesh_component, element, holo);
	UObject* instance = InstanceMaterial(mesh_component, element);
	if (instance && UE3::FindNameIndex("SpawnInterp") >= 0) {
		SetScalarParameter(instance, UE3::FindNameIndex("SpawnInterp"), 0.0f);
	}
	return instance;
}

static UObject* ElementMaterial(UObject* mesh_component, int32_t element)
{
	UE3::TArray<UObject*>& materials = Field<UE3::TArray<UObject*> >(mesh_component, eoff.Mesh_Materials);
	return element < materials.Num ? materials.Data[element] : 0;
}

static void MaintainHeads(UObject* controller, DWORD now)
{
	if ((int32_t)(now - next_heads_tick) < 0) {
		return;
	}
	next_heads_tick = now + HEADS_INTERVAL_MS;
	for (size_t i = heads.size(); i-- > 0; ) {
		Head& head = heads[i];
		if (!UE3::IsValidObject(head.Pawn) || !IsAlive(head.Pawn)) {
			if (UE3::IsValidObject(head.Pawn)) {
				RiseFrom(controller, head.Pawn, now);
			}
			if (UE3::IsValidObject(head.Actor)) {
				DestroyActor(head.Actor);
			}
			heads.erase(heads.begin() + i);
			continue;
		}
		UObject* mesh = PawnSkeletalMesh(head.Pawn) ? Field<UObject*>(head.Pawn, look.Pawn_Mesh) : 0;
		if (mesh && head.HelmetElement >= 0 && UE3::IsValidObject(head.HelmetInstance) && ElementMaterial(mesh, head.HelmetElement) != head.HelmetInstance) {
			SetMaterial(mesh, head.HelmetElement, head.HelmetInstance);
		}
	}
	UObject* sphere = UE3::FindObject(SPHERE_MESH);
	UObject* holo = UE3::FindObject(HOLO_MATERIAL);
	if (!sphere) {
		return;
	}
	bool colour = holo && name_holo_color >= 0 && name_emissive_color >= 0 && name_dissolve >= 0;
	bool pumpkin = SkinsReady();
	UObject* world = Field<UObject*>(controller, off.Actor_WorldInfo);
	UObject* own = Field<UObject*>(controller, off.Controller_Pawn);
	int listed = 0;
	for (UObject* pawn = world ? Field<UObject*>(world, off.WorldInfo_PawnList) : 0; pawn && listed < NavMesh::LIST_LIMIT; pawn = Field<UObject*>(pawn, off.Pawn_NextPawn), listed++) {
		if (pawn == own || !IsAlive(pawn) || heads.size() >= HEAD_LIMIT) {
			continue;
		}
		bool known = false;
		for (const Head& head : heads) {
			known = known || head.Pawn == pawn;
		}
		UObject* skeletal = PawnSkeletalMesh(pawn);
		UObject* mesh = skeletal ? Field<UObject*>(pawn, look.Pawn_Mesh) : 0;
		if (known || !mesh || strstr(UE3::ObjectName(skeletal), "Turret")) {
			continue;
		}
		bool trail = pumpkin && TrailStyle();
		UObject* material = trail ? UE3::FindObject(TRAIL_MATERIAL) : pumpkin ? UE3::FindObject(GLOW_SKIN_MATERIAL) : colour ? holo : UE3::FindObject(GLOW_MATERIAL);
		FVector headScale = { HeadScale(), HeadScale(), HeadScale() * PUMPKIN_SQUASH };
		UObject* actor = SpawnMeshActor(controller, Field<FVector>(pawn, off.Actor_Location), headScale, sphere, HeadMaterial(material));
		if (!actor) {
			continue;
		}
		bool skinned = trail ? SkinMeshTrail(actor, UE3::FindObject(PUMPKIN_SKIN), TRAIL_COLOR, EnvFloat("BLTD_HEAD_INTERP", TRAIL_INTERP))
			: pumpkin ? SkinMesh(actor, HeadSkin(false), HeadSkin(true), HEAD_MIN_GLOW, HEAD_MAX_GLOW)
			: colour && ColourMesh(actor, HeadColor(), EnvFloat("BLTD_HEAD_DISSOLVE", HEAD_DISSOLVE));
		if (!skinned && (pumpkin || colour)) {
			EVENTS_LOG(LOG_LEVEL_WARN, "%s", "The head got no material instance, it keeps the material's own look.");
		}
		AttachToBone(actor, pawn, mesh, name_head);
		Head head = { pawn, actor, -1, 0 };
		if (pumpkin && colour) {
			head.HelmetElement = HelmetElement(mesh);
			if (head.HelmetElement >= 0) {
				head.HelmetInstance = HideHelmet(mesh, head.HelmetElement, holo);
			}
			else {
				EVENTS_LOG(LOG_LEVEL_DEBUG, "Pawn 0x%08x has no helmet element, its head keeps the helmet.", pawn);
			}
		}
		heads.push_back(head);
	}
}

static void Sounds(DWORD now)
{
	if (thunder_tick && (int32_t)(now - thunder_tick) >= 0) {
		thunder_tick = 0;
		PostSound(local_controller, THUNDER_SOUND);
	}
	if (bell_tick && (int32_t)(now - bell_tick) >= 0) {
		bell_tick = 0;
		PostSound(local_controller, BELL_SOUND);
	}
	if ((int32_t)(now - next_ambient_tick) >= 0) {
		next_ambient_tick = now + AMBIENT_MIN_MS + (DWORD)(rand() % (int)AMBIENT_SPREAD_MS);
		PostSound(local_controller, AMBIENT_SOUNDS[rand() % (sizeof(AMBIENT_SOUNDS) / sizeof(AMBIENT_SOUNDS[0]))]);
	}
}

static void Undress()
{
	ResetPostProcess();
	lights.clear();
	heads.clear();
	ghosts.clear();
	transients.clear();
	props.clear();
	ground_pumpkins.clear();
	moon = 0;
	local_player = 0;
	local_controller = 0;
	flash_step = -1;
	thunder_tick = 0;
	bell_tick = 0;
	sky_materials.clear();
	fog_components.clear();
	glows.clear();
	surge_step = -1;
	blood = 0.0f;
	blood_wanted = false;
	blood_moon = 0;
	blood_moon_instance = 0;
	active = false;
}

bool OnScriptCall(UObject* self, UE3::FFrame* stack)
{
	if (!active || self != local_player || stack->Node != ref.ClearOverride) {
		return true;
	}
	EVENTS_LOG(LOG_LEVEL_DEBUG, "%s", "The game cleared the view override, the theme's view stays.");
	ApplyPostProcess();
	return false;
}

void OnLevelUnload()
{
	if (active) {
		Undress();
	}
}

void OnPlayerTick(UObject* controller)
{
	if (!Resolve()) {
		return;
	}
	UObject* player = Field<UObject*>(controller, eoff.PC_Player);
	if (!player || !UE3::IsA(player, ref.LocalPlayerClass)) {
		return;
	}
	const char* name = LevelName(controller);
	if (level != name) {
		if (active) {
			Undress();
		}
		level = name;
		if (!*name || strcmp(name, MENU_LEVEL) == 0 || strncmp(name, BLACK_OPS_PREFIX, strlen(BLACK_OPS_PREFIX)) == 0 || !ThemeWanted()) {
			return;
		}
		name_final_rgb = UE3::FindNameIndex("FinalRGB_Multiply");
		name_final_desaturation = UE3::FindNameIndex("FinalDesaturation");
		name_head = UE3::FindNameIndex("Head");
		name_holo_color = UE3::FindNameIndex("HoloColor");
		name_emissive_color = UE3::FindNameIndex("EmissiveColor");
		name_dissolve = UE3::FindNameIndex("DeathEffectInterp");
		name_diffuse = UE3::FindNameIndex("Diffuse");
		name_mask = UE3::FindNameIndex("Mask");
		name_min_emis = UE3::FindNameIndex("Min_Emis");
		name_max_emis = UE3::FindNameIndex("Max_Emis");
		name_htv_mode = UE3::FindNameIndex("HTV_mode");
		if (name_final_rgb < 0 || name_final_desaturation < 0 || name_head < 0) {
			EVENTS_LOG(LOG_LEVEL_WARN, "%s", "The sky or bone names are missing, the theme stays off.");
			return;
		}
		local_player = player;
		local_controller = controller;
		active = true;
		EVENTS_LOG(LOG_LEVEL_DEBUG, "Dressing %s for Halloween.", name);
		srand(GetTickCount());
		next_flash_tick = GetTickCount() + 5000;
		next_heads_tick = 0;
		bell_tick = (GetTickCount() + BELL_DELAY_MS) | 1;
		next_ambient_tick = GetTickCount() + AMBIENT_MIN_MS;
		next_flyby_tick = GetTickCount() + FLYBY_MIN_MS;
		next_burst_check_tick = 0;
		Dress(controller);
		return;
	}
	if (!active || player != local_player) {
		return;
	}
	DWORD now = GetTickCount();
	KeepView();
	Flash(now);
	Surge(now);
	if (moon && UE3::IsValidObject(moon)) {
		MoveActor(moon, MoonFor(controller));
	}
	if (extra_resolved > 0) {
		BloodMoon(controller, now);
	}
	Sounds(now);
	MaintainHeads(controller, now);
	MoveGhosts(now);
	MoveTransients(now);
	Flyby(controller, now);
	BurstPumpkins(controller, now);
	PlaceTestProps(controller);
}

}
}
