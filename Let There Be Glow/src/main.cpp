// Let There Be Glow - SKSE plugin
// Copyright (C) 2026 izzydoingit
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see the
// GNU General Public License in LICENSE.txt for details.
//
// What it does, once, when the game has finished loading its plugins. It is the in-memory
// counterpart of the Glowified Patcher xEdit script and follows the same rules, so a player picks
// one of the two and never both:
//   1. a magic effect whose casting art Let There Be Glow or CS Light lights loses the game's own
//      casting light, so the hand does not carry two lights;
//   2. the same for projectiles, explosions and hazards whose model is lit - except cone and flame
//      projectiles, which keep their light, and poison sprays, which lose it whether lit or not;
//   3. the Dragonborn poison rune gets the casting art its lit hand needs;
//   4. with the Spray Lights option installed, each spray projectile gets a private copy of its own
//      light, stretched to cover the spray and coloured from the installer's markers;
//   5. an enchantment carrying two or more lit shaders keeps the light of its first one only.
// If GlowifiedSkyrim.esp (the patcher's output) is active, nothing is changed at all.

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace
{
	namespace fs = std::filesystem;

	// ------------------------------------------------------------------ rules (the patcher's defaults)
	constexpr std::string_view kPatchPlugin = "GlowifiedSkyrim.esp";
	constexpr std::string_view kOurFolder = "Let There Be Glow";
	constexpr std::string_view kCSFolder = "CS Light";
	constexpr std::size_t      kMaxCounterEffects = 512;
	constexpr std::string_view kPoisonRuneEffect = "DLC2PoisonRuneFFLocation";
	constexpr std::string_view kPoisonRuneArtModel = "magic\\poisonrunefxhand01.nif";
	const std::vector<std::string> kSkipPrefixes{ "trap", "hazard", "voice", "ench", "test" };
	const std::set<std::string>    kForceNullProjectiles{ "tvr_geist_projectile" };
	const std::set<std::string>    kSprayNotSpells{ "trapspotlightprojectile", "sum_any_projectile_defaultcloakprojectile" };
	constexpr std::uint32_t        kSprayFlags = 0x2001;  // Dynamic | Portal-strict

	// ------------------------------------------------------------------ small text helpers
	std::string Lower(std::string_view a_text)
	{
		std::string out(a_text);
		for (auto& c : out) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return out;
	}

	std::string NormalPath(std::string_view a_path)
	{
		std::string out = Lower(a_path);
		std::replace(out.begin(), out.end(), '/', '\\');
		return out;
	}

	bool Contains(std::string_view a_text, std::string_view a_part)
	{
		return a_text.find(a_part) != std::string_view::npos;
	}

	std::string Trim(std::string_view a_text)
	{
		const auto first = a_text.find_first_not_of(" \t\r\n");
		if (first == std::string_view::npos) {
			return {};
		}
		const auto last = a_text.find_last_not_of(" \t\r\n");
		return std::string(a_text.substr(first, last - first + 1));
	}

	bool ParseInt(std::string_view a_text, int& a_out)
	{
		const auto t = Trim(a_text);
		const auto r = std::from_chars(t.data(), t.data() + t.size(), a_out);
		return r.ec == std::errc() && r.ptr == t.data() + t.size();
	}

	bool ParseFloat(std::string_view a_text, float& a_out)
	{
		const auto t = Trim(a_text);
		const auto r = std::from_chars(t.data(), t.data() + t.size(), a_out);
		return r.ec == std::errc() && r.ptr == t.data() + t.size();
	}

	// ------------------------------------------------------------------ editor IDs
	// The game throws most editor IDs away while it loads. The rules above are written against them,
	// so every form type these passes read has its editor ID recorded as it arrives.
	std::unordered_map<const RE::TESForm*, std::string> gEditorIDs;
	RE::BSSpinLock                                      gEditorIDLock;

	template <class T>
	struct EditorIDHook
	{
		static bool thunk(RE::TESForm* a_this, const char* a_id)
		{
			if (a_this && a_id && *a_id) {
				RE::BSSpinLockGuard guard(gEditorIDLock);
				gEditorIDs[a_this] = a_id;
			}
			return func(a_this, a_id);
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static void                                    Install()
		{
			REL::Relocation<std::uintptr_t> vtbl{ T::VTABLE[0] };
			func = vtbl.write_vfunc(0x33, thunk);
		}
	};

	std::string EditorID(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return {};
		}
		{
			RE::BSSpinLockGuard guard(gEditorIDLock);
			if (const auto it = gEditorIDs.find(a_form); it != gEditorIDs.end()) {
				return it->second;
			}
		}
		const char* own = a_form->GetFormEditorID();
		return own ? std::string(own) : std::string();
	}

	std::string Label(const RE::TESForm* a_form)
	{
		const auto id = EditorID(a_form);
		const auto* file = a_form ? a_form->GetFile(0) : nullptr;
		return std::format("{} | {:08X} | {}", id.empty() ? "(no editor ID)" : id, a_form ? a_form->GetFormID() : 0,
			file ? file->GetFilename() : "(created)");
	}

	// ------------------------------------------------------------------ what the configs light
	struct Coverage
	{
		std::set<std::string> models;   // lowercased .nif paths
		std::set<std::string> shaders;  // lowercased tokens from "formIDs" arrays
		std::size_t           files{ 0 };
	};

	fs::path LightPlacerDir(std::string_view a_folder)
	{
		return fs::current_path() / "Data" / "LightPlacer" / fs::path(std::string(a_folder));
	}

	// A small reader for the one shape these configs have. It never fails a file: a string it cannot
	// place is simply not counted, which is the patcher's behaviour too.
	void ReadConfig(const fs::path& a_file, Coverage& a_cov)
	{
		std::ifstream in(a_file, std::ios::binary);
		if (!in) {
			return;
		}
		std::stringstream buf;
		buf << in.rdbuf();
		const std::string text = buf.str();
		++a_cov.files;

		std::string lastString;
		bool        lastWasKey = false;
		int         formIDsDepth = -1;  // bracket depth at which a formIDs array opened
		int         depth = 0;
		bool        pendingFormIDs = false;

		for (std::size_t i = 0; i < text.size(); ++i) {
			const char c = text[i];
			if (c == '"') {
				std::string s;
				for (++i; i < text.size() && text[i] != '"'; ++i) {
					if (text[i] == '\\' && i + 1 < text.size()) {
						++i;
						s.push_back(text[i] == 'n' ? '\n' : text[i] == 't' ? '\t' : text[i]);
					} else {
						s.push_back(text[i]);
					}
				}
				std::size_t j = i + 1;
				while (j < text.size() && std::isspace(static_cast<unsigned char>(text[j]))) {
					++j;
				}
				const bool isKey = j < text.size() && text[j] == ':';
				if (isKey) {
					pendingFormIDs = Lower(s) == "formids";
				} else {
					const auto low = NormalPath(s);
					if (low.size() > 4 && low.ends_with(".nif")) {
						a_cov.models.insert(low);
					} else if (formIDsDepth >= 0 && low.size() > 2) {
						a_cov.shaders.insert(low);
					}
				}
				lastString = std::move(s);
				lastWasKey = isKey;
				continue;
			}
			if (c == '[') {
				++depth;
				if (pendingFormIDs && formIDsDepth < 0) {
					formIDsDepth = depth;
				}
				pendingFormIDs = false;
			} else if (c == ']') {
				if (formIDsDepth == depth) {
					formIDsDepth = -1;
				}
				--depth;
			} else if (c == '{' || c == ',') {
				if (!lastWasKey) {
					pendingFormIDs = false;
				}
			}
		}
	}

	Coverage ReadCoverage()
	{
		Coverage cov;
		for (const auto folder : { kOurFolder, kCSFolder }) {
			std::error_code ec;
			const auto      dir = LightPlacerDir(folder);
			if (!fs::is_directory(dir, ec)) {
				continue;
			}
			for (const auto& entry : fs::directory_iterator(dir, ec)) {
				if (entry.is_regular_file() && Lower(entry.path().extension().string()) == ".json") {
					ReadConfig(entry.path(), cov);
				}
			}
		}
		return cov;
	}

	// ------------------------------------------------------------------ the installer's spray markers
	struct Rgb
	{
		int r{ 0 }, g{ 0 }, b{ 0 };
	};

	struct SprayChoice
	{
		bool  on{ false };
		int   radiusAbs{ 1200 };
		int   radiusPc{ 216 };
		float fade{ 1.7f };
		float frostFade{ 0.8f };
		float falloff{ 2.0f };
		bool  frostSet{ false }, shockSet{ false }, fireSet{ false };
		Rgb   frost, shock, fireDelta;
		std::string found;
	};

	std::map<std::string, std::string> ReadMarker(const fs::path& a_file)
	{
		std::map<std::string, std::string> out;
		std::ifstream                      in(a_file);
		std::string                        line;
		while (std::getline(in, line)) {
			const auto t = Trim(line);
			if (t.empty() || t[0] == '#') {
				continue;
			}
			const auto eq = t.find('=');
			if (eq == std::string::npos || eq == 0) {
				continue;
			}
			out[Lower(Trim(t.substr(0, eq)))] = Trim(t.substr(eq + 1));
		}
		return out;
	}

	bool ParseRgb(std::string_view a_text, Rgb& a_out)
	{
		const auto c1 = a_text.find(',');
		const auto c2 = c1 == std::string_view::npos ? c1 : a_text.find(',', c1 + 1);
		if (c1 == std::string_view::npos || c2 == std::string_view::npos) {
			return false;
		}
		return ParseInt(a_text.substr(0, c1), a_out.r) && ParseInt(a_text.substr(c1 + 1, c2 - c1 - 1), a_out.g) &&
		       ParseInt(a_text.substr(c2 + 1), a_out.b);
	}

	SprayChoice ReadSprayChoice()
	{
		SprayChoice                                   sc;
		std::vector<std::pair<std::string, fs::path>> markers;
		for (const auto folder : { kOurFolder, kCSFolder }) {
			std::error_code ec;
			const auto      dir = LightPlacerDir(folder);
			if (!fs::is_directory(dir, ec)) {
				continue;
			}
			for (const auto& entry : fs::directory_iterator(dir, ec)) {
				if (entry.is_regular_file() && Lower(entry.path().extension().string()) == ".txt") {
					markers.emplace_back(Lower(entry.path().filename().string()), entry.path());
				}
			}
		}
		// the base marker first, so every axis marker wins over it whatever order the folder lists them
		for (const auto& [name, path] : markers) {
			if (name != "glowified sprays.txt") {
				continue;
			}
			sc.on = true;
			sc.found += "on ";
			auto kv = ReadMarker(path);
			int  n = 0;
			float f = 0.0f;
			if (ParseInt(kv["radiuspc"], n) && n > 0) sc.radiusPc = n;
			if (ParseInt(kv["radius"], n) && n >= 0) sc.radiusAbs = n;
			if (ParseFloat(kv["fade"], f)) sc.fade = f;
			if (ParseFloat(kv["frostfade"], f)) sc.frostFade = f;
			if (ParseFloat(kv["falloff"], f)) sc.falloff = f;
		}
		for (const auto& [name, path] : markers) {
			auto kv = ReadMarker(path);
			float f = 0.0f;
			if (name == "glowified sprays - reduced.txt") {
				sc.found += "reduced ";
				if (ParseFloat(kv["fade"], f)) sc.fade = f;
				if (ParseFloat(kv["frostfade"], f)) sc.frostFade = f;
			} else if (name == "glowified sprays - frost.txt" && ParseRgb(kv["frost"], sc.frost)) {
				sc.frostSet = true;
				sc.found += "frost ";
			} else if (name == "glowified sprays - shock.txt" && ParseRgb(kv["shock"], sc.shock)) {
				sc.shockSet = true;
				sc.found += "shock ";
			} else if (name == "glowified sprays - fire.txt" && ParseRgb(kv["firedelta"], sc.fireDelta)) {
				sc.fireSet = true;
				sc.found += "fire ";
			}
		}
		return sc;
	}

	// ------------------------------------------------------------------ making copies in memory
	template <class T>
	T* NewForm()
	{
		// Create() is not const, so the factory pointer must not be either
		auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<T>();
		return factory ? factory->Create() : nullptr;
	}

	RE::TESObjectLIGH* CopyLight(const RE::TESObjectLIGH* a_src)
	{
		auto* out = NewForm<RE::TESObjectLIGH>();
		if (!out) {
			return nullptr;
		}
		out->data = a_src->data;
		out->fade = a_src->fade;
		out->emittanceColor = a_src->emittanceColor;
		out->lensFlare = a_src->lensFlare;
		out->sound = a_src->sound;
		out->SetModel(a_src->GetModel());
		return out;
	}

	RE::TESEffectShader* CopyShader(const RE::TESEffectShader* a_src)
	{
		auto* out = NewForm<RE::TESEffectShader>();
		if (!out) {
			return nullptr;
		}
		out->data = a_src->data;
		out->fillTexture.textureName = a_src->fillTexture.textureName;
		out->particleShaderTexture.textureName = a_src->particleShaderTexture.textureName;
		out->holesTexture.textureName = a_src->holesTexture.textureName;
		out->membranePaletteTexture.textureName = a_src->membranePaletteTexture.textureName;
		out->particlePaletteTexture.textureName = a_src->particlePaletteTexture.textureName;
		return out;
	}

	RE::EffectSetting* CopyEffect(RE::EffectSetting* a_src)
	{
		auto* out = NewForm<RE::EffectSetting>();
		if (!out) {
			return nullptr;
		}
		out->Copy(a_src);
		return out;
	}

	// ------------------------------------------------------------------ pass 1: casting lights
	bool SkippedPrefix(const std::string& a_editorID)
	{
		auto low = Lower(a_editorID);
		if (low.size() > 4 && low.starts_with("dlc")) {
			low = low.substr(4);
		}
		return std::any_of(kSkipPrefixes.begin(), kSkipPrefixes.end(), [&](const std::string& p) { return low.starts_with(p); });
	}

	std::size_t CounterEffectCount(RE::EffectSetting* a_effect)
	{
		std::size_t listed = 0;
		for ([[maybe_unused]] auto* e : a_effect->counterEffects) {
			++listed;
		}
		const std::size_t declared = static_cast<std::uint16_t>(a_effect->data.numCounterEffects);
		// (std::max) in brackets: the Windows headers define a max macro
		return (std::max)(listed, declared);
	}

	void CastingLights(const Coverage& a_cov)
	{
		std::size_t scanned = 0, constant = 0, archetype = 0, prefix = 0, clean = 0, bloated = 0, nulled = 0;
		for (auto* effect : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::EffectSetting>()) {
			if (!effect || !effect->data.castingArt) {
				continue;
			}
			const auto model = NormalPath(effect->data.castingArt->GetModel() ? effect->data.castingArt->GetModel() : "");
			if (model.empty() || !a_cov.models.contains(model)) {
				continue;
			}
			++scanned;
			if (effect->data.castingType == RE::MagicSystem::CastingType::kConstantEffect) {
				++constant;
				continue;
			}
			if (effect->data.archetype == RE::EffectArchetypes::ArchetypeID::kLight) {
				++archetype;
				continue;
			}
			if (SkippedPrefix(EditorID(effect))) {
				++prefix;
				continue;
			}
			if (!effect->data.light) {
				++clean;
				SKSE::log::info("[CAST-CLEAN] {} | {}", Label(effect), model);
				continue;
			}
			if (CounterEffectCount(effect) > kMaxCounterEffects) {
				++bloated;
				continue;
			}
			effect->data.light = nullptr;
			++nulled;
			SKSE::log::info("[CAST-NULLED] {} | {}", Label(effect), model);
		}
		SKSE::log::info("casting lights: {} lit effects seen; {} nulled, {} already had none, skipped: {} constant effect, "
						"{} light archetype, {} editor ID prefix, {} oversized",
			scanned, nulled, clean, constant, archetype, prefix, bloated);
	}

	// ------------------------------------------------------------------ pass 2: projectiles, explosions, hazards
	bool IsPoisonSpray(const std::string& a_editorID, const std::string& a_model)
	{
		const auto id = Lower(a_editorID);
		return Contains(a_model, "spray") && (Contains(id, "poison") || Contains(id, "poision"));
	}

	template <class T>
	void EffectLights(const Coverage& a_cov, std::string_view a_kind)
	{
		std::size_t scanned = 0, clean = 0, nulled = 0, coneKept = 0, poison = 0, forced = 0;
		for (auto* form : RE::TESDataHandler::GetSingleton()->GetFormArray<T>()) {
			if (!form) {
				continue;
			}
			const auto model = NormalPath(form->GetModel() ? form->GetModel() : "");
			if (model.empty()) {
				continue;
			}
			const auto id = EditorID(form);
			const bool force = kForceNullProjectiles.contains(Lower(id));
			const bool poisonSpray = IsPoisonSpray(id, model);
			if (!a_cov.models.contains(model) && !force && !poisonSpray) {
				continue;
			}
			if (force) {
				++forced;
				SKSE::log::info("[FX-FORCE] {} {} | {}", a_kind, Label(form), model);
			}
			// decided at compile time: an explosion or a hazard has no projectile type to read
			if constexpr (std::is_same_v<T, RE::BGSProjectile>) {
				if (!poisonSpray && form->data.types.any(RE::BGSProjectileData::Type::kFlamethrower, RE::BGSProjectileData::Type::kCone)) {
					++coneKept;
					continue;
				}
			}
			if (poisonSpray) {
				++poison;
				SKSE::log::info("[FX-POISON-SPRAY] {} {} | {}", a_kind, Label(form), model);
			}
			++scanned;
			if (!form->data.light) {
				++clean;
				SKSE::log::info("[FX-CLEAN] {} {} | {}", a_kind, Label(form), model);
				continue;
			}
			form->data.light = nullptr;
			++nulled;
			SKSE::log::info("[FX-NULLED] {} {} | {}", a_kind, Label(form), model);
		}
		SKSE::log::info("{} lights: {} lit or named seen; {} nulled, {} already had none, {} cone/flame kept on purpose, "
						"{} poison sprays, {} named",
			a_kind, scanned, nulled, clean, coneKept, poison, forced);
	}

	// ------------------------------------------------------------------ pass 3: the poison rune's casting art
	void PoisonRuneArt()
	{
		RE::EffectSetting* rune = nullptr;
		for (auto* effect : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::EffectSetting>()) {
			if (effect && EditorID(effect) == kPoisonRuneEffect) {
				rune = effect;
				break;
			}
		}
		if (!rune) {
			SKSE::log::info("poison rune: {} is not in this load order; nothing to do", kPoisonRuneEffect);
			return;
		}
		auto* art = NewForm<RE::BGSArtObject>();
		if (!art) {
			SKSE::log::warn("poison rune: could not create an art object; it keeps what it had");
			return;
		}
		art->SetModel(kPoisonRuneArtModel.data());
		art->data.artType = RE::BGSArtObject::ArtType::kMagicCastingArt;
		rune->data.castingArt = art;
		rune->data.light = nullptr;
		SKSE::log::info("poison rune: {} now plays {} with no casting light", Label(rune), kPoisonRuneArtModel);
	}

	// ------------------------------------------------------------------ pass 4: spray lights
	std::string SprayFamily(const std::string& a_editorID)
	{
		const auto id = Lower(a_editorID);
		if (Contains(id, "frost") || Contains(id, "ice")) return "frost";
		if (Contains(id, "flame") || Contains(id, "fire")) return "fire";
		if (Contains(id, "shock") || Contains(id, "lightning")) return "shock";
		return {};
	}

	std::uint8_t Clamp255(int a_value)
	{
		return static_cast<std::uint8_t>(std::clamp(a_value, 0, 255));
	}

	void SprayLights()
	{
		const auto sc = ReadSprayChoice();
		if (!sc.on) {
			SKSE::log::info("spray lights: off (no Glowified Sprays.txt installed); no spray light is touched");
			return;
		}
		SKSE::log::info("spray lights: installer says {}| radius {} | fade {} | frost fade {} | falloff {}", sc.found,
			sc.radiusAbs, sc.fade, sc.frostFade, sc.falloff);
		std::size_t seen = 0, raised = 0, noLight = 0, poison = 0, named = 0, failed = 0;
		for (auto* proj : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::BGSProjectile>()) {
			if (!proj) {
				continue;
			}
			const auto model = NormalPath(proj->GetModel() ? proj->GetModel() : "");
			if (!Contains(model, "spray")) {
				continue;
			}
			++seen;
			const auto id = EditorID(proj);
			const auto low = Lower(id);
			if (Contains(low, "poison")) {
				++poison;
				proj->data.light = nullptr;
				continue;
			}
			if (kSprayNotSpells.contains(low)) {
				++named;
				continue;
			}
			auto* bulb = proj->data.light;
			if (!bulb) {
				++noLight;
				continue;
			}
			const int range = static_cast<int>(std::lround(proj->data.range));
			if (range <= 0) {
				++failed;
				SKSE::log::warn("[SPRAY-FAILED] {} | could not read its range", Label(proj));
				continue;
			}
			const int radius = sc.radiusAbs > 0 ? sc.radiusAbs : static_cast<int>(std::lround(range * sc.radiusPc / 100.0));
			const auto family = SprayFamily(id);
			auto*      copy = CopyLight(bulb);
			if (!copy) {
				++failed;
				SKSE::log::warn("[SPRAY-FAILED] {} | could not copy its light", Label(proj));
				continue;
			}
			copy->data.radius = static_cast<std::uint32_t>(radius);
			copy->fade = family == "frost" ? sc.frostFade : sc.fade;
			copy->data.flags = static_cast<RE::TES_LIGHT_FLAGS>(kSprayFlags);
			copy->data.fallofExponent = sc.falloff;
			if (family == "frost" && sc.frostSet) {
				copy->data.color.red = Clamp255(sc.frost.r);
				copy->data.color.green = Clamp255(sc.frost.g);
				copy->data.color.blue = Clamp255(sc.frost.b);
			} else if (family == "shock" && sc.shockSet) {
				copy->data.color.red = Clamp255(sc.shock.r);
				copy->data.color.green = Clamp255(sc.shock.g);
				copy->data.color.blue = Clamp255(sc.shock.b);
			} else if (family == "fire" && sc.fireSet) {
				copy->data.color.red = Clamp255(copy->data.color.red + sc.fireDelta.r);
				copy->data.color.green = Clamp255(copy->data.color.green + sc.fireDelta.g);
				copy->data.color.blue = Clamp255(copy->data.color.blue + sc.fireDelta.b);
			}
			proj->data.light = copy;
			if (proj->data.light != copy || copy->data.radius != static_cast<std::uint32_t>(radius)) {
				++failed;
				SKSE::log::warn("[SPRAY-FAILED] {} | the new light did not take", Label(proj));
				continue;
			}
			++raised;
			SKSE::log::info("[SPRAY-RAISED] {} | range {} | radius {} | from {} | family {} | colour {},{},{}", Label(proj), range,
				radius, EditorID(bulb), family, copy->data.color.red, copy->data.color.green, copy->data.color.blue);
		}
		SKSE::log::info("spray lights: {} spray projectiles seen; {} raised, {} with no light of their own, {} poison left dark, "
						"{} named as not spray spells, {} failed",
			seen, raised, noLight, poison, named, failed);
	}

	// ------------------------------------------------------------------ pass 5: enchantments with two lit shaders
	struct Swap
	{
		RE::Effect*        effect;
		RE::EffectSetting* original;
		RE::EffectSetting* quiet;
	};
	std::vector<Swap> gSwaps;

	std::string EnchantShaderID(const RE::EffectSetting* a_effect)
	{
		return a_effect && a_effect->data.enchantShader ? Lower(EditorID(a_effect->data.enchantShader)) : std::string();
	}

	void DoubledEnchantments(const Coverage& a_cov)
	{
		std::unordered_map<RE::EffectSetting*, RE::EffectSetting*>     quietEffects;
		std::unordered_map<RE::TESEffectShader*, RE::TESEffectShader*> quietShaders;
		std::size_t scanned = 0, doubled = 0, fixed = 0, dropped = 0, failed = 0;

		for (auto* ench : RE::TESDataHandler::GetSingleton()->GetFormArray<RE::EnchantmentItem>()) {
			if (!ench) {
				continue;
			}
			++scanned;
			std::size_t claimed = 0;
			std::string first;
			for (auto* eff : ench->effects) {
				const auto id = eff ? EnchantShaderID(eff->baseEffect) : std::string();
				if (!id.empty() && a_cov.shaders.contains(id)) {
					if (first.empty()) first = id;
					++claimed;
				}
			}
			if (claimed < 2) {
				continue;
			}
			++doubled;
			bool        keptOne = false;
			std::size_t droppedHere = 0;
			for (std::size_t i = 0; i < ench->effects.size(); ++i) {
				auto* eff = ench->effects[i];
				const auto id = eff ? EnchantShaderID(eff->baseEffect) : std::string();
				if (id.empty() || !a_cov.shaders.contains(id)) {
					continue;
				}
				if (id == first && !keptOne) {
					keptOne = true;
					continue;
				}
				auto* original = eff->baseEffect;
				auto& quiet = quietEffects[original];
				if (!quiet) {
					auto*& shader = quietShaders[original->data.enchantShader];
					if (!shader) {
						shader = CopyShader(original->data.enchantShader);
					}
					quiet = shader ? CopyEffect(original) : nullptr;
					if (quiet) {
						quiet->data.enchantShader = shader;
					}
				}
				if (!quiet || quiet->data.enchantShader == original->data.enchantShader) {
					++failed;
					SKSE::log::warn("[ENCH-FAILED] {} | effect {} | no quiet copy of {}", Label(ench), i, Label(original));
					continue;
				}
				eff->baseEffect = quiet;
				gSwaps.push_back({ eff, original, quiet });
				++dropped;
				++droppedHere;
				SKSE::log::info("[ENCH-DROPPED] {} | effect {} | {} | {} now plays an unlit copy of its shader", Label(ench), i, id,
					EditorID(original));
			}
			if (droppedHere) {
				++fixed;
				SKSE::log::info("[ENCH-KEPT] {} | {} | {} other light(s) removed", Label(ench), first, droppedHere);
			}
		}
		SKSE::log::info("enchantments: {} scanned, {} carrying two or more lit shaders, {} fixed, {} effect(s) moved to "
						"{} unlit effect copies and {} shader copies, {} failed",
			scanned, doubled, fixed, dropped, quietEffects.size(), quietShaders.size(), failed);
	}

	// While the crafting menu is open every enchantment carries its original effects, so an item the
	// player enchants stores only effects that exist in a plugin - never a copy made in memory, which
	// a save could not find again.
	class CraftingWatch : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static CraftingWatch* Get()
		{
			static CraftingWatch watch;
			return &watch;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && a_event->menuName == RE::CraftingMenu::MENU_NAME) {
				for (auto& s : gSwaps) {
					s.effect->baseEffect = a_event->opening ? s.original : s.quiet;
				}
				SKSE::log::info("crafting menu {}: {} enchantment effect(s) {}", a_event->opening ? "opened" : "closed", gSwaps.size(),
					a_event->opening ? "back on their originals" : "back on their unlit copies");
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};

	// ------------------------------------------------------------------ order of work
	bool PatcherActive()
	{
		auto* dh = RE::TESDataHandler::GetSingleton();
		return dh->GetLoadedModIndex(kPatchPlugin).has_value() || dh->GetLoadedLightModIndex(kPatchPlugin).has_value();
	}

	void OnDataLoaded()
	{
		if (PatcherActive()) {
			SKSE::log::info("{} is active, so the xEdit patcher's changes are in use: this plugin changes nothing. "
							"Use one or the other.",
				kPatchPlugin);
			return;
		}
		const auto cov = ReadCoverage();
		SKSE::log::info("configs: {} file(s), {} lit model(s), {} shader name(s)", cov.files, cov.models.size(), cov.shaders.size());
		if (cov.files == 0) {
			SKSE::log::warn("no Let There Be Glow or CS Light configs were found under Data\\LightPlacer; nothing was changed");
			return;
		}
		CastingLights(cov);
		EffectLights<RE::BGSProjectile>(cov, "projectile");
		EffectLights<RE::BGSExplosion>(cov, "explosion");
		EffectLights<RE::BGSHazard>(cov, "hazard");
		PoisonRuneArt();
		SprayLights();
		DoubledEnchantments(cov);
		if (!gSwaps.empty()) {
			RE::UI::GetSingleton()->AddEventSink<RE::MenuOpenCloseEvent>(CraftingWatch::Get());
		}
		{
			RE::BSSpinLockGuard guard(gEditorIDLock);
			gEditorIDs.clear();
			gEditorIDs.rehash(0);
		}
		SKSE::log::info("done");
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	EditorIDHook<RE::EffectSetting>::Install();
	EditorIDHook<RE::BGSProjectile>::Install();
	EditorIDHook<RE::BGSExplosion>::Install();
	EditorIDHook<RE::BGSHazard>::Install();
	EditorIDHook<RE::TESObjectLIGH>::Install();
	EditorIDHook<RE::TESEffectShader>::Install();
	EditorIDHook<RE::EnchantmentItem>::Install();
	SKSE::GetMessagingInterface()->RegisterListener([](SKSE::MessagingInterface::Message* a_msg) {
		if (a_msg && a_msg->type == SKSE::MessagingInterface::kDataLoaded) {
			OnDataLoaded();
		}
	});
	SKSE::log::info("Let There Be Glow plugin loaded; waiting for the game's data");
	return true;
}
