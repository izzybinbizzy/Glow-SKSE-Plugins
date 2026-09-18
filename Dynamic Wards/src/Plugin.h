// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// What the files share.

#pragma once

#include "PCH.h"

namespace Plugin
{
	// ------------------------------------------------------------------ making forms in memory
	// The same route Luminous Arcana's pass 0 uses and that is proved in his game: the game's own
	// concrete form factory, so the form is a real one the engine will render and resolve.
	template <class T>
	T* NewForm()
	{
		auto* factory = RE::IFormFactory::GetConcreteFormFactoryByType<T>();
		return factory ? factory->Create() : nullptr;
	}

	// ------------------------------------------------------------------ CopyProbe.cpp
	void RunCopyProbe();

	// What the probe found, as a JSON object, so it can be read over DevBench instead of out of a
	// log file. Written once when the probe runs; read from DevBench's listener thread.
	std::string ProbeReport();
	void        SetProbeReport(std::string a_json);

	// ------------------------------------------------------------------ DevBench.cpp
	// Offers the probe's findings to DevBench, if DevBench is in the load order. Safe to call when
	// it is not: it simply does nothing. Call at kPostLoad, which is when the interface exists.
	void OfferToDevBench();

	// ------------------------------------------------------------------ small helpers
	std::string Lower(std::string_view a_text);
	bool        Contains(std::string_view a_haystack, std::string_view a_needle);
	std::string Where(const RE::TESForm* a_form);
	std::string JsonEscape(std::string_view a_text);
}
