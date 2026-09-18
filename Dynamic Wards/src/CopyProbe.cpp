// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// STEP 1 OF THE 2.0 REWORK, AND IT ONLY READS AND COPIES - IT CHANGES NOTHING IN THE GAME.
//
// 2.0 has no plugin of its own, so every art record this mod ships today has to be made in memory
// instead. Making a light that way is proved in his game (Luminous Arcana pass 0, 19 of 20 made,
// 1896 uses pointed at them). Making an ART OBJECT is very likely and making a REFERENCE EFFECT -
// the 360 hit flash - is NOT KNOWN. This probe answers all three before anything is built on them.
//
// It also writes down something nothing on the drive has: WHICH vanilla art records the wards
// actually use, read off the records rather than guessed.
//
// Everything it finds goes into a JSON report as well as the log, because no SKSE plugin log file
// is written on his machine - see DevBench.cpp.

#include "Plugin.h"

namespace Plugin
{
	namespace
	{
		// the meshes a ward's art points at, lowercased; a record naming one of these is a ward's art
		constexpr std::string_view kWardBody = "wardbodyfx";
		constexpr std::string_view kWardHand = "wardinhandfx";
		// vanilla's empty art, which the mod uses to silence a row (it must be a RECORD, never a None)
		constexpr std::string_view kEmpty = "fxemptyobject";

		// how many ward-wearing magic effects to name one by one before only counting them
		constexpr std::size_t kNameAtMost = 40;

		std::size_t gMade = 0;
		std::size_t gFailed = 0;

		std::mutex  gReportLock;
		std::string gReport = R"({"probeRan":false,"note":"the probe has not run yet"})";

		// ---------------------------------------------------------- copy an art object
		RE::BGSArtObject* CopyArt(const RE::BGSArtObject* a_src, const char* a_model)
		{
			auto* out = NewForm<RE::BGSArtObject>();
			if (!out) {
				return nullptr;
			}
			out->SetModel(a_model && *a_model ? a_model : a_src->GetModel());
			return out;
		}

		// one form, named the way the rest of this project names one, as a JSON object
		std::string FormJson(const RE::TESForm* a_form, std::string_view a_model)
		{
			if (!a_form) {
				return "null";
			}
			const char* id = a_form->GetFormEditorID();
			const auto* file = a_form->GetFile(0);
			return std::format(R"({{"editorId":"{}","formId":"{:08X}","file":"{}","model":"{}"}})",
				JsonEscape(id && *id ? id : ""), a_form->GetFormID(),
				JsonEscape(file ? file->GetFilename() : "(created)"), JsonEscape(a_model));
		}
	}

	std::string ProbeReport()
	{
		std::scoped_lock lock(gReportLock);
		return gReport;
	}

	void SetProbeReport(std::string a_json)
	{
		std::scoped_lock lock(gReportLock);
		gReport = std::move(a_json);
	}

