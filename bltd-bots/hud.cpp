#include "bots-internal.hpp"
#include "hud.hpp"
#include <algorithm>
#include <math.h>
#include <string>
#include <wchar.h>

#define HUD_LOG(level, format, ...) Log::Write(level, "[Hud] " format, __VA_ARGS__)

using UE3::UObject;
using UE3::FFrame;
using UE3::FString;
using UE3::FVector;
using UE3::Field;
using UE3::TArray;

namespace Bots {
namespace Hud {

static const wchar_t HOST_MARK[] = L"<font color='#BA3801'>HOST</font> ";
static const size_t SCOREBOARD_NAME_CHARS = 23;
static const size_t HOST_MARK_CHARS = 5;
static const wchar_t HOST_ICON[] = L"<IMG src='imgps://BltdIcons.TX_HostIcon' width='24' height='24'> ";
static const int FIRST_LOBBY_ROW_FUNCTION = 4;

static const char* const ROW_FUNCTIONS[] = {
	"FoxGame.FoxMainMenuMovieBackBO.UpdateEntry",
	"FoxGame.FoxMainMenuMovieBackDM.UpdateEntry",
	"FoxGame.FoxMainMenuMovieBackTDM.UpdateRedEntry",
	"FoxGame.FoxMainMenuMovieBackTDM.UpdateBlueEntry",
	"FoxGame.FoxMainMenuMovieGameLobbyBO.UpdateEntry",
	"FoxGame.FoxMainMenuMovieGameLobbyDM.UpdateEntry",
	"FoxGame.FoxMainMenuMovieGameLobbyTDM.UpdateRedEntry",
	"FoxGame.FoxMainMenuMovieGameLobbyTDM.UpdateBlueEntry",
};
static const int ROW_FUNCTION_COUNT = sizeof(ROW_FUNCTIONS) / sizeof(ROW_FUNCTIONS[0]);

static const wchar_t ICON_PACKAGE[] = L"BltdIcons";
static const char ICON_TEXTURE[] = "BltdIcons.TX_TurretIcon";
static const uint8_t VISOR_HRV = 3;
static const float ICON_ABOVE = 110.0f;
static const float ICON_NEAR_SIZE = 56.0f;
static const float ICON_FAR_SIZE = 34.0f;
static const float ICON_NEAR_DISTANCE = 600.0f;
static const float ICON_FAR_DISTANCE = 6000.0f;
static const float ICON_REFERENCE_LINES = 1080.0f;
static const uint8_t ICON_ALPHA = 170;
static const char HRV_COLOR_PARAMETER[] = "Color";
static const float TURRET_HRV_STRENGTH = 2.0f;

static const float CROSSHAIR_STATIC_SCALE = 0.6f;
static const int CROSSHAIR_STATIC_WIDEN = 1;
static const float CROSSHAIR_OUTLINE_OPACITY = 0.6f;
static const float CROSSHAIR_DIMENSIONS[] = { 1.0f, 0.66f, 0.33f, 0.0f };
static const wchar_t* const CROSSHAIR_DIMENSION_NAMES[] = { L"NORMAL", L"MEDIUM", L"SMALL", L"TINY" };
static const int CROSSHAIR_DIMENSION_COUNT = sizeof(CROSSHAIR_DIMENSIONS) / sizeof(CROSSHAIR_DIMENSIONS[0]);
static const int CROSSHAIR_TILES = 5;
struct TileCoords {
	float U;
	float V;
	float UL;
	float VL;
};

struct Color {
	uint8_t B;
	uint8_t G;
	uint8_t R;
	uint8_t A;
};

static const Color CROSSHAIR_COLORS[] = { { 255, 255, 255, 255 }, { 0, 255, 0, 255 }, { 255, 255, 0, 255 }, { 255, 0, 255, 255 } };
static const wchar_t* const CROSSHAIR_COLOR_NAMES[] = { L"WHITE", L"GREEN", L"CYAN", L"MAGENTA" };
static const int CROSSHAIR_COLOR_COUNT = sizeof(CROSSHAIR_COLORS) / sizeof(CROSSHAIR_COLORS[0]);

struct LinearColor {
	float R;
	float G;
	float B;
	float A;
};

static struct {
	UObject* Weapon_DrawNormal;
	UObject* PRI_bHostPRI;
	UObject* Rows[ROW_FUNCTION_COUNT];
	UObject* Pawn_PostRenderFor;
	UObject* Pawn_DrawEnemyHVTIcon;
	UObject* Pawn_ToggleHRVEffects;
	UObject* Actor_bPostRenderIfNotVisible;
	UObject* Pawn_bInvincible;
	UObject* Mesh_GetNumElements;
	UObject* Mesh_SetMaterial;
	UObject* Mesh_CreateMaterial;
	UObject* Material_SetVector;
	UObject* MaterialInstance;
	UObject* TurretPawn;
	UObject* Canvas_Project;
	UObject* Canvas_DrawTile;
} ref;

static struct {
	int32_t DrawNormal_H;
	int32_t Hud_Canvas;
	int32_t Canvas_ClipX;
	int32_t Weapon_HudSpreadSize;
	int32_t Weapon_BaseSpread;
	int32_t Weapon_MaximumSpread;
	int32_t Weapon_ModSpreadMultipler;
	int32_t Weapon_CrosshairIcon;
	int32_t Weapon_CrosshairSheet;
	int32_t Tile_U;
	int32_t Tile_V;
	int32_t Movie_PCOwner;
	int32_t WorldInfo_GRI;
	int32_t GRI_PRIArray;
	int32_t PRI_PlayerName;
	int32_t Rows_PlayerName[ROW_FUNCTION_COUNT];
	int32_t PostRender_PC;
	int32_t PostRender_Canvas;
	int32_t PostRender_CameraPosition;
	int32_t PostRender_CameraDir;
	int32_t PC_CurrentVisorType;
	int32_t Canvas_ClipY;
	int32_t Canvas_CurX;
	int32_t Canvas_CurY;
	int32_t Canvas_DrawColor;
	int32_t Project_Location;
	int32_t Project_ReturnValue;
	int32_t DrawTile_Tex;
	int32_t DrawTile_XL;
	int32_t DrawTile_YL;
	int32_t DrawTile_UL;
	int32_t DrawTile_VL;
	int32_t Texture_SizeX;
	int32_t Texture_SizeY;
	int32_t ToggleHRV_bEnabled;
	int32_t Pawn_Mesh;
	int32_t GetNumElements_ReturnValue;
	int32_t SetMaterial_Index;
	int32_t SetMaterial_Material;
	int32_t CreateMaterial_ReturnValue;
	int32_t SetVector_Name;
	int32_t SetVector_Value;
} hoff;

static bool crosshair_ready = false;
static bool marker_ready = false;
static bool icon_ready = false;
static bool icon_loaded = false;
static UObject* icon_texture = 0;
static UObject* turret_material = 0;
static bool turret_was_invincible = false;

bool Resolve()
{
	LoadHudConfig(config.Hud);
	ref.Weapon_DrawNormal = UE3::FindObject("FoxGame.FoxWeapon.DrawWeaponCrosshairNormal");
	struct { int32_t* Target; const char* Path; } crosshairRefs[] = {
		{ &hoff.DrawNormal_H, "FoxGame.FoxWeapon.DrawWeaponCrosshairNormal.H" },
		{ &hoff.Hud_Canvas, "Engine.HUD.Canvas" },
		{ &hoff.Canvas_ClipX, "Engine.Canvas.ClipX" },
		{ &hoff.Weapon_HudSpreadSize, "FoxGame.FoxWeapon.HudSpreadSize" },
		{ &hoff.Weapon_BaseSpread, "FoxGame.FoxWeapon.BaseSpread" },
		{ &hoff.Weapon_MaximumSpread, "FoxGame.FoxWeapon.MaximumSpread" },
		{ &hoff.Weapon_ModSpreadMultipler, "FoxGame.FoxWeapon.ModSpreadMultipler" },
		{ &hoff.Weapon_CrosshairIcon, "FoxGame.FoxWeapon.CrosshairIcon" },
		{ &hoff.Weapon_CrosshairSheet, "FoxGame.FoxWeapon.CrosshairSheet" },
		{ &hoff.Canvas_ClipY, "Engine.Canvas.ClipY" },
		{ &hoff.Canvas_CurX, "Engine.Canvas.CurX" },
		{ &hoff.Canvas_CurY, "Engine.Canvas.CurY" },
		{ &hoff.Canvas_DrawColor, "Engine.Canvas.DrawColor" },
		{ &hoff.DrawTile_Tex, "Engine.Canvas.DrawTile.Tex" },
		{ &hoff.DrawTile_XL, "Engine.Canvas.DrawTile.XL" },
		{ &hoff.DrawTile_YL, "Engine.Canvas.DrawTile.YL" },
		{ &hoff.Tile_U, "Engine.Canvas.DrawTile.U" },
		{ &hoff.Tile_V, "Engine.Canvas.DrawTile.V" },
		{ &hoff.DrawTile_UL, "Engine.Canvas.DrawTile.UL" },
		{ &hoff.DrawTile_VL, "Engine.Canvas.DrawTile.VL" },
	};
	ref.Canvas_DrawTile = UE3::FindObject("Engine.Canvas.DrawTile");
	crosshair_ready = ref.Weapon_DrawNormal != 0 && ref.Canvas_DrawTile != 0;
	for (auto& offsetRef : crosshairRefs) {
		*offsetRef.Target = UE3::FindPropertyOffset(offsetRef.Path);
		if (*offsetRef.Target < 0) {
			HUD_LOG(LOG_LEVEL_WARN, "%s not found, the crosshair stays dynamic.", offsetRef.Path);
			crosshair_ready = false;
		}
	}
	ref.PRI_bHostPRI = UE3::FindObject("FoxGame.FoxPlayerReplicationInfo.bHostPRI");
	struct { int32_t* Target; const char* Path; } markerRefs[] = {
		{ &hoff.Movie_PCOwner, "FoxGame.FoxMovieBase.PCOwner" },
		{ &hoff.WorldInfo_GRI, "Engine.WorldInfo.GRI" },
		{ &hoff.GRI_PRIArray, "Engine.GameReplicationInfo.PRIArray" },
		{ &hoff.PRI_PlayerName, "Engine.PlayerReplicationInfo.PlayerName" },
	};
	marker_ready = ref.PRI_bHostPRI != 0;
	for (auto& offsetRef : markerRefs) {
		*offsetRef.Target = UE3::FindPropertyOffset(offsetRef.Path);
		marker_ready = marker_ready && *offsetRef.Target >= 0;
	}
	for (int i = 0; i < ROW_FUNCTION_COUNT; i++) {
		ref.Rows[i] = UE3::FindObject(ROW_FUNCTIONS[i]);
		std::string name = std::string(ROW_FUNCTIONS[i]) + ".PlayerName";
		hoff.Rows_PlayerName[i] = UE3::FindPropertyOffset(name.c_str());
		marker_ready = marker_ready && ref.Rows[i] && hoff.Rows_PlayerName[i] >= 0;
	}
	if (!marker_ready) {
		HUD_LOG(LOG_LEVEL_WARN, "%s", "The player lists do not mark the host.");
	}
	struct { UObject** Target; const char* Path; } iconObjects[] = {
		{ &ref.Pawn_PostRenderFor, "FoxGame.FoxPawn.PostRenderFor" },
		{ &ref.Pawn_DrawEnemyHVTIcon, "FoxGame.FoxPawn.DrawEnemyHVTIcon" },
		{ &ref.Pawn_ToggleHRVEffects, "FoxGame.FoxPawn.ToggleHRVEffects" },
		{ &ref.Actor_bPostRenderIfNotVisible, "Engine.Actor.bPostRenderIfNotVisible" },
		{ &ref.Pawn_bInvincible, "FoxGame.FoxPawn.bInvincible" },
		{ &ref.Mesh_GetNumElements, "Engine.MeshComponent.GetNumElements" },
		{ &ref.Mesh_SetMaterial, "Engine.MeshComponent.SetMaterial" },
		{ &ref.Mesh_CreateMaterial, "Engine.MeshComponent.CreateAndSetMaterialInstanceConstant" },
		{ &ref.Material_SetVector, "Engine.MaterialInstanceConstant.SetVectorParameterValue" },
		{ &ref.MaterialInstance, "Engine.MaterialInstanceConstant" },
		{ &ref.TurretPawn, "FoxGame.FoxGame_AITurretPawn" },
		{ &ref.Canvas_Project, "Engine.Canvas.Project" },
		{ &ref.Canvas_DrawTile, "Engine.Canvas.DrawTile" },
	};
	struct { int32_t* Target; const char* Path; } iconOffsets[] = {
		{ &hoff.PostRender_PC, "FoxGame.FoxPawn.PostRenderFor.PC" },
		{ &hoff.PostRender_Canvas, "FoxGame.FoxPawn.PostRenderFor.Canvas" },
		{ &hoff.PostRender_CameraPosition, "FoxGame.FoxPawn.PostRenderFor.CameraPosition" },
		{ &hoff.PostRender_CameraDir, "FoxGame.FoxPawn.PostRenderFor.CameraDir" },
		{ &hoff.PC_CurrentVisorType, "FoxGame.FoxPC.CurrentVisorType" },
		{ &hoff.Canvas_ClipY, "Engine.Canvas.ClipY" },
		{ &hoff.Canvas_CurX, "Engine.Canvas.CurX" },
		{ &hoff.Canvas_CurY, "Engine.Canvas.CurY" },
		{ &hoff.Canvas_DrawColor, "Engine.Canvas.DrawColor" },
		{ &hoff.Project_Location, "Engine.Canvas.Project.Location" },
		{ &hoff.Project_ReturnValue, "Engine.Canvas.Project.ReturnValue" },
		{ &hoff.DrawTile_Tex, "Engine.Canvas.DrawTile.Tex" },
		{ &hoff.DrawTile_XL, "Engine.Canvas.DrawTile.XL" },
		{ &hoff.DrawTile_YL, "Engine.Canvas.DrawTile.YL" },
		{ &hoff.DrawTile_UL, "Engine.Canvas.DrawTile.UL" },
		{ &hoff.DrawTile_VL, "Engine.Canvas.DrawTile.VL" },
		{ &hoff.Texture_SizeX, "Engine.Texture2D.SizeX" },
		{ &hoff.Texture_SizeY, "Engine.Texture2D.SizeY" },
		{ &hoff.ToggleHRV_bEnabled, "FoxGame.FoxPawn.ToggleHRVEffects.bEnabled" },
		{ &hoff.Pawn_Mesh, "Engine.Pawn.Mesh" },
		{ &hoff.GetNumElements_ReturnValue, "Engine.MeshComponent.GetNumElements.ReturnValue" },
		{ &hoff.SetMaterial_Index, "Engine.MeshComponent.SetMaterial.ElementIndex" },
		{ &hoff.SetMaterial_Material, "Engine.MeshComponent.SetMaterial.Material" },
		{ &hoff.CreateMaterial_ReturnValue, "Engine.MeshComponent.CreateAndSetMaterialInstanceConstant.ReturnValue" },
		{ &hoff.SetVector_Name, "Engine.MaterialInstanceConstant.SetVectorParameterValue.ParameterName" },
		{ &hoff.SetVector_Value, "Engine.MaterialInstanceConstant.SetVectorParameterValue.Value" },
	};
	icon_ready = true;
	for (auto& objectRef : iconObjects) {
		*objectRef.Target = UE3::FindObject(objectRef.Path);
		if (!*objectRef.Target) {
			HUD_LOG(LOG_LEVEL_WARN, "%s not found, the HRV view shows no turret icon.", objectRef.Path);
			icon_ready = false;
		}
	}
	for (auto& offsetRef : iconOffsets) {
		*offsetRef.Target = UE3::FindPropertyOffset(offsetRef.Path);
		if (*offsetRef.Target < 0) {
			HUD_LOG(LOG_LEVEL_WARN, "%s not found, the HRV view shows no turret icon.", offsetRef.Path);
			icon_ready = false;
		}
	}
	return crosshair_ready && marker_ready && icon_ready;
}

static void PlaceCrosshairArms(UObject* weapon, FFrame* stack)
{
	UObject* hud = Field<UObject*>(stack->Locals, hoff.DrawNormal_H);
	UObject* canvas = hud ? Field<UObject*>(hud, hoff.Hud_Canvas) : 0;
	const TArray<float>& base = Field<TArray<float> >(weapon, hoff.Weapon_BaseSpread);
	if (!canvas || !base.Data || base.Num < 1) {
		return;
	}
	float rest = (std::min)(Field<float>(weapon, hoff.Weapon_MaximumSpread), base.Data[0] * Field<float>(weapon, hoff.Weapon_ModSpreadMultipler));
	Field<float>(weapon, hoff.Weapon_HudSpreadSize) = Field<float>(canvas, hoff.Canvas_ClipX) * rest;
}

static float CrosshairScale()
{
	return config.Hud.CrosshairDynamic ? 1.0f : CROSSHAIR_STATIC_SCALE;
}

static void DrawCrosshairTile(UObject* canvas, UObject* sheet, const TileCoords& tile, float left, float top, float width, float height)
{
	Field<float>(canvas, hoff.Canvas_CurX) = left;
	Field<float>(canvas, hoff.Canvas_CurY) = top;
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.Canvas_DrawTile) + 16, 0);
	Field<UObject*>(parms.data(), hoff.DrawTile_Tex) = sheet;
	Field<float>(parms.data(), hoff.DrawTile_XL) = width;
	Field<float>(parms.data(), hoff.DrawTile_YL) = height;
	Field<float>(parms.data(), hoff.Tile_U) = tile.U;
	Field<float>(parms.data(), hoff.Tile_V) = tile.V;
	Field<float>(parms.data(), hoff.DrawTile_UL) = tile.UL;
	Field<float>(parms.data(), hoff.DrawTile_VL) = tile.VL;
	UE3::ProcessNativeEvent(canvas, ref.Canvas_DrawTile, parms.data());
}

