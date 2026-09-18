// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
//
// This program is free software: you can redistribute it and/or modify it under the terms of the
// GNU General Public License as published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see the
// GNU General Public License in LICENSE.txt for details.
//
// DYNAMIC WARDS 2.0 - STEP 1, THE COPY PROBE. IT READS AND IT CHANGES NOTHING.
//
// Why 2.0 exists: 1.0 sets ward art from a Papyrus script, and that script registers for the
// Loading Menu through SKSE. The registration is written into the co-save under a handle whose
// type reads 0xFFFF - an id no form has - so it cannot be cleanly dropped when the plugin goes
// away. Measured off his own co-saves, 2026-09-18: every other mod's Loading Menu handle carries
// a real type, ours does not, and a save carrying ours loads with a dead entry in the menu list.
// A DLL registers nothing with SKSE's Papyrus event system, so 2.0 leaves nothing behind at all.
//
// This build does none of that yet. It answers the three questions the rework rests on:
//   1. can an ART OBJECT be made in memory?      (very likely - the light version is proved)
//   2. can the EMPTY art be copied the same way?  (it is how a row is silenced)
//   3. can a REFERENCE EFFECT be made in memory?  (the 360 hit flash - NOT KNOWN)
// and it writes down which vanilla art records the wards actually wear, which nothing on the
// drive records today.
//
// HOW THE ANSWER COMES BACK. The first build reported through a log file. No SKSE plugin log file
// is written on his machine - measured 2026-09-18, not ours and not DevBench's own - so this build
// also hands its findings to DevBench, which can be read over the same connection as everything
// else. The log lines are kept; they cost nothing and they are the fallback anywhere a log works.
//
// Where each part lives: main.cpp (this file) is the entry point and the log; Plugin.h lists what
// the files share; CopyProbe.cpp is the probe; DevBench.cpp offers its findings to DevBench.

#include "Plugin.h"

using namespace Plugin;

namespace
{
	void OnMessage(SKSE::MessagingInterface::Message* a_msg)
	{
		if (!a_msg) {
			return;
		}

		// kPostLoad is when every plugin is loaded and DevBench can answer for its interface
		if (a_msg->type == SKSE::MessagingInterface::kPostLoad) {
			OfferToDevBench();
			return;
		}

		if (a_msg->type != SKSE::MessagingInterface::kDataLoaded) {
			return;
		}
		const auto started = std::chrono::steady_clock::now();
		RunCopyProbe();
		const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
		SKSE::log::info("done in {:.1f} ms", ms);
	}
}

namespace Plugin
{
	std::string Lower(std::string_view a_text)
	{
		std::string out(a_text);
		std::transform(out.begin(), out.end(), out.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return out;
	}

	bool Contains(std::string_view a_haystack, std::string_view a_needle)
	{
		return !a_needle.empty() && a_haystack.find(a_needle) != std::string_view::npos;
	}

	// a form named the way this project names one everywhere else: editor ID, form ID, and the file it came from
	std::string Where(const RE::TESForm* a_form)
	{
		if (!a_form) {
			return "(none)";
		}
		const char* id = a_form->GetFormEditorID();
		const auto* file = a_form->GetFile(0);
		return std::format("{} | {:08X} | {}", id && *id ? id : "(no editor ID)", a_form->GetFormID(),
			file ? file->GetFilename() : "(created)");
	}

	// Text safe to put between quotes in the report. A mesh path is full of backslashes, and one
	// unescaped backslash turns the whole report into something the other end cannot read.
	std::string JsonEscape(std::string_view a_text)
	{
		std::string out;
		out.reserve(a_text.size() + 8);
		for (const unsigned char c : a_text) {
			switch (c) {
			case '"':
				out += "\\\"";
				break;
			case '\\':
				out += "\\\\";
				break;
			case '\n':
				out += "\\n";
				break;
			case '\r':
				out += "\\r";
				break;
			case '\t':
				out += "\\t";
				break;
			default:
				if (c < 0x20) {
					out += std::format("\\u{:04x}", static_cast<unsigned>(c));
				} else {
					out += static_cast<char>(c);
				}
				break;
			}
		}
		return out;
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* a_skse)
{
	SKSE::Init(a_skse);
	SKSE::log::info("Dynamic Wards plugin - the 2.0 copy probe. It reads and copies; it changes nothing.");
	SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
	return true;
}