	void RunCopyProbe()
	{
		auto* handler = RE::TESDataHandler::GetSingleton();
		if (!handler) {
			SKSE::log::error("[PROBE] no data handler - nothing was read");
			SetProbeReport(R"({"probeRan":false,"note":"no data handler was available at data load"})");
			return;
		}

		std::vector<std::string> wardArtJson;
		std::vector<std::string> wardEffectJson;

		// ---------------------------------------------------------- 1. what the wards actually wear
		SKSE::log::info("[PROBE] ---- every art record whose mesh is a ward's ----");
		const RE::BGSArtObject* wardBody = nullptr;
		const RE::BGSArtObject* wardHand = nullptr;
		const RE::BGSArtObject* emptyArt = nullptr;
		std::size_t seen = 0;
		for (auto* art : handler->GetFormArray<RE::BGSArtObject>()) {
			if (!art) {
				continue;
			}
			++seen;
			const auto model = Lower(art->GetModel() ? art->GetModel() : "");
			if (model.empty()) {
				continue;
			}
			if (Contains(model, kWardBody) || Contains(model, kWardHand)) {
				SKSE::log::info("[PROBE-WARDART] {} | {}", Where(art), model);
				wardArtJson.push_back(FormJson(art, model));
				if (!wardBody && Contains(model, kWardBody)) {
					wardBody = art;
				}
				if (!wardHand && Contains(model, kWardHand)) {
					wardHand = art;
				}
			}
			if (!emptyArt && Contains(model, kEmpty)) {
				emptyArt = art;
			}
		}
		SKSE::log::info("[PROBE] {} art record(s) in this load order", seen);

		// ---------------------------------------------------------- 2. can we make an art object?
		std::string artCopyJson = R"({"tried":false,"why":"no ward body art was found to copy"})";
		if (wardBody) {
			auto* copy = CopyArt(wardBody, "magic\\wardbodyfx - Adept.nif");
			if (copy && copy->GetModel()) {
				++gMade;
				SKSE::log::info("[PROBE-ART-OK] made {:08X} from {} | model now {}", copy->GetFormID(), Where(wardBody), copy->GetModel());
				artCopyJson = std::format(R"({{"tried":true,"made":true,"formId":"{:08X}","model":"{}","from":{}}})",
					copy->GetFormID(), JsonEscape(copy->GetModel()), FormJson(wardBody, Lower(wardBody->GetModel() ? wardBody->GetModel() : "")));
			} else {
				++gFailed;
				SKSE::log::error("[PROBE-ART-FAILED] an art object could not be made in memory");
				artCopyJson = R"({"tried":true,"made":false})";
			}
		} else {
			SKSE::log::warn("[PROBE-ART-SKIPPED] no ward body art was found to copy");
		}

		// ---------------------------------------------------------- 3. the empty art, the silencer
		std::string emptyCopyJson = R"({"tried":false,"why":"vanilla's empty art was not found"})";
		if (emptyArt) {
			auto* copy = CopyArt(emptyArt, nullptr);
			if (copy) {
				++gMade;
				SKSE::log::info("[PROBE-EMPTY-OK] made {:08X} from {} | model {}", copy->GetFormID(), Where(emptyArt),
					copy->GetModel() ? copy->GetModel() : "(none)");
				emptyCopyJson = std::format(R"({{"tried":true,"made":true,"formId":"{:08X}","model":"{}"}})",
					copy->GetFormID(), JsonEscape(copy->GetModel() ? copy->GetModel() : ""));
			} else {
				++gFailed;
				SKSE::log::error("[PROBE-EMPTY-FAILED] the empty art could not be copied");
				emptyCopyJson = R"({"tried":true,"made":false})";
			}
		} else {
			SKSE::log::warn("[PROBE-EMPTY-SKIPPED] vanilla's empty art was not found");
		}

		// ---------------------------------------------------------- 4. THE UNKNOWN: the hit flash
		const auto& rfcts = handler->GetFormArray<RE::BGSReferenceEffect>();
		SKSE::log::info("[PROBE] {} reference effect(s) in this load order", rfcts.size());
		std::string             flashJson = R"({"tried":false,"why":"no reference effect was found to copy"})";
		RE::BGSReferenceEffect* rfctSrc = rfcts.empty() ? nullptr : rfcts.front();
		if (rfctSrc) {
			auto* copy = NewForm<RE::BGSReferenceEffect>();
			if (copy) {
				copy->data = rfctSrc->data;
				++gMade;
				SKSE::log::info("[PROBE-FLASH-OK] made {:08X} from {} - a reference effect CAN be made in memory",
					copy->GetFormID(), Where(rfctSrc));
				flashJson = std::format(R"({{"tried":true,"made":true,"formId":"{:08X}","from":{}}})",
					copy->GetFormID(), FormJson(rfctSrc, ""));
			} else {
				++gFailed;
				SKSE::log::error("[PROBE-FLASH-FAILED] a reference effect could NOT be made in memory - "
								 "the 360 hit flash needs a plugin record and the rest of 2.0 does not");
				flashJson = R"({"tried":true,"made":false})";
			}
		} else {
			SKSE::log::warn("[PROBE-FLASH-SKIPPED] no reference effect was found to copy");
		}

		// ---------------------------------------------------------- 5. what a ward effect points at
		SKSE::log::info("[PROBE] ---- every magic effect whose art is a ward's ----");
		std::size_t wardEffects = 0;
		for (auto* eff : handler->GetFormArray<RE::EffectSetting>()) {
			if (!eff) {
				continue;
			}
			const auto* cast = eff->data.castingArt;
			const auto* hit = eff->data.hitEffectArt;
			const auto castModel = Lower(cast && cast->GetModel() ? cast->GetModel() : "");
			const auto hitModel = Lower(hit && hit->GetModel() ? hit->GetModel() : "");
			if (Contains(castModel, kWardHand) || Contains(hitModel, kWardBody)) {
				++wardEffects;
				if (wardEffects <= kNameAtMost) {
					SKSE::log::info("[PROBE-WARDEFFECT] {} | casting {} | hit {}", Where(eff),
						castModel.empty() ? "(none)" : castModel, hitModel.empty() ? "(none)" : hitModel);
					const char* id = eff->GetFormEditorID();
					const auto* file = eff->GetFile(0);
					wardEffectJson.push_back(std::format(
						R"({{"editorId":"{}","formId":"{:08X}","file":"{}","castingArt":"{}","hitEffectArt":"{}"}})",
						JsonEscape(id && *id ? id : ""), eff->GetFormID(),
						JsonEscape(file ? file->GetFilename() : "(created)"),
						JsonEscape(castModel), JsonEscape(hitModel)));
				}
			}
		}
		SKSE::log::info("[PROBE] {} magic effect(s) wear a ward's art", wardEffects);

		SKSE::log::info("[PROBE] ==== {} copy/copies made, {} failed ====", gMade, gFailed);
		SKSE::log::info("[PROBE] nothing in the game was changed by this build.");

		// ---------------------------------------------------------- the report DevBench hands back
		auto join = [](const std::vector<std::string>& a_items) {
			std::string out;
			for (const auto& item : a_items) {
				if (!out.empty()) {
					out += ',';
				}
				out += item;
			}
			return out;
		};

		SetProbeReport(std::format(
			R"({{"probeRan":true,"changedNothing":true,"artRecordsInLoadOrder":{},"wardArt":[{}],)"
			R"("artObjectCopy":{},"emptyArtCopy":{},"referenceEffectsInLoadOrder":{},"referenceEffectCopy":{},)"
			R"("wardEffectCount":{},"wardEffectsNamed":{},"wardEffects":[{}],"copiesMade":{},"copiesFailed":{}}})",
			seen, join(wardArtJson), artCopyJson, emptyCopyJson, rfcts.size(), flashJson,
			wardEffects, wardEffectJson.size(), join(wardEffectJson), gMade, gFailed));
	}
}
