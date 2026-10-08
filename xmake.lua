-- /////////////////////////////////////////////////////////////////////////////
-- @file xmake.lua
-- @brief Build configuration for LplKnowledge — the memory of the Laplace project.
--
-- Same dual contract as LplPlugin: the modules that a constrained target must be
-- able to run compile -ffreestanding into a static library the kernel links, and
-- the heavy half stays hosted. The line is the one the project already drew for
-- editor/ versus procgen/: READER versus WRITER. Harvesting the world's archives
-- needs a heap, a network stack and text parsing; reading what was harvested needs
-- none of the three, and the demon in ring 0 is the least capable reader there is.
--
-- C, C++ and assembly only. The reference implementations in this field are
-- written in Python and are bottlenecked by it; rewriting them natively is not a
-- stylistic preference, it is the stated lever for making this affordable.
-- /////////////////////////////////////////////////////////////////////////////

add_rules("mode.debug", "mode.release")
set_languages("c++23", "c17")
set_warnings("allextra")
-- snprintf truncates in silence: a 512-byte buffer once cut a generated header in the middle of an
-- identifier. At level 2 GCC sizes each call for the largest number each argument can hold, and as an
-- error a buffer too small for it stops the build. A string of unknown length counts as one byte, so a
-- `%s` still checks what snprintf returns. One flag on purpose: `-Wformat-truncation=2
-- -Werror=format-truncation` resets the level to 1. Clang has no levels, hence GCC alone.
add_cxflags("-Werror=format-truncation=2", {tools = {"gcc", "gxx"}})

-- ─────────────────────────────────────────────────────────────────────────────
-- Le socle LplPlugin (Fixed32, CORDIC, les ombrelles lpl::pmr) est une AMÉLIORATION
-- détectée, jamais une exigence. Ce dépôt doit se construire, tourner et être testé
-- seul — comme LplPlugin se construit sans LplKernel. Même dispositif que le noyau,
-- qui boote sans le moteur quand le sous-module manque (LPL_PLUGIN_UNAVAILABLE).
--
--   présent  -> LPL_HAS_FOUNDATION : contrat de déterminisme accessible, et ces
--               modules peuvent être compilés -ffreestanding pour le ring 0 ;
--   absent   -> build AUTONOME, hôte uniquement. Fixed32 n'est pas émulé : une
--               fausse virgule fixe laisserait un build autonome revendiquer une
--               parité qu'il ne peut pas avoir.
-- ─────────────────────────────────────────────────────────────────────────────
option("foundation")
    set_default("detect")
    -- ⚠ The values used to be auto|y|n, and that was unusable: xmake COERCES an option
    -- whose values look boolean, so `--foundation=n` AND `--foundation=auto` were both
    -- stored as the boolean false — the documented default and the opt-out became the
    -- same setting. Measured, not guessed: y stored true, n stored false, auto stored
    -- false — and so was "auto", which xmake also reads as a boolean. Three words it has
    -- no boolean reading for keep the three settings apart.
    set_values("detect", "force", "off")
    set_showmenu(true)
    set_description("Use the LplPlugin foundation when available (detect|force|off)")
option_end()

-- LplPlugin is a sibling of this repository. LPLPLUGIN_ROOT names another checkout; the
-- kernel's submodule path is still tried while it exists (MasterLaplace/LplKernel#431).
local function foundationRoot()
    local root = os.getenv("LPLPLUGIN_ROOT")
    if root and root ~= "" then
        return root
    end
    for _, candidate in ipairs({"../LplPlugin", "../LplKernel/LplPlugin"}) do
        local candidatePath = path.join(os.scriptdir(), candidate)
        if os.isdir(path.join(candidatePath, "core/include")) then
            return candidatePath
        end
    end
    return path.join(os.scriptdir(), "../LplPlugin")
end

local kProjectRoot = os.scriptdir()
local kFoundationRoot = foundationRoot()
local kConfigHeader = path.join(os.scriptdir(), "include/lplknowledge/config.h")

rule("laplace.version")
    on_load(function (target)
        local text = io.readfile(kConfigHeader)
        local version = {}
        for _, part in ipairs({"MAJOR", "MINOR", "PATCH"}) do
            table.insert(version, text:match("#define LPLKNOWLEDGE_VERSION_" .. part .. " (%d+)"))
        end
        target:set("version", table.concat(version, "."))
    end)
rule_end()

add_rules("laplace.version")


local function hasFoundation()
    -- Booleans are still handled, because a configuration stored by an older checkout
    -- carries them: true reads as force, false reads as off. Without that a stale
    -- .xmake/ would flip a build to standalone with no message saying why.
    local mode = get_config("foundation")
    if mode == nil then
        mode = "detect"
    elseif mode == true then
        mode = "force"
    elseif mode == false then
        -- A stored boolean can only come from an older checkout, where it meant either
        -- "auto" or "n" and there is no way left to tell which. Read as DETECT, because
        -- the failure modes are not symmetric: detecting a sibling that is there costs
        -- nothing, while silently skipping one makes every gate target vanish.
        mode = "detect"
    end
    if mode == "off" then
        return false
    end
    if os.isdir(path.join(kFoundationRoot, "core/include")) then
        return true
    end
    if mode == "force" then
        raise("--foundation=force was requested but " .. kFoundationRoot .. " is not present")
    end
    return false
end

-- L'ombrelle du dépôt : le seul endroit qui sait laquelle des deux situations on est.
add_includedirs("include")

-- Global, not a local: `includes()` loads each module script as its own chunk, so a
-- local here is invisible to tests/xmake.lua. Same convention as LplKernel's
-- LPLPLUGIN_AVAILABLE / LPLASSISTANT_AVAILABLE.
LPL_FOUNDATION_AVAILABLE = hasFoundation()