static void DrawScaledCrosshair(UObject* weapon, FFrame* stack)
{
	UObject* hud = Field<UObject*>(stack->Locals, hoff.DrawNormal_H);
	UObject* canvas = hud ? Field<UObject*>(hud, hoff.Hud_Canvas) : 0;
	UObject* sheet = Field<UObject*>(weapon, hoff.Weapon_CrosshairSheet);
	if (!canvas || !sheet) {
		return;
	}
	const float scale = CrosshairScale();
	const uint8_t* icon = &Field<uint8_t>(weapon, hoff.Weapon_CrosshairIcon);
	const TileCoords* tiles = (const TileCoords*)icon;
	const float* size = (const float*)(icon + sizeof(TileCoords) * CROSSHAIR_TILES + sizeof(uint32_t));
	const float spread = Field<float>(weapon, hoff.Weapon_HudSpreadSize);
	const float x = 0.5f * Field<float>(canvas, hoff.Canvas_ClipX);
	const float y = 0.5f * Field<float>(canvas, hoff.Canvas_ClipY);
	const int choice = config.Hud.CrosshairDimensions;
	const float dimension = CROSSHAIR_DIMENSIONS[choice >= 0 && choice < CROSSHAIR_DIMENSION_COUNT ? choice : 0];
	const float vertical = (std::max)(0.0f, spread - size[1]) * 0.5f * scale * dimension;
	const float horizontal = (std::max)(0.0f, spread - size[0]) * 0.5f * scale * dimension;
	const float offsets[CROSSHAIR_TILES][2] = { { 0.0f, 0.0f }, { 0.0f, -vertical }, { 0.0f, vertical }, { horizontal, 0.0f }, { -horizontal, 0.0f } };
	const bool upright[CROSSHAIR_TILES] = { false, true, true, false, false };
	const float pixel = (std::max)(1.0f, floorf(Field<float>(canvas, hoff.Canvas_ClipY) / 1080.0f + 0.5f));
	Color& color = Field<Color>(canvas, hoff.Canvas_DrawColor);
	const Color drawn = color;
	for (int pass = 0; pass < 2; pass++) {
		const bool outline = pass == 0;
		const int across = CROSSHAIR_STATIC_WIDEN + (outline ? 1 : 0);
		const int along = outline ? 1 : 0;
		color = outline ? Color{ 0, 0, 0, (uint8_t)(drawn.A * CROSSHAIR_OUTLINE_OPACITY) } : drawn;
		for (int i = 0; i < CROSSHAIR_TILES; i++) {
			const TileCoords& tile = tiles[i];
			if (tile.UL <= 0.0f || tile.VL <= 0.0f) {
				continue;
			}
			const float width = tile.UL * scale;
			const float height = tile.VL * scale;
			const float left = x - 0.5f * width + offsets[i][0];
			const float top = y - 0.5f * height + offsets[i][1];
			for (int a = -across; a <= across; a++) {
				for (int b = -along; b <= along; b++) {
					const float dx = (upright[i] ? a : b) * pixel;
					const float dy = (upright[i] ? b : a) * pixel;
					DrawCrosshairTile(canvas, sheet, tile, left + dx, top + dy, width, height);
				}
			}
		}
	}
	color = drawn;
}

