-- Dynamic Wards - SKSE plugin. GPL-3.0-or-later, see LICENSE.txt.
set_xmakever("3.0.0")
set_project("DynamicWards")
set_version("2.0.0")
set_license("GPL-3.0-or-later")
set_arch("x64")
set_languages("c++23")
-- the DLL carries its own Visual C++ runtime, so an older runtime on a player's PC cannot stop it loading
set_runtimes("MT")
add_rules("mode.releasedbg")
set_defaultmode("releasedbg")

set_config("skyrim_se", true)
set_config("skyrim_ae", true)
set_config("skyrim_vr", false)

includes("lib/commonlibsse-ng")

target("DynamicWards", function()
    add_deps("commonlibsse-ng")
    add_rules("commonlibsse-ng.plugin", {
        name = "DynamicWards",
        author = "izzydoingit",
        description = "Dynamic Wards - gives every rank of ward its own art, in memory, with no plugin of its own",
    })
    add_files("src/*.cpp")
    add_headerfiles("src/*.h")
    add_includedirs("src")
    set_pcxxheader("src/PCH.h")
end)
