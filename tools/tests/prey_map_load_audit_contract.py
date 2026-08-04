#!/usr/bin/env python3
"""Focused contracts for the Prey map manifest and conDump audit."""

from __future__ import annotations

import argparse
import importlib.util
import json
import sys
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / ".vscode" / "prey-maps.json"
LAUNCH = ROOT / ".vscode" / "launch.json"
AUDIT_SCRIPT = ROOT / "tools" / "validation" / "prey_map_load_condump_audit.py"


def load_audit_module():
    spec = importlib.util.spec_from_file_location("openprey_map_load_audit", AUDIT_SCRIPT)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Could not load {AUDIT_SCRIPT}")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


AUDIT = load_audit_module()

EXPECTED_MAPS = (
    ("(SP) roadhouse 'Last Call'", "sp", "game/roadhouse"),
    ("(SP) feedingtowera 'Escape Velocity'", "sp", "game/feedingtowera"),
    ("(SP) feedingtowerb 'Downward Spiral'", "sp", "game/feedingtowerb"),
    ("(SP) lotaa 'Rites of Passage'", "sp", "game/lotaa"),
    ("(SP) feedingtowerc 'Second Chances'", "sp", "game/feedingtowerc"),
    ("(SP) feedingtowerd 'All Fall Down'", "sp", "game/feedingtowerd"),
    ("(SP) salvage 'Crash Landing'", "sp", "game/salvage"),
    ("(SP) salvageboss 'Sacrifices'", "sp", "game/salvageboss"),
    ("(SP) lotab 'There Are Others'", "sp", "game/lotab"),
    ("(SP) shuttlea 'Guiding Fires'", "sp", "game/shuttlea"),
    ("(SP) shuttleb 'The Old Tribes'", "sp", "game/shuttleb"),
    ("(SP) biolabsa 'Hidden Agenda'", "sp", "game/biolabsa"),
    ("(SP) biolabsb 'Jen'", "sp", "game/biolabsb"),
    ("(SP) superportal 'The Dark Harvest'", "sp", "game/superportal"),
    ("(SP) harvestera 'Following Her'", "sp", "game/harvestera"),
    ("(SP) harvesterb 'The Complex'", "sp", "game/harvesterb"),
    ("(SP) spindlea 'Ascent'", "sp", "game/spindlea"),
    ("(SP) spindleb 'Center of Gravity'", "sp", "game/spindleb"),
    ("(SP) girlfriendx 'Resolutions'", "sp", "game/girlfriendx"),
    ("(SP) lotad 'Oath of Vengeance'", "sp", "game/lotad"),
    ("(SP) keeperfortress 'Facing the Enemy'", "sp", "game/keeperfortress"),
    ("(SP) spherebrain 'Mother's Embrace'", "sp", "game/spherebrain"),
    ("(SP) deathwalk1 'Death Walk 1'", "sp", "game/deathwalk1"),
    ("(SP) deathwalk2 'Death Walk 2'", "sp", "game/deathwalk2"),
    ("(SP) deathwalk3 'Death Walk 3'", "sp", "game/deathwalk3"),
    ("(MP) dmescher 'Keeper Gravity'", "mp", "game/dmescher"),
    ("(MP) dmescher2 'Cubiks Rube'", "mp", "game/dmescher2"),
    ("(MP) dmgravitylab_6 'Gravity Lab'", "mp", "game/dmgravitylab_6"),
    ("(MP) dmplanes_4 'Space Oddity'", "mp", "game/dmplanes_4"),
    ("(MP) dmroadhouse 'Roadhouse'", "mp", "game/dmroadhouse"),
    ("(MP) dmsalvagewalk 'Salvage Walk'", "mp", "game/dmsalvagewalk"),
    ("(MP) dmshuttle1 'Shuttle 1'", "mp", "game/dmshuttle1"),
    ("(MP) dmshuttle2 'Shuttle 2'", "mp", "game/dmshuttle2"),
    ("(MP) dmsphere 'Spindle Spheres'", "mp", "game/dmsphere"),
    ("(MP) dmtopillogical_4 'Topillogical'", "mp", "game/dmtopillogical_4"),
    ("(MP) dmtunnelrat_8 'Tunnel Rat'", "mp", "game/dmtunnelrat_8"),
    ("(MP) dmwallwalk2 'Wall Walk'", "mp", "game/dmwallwalk2"),
)

