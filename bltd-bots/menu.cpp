#include "bots-internal.hpp"
#include "menu.hpp"
#include "hud.hpp"
#include "platform.hpp"
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using UE3::UObject;
using UE3::FFrame;
using UE3::FString;
using UE3::TArray;
using UE3::Field;

namespace Bots {
namespace Menu {

static const int CHOICE_OFF = 0;
static const int CHOICE_FILL = 1;
static const char PATCH_MARK[] = "FSCommand:changeBots";
static const char CONTROLS_PATCH_MARK[] = "FSCommand:setCrosshairDynamic";
static const wchar_t CROSSHAIR_COMMAND[] = L"setCrosshairDynamic";
static const wchar_t CROSSHAIR_TOOLTIP_KEY[] = L"crosshairTooltip";
static const wchar_t CROSSHAIR_LABEL[] = L"CROSSHAIRS DYNAMIC";
static const wchar_t CROSSHAIR_TOOLTIP[] = L"Checked: the crosshair moves and spreads while you move and fire. Unchecked: it is smaller and stays still. Your shots spread the same either way.";
static const wchar_t CROSSHAIR_DIMENSIONS_COMMAND[] = L"cycleCrosshairDimensions";
static const wchar_t CROSSHAIR_DIMENSIONS_TOOLTIP_KEY[] = L"crosshairDimensionsTooltip";
static const wchar_t CROSSHAIR_DIMENSIONS_LABEL[] = L"CROSSHAIR DIMENSIONS";
static const wchar_t CROSSHAIR_DIMENSIONS_TOOLTIP[] = L"Click to change the size of the static crosshair. Turn off CROSSHAIRS DYNAMIC to use it.";
static const wchar_t CROSSHAIR_COLOR_COMMAND[] = L"cycleCrosshairColor";
static const wchar_t CROSSHAIR_COLOR_TOOLTIP_KEY[] = L"crosshairColorTooltip";
static const wchar_t CROSSHAIR_COLOR_LABEL[] = L"CROSSHAIR COLOR";
static const wchar_t CROSSHAIR_COLOR_TOOLTIP[] = L"Click to change the color of the crosshair. It still turns red on an enemy in range.";
static const wchar_t BOTS_COMMAND[] = L"changeBots";
static const wchar_t SQUAD_COMMAND[] = L"changeSquad";
static const wchar_t SKILL_COMMAND[] = L"changeSkill";
static const wchar_t BOTS_TOOLTIP_KEY[] = L"botsTooltip";
static const wchar_t SQUAD_TOOLTIP_KEY[] = L"squadTooltip";
static const wchar_t SKILL_TOOLTIP_KEY[] = L"skillTooltip";
static const wchar_t BOTS_LABEL[] = L"BOTS";
static const wchar_t SQUAD_LABEL[] = L"SQUAD";
static const wchar_t SKILL_LABEL[] = L"BOT SKILL";
static const wchar_t BLACKOPS_ITEMS[] = L"OFF|FILL|1|2|3";
static const wchar_t VERSUS_ITEMS[] = L"OFF|FILL|1|2|3|4|5|6|7|8|9|10|11|12|13|14|15";
static const wchar_t SKILL_ITEMS[] = L"EASY|NORMAL|HARD|VETERAN";
static const wchar_t BLACKOPS_TOOLTIP[] = L"CHANGE HOW MANY BOTS FILL YOUR SQUAD: OFF, EVERY EMPTY SLOT, OR A NUMBER";
static const wchar_t SQUAD_TOOLTIP[] = L"CHANGE THE SQUAD OF YOUR BOTS: THEIR ROLES, WEAPONS, ARMOUR AND GRENADES";
static const wchar_t VERSUS_TOOLTIP[] = L"CHANGE HOW MANY BOTS JOIN THE MATCH: OFF, THE PLAYER COUNT OF THE INI FILE, OR A NUMBER";
static const wchar_t QUICKMATCH_TOOLTIP[] = L"CHANGE HOW MANY BOTS JOIN IF NO GAME IS FOUND AND YOU HOST: OFF, THE INI FILE'S COUNT, OR A NUMBER";
static const wchar_t SKILL_TOOLTIP[] = L"CHANGE HOW GOOD THE BOTS ARE: HOW FAST THEY REACT, HOW WELL THEY AIM AND HOW HARD THEY HIT";

struct SkillPreset {
	int32_t ReactionMs;
	int32_t WarningShots;
	float FocusAngle;
	float FiringAngle;
	int32_t Skill;
	float DamageScale;
};
static const int SKILL_NORMAL = 1;
static const SkillPreset SKILL_PRESETS[] = {
	{ 550, 3, 18.0f, 10.0f, 30, 0.8f },
	{ 375, 2, 14.0f, 8.0f, 50, 1.0f },
	{ 300, 1, 12.0f, 7.0f, 65, 1.0f },
	{ 220, 1, 10.0f, 6.0f, 80, 1.1f },
};
static const int SKILL_COUNT = sizeof(SKILL_PRESETS) / sizeof(SKILL_PRESETS[0]);
static const char BLACKOPS_MENU_CLASS[] = "FoxMainMenuMovieBlackOps";
static const char QUICKMATCH_MENU_CLASS[] = "FoxMainMenuMovieJoinGame";
static const uint16_t SET_STRING_PARMS = 24;
static const uint16_t SET_NUMBER_PARMS = 16;
static const int32_t PARM_VALUE = 12;

struct Movie {
	const char* Name;
	const wchar_t* File;
	const char* Mark;
	std::vector<uint8_t> Data;
	bool Loaded;
};

static Movie movies[] = {
	{ "blackops_02", L"blackops_02.gfx", PATCH_MARK },
	{ "create_combat_02", L"create_combat_02.gfx", PATCH_MARK },
	{ "join_combat_02", L"join_combat_02.gfx", PATCH_MARK },
	{ "controls_02", L"controls_02.gfx", CONTROLS_PATCH_MARK },
};

static struct {
	UObject* Movie_LoadMovieInfo;
	UObject* BlackOps_InitializeMenu;
	UObject* BlackOps_OnReceiveFSCommand;
	UObject* HostGame_InitializeMenu;
	UObject* HostGame_OnReceiveFSCommand;
	UObject* QuickMatch_InitializeMenu;
	UObject* QuickMatch_OnReceiveFSCommand;
	UObject* Controls_InitializeMenu;
	UObject* Controls_OnReceiveFSCommand;
	UObject* Handler_ProccessToolTip;
	UObject* GFx_SetVariableString;
	UObject* GFx_SetVariableNumber;
} ref;

static struct {
	int32_t Movie_MovieInfo;
	int32_t MovieInfo_RawData;
	int32_t BlackOps_Cmd;
	int32_t BlackOps_Arg;
	int32_t HostGame_Cmd;
	int32_t HostGame_Arg;
	int32_t QuickMatch_Cmd;
	int32_t QuickMatch_Arg;
	int32_t Controls_Cmd;
	int32_t Controls_Arg;
	int32_t ToolTip_Movie;
	int32_t ToolTip_Arg;
} moff;

static bool ready = false;
static int blackops_choice = -1;
static int blackops_squad = -1;
static int versus_choice = -1;
static int versus_skill = -1;
static int public_choice = -1;
static int public_skill = -1;

bool Resolve()
{
	struct { UObject** Target; const char* Path; } objectRefs[] = {
		{ &ref.Movie_LoadMovieInfo, "FoxGame.FoxMovieBase.LoadMovieInfo" },
		{ &ref.BlackOps_InitializeMenu, "FoxGame.FoxMainMenuMovieBlackOps.InitializeMenu" },
		{ &ref.BlackOps_OnReceiveFSCommand, "FoxGame.FoxMainMenuMovieBlackOps.OnReceiveFSCommand" },
		{ &ref.HostGame_InitializeMenu, "FoxGame.FoxMainMenuMovieHostGame.InitializeMenu" },
		{ &ref.HostGame_OnReceiveFSCommand, "FoxGame.FoxMainMenuMovieHostGame.OnReceiveFSCommand" },
		{ &ref.QuickMatch_InitializeMenu, "FoxGame.FoxMainMenuMovieJoinGame.InitializeMenu" },
		{ &ref.QuickMatch_OnReceiveFSCommand, "FoxGame.FoxMainMenuMovieJoinGame.OnReceiveFSCommand" },
		{ &ref.Controls_InitializeMenu, "FoxGame.FoxMainMenuMovieControls.InitializeMenu" },
		{ &ref.Controls_OnReceiveFSCommand, "FoxGame.FoxMainMenuMovieControls.OnReceiveFSCommand" },
		{ &ref.Handler_ProccessToolTip, "FoxGame.FoxMainMenuCmdHandler.ProccessToolTip" },
		{ &ref.GFx_SetVariableString, "GFxUI.GFxMovie.SetVariableString" },
		{ &ref.GFx_SetVariableNumber, "GFxUI.GFxMovie.SetVariableNumber" },
	};
	for (auto& objectRef : objectRefs) {
		*objectRef.Target = UE3::FindObject(objectRef.Path);
		if (!*objectRef.Target) {
			BOTS_LOG(LOG_LEVEL_WARN, "%s not found, the menus get no Bots row.", objectRef.Path);
			return false;
		}
	}
	struct { int32_t* Target; const char* Path; } offsetRefs[] = {
		{ &moff.Movie_MovieInfo, "GFxUI.GFxMovie.MovieInfo" },
		{ &moff.Controls_Cmd, "FoxGame.FoxMainMenuMovieControls.OnReceiveFSCommand.Cmd" },
		{ &moff.Controls_Arg, "FoxGame.FoxMainMenuMovieControls.OnReceiveFSCommand.Arg" },
		{ &moff.MovieInfo_RawData, "GFxUI.GFxMovieInfo.RawData" },
		{ &moff.BlackOps_Cmd, "FoxGame.FoxMainMenuMovieBlackOps.OnReceiveFSCommand.Cmd" },
		{ &moff.BlackOps_Arg, "FoxGame.FoxMainMenuMovieBlackOps.OnReceiveFSCommand.Arg" },
		{ &moff.HostGame_Cmd, "FoxGame.FoxMainMenuMovieHostGame.OnReceiveFSCommand.Cmd" },
		{ &moff.HostGame_Arg, "FoxGame.FoxMainMenuMovieHostGame.OnReceiveFSCommand.Arg" },
		{ &moff.QuickMatch_Cmd, "FoxGame.FoxMainMenuMovieJoinGame.OnReceiveFSCommand.Cmd" },
		{ &moff.QuickMatch_Arg, "FoxGame.FoxMainMenuMovieJoinGame.OnReceiveFSCommand.Arg" },
		{ &moff.ToolTip_Movie, "FoxGame.FoxMainMenuCmdHandler.ProccessToolTip.Movie" },
		{ &moff.ToolTip_Arg, "FoxGame.FoxMainMenuCmdHandler.ProccessToolTip.Arg" },
	};
	for (auto& offsetRef : offsetRefs) {
		*offsetRef.Target = UE3::FindPropertyOffset(offsetRef.Path);
		if (*offsetRef.Target < 0) {
			BOTS_LOG(LOG_LEVEL_WARN, "%s not found, the menus get no Bots row.", offsetRef.Path);
			return false;
		}
	}
	if (UE3::FunctionParmsSize(ref.GFx_SetVariableString) != SET_STRING_PARMS || UE3::FunctionParmsSize(ref.GFx_SetVariableNumber) != SET_NUMBER_PARMS) {
		BOTS_LOG(LOG_LEVEL_WARN, "The GFx SetVariable calls take %u and %u bytes of parameters, the menus get no Bots row."
			, UE3::FunctionParmsSize(ref.GFx_SetVariableString), UE3::FunctionParmsSize(ref.GFx_SetVariableNumber));
		return false;
	}
	ready = true;
	return true;
}

static bool FillString(FString& text, const wchar_t* value)
{
	size_t length = wcslen(value) + 1;
	text.Data = (wchar_t*)UE3::Malloc((uint32_t)(length * sizeof(wchar_t)), 8);
	if (!text.Data) {
		return false;
	}
	memcpy(text.Data, value, length * sizeof(wchar_t));
	text.Num = text.Max = (int32_t)length;
	return true;
}

static void SetString(UObject* movie, const wchar_t* path, const wchar_t* value)
{
	std::vector<uint8_t> parms(SET_STRING_PARMS + 16, 0);
	FString& pathText = Field<FString>(parms.data(), 0);
	FString& valueText = Field<FString>(parms.data(), PARM_VALUE);
	if (FillString(pathText, path) && FillString(valueText, value)) {
		UE3::ProcessNativeEvent(movie, ref.GFx_SetVariableString, parms.data());
	}
	UE3::FreeArray(&pathText);
	UE3::FreeArray(&valueText);
}

void SetMovieText(UObject* movie, const wchar_t* path, const wchar_t* value)
{
	if (ready) {
		SetString(movie, path, value);
	}
}

static void SetNumber(UObject* movie, const wchar_t* path, float value)
{
	std::vector<uint8_t> parms(SET_NUMBER_PARMS + 16, 0);
	FString& pathText = Field<FString>(parms.data(), 0);
	Field<float>(parms.data(), PARM_VALUE) = value;
	if (FillString(pathText, path)) {
		UE3::ProcessNativeEvent(movie, ref.GFx_SetVariableNumber, parms.data());
	}
	UE3::FreeArray(&pathText);
}

static void ShowCrosshairRows(UObject* movie)
{
	static int stamp = 0;
	wchar_t text[64];
	swprintf_s(text, L"%ls %ls", CROSSHAIR_DIMENSIONS_LABEL, Hud::CrosshairDimensionsName(config.Hud.CrosshairDimensions));
	SetString(movie, L"_root.crosshairDimensionsLabelText", text);
	SetNumber(movie, L"_root.crosshairDimensionsChecked", config.Hud.CrosshairDimensions > 0 ? 1.0f : 0.0f);
	SetNumber(movie, L"_root.crosshairDimensionsDisabled", config.Hud.CrosshairDynamic ? 1.0f : 0.0f);
	swprintf_s(text, L"%ls %ls", CROSSHAIR_COLOR_LABEL, Hud::CrosshairColorName(config.Hud.CrosshairColor));
	SetString(movie, L"_root.crosshairColorLabelText", text);
	SetNumber(movie, L"_root.crosshairColorChecked", config.Hud.CrosshairColor > 0 ? 1.0f : 0.0f);
	SetNumber(movie, L"_root.crosshairStamp", (float)++stamp);
}

static int NextChoice(int choice, int count)
{
	return choice >= 0 && choice < count - 1 ? choice + 1 : 0;
}

static bool LoadMovieFile(Movie& movie)
{
	if (movie.Loaded) {
		return !movie.Data.empty();
	}
	movie.Loaded = true;
	std::wstring path = Platform::FilePath(Platform::FILE_UI, movie.File, false);
	if (path.empty()) {
		return false;
	}
	FILE* file = 0;
	if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file) {
		BOTS_LOG(LOG_LEVEL_WARN, "%ls is missing, the menu keeps the game's own movie.", path.c_str());
		return false;
	}
	fseek(file, 0, SEEK_END);
	long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	if (size > 0) {
		movie.Data.resize((size_t)size);
		if (fread(movie.Data.data(), 1, (size_t)size, file) != (size_t)size) {
			movie.Data.clear();
		}
	}
	fclose(file);
	return !movie.Data.empty();
}

static void SwapMovie(UObject* movie_info)
{
	const char* name = UE3::ObjectName(movie_info);
	for (Movie& movie : movies) {
		if (strcmp(movie.Name, name) != 0) {
			continue;
		}
		TArray<uint8_t>& raw = Field<TArray<uint8_t> >(movie_info, moff.MovieInfo_RawData);
		if (raw.Data && raw.Num > 0 && std::search(raw.Data, raw.Data + raw.Num, movie.Mark, movie.Mark + strlen(movie.Mark)) != raw.Data + raw.Num) {
			return;
		}
		if (!LoadMovieFile(movie)) {
			return;
		}
		uint8_t* data = (uint8_t*)UE3::Malloc((uint32_t)movie.Data.size(), 8);
		if (!data) {
			return;
		}
		memcpy(data, movie.Data.data(), movie.Data.size());
		UE3::FreeArray(&raw);
		raw.Data = data;
		raw.Num = raw.Max = (int32_t)movie.Data.size();
		BOTS_LOG(LOG_LEVEL_INFO, "The %s menu movie now has a Bots row (%u bytes).", name, (uint32_t)movie.Data.size());
		return;
	}
}

static void SquadItems(wchar_t* items, size_t size)
{
	size_t at = 0;
	for (int i = 0; i < SquadCount() && at + 2 < size; i++) {
		if (i > 0) {
			items[at++] = L'|';
		}
		for (const char* c = SquadName(i); *c && at + 2 < size; c++) {
			if (c != SquadName(i) && *c >= 'A' && *c <= 'Z') {
				items[at++] = L' ';
			}
			items[at++] = (wchar_t)toupper((unsigned char)*c);
		}
	}
	items[at] = 0;
}

enum MenuKind { MENU_BLACKOPS, MENU_HOSTGAME, MENU_QUICKMATCH };

static void FillRow(UObject* movie, MenuKind kind)
{
	bool blackops = kind == MENU_BLACKOPS;
	bool quickmatch = kind == MENU_QUICKMATCH;
	int& choice = blackops ? blackops_choice : quickmatch ? public_choice : versus_choice;
	if (choice < 0) {
		choice = CHOICE_OFF;
	}
	SetString(movie, L"_root.botsLabelText", BOTS_LABEL);
	SetString(movie, L"_root.botsItems", blackops ? BLACKOPS_ITEMS : VERSUS_ITEMS);
	SetNumber(movie, L"_root.botsIndex", (float)choice);
	if (!blackops) {
		int& skill = quickmatch ? public_skill : versus_skill;
		if (skill < 0) {
			skill = SKILL_NORMAL;
		}
		SetString(movie, L"_root.skillLabelText", SKILL_LABEL);
		SetString(movie, L"_root.skillItems", SKILL_ITEMS);
		SetNumber(movie, L"_root.skillIndex", (float)skill);
		return;
	}
	if (blackops_squad < 0) {
		blackops_squad = 0;
	}
	wchar_t items[256];
	SquadItems(items, sizeof(items) / sizeof(items[0]));
	SetString(movie, L"_root.squadLabelText", SQUAD_LABEL);
	SetString(movie, L"_root.squadItems", items);
	SetNumber(movie, L"_root.squadIndex", (float)blackops_squad);
}

static bool IsCommand(FFrame* stack, int32_t cmd_offset, int32_t arg_offset, const wchar_t* command, int& choice)
{
	const FString& cmd = Field<FString>(stack->Locals, cmd_offset);
	const FString& arg = Field<FString>(stack->Locals, arg_offset);
	if (!cmd.Data || wcscmp(cmd.Data, command) != 0) {
		return false;
	}
	choice = arg.Data ? _wtoi(arg.Data) : 0;
	return true;
}

bool OnScriptCall(UObject* self, FFrame* stack)
{
	if (!ready) {
		return true;
	}
	UObject* node = stack->Node;
	if (node == ref.BlackOps_OnReceiveFSCommand) {
		if (IsCommand(stack, moff.BlackOps_Cmd, moff.BlackOps_Arg, BOTS_COMMAND, blackops_choice)) {
			BOTS_LOG(LOG_LEVEL_INFO, "The host set the Black Ops bots row to %d.", blackops_choice);
		}
		else if (IsCommand(stack, moff.BlackOps_Cmd, moff.BlackOps_Arg, SQUAD_COMMAND, blackops_squad)) {
			BOTS_LOG(LOG_LEVEL_INFO, "The host set the Black Ops squad row to %d (%s).", blackops_squad, SquadName(blackops_squad));
		}
		return true;
	}
	if (node == ref.HostGame_OnReceiveFSCommand) {
		if (IsCommand(stack, moff.HostGame_Cmd, moff.HostGame_Arg, BOTS_COMMAND, versus_choice)) {
			BOTS_LOG(LOG_LEVEL_INFO, "The host set the private match bots row to %d.", versus_choice);
		}
		else if (IsCommand(stack, moff.HostGame_Cmd, moff.HostGame_Arg, SKILL_COMMAND, versus_skill)) {
			BOTS_LOG(LOG_LEVEL_INFO, "The host set the private match skill row to %d.", versus_skill);
		}
		return true;
	}
	if (node == ref.QuickMatch_OnReceiveFSCommand) {
		if (IsCommand(stack, moff.QuickMatch_Cmd, moff.QuickMatch_Arg, BOTS_COMMAND, public_choice)) {
			BOTS_LOG(LOG_LEVEL_INFO, "The player set the Quick Match bots row to %d.", public_choice);
		}
		else if (IsCommand(stack, moff.QuickMatch_Cmd, moff.QuickMatch_Arg, SKILL_COMMAND, public_skill)) {
			BOTS_LOG(LOG_LEVEL_INFO, "The player set the Quick Match skill row to %d.", public_skill);
		}
		return true;
	}
	if (node == ref.Controls_OnReceiveFSCommand) {
		const FString& command = Field<FString>(stack->Locals, moff.Controls_Cmd);
		const FString& arg = Field<FString>(stack->Locals, moff.Controls_Arg);
		if (command.Data && wcscmp(command.Data, CROSSHAIR_COMMAND) == 0) {
			config.Hud.CrosshairDynamic = arg.Data && wcscmp(arg.Data, L"true") == 0;
			SaveHudConfig(config.Hud);
			ShowCrosshairRows(self);
			BOTS_LOG(LOG_LEVEL_INFO, "The player set the crosshair to %s.", config.Hud.CrosshairDynamic ? "dynamic" : "static");
			return false;
		}
		if (command.Data && wcscmp(command.Data, CROSSHAIR_DIMENSIONS_COMMAND) == 0) {
			config.Hud.CrosshairDimensions = NextChoice(config.Hud.CrosshairDimensions, Hud::CrosshairDimensionsCount());
			SaveHudConfig(config.Hud);
			ShowCrosshairRows(self);
			BOTS_LOG(LOG_LEVEL_INFO, "The player set the crosshair dimensions to choice %d.", config.Hud.CrosshairDimensions);
			return false;
		}
		if (command.Data && wcscmp(command.Data, CROSSHAIR_COLOR_COMMAND) == 0) {
			config.Hud.CrosshairColor = NextChoice(config.Hud.CrosshairColor, Hud::CrosshairColorCount());
			SaveHudConfig(config.Hud);
			ShowCrosshairRows(self);
			BOTS_LOG(LOG_LEVEL_INFO, "The player set the crosshair color to choice %d.", config.Hud.CrosshairColor);
			return false;
		}
		return true;
	}
	if (node == ref.Handler_ProccessToolTip) {
		const FString& arg = Field<FString>(stack->Locals, moff.ToolTip_Arg);
		UObject* movie = Field<UObject*>(stack->Locals, moff.ToolTip_Movie);
		if (!movie || !arg.Data) {
			return true;
		}
		const wchar_t* text = 0;
		if (wcscmp(arg.Data, BOTS_TOOLTIP_KEY) == 0) {
			const char* menuClass = UE3::ObjectName(UE3::ObjectClass(movie));
			text = strcmp(menuClass, BLACKOPS_MENU_CLASS) == 0 ? BLACKOPS_TOOLTIP : strcmp(menuClass, QUICKMATCH_MENU_CLASS) == 0 ? QUICKMATCH_TOOLTIP : VERSUS_TOOLTIP;
		}
		else if (wcscmp(arg.Data, SQUAD_TOOLTIP_KEY) == 0) {
			text = SQUAD_TOOLTIP;
		}
		else if (wcscmp(arg.Data, SKILL_TOOLTIP_KEY) == 0) {
			text = SKILL_TOOLTIP;
		}
		else if (wcscmp(arg.Data, CROSSHAIR_TOOLTIP_KEY) == 0) {
			text = CROSSHAIR_TOOLTIP;
		}
		else if (wcscmp(arg.Data, CROSSHAIR_DIMENSIONS_TOOLTIP_KEY) == 0) {
			text = CROSSHAIR_DIMENSIONS_TOOLTIP;
		}
		else if (wcscmp(arg.Data, CROSSHAIR_COLOR_TOOLTIP_KEY) == 0) {
			text = CROSSHAIR_COLOR_TOOLTIP;
		}
		if (!text) {
			return true;
		}
		SetString(movie, L"_root.p_tooltip.l_tooltip.htmlText", text);
		return false;
	}
	return true;
}

void OnScriptReturn(UObject* self, FFrame* stack)
{
	if (!ready) {
		return;
	}
	UObject* node = stack->Node;
	if (node == ref.Movie_LoadMovieInfo) {
		UObject* movieInfo = Field<UObject*>(self, moff.Movie_MovieInfo);
		if (movieInfo) {
			SwapMovie(movieInfo);
		}
	}
	else if (node == ref.BlackOps_InitializeMenu) {
		FillRow(self, MENU_BLACKOPS);
	}
	else if (node == ref.HostGame_InitializeMenu) {
		FillRow(self, MENU_HOSTGAME);
	}
	else if (node == ref.QuickMatch_InitializeMenu) {
		FillRow(self, MENU_QUICKMATCH);
	}
	else if (node == ref.Controls_InitializeMenu) {
		SetNumber(self, L"_root.crosshairChecked", config.Hud.CrosshairDynamic ? 1.0f : 0.0f);
		SetString(self, L"_root.crosshairLabelText", CROSSHAIR_LABEL);
		ShowCrosshairRows(self);
	}
}

void ApplyChoice(Config& config, bool public_match)
{
	if (blackops_choice >= 0) {
		config.Enabled = blackops_choice != CHOICE_OFF;
		if (blackops_choice > CHOICE_FILL) {
			config.Count = (std::min)(SLOT_COUNT, blackops_choice - CHOICE_FILL);
		}
	}
	int choice = public_match ? public_choice : versus_choice;
	int skill = public_match ? public_skill : versus_skill;
	if (choice >= 0) {
		config.Versus.Enabled = choice != CHOICE_OFF;
		config.Versus.BotCount = choice > CHOICE_FILL ? choice - CHOICE_FILL : -1;
	}
	const SkillPreset& preset = SKILL_PRESETS[skill >= 0 && skill < SKILL_COUNT ? skill : SKILL_NORMAL];
	config.Versus.ReactionMs = preset.ReactionMs;
	config.Versus.WarningShots = preset.WarningShots;
	config.Versus.FocusAngle = preset.FocusAngle;
	config.Versus.FiringAngle = preset.FiringAngle;
	config.Versus.Skill = preset.Skill;
	config.Versus.DamageScale = preset.DamageScale;
	if (blackops_choice >= 0 || choice >= 0) {
		BOTS_LOG(LOG_LEVEL_INFO, "Menu: Black Ops bots row %d, squad %s; %s match bots row %d, skill row %d: reaction %d ms, warning shots %d, focus %.0f to %.0f degrees, skill %d, damage %.2f."
			, blackops_choice, config.Squad, public_match ? "public" : "private", choice, skill, config.Versus.ReactionMs, config.Versus.WarningShots
			, config.Versus.FocusAngle, config.Versus.FiringAngle, config.Versus.Skill, config.Versus.DamageScale);
	}
}

const char* ChosenSquad()
{
	return blackops_squad >= 0 && blackops_squad < SquadCount() ? SquadName(blackops_squad) : 0;
}

}
}