local kWithFoundation = LPL_FOUNDATION_AVAILABLE

-- Stamps the one translation unit that prints a tool's identity with what the source cannot
-- know: the commits this build came from, and the build itself. Only that target recompiles
-- when a commit changes.
rule("laplace.identity")
    on_load(function (target)
        local function git(directory, arguments)
            local output = try { function () return os.iorunv("git", table.join({"-C", directory}, arguments)) end }
            return output and output:trim() or ""
        end
        local function commit(directory)
            local sha = git(directory, {"rev-parse", "--short=7", "HEAD"})
            if sha == "" then
                return "unknown"
            end
            local dirty = git(directory, {"status", "--porcelain", "--untracked-files=no"})
            return dirty ~= "" and (sha .. "-dirty") or sha
        end
        target:add("defines", 'LPLKNOWLEDGE_COMMIT="' .. commit(kProjectRoot) .. '"')
        target:add("defines", 'LPLKNOWLEDGE_BUILD="' .. target:plat() .. "." .. (get_config("mode") or "debug") .. '"')
        if kWithFoundation then
            target:add("defines", 'LPLPLUGIN_COMMIT="' .. commit(kFoundationRoot) .. '"')
        end
    end)
rule_end()

if LPL_FOUNDATION_AVAILABLE then
    add_includedirs(path.join(kFoundationRoot, "core/include"))
    add_includedirs(path.join(kFoundationRoot, "math/include"))
    add_includedirs(path.join(kFoundationRoot, "memory/include"))
    -- history/, and this is the seam of the whole repository rather than a convenience.
    -- `history/Fact.hpp` states the division: it trades in IDENTIFIERS and says the strings
    -- live here. So `history/` owns the arithmetic of doubt — trust, fusion, the demotion of
    -- a contradicted claim, gate P13 — and this repository owns the corpus, the format and
    -- the identity. The dependency is one-way by design: LplKnowledge knows about
    -- LplPlugin, never the reverse, exactly as LplAssistant does.
    add_includedirs(path.join(kFoundationRoot, "history/include"))
    -- testing/, for gate P18's test: lpl::testing declares it, and runs it on the host and in ring 0.
    add_includedirs(path.join(kFoundationRoot, "testing/include"))
    add_defines("LPL_HAS_FOUNDATION")
else
    print("[%s] standalone build: LplPlugin foundation absent, host only", "LplKnowledge")
end

-- ─────────────────────────────────────────────────────────────────────────────
-- Le socle, COMPILÉ. Les `add_includedirs` ci-dessus donnent les en-têtes ; il faut en
-- plus le code hors-ligne : CORDIC (les rotations), l'arène, Log, et `history/` —
-- l'arithmétique du doute que ce dépôt CONSOMME et ne réécrit pas.
--
-- `history/src/HistorySystem.cpp` est délibérément absent : c'est le seul fichier du
-- module qui tire `ecs`, `ecology` et `procgen`, et c'est un système d'ordonnanceur dont
-- aucun outil de moisson n'a besoin. L'inclure ferait entrer trois modules de moteur dans
-- un dépôt de données pour un symbole que personne n'appelle ici.
--
-- Même dispositif que la cible `lpl-foundation` de LplAssistant, et pour la même raison :
-- une cible locale qui compile quelques fichiers, pas un paquet xmake. En ring 0 la
-- question ne se pose pas — ces objets sont déjà dans libengine.a.
-- ─────────────────────────────────────────────────────────────────────────────
if LPL_FOUNDATION_AVAILABLE then
    target("lpl-foundation")
        set_kind("static")
        set_group("modules")
        add_files(path.join(kFoundationRoot, "math/src/Cordic.cpp"))
        -- The projection: degrees to cells, metres to world units. It lives in math/ because that
        -- is the only layer both repositories can see -- see lpl/math/Geo.hpp for why procgen and
        -- history were each a violation or a cycle.
        add_files(path.join(kFoundationRoot, "math/src/Geo.cpp"))
        add_files(path.join(kFoundationRoot, "memory/src/ArenaAllocator.cpp"))
        add_files(path.join(kFoundationRoot, "core/src/Log.cpp"))
        add_files(path.join(kFoundationRoot, "history/src/Attestation.cpp"))
        add_files(path.join(kFoundationRoot, "history/src/Chronicle.cpp"))
        add_files(path.join(kFoundationRoot, "history/src/Constraint.cpp"))
        add_files(path.join(kFoundationRoot, "history/src/Divergence.cpp"))
        add_files(path.join(kFoundationRoot, "history/src/Era.cpp"))
        add_files(path.join(kFoundationRoot, "history/src/Fact.cpp"))
        add_files(path.join(kFoundationRoot, "history/src/Parity.cpp"))
        add_files(path.join(kFoundationRoot, "history/src/PossibleWorld.cpp"))
        add_files(path.join(kFoundationRoot, "history/src/Timeline.cpp"))
    target_end()

    -- The runner of lpl::testing and its host entry point, for test-knowledge.
    target("lpl-testing")
        set_kind("static")
        set_group("modules")
        add_files(path.join(kFoundationRoot, "testing/src/Runner.cpp"))
        add_files(path.join(kFoundationRoot, "testing/host/main.cpp"))
    target_end()
end

-- graph/ n'est plus inclus : ses huit en-têtes décrivaient tous quelque chose que
-- `lpl::history` (gate P13) implémente et gate déjà, ou que `knowledge/Query.hpp` fait.
-- La raison, fichier par fichier, est dans graph/FOLDED.md. Ne pas le remettre.
includes(
    "knowledge",
    "corpus",
    "harvest",
    "mirror",
    "media",
    "apps",
    "tests"
)