static bool IsHostRow(UObject* movie, const FString& row)
{
	UObject* pc = Field<UObject*>(movie, hoff.Movie_PCOwner);
	UObject* world = pc ? Field<UObject*>(pc, off.Actor_WorldInfo) : 0;
	UObject* gri = world ? Field<UObject*>(world, hoff.WorldInfo_GRI) : 0;
	if (!gri || !row.Data) {
		return false;
	}
	TArray<UObject*>& pris = Field<TArray<UObject*> >(gri, hoff.GRI_PRIArray);
	for (int32_t i = 0; i < pris.Num; i++) {
		UObject* pri = pris.Data[i];
		if (!pri || !UE3::GetBoolProperty(pri, ref.PRI_bHostPRI)) {
			continue;
		}
		const FString& name = Field<FString>(pri, hoff.PRI_PlayerName);
		if (!name.Data || name.Num < 2 || row.Num < name.Num) {
			return false;
		}
		return wcscmp(row.Data + (row.Num - name.Num), name.Data) == 0;
	}
	return false;
}

static bool LoadIcons()
{
	static bool tried = false;
	static bool loaded = false;
	if (!tried) {
		tried = true;
		loaded = UE3::LoadPackage(ICON_PACKAGE);
		if (!loaded) {
			HUD_LOG(LOG_LEVEL_WARN, "%ls.upk did not load, the HUD icons are missing.", ICON_PACKAGE);
		}
	}
	return loaded;
}