_DEFAULT_CONDUMP = object()


def write_text(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8")
    return path


def make_result(
    root: Path,
    identifier: str,
    *,
    log_text: str | None = "",
    condump_text: str | None | object = _DEFAULT_CONDUMP,
    stdout_text: str = "",
    stderr_text: str = "",
    exit_code: int | None = 0,
    timed_out: bool = False,
):
    if condump_text is _DEFAULT_CONDUMP:
        actual_condump_text: str | None = (
            f"console dump\n  123 msec to load game/{identifier}\n"
        )
    else:
        actual_condump_text = condump_text if isinstance(condump_text, str) else None
    stdout_path = write_text(root / f"{identifier}.stdout.txt", stdout_text)
    stderr_path = write_text(root / f"{identifier}.stderr.txt", stderr_text)
    log_path = None if log_text is None else write_text(root / f"{identifier}.log", log_text)
    condump_path = (
        None
        if actual_condump_text is None
        else write_text(root / f"{identifier}.txt", actual_condump_text)
    )
    return AUDIT.MapResult(
        entry=AUDIT.MapEntry(name=identifier, kind="sp", map_name=f"game/{identifier}"),
        safe_id=identifier,
        savepath=root / identifier,
        stdout_path=stdout_path,
        stderr_path=stderr_path,
        log_path=log_path,
        condump_path=condump_path,
        exit_code=exit_code,
        timed_out=timed_out,
        elapsed_seconds=1.0,
        map_load_completed=actual_condump_text is not None,
    )


class ManifestAndLaunchContracts(unittest.TestCase):
    def test_manifest_is_the_canonical_37_map_retail_set(self) -> None:
        entries = AUDIT.load_manifest(MANIFEST)
        actual = tuple((entry.name, entry.kind, entry.map_name) for entry in entries)
        self.assertEqual(EXPECTED_MAPS, actual)
        self.assertNotIn("game/flashinghead", {entry.map_name for entry in entries})

    def test_generated_launch_configurations_match_manifest(self) -> None:
        entries = AUDIT.load_manifest(MANIFEST)
        document = json.loads(LAUNCH.read_text(encoding="utf-8"))
        configurations = document["configurations"]
        self.assertEqual(len(entries) + 3, len(configurations))
        by_name = {configuration["name"]: configuration for configuration in configurations}
        self.assertEqual(len(configurations), len(by_name))

        for entry in entries:
            with self.subTest(map=entry.map_name):
                arguments = by_name[entry.name]["args"]
                map_command = "+map" if entry.kind == "sp" else "+devmap"
                self.assertEqual(entry.map_name, arguments[arguments.index(map_command) + 1])
                self.assertEqual("0", arguments[arguments.index("r_fullscreen") + 1])


class AuditContracts(unittest.TestCase):
    def test_canonical_source_dedup_replay_and_severity(self) -> None:
        uppercase_base = (
            "WARNING: Non-portable: path contains uppercase characters: "
            "base/models/mapobjects/RoadHouse/barrel"
        )
        uppercase_basepr = uppercase_base.replace("base/", "basepr/", 1)
        gl_probe = "X..GL_ATI_fragment_shader not found"
        ase_path = "WARNING: idFileSystem::OSPathToRelativePath failed on CAGE_ARM.TGA"
        replay = "\n".join(
            (
                "------------- Warnings ---------------",
                "during openPREY initialization...",
                uppercase_base,
                "1 warnings",
            )
        )

        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            result = make_result(
                run_root,
                "source_order",
                log_text="\n".join((uppercase_base, uppercase_basepr, replay, gl_probe, ase_path)),
                stderr_text="\n".join((gl_probe, "ERROR: supplemental crash detail")),
                stdout_text=uppercase_base,
            )

            self.assertEqual(["log", "stderr", "stdout"], [name for name, _ in AUDIT.select_audit_sources(result)])
            issues = AUDIT.collect_issues([result], run_root=run_root, basepath="")

        uppercase_signature = next(
            signature for signature in issues if "uppercase characters" in signature.lower()
        )
        self.assertIn("<game-root>/", uppercase_signature)
        self.assertEqual("notice", issues[uppercase_signature].severity)
        self.assertEqual(2, len(issues[uppercase_signature].occurrences))
        self.assertEqual({"log"}, {occurrence.source for occurrence in issues[uppercase_signature].occurrences})

        gl_issue = issues[gl_probe]
        self.assertEqual("notice", gl_issue.severity)
        self.assertEqual(1, len(gl_issue.occurrences))
        self.assertEqual("notice", issues[ase_path].severity)
        self.assertEqual(1, len(issues[ase_path].occurrences))
        self.assertEqual("error", issues["ERROR: supplemental crash detail"].severity)

    def test_condump_keeps_replay_only_evidence_without_double_counting(self) -> None:
        direct = "WARNING: Non-portable: path contains uppercase characters: base/models/Thing"
        replay_only = "WARNING: idFileSystem::OSPathToRelativePath failed on CAGE_BAS.TGA"
        text = "\n".join(
            (
                direct,
                "------------- Warnings ---------------",
                "during map load...",
                direct,
                replay_only,
                "2 warnings",
            )
        )
        with tempfile.TemporaryDirectory() as temporary:
            kwargs = {
                "map_name": "game/test",
                "run_root": Path(temporary),
                "basepath": "",
            }
            condump_lines = list(AUDIT.source_issue_lines(text, source_name="conDump", **kwargs))
            log_lines = list(AUDIT.source_issue_lines(text, source_name="log", **kwargs))

        self.assertEqual(2, len(condump_lines))
        self.assertEqual(1, len(log_lines))
        self.assertEqual(1, sum(1 for signature, *_rest in condump_lines if "uppercase" in signature.lower()))

    def test_only_known_retail_ase_metadata_is_downgraded(self) -> None:
        known = "WARNING: idFileSystem::OSPathToRelativePath failed on STONE_70.JPG"
        unknown = "WARNING: idFileSystem::OSPathToRelativePath failed on Z:/custom/asset.tga"
        self.assertEqual(("Stock ASE authoring-path diagnostic", "notice"), AUDIT.classify_issue_family(known))
        self.assertEqual(("Filesystem relative-path warning", "warning"), AUDIT.classify_issue_family(unknown))

    def test_only_known_retail_uppercase_paths_are_downgraded(self) -> None:
        known = (
            "WARNING: Non-portable: path contains uppercase characters: "
            "<game-root>/models/mapobjects/RoadHouse/barrel"
        )
        known_deathwalk = (
            "WARNING: Non-portable: path contains uppercase characters: "
            "basepr/models/mapobjects/deathwalk/ASH"
        )
        unknown = (
            "WARNING: Non-portable: path contains uppercase characters: "
            "<game-root>/models/newContent/MixedCase"
        )
        self.assertEqual(("Uppercase retail asset path diagnostic", "notice"), AUDIT.classify_issue_family(known))
        self.assertEqual(
            ("Uppercase retail asset path diagnostic", "notice"),
            AUDIT.classify_issue_family(
                AUDIT.normalize_issue(known_deathwalk, run_root=Path("."), basepath="")
            ),
        )
        self.assertEqual(("Uppercase asset path warning", "warning"), AUDIT.classify_issue_family(unknown))

    def test_target_map_requires_positive_completion_marker(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            completed = make_result(
                run_root,
                "completed",
                log_text="log without the completion marker",
                condump_text="console dump\n  456 msec to load game/completed\n",
            )
            wrong_map = make_result(
                run_root,
                "wrong_map",
                condump_text="console dump\n  456 msec to load game/somewhere_else\n",
            )
            failed = make_result(
                run_root,
                "failed",
                log_text="ERROR: failed to load map game/failed",
            )
            AUDIT.collect_issues(
                [completed, wrong_map, failed], run_root=run_root, basepath=""
            )

        self.assertEqual("ok", completed.status)
        self.assertTrue(completed.map_load_completion.startswith("conDump:"))
        self.assertEqual("map load incomplete", wrong_map.status)
        self.assertFalse(wrong_map.map_load_completed)
        self.assertEqual("map load failed", failed.status)
        self.assertTrue(failed.map_load_failed)

    def test_unknown_exit_is_incomplete_and_reported(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            run_root = Path(temporary)
            results = [
                make_result(run_root, "ok"),
                make_result(run_root, "unknown", exit_code=None),
                make_result(run_root, "timeout", exit_code=None, timed_out=True, condump_text=None),
                make_result(run_root, "nonzero", exit_code=5),
            ]
            self.assertEqual(["ok", "unknown exit", "timeout", "exit 5"], [result.status for result in results])

            report_path = run_root / "audit.md"
            args = argparse.Namespace(
                manifest=MANIFEST,
                renderer_api="gl",
                basepath="",
                install_root=ROOT / ".install",
                audit=report_path,
                timeout=10.0,
                settle_ms=0,
            )
            AUDIT.write_audit_markdown(
                report_path,
                results=results,
                issues={},
                args=args,
                run_root=run_root,
                started_at="2026-08-02T00:00:00+00:00",
                finished_at="2026-08-02T00:00:01+00:00",
            )
            report = report_path.read_text(encoding="utf-8")

        self.assertIn("- Map-command loads that completed: 1", report)
        self.assertIn("- Timeouts: 1", report)
        self.assertIn("- Non-zero process exits: 1", report)
        self.assertIn("- Unknown process exit statuses: 1", report)
        self.assertIn("- Actionable issue signatures: 0", report)
        self.assertIn("- Known notice signatures: 0", report)
        self.assertIn("| game/unknown | sp | unknown exit |", report)


class EngineFixContracts(unittest.TestCase):
    def test_ase_only_converts_complete_game_directory_asset_paths(self) -> None:
        ase = (ROOT / "src" / "renderer" / "Model_ase.cpp").read_text(encoding="utf-8")
        helper_start = ase.index("static bool ASE_HasGameDirAssetPath")
        diffuse_start = ase.index("static void ASE_KeyMAP_DIFFUSE", helper_start)
        helper = ase[helper_start:diffuse_start]
        diffuse_end = ase.index('else if ( !strcmp( token, "*UVW_U_OFFSET" ) )', diffuse_start)
        diffuse = ase[diffuse_start:diffuse_end]

        self.assertIn("if ( path.IsEmpty() )", helper)
        self.assertIn("BASE_GAMEDIR", helper)
        self.assertIn("OPENQ4_GAMEDIR", helper)
        self.assertIn('cvarSystem->GetCVarString( "fs_game" )', helper)
        self.assertIn('cvarSystem->GetCVarString( "fs_game_base" )', helper)
        self.assertIn("gameDir == NULL || gameDir[0] == '\\0'", helper)
        self.assertIn("segment == path.c_str() || segment[-1] == '/'", helper)
        self.assertIn("assetPath[0] == '/'", helper)
        self.assertIn("assetPath[1] != '\\0'", helper)
        guard = "if ( ASE_HasGameDirAssetPath( matname ) ) {"
        conversion = "qpath = fileSystem->OSPathToRelativePath( matname );"
        self.assertLess(diffuse.index("matname.BackSlashesToSlashes();"), diffuse.index(guard))
        self.assertIn(conversion, diffuse[diffuse.index(guard) :])
        self.assertEqual(1, ase.count("fileSystem->OSPathToRelativePath( matname )"))

    def test_particle_and_particle2_resolve_particle_declarations(self) -> None:
        material = (ROOT / "src" / "renderer" / "Material.cpp").read_text(encoding="utf-8")
        particle_start = material.index('if ( !token.Icmp( "particle" ) )')
        particle2_start = material.index('if ( !token.Icmp( "particle2" ) )', particle_start)
        bad_deform = material.index('src.Warning( "Bad deform type', particle2_start)
        self.assertIn("FindType( DECL_PARTICLE", material[particle_start:particle2_start])
        self.assertIn("FindType( DECL_PARTICLE", material[particle2_start:bad_deform])

    def test_surface_particle_deform_emits_runtime_geometry(self) -> None:
        deform = (ROOT / "src" / "renderer" / "tr_deform.cpp").read_text(encoding="utf-8")
        start = deform.index("static void R_ParticleDeform")
        end = deform.index("//========================================================================================", start)
        body = deform[start:end]
        for token in (
            "stage->CreateParticle(",
            "idWinding::TriangleArea(",
            "vertexCache.AllocFrameTemp(",
            "R_AddDrawSurf(",
            "stage->particleLife <= 0.0f",
        ):
            self.assertIn(token, body)

    def test_particle_seeds_and_geometry_counts_are_overflow_safe(self) -> None:
        local = (ROOT / "src" / "renderer" / "tr_local.h").read_text(encoding="utf-8")
        deform = (ROOT / "src" / "renderer" / "tr_deform.cpp").read_text(encoding="utf-8")
        model = (ROOT / "src" / "renderer" / "Model_prt.cpp").read_text(encoding="utf-8")

        seed_start = local.index("ID_INLINE int R_ParticleCycleSeed")
        count_start = local.index("ID_INLINE bool R_GetParticleGeometryCounts", seed_start)
        seed_helper = local[seed_start:count_start]
        count_end = local.index("void R_DeformDrawSurf", count_start)
        count_helper = local[count_start:count_end]

        self.assertIn("static_cast<unsigned int>( stageCycle )", seed_helper)
        self.assertIn("previousCycle ? 1u : 0u", seed_helper)
        self.assertIn("cycleBits << 10", seed_helper)
        self.assertIn("seedBits & 0x80000000u", seed_helper)
        self.assertIn("const int64 totalQuads", count_helper)
        self.assertIn("allocationLimit / vertexBytesPerQuad", count_helper)
        self.assertIn("allocationLimit / indexBytesPerQuad", count_helper)
        self.assertIn("allocationLimit / planeBytesPerQuad", count_helper)

        for source in (deform, model):
            self.assertIn("R_ParticleCycleSeed( stageCycle, false, diversitySeed )", source)
            self.assertIn("R_ParticleCycleSeed( stageCycle, true, diversitySeed )", source)
            self.assertIn("R_GetParticleGeometryCounts(", source)
            self.assertNotIn("stageCycle << 10", source)
            self.assertNotIn("( stageCycle - 1 ) << 10", source)

        self.assertIn("const double scaledParticles", deform)
        self.assertIn("scaledParticles > static_cast<double>( idMath::INT_MAX )", deform)
        self.assertIn("R_FrameAlloc( geometryCounts.vertexBytes )", deform)
        self.assertIn("R_FrameAlloc( geometryCounts.indexBytes )", deform)
        self.assertIn("numAllocedVerts < geometryCounts.numVerts", model)
        self.assertIn("numAllocedIndices < geometryCounts.numIndexes", model)

    def test_cached_particle_surfaces_follow_reloaded_declarations(self) -> None:
        model = (ROOT / "src" / "renderer" / "Model_prt.cpp").read_text(encoding="utf-8")
        prune_start = model.index(
            "for ( int surfaceNum = staticModel->surfaces.Num() - 1; surfaceNum >= 0; --surfaceNum )"
        )
        stage_start = model.index(
            "for ( int stageNum = 0; stageNum < particleSystem->stages.Num(); ++stageNum )",
            prune_start,
        )
        prune = model[prune_start:stage_start]
        self.assertIn("const int surfaceId = staticModel->surfaces[ surfaceNum ].id;", prune)
        self.assertIn("surfaceId < 0 || surfaceId >= particleSystem->stages.Num()", prune)
        self.assertIn("staticModel->DeleteSurfaceWithId( surfaceId );", prune)

        reuse_start = model.index("if ( staticModel->FindSurfaceWithId( stageNum, surfaceNum ) )", stage_start)
        emit_start = model.index("int numVerts = 0;", reuse_start)
        reuse = model[reuse_start:emit_start]
        self.assertIn("numAllocedVerts < geometryCounts.numVerts", reuse)
        self.assertIn("numAllocedIndices < geometryCounts.numIndexes", reuse)
        self.assertIn("R_AllocStaticTriSurfVerts( surf->geometry, geometryCounts.numVerts );", reuse)
        self.assertIn("R_AllocStaticTriSurfIndexes( surf->geometry, geometryCounts.numIndexes );", reuse)
        self.assertIn("R_AllocStaticTriSurfPlanes( surf->geometry, geometryCounts.numIndexes );", reuse)
        self.assertEqual(1, reuse.count("surf->shader = stage->material;"))
        self.assertIn("\t\t}\n\t\t// A declaration reload", reuse)
        self.assertLess(
            reuse.index("\t\t}\n\t\t// A declaration reload"),
            reuse.index("surf->shader = stage->material;"),
        )

    def test_dormant_quake4_arb_programs_are_loaded_on_demand(self) -> None:
        arb = (ROOT / "src" / "renderer" / "draw_arb2.cpp").read_text(encoding="utf-8")
        init = (ROOT / "src" / "renderer" / "RenderSystem_init.cpp").read_text(encoding="utf-8")
        eager_start = arb.index("static bool RB_ShouldEagerLoadARBProgram")
        eager_end = arb.index("static GLuint RB_CurrentInteractionProgramIdent", eager_start)
        eager = arb[eager_start:eager_end]
        self.assertIn("RB_UseSimpleInteractionShader()", eager)
        self.assertIn("VPROG_GLASSWARP", eager)
        self.assertIn("FPROG_GLASSWARP", eager)
        self.assertIn("RB_IsDormantMD5RProgram", eager)
        self.assertIn("loadAttempted", arb)
        self.assertIn("if ( prog != NULL && !prog->loadAttempted )", arb)

        load_set_start = arb.index("static void RB_LoadARBProgramSet")
        startup_start = arb.index("void R_LoadARBProgramsForStartup", load_set_start)
        reload_start = arb.index("void R_ReloadARBPrograms_f", startup_start)
        report_start = arb.index("void R_ReportShaderPrograms_f", reload_start)
        load_set = arb[load_set_start:startup_start]
        startup = arb[startup_start:reload_start]
        reload_command = arb[reload_start:report_start]
        self.assertIn("!startupEagerOnly || RB_ShouldEagerLoadARBProgram", load_set)
        self.assertIn("RB_LoadARBProgramSet( true );", startup)
        self.assertIn("RB_LoadARBProgramSet( false );", reload_command)
        self.assertIn("R_LoadARBProgramsForStartup();", init)
        self.assertNotIn("R_ReloadARBPrograms_f( idCmdArgs() );", init)

        current_validation = arb[
            arb.index("static bool RB_CurrentInteractionProgramsValid") :
            arb.index("static void RB_ErrorIfDriverRequiredSimpleInteractionFailed")
        ]
        self.assertIn("const bool vertexValid = R_IsARBProgramValid", current_validation)
        self.assertIn("const bool fragmentValid = R_IsARBProgramValid", current_validation)

        report = arb[report_start : arb.index("void R_ARB2_Init", report_start)]
        validation = "const bool currentInteractionProgramsValid = RB_CurrentInteractionProgramsValid();"
        tally = "for ( int i = 0; i < MAX_GLPROGS && progs[i].name[0]; i++ )"
        self.assertIn(validation, report)
        self.assertLess(report.index(validation), report.index(tally))
        self.assertIn('currentInteractionProgramsValid ? "off" : "on"', report)

    def test_stock_material_and_model_diagnostics_are_bounded(self) -> None:
        material = (ROOT / "src" / "renderer" / "Material.cpp").read_text(encoding="utf-8")
        model = (ROOT / "src" / "renderer" / "Model.cpp").read_text(encoding="utf-8")
        init = (ROOT / "src" / "renderer" / "RenderSystem_init.cpp").read_text(encoding="utf-8")
        self.assertNotIn("has multiple stages with a texgen", material)
        self.assertIn("if ( poly->nverts < 3 )", model)
        self.assertIn("if ( poly->nverts > 3 )", model)
        self.assertNotIn("has too many verts for a poly", model)
        self.assertIn('GLCapabilityProbe_HasExtension( "GL_EXT_shared_texture_palette" )', init)
        self.assertIn('GLCapabilityProbe_HasExtension( "GL_ATI_fragment_shader" )', init)

    def test_retail_image_compatibility_is_exact_and_usage_neutral(self) -> None:
        image = (ROOT / "src" / "renderer" / "Image_load.cpp").read_text(encoding="utf-8")
        for token in (
            '{ "env/spheresky1", "env/spheresky" }',
            '{ "textures/organic_wall/bio_organic_062_local", "textures/organic_wall/bio_organic_062b_local" }',
            '{ "textures/organic_wall/kf_bio_organic_344_local", "textures/organic_wall/bio_organic_344_local" }',
            'idStr::Icmp( leafName, "unnamed" ) == 0',
            "R_ReplaceExactImageProgramToken(",
            "R_IsKnownMissingRetailImageLeaf(",
            "R_LoadImageForUsage( token.c_str()",
            "if ( !R_IsKnownMissingRetailImageLeaf( token.c_str() ) )",
            "return foundLeaf && foundMissingLeaf;",
            'R_IsKnownMissingRetailImageLeaf( "textures/organic_wall/bio_organic_308_local.tga" )',
            '!R_IsKnownMissingRetailImageLeaf( "textures/organic_wall/bio_organic_308_local_extra" )',
            "R_FillUsageNeutralMissingImage(",
        ):
            self.assertIn(token, image)

        mixed_program = 'addnormals( textures/mod/unknown_missing, textures/organic_wall/bio_organic_308_local )'
        mixed_index = image.index(mixed_program)
        self.assertIn(
            "assert( !R_ShouldSuppressMissingImageWarning(",
            image[mixed_index - 100 : mixed_index],
        )

    def test_openal_ogg_decode_uses_libvorbisfile(self) -> None:
        sample = (ROOT / "src" / "sound" / "OpenAL" / "AL_SoundSample.cpp").read_text(encoding="utf-8")
        self.assertIn("#include <vorbis/vorbisfile.h>", sample)
        self.assertIn("ov_open_callbacks(", sample)
        self.assertIn("ov_read(", sample)
        self.assertIn("Swap_IsBigEndian() ? 1 : 0", sample)
        self.assertNotIn("stb_vorbis_decode_memory(", sample)


if __name__ == "__main__":
    unittest.main(verbosity=2)
