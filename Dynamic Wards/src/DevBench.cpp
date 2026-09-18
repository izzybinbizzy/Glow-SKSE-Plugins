// Dynamic Wards - SKSE plugin
// Copyright (C) 2026 izzydoingit
// GPL-3.0-or-later; see LICENSE.txt and the notice at the top of main.cpp.
//
// HOW THE PROBE'S ANSWER GETS OUT.
//
// The first build wrote its findings to a log file. Measured on his machine 2026-09-18: no SKSE
// plugin log file is written there at all - not ours, not DevBench's own - so a log is a dead
// letter box on this setup. DevBench offers a way out that does not need one. A mod may hand
// DevBench a handler keyed by a name, and DevBench's own inspect tool then routes to it, so the
// findings come back over the same connection everything else is read over.
//
// The handler runs on DevBench's listener thread, NOT the game's main thread. That is why it does
// no game work at all: the probe already ran at data load and left its answer in a string, and
// this only hands that string back. Nothing here touches a form.
//
// DevBenchAPI.h and DevBenchAPI.cpp beside this file are DevBench's own interface files, MIT
// licensed by their author precisely so any plugin may carry them. They are copied unchanged.

#include "Plugin.h"

#include "DevBenchAPI.h"

namespace Plugin
{
	namespace
	{
		// the key DevBench's inspect tool routes on: inspect kind=dynamicwards
		constexpr const char* kKey = "dynamicwards";

		// RegisterToolExtension was added in DevBench 1.5.0; the build number is major*10000 +
		// minor*100 + patch, so 1.5.0 is 10500. Below that, the call is not in the interface.
		constexpr unsigned int kNeedsBuild = 10500;

		constexpr const char* kDescriptor =
			R"({"description":"Dynamic Wards 2.0 copy probe - which art records the wards wear, and whether an art object, the empty art and a reference effect can each be made in memory. Read only; it reports what the probe found at data load and does no work when called.","inputSchema":{"type":"object","properties":{}},"readOnly":true})";

		void Handler(void*, const char*, void* a_sink, DevBenchAPI::WriteFn a_write)
		{
			if (a_write) {
				a_write(a_sink, ProbeReport().c_str());
			}
		}
	}

	void OfferToDevBench()
	{
		auto* devbench = DevBenchAPI::GetDevBenchInterface001();
		if (!devbench) {
			SKSE::log::info("[DEVBENCH] DevBench is not in this load order - the probe's findings stay in the log");
			return;
		}

		const auto build = devbench->GetBuildNumber();
		if (build < kNeedsBuild) {
			SKSE::log::warn("[DEVBENCH] DevBench build {} is older than {} and cannot take an inspect extension", build, kNeedsBuild);
			return;
		}

		// the call answers true when this is a fresh entry and false when it replaced one already there
		const bool fresh = devbench->RegisterToolExtension("inspect", kKey, kDescriptor, Handler, nullptr);
		SKSE::log::info("[DEVBENCH] offered the probe to DevBench build {} as inspect kind={} ({})",
			build, kKey, fresh ? "a new entry" : "replacing one already registered");
	}
}