static size_t VisibleChars(const wchar_t* text)
{
	size_t count = 0;
	bool in_tag = false;
	for (; *text; text++) {
		if (*text == L'<') {
			in_tag = true;
		}
		else if (*text == L'>') {
			in_tag = false;
		}
		else if (!in_tag) {
			count++;
		}
	}
	return count;
}

static void MarkHost(FString& row, bool scoreboard)
{
	const wchar_t* mark_text = HOST_MARK;
	if (scoreboard && VisibleChars(row.Data) + HOST_MARK_CHARS > SCOREBOARD_NAME_CHARS && LoadIcons()) {
		mark_text = HOST_ICON;
	}
	size_t mark = wcslen(mark_text);
	size_t old_length = row.Num > 0 ? (size_t)row.Num - 1 : 0;
	size_t length = mark + old_length + 1;
	wchar_t* text = (wchar_t*)UE3::Malloc((uint32_t)(length * sizeof(wchar_t)), 8);
	if (!text) {
		return;
	}
	memcpy(text, mark_text, mark * sizeof(wchar_t));
	if (old_length) {
		memcpy(text + mark, row.Data, old_length * sizeof(wchar_t));
	}
	text[length - 1] = 0;
	UE3::FreeArray(&row);
	row.Data = text;
	row.Num = row.Max = (int32_t)length;
}

static void DrawTurretIcon(UObject* turret, FFrame* stack)
{
	UObject* pc = Field<UObject*>(stack->Locals, hoff.PostRender_PC);
	UObject* canvas = Field<UObject*>(stack->Locals, hoff.PostRender_Canvas);
	if (!pc || !canvas || !UE3::IsA(pc, cls.FoxPC) || Field<uint8_t>(pc, hoff.PC_CurrentVisorType) != VISOR_HRV || !IsAlive(turret)) {
		return;
	}
	if (!icon_loaded) {
		icon_loaded = true;
		if (LoadIcons()) {
			icon_texture = UE3::FindObject(ICON_TEXTURE);
		}
		if (!icon_texture) {
			HUD_LOG(LOG_LEVEL_WARN, "%s was not found, the HRV view shows no turret icon.", ICON_TEXTURE);
		}
	}
	if (!icon_texture) {
		return;
	}
	const FVector& camera = Field<FVector>(stack->Locals, hoff.PostRender_CameraPosition);
	const FVector& facing = Field<FVector>(stack->Locals, hoff.PostRender_CameraDir);
	FVector at = Field<FVector>(turret, off.Actor_Location);
	at.Z += ICON_ABOVE;
	if ((at.X - camera.X) * facing.X + (at.Y - camera.Y) * facing.Y + (at.Z - camera.Z) * facing.Z <= 0.0f) {
		return;
	}
	std::vector<uint8_t> project(UE3::FunctionParmsSize(ref.Canvas_Project) + 16, 0);
	Field<FVector>(project.data(), hoff.Project_Location) = at;
	UE3::ProcessNativeEvent(canvas, ref.Canvas_Project, project.data());
	const FVector& screen = Field<FVector>(project.data(), hoff.Project_ReturnValue);
	float along = (Distance(at, camera) - ICON_NEAR_DISTANCE) / (ICON_FAR_DISTANCE - ICON_NEAR_DISTANCE);
	along = (std::max)(0.0f, (std::min)(1.0f, along));
	float size = (ICON_NEAR_SIZE + (ICON_FAR_SIZE - ICON_NEAR_SIZE) * along) * Field<float>(canvas, hoff.Canvas_ClipY) / ICON_REFERENCE_LINES;
	Field<float>(canvas, hoff.Canvas_CurX) = screen.X - size / 2.0f;
	Field<float>(canvas, hoff.Canvas_CurY) = screen.Y - size / 2.0f;
	Field<Color>(canvas, hoff.Canvas_DrawColor) = Color{ 255, 255, 255, ICON_ALPHA };
	std::vector<uint8_t> tile(UE3::FunctionParmsSize(ref.Canvas_DrawTile) + 16, 0);
	Field<UObject*>(tile.data(), hoff.DrawTile_Tex) = icon_texture;
	Field<float>(tile.data(), hoff.DrawTile_XL) = size;
	Field<float>(tile.data(), hoff.DrawTile_YL) = size;
	Field<float>(tile.data(), hoff.DrawTile_UL) = (float)Field<int32_t>(icon_texture, hoff.Texture_SizeX);
	Field<float>(tile.data(), hoff.DrawTile_VL) = (float)Field<int32_t>(icon_texture, hoff.Texture_SizeY);
	UE3::ProcessNativeEvent(canvas, ref.Canvas_DrawTile, tile.data());
}

static UObject* TurretMaterial(UObject* mesh)
{
	if (UE3::IsValidObject(turret_material) && UE3::ObjectClass(turret_material) == ref.MaterialInstance) {
		return turret_material;
	}
	std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.Mesh_CreateMaterial) + 16, 0);
	UE3::ProcessNativeEvent(mesh, ref.Mesh_CreateMaterial, parms.data());
	turret_material = Field<UObject*>(parms.data(), hoff.CreateMaterial_ReturnValue);
	if (!turret_material) {
		return 0;
	}
	std::vector<uint8_t> color(UE3::FunctionParmsSize(ref.Material_SetVector) + 16, 0);
	Field<UE3::FName>(color.data(), hoff.SetVector_Name) = UE3::FName{ UE3::FindNameIndex(HRV_COLOR_PARAMETER), 0 };
	Field<LinearColor>(color.data(), hoff.SetVector_Value) = LinearColor{ TURRET_HRV_STRENGTH, TURRET_HRV_STRENGTH, TURRET_HRV_STRENGTH, 1.0f };
	UE3::ProcessNativeEvent(turret_material, ref.Material_SetVector, color.data());
	return turret_material;
}

static void PaintTurret(UObject* turret)
{
	UObject* mesh = Field<UObject*>(turret, hoff.Pawn_Mesh);
	UObject* material = mesh ? TurretMaterial(mesh) : 0;
	if (!material) {
		return;
	}
	std::vector<uint8_t> count(UE3::FunctionParmsSize(ref.Mesh_GetNumElements) + 16, 0);
	UE3::ProcessNativeEvent(mesh, ref.Mesh_GetNumElements, count.data());
	int32_t elements = Field<int32_t>(count.data(), hoff.GetNumElements_ReturnValue);
	for (int32_t i = 0; i < elements; i++) {
		std::vector<uint8_t> parms(UE3::FunctionParmsSize(ref.Mesh_SetMaterial) + 16, 0);
		Field<int32_t>(parms.data(), hoff.SetMaterial_Index) = i;
		Field<UObject*>(parms.data(), hoff.SetMaterial_Material) = material;
		UE3::ProcessNativeEvent(mesh, ref.Mesh_SetMaterial, parms.data());
	}
}

void OnScriptReturn(UObject* self, FFrame* stack)
{
	if (!icon_ready || !UE3::IsA(self, ref.TurretPawn)) {
		return;
	}
	if (stack->Node == ref.Pawn_PostRenderFor) {
		DrawTurretIcon(self, stack);
	}
	else if (stack->Node == ref.Pawn_ToggleHRVEffects) {
		UE3::SetBoolProperty(self, ref.Pawn_bInvincible, turret_was_invincible);
		// The HUD's overlay pass skips a pawn that was not drawn, such as a turret behind a wall.
		UE3::SetBoolProperty(self, ref.Actor_bPostRenderIfNotVisible, true);
		if (Field<uint32_t>(stack->Locals, hoff.ToggleHRV_bEnabled) & 1) {
			PaintTurret(self);
		}
	}
}

static void TintCrosshair(FFrame* stack)
{
	const int choice = config.Hud.CrosshairColor;
	UObject* hud = Field<UObject*>(stack->Locals, hoff.DrawNormal_H);
	UObject* canvas = hud ? Field<UObject*>(hud, hoff.Hud_Canvas) : 0;
	if (choice <= 0 || choice >= CROSSHAIR_COLOR_COUNT || !canvas) {
		return;
	}
	Color& color = Field<Color>(canvas, hoff.Canvas_DrawColor);
	if (color.R == 255 && color.G == 255 && color.B == 255) {
		color.R = CROSSHAIR_COLORS[choice].R;
		color.G = CROSSHAIR_COLORS[choice].G;
		color.B = CROSSHAIR_COLORS[choice].B;
	}
}

int CrosshairDimensionsCount()
{
	return CROSSHAIR_DIMENSION_COUNT;
}

const wchar_t* CrosshairDimensionsName(int choice)
{
	return CROSSHAIR_DIMENSION_NAMES[choice >= 0 && choice < CROSSHAIR_DIMENSION_COUNT ? choice : 0];
}

int CrosshairColorCount()
{
	return CROSSHAIR_COLOR_COUNT;
}

const wchar_t* CrosshairColorName(int choice)
{
	return CROSSHAIR_COLOR_NAMES[choice >= 0 && choice < CROSSHAIR_COLOR_COUNT ? choice : 0];
}

bool OnScriptCall(UObject* self, FFrame* stack)
{
	UObject* node = stack->Node;
	if (node == ref.Weapon_DrawNormal) {
		if (crosshair_ready) {
			TintCrosshair(stack);
		}
		if (crosshair_ready && !config.Hud.CrosshairDynamic) {
			PlaceCrosshairArms(self, stack);
		}
		if (crosshair_ready && CrosshairScale() != 1.0f) {
			DrawScaledCrosshair(self, stack);
			return false;
		}
		return true;
	}
	if (icon_ready && node == ref.Pawn_ToggleHRVEffects && UE3::IsA(self, ref.TurretPawn)) {
		turret_was_invincible = UE3::GetBoolProperty(self, ref.Pawn_bInvincible);
		UE3::SetBoolProperty(self, ref.Pawn_bInvincible, false);
		return true;
	}
	if (icon_ready && node == ref.Pawn_DrawEnemyHVTIcon && UE3::IsA(self, ref.TurretPawn)) {
		return false;
	}
	if (!marker_ready) {
		return true;
	}
	for (int i = 0; i < ROW_FUNCTION_COUNT; i++) {
		if (node == ref.Rows[i]) {
			FString& row = Field<FString>(stack->Locals, hoff.Rows_PlayerName[i]);
			if (IsHostRow(self, row)) {
				MarkHost(row, i < FIRST_LOBBY_ROW_FUNCTION);
			}
			return true;
		}
	}
	return true;
}

}
}
