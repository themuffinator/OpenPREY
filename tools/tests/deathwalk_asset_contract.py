#!/usr/bin/env python3
"""Focused contracts for standalone retail deathwalk asset compatibility."""

from __future__ import annotations

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MODEL_SOURCE = ROOT / "src" / "renderer" / "Model.cpp"
CM_LOAD_SOURCE = ROOT / "src" / "cm" / "CollisionModel_load.cpp"
CM_FILES_SOURCE = ROOT / "src" / "cm" / "CollisionModel_files.cpp"
CM_CONTENTS_SOURCE = ROOT / "src" / "cm" / "CollisionModel_contents.cpp"
CM_TRANSLATE_SOURCE = ROOT / "src" / "cm" / "CollisionModel_translate.cpp"


def cxx_function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function {signature!r}")
    open_brace = source.index("{", start)
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1 : index]
    raise AssertionError(f"Unclosed function {signature!r}")


class DeathwalkAssetContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.model_source = MODEL_SOURCE.read_text(encoding="utf-8", errors="replace")
        cls.cm_load_source = CM_LOAD_SOURCE.read_text(encoding="utf-8", errors="replace")
        cls.cm_files_source = CM_FILES_SOURCE.read_text(encoding="utf-8", errors="replace")
        cls.cm_contents_source = CM_CONTENTS_SOURCE.read_text(encoding="utf-8", errors="replace")
        cls.cm_translate_source = CM_TRANSLATE_SOURCE.read_text(encoding="utf-8", errors="replace")
        cls.lwo_conversion = cxx_function_body(
            cls.model_source, "bool idRenderModelStatic::ConvertLWOToModelSurfaces("
        )
        cls.full_model_name = cxx_function_body(
            cls.cm_load_source, "const char *idCollisionModelManagerLocal::GetFullModelName("
        )
        cls.load_model = cxx_function_body(
            cls.cm_load_source, "idCollisionModel *idCollisionModelManagerLocal::LoadModel("
        )
        cls.world_model = cxx_function_body(
            cls.cm_load_source, "idCollisionModelLocal *idCollisionModelManagerLocal::GetWorldModel("
        )
        cls.contents = cxx_function_body(
            cls.cm_contents_source, "int idCollisionModelManagerLocal::Contents("
        )
        cls.translation = cxx_function_body(
            cls.cm_translate_source, "void idCollisionModelManagerLocal::Translation("
        )

    def test_missing_uvs_are_quiet_only_for_entirely_collision_only_models(self) -> None:
        collision_only_material = cxx_function_body(
            self.model_source, "static bool R_IsCollisionOnlyLwoMaterial("
        )
        self.assertIn("allSurfacesAreCollisionOnly = true", self.lwo_conversion)
        self.assertIn("!material->IsDrawn()", collision_only_material)
        self.assertIn("!material->SurfaceCastsShadow()", collision_only_material)
        self.assertIn(
            "material->GetContentFlags() & CONTENTS_REMOVE_UTIL", collision_only_material
        )
        self.assertIn("!R_IsCollisionOnlyLwoMaterial( material )", self.lwo_conversion)
        self.assertIn("if ( !allSurfacesAreCollisionOnly )", self.lwo_conversion)
        self.assertIn("has bad or missing uv data", self.lwo_conversion)

        # Missing UVs still receive a valid zero texcoord for collision geometry.
        self.assertIn("numTVertexes = 1;", self.lwo_conversion)
        self.assertIn("Mem_ClearedAlloc( numTVertexes", self.lwo_conversion)

    def test_retail_deathwalk_world_name_is_canonicalized(self) -> None:
        alias = cxx_function_body(self.cm_load_source, "static bool CM_IsWorldModelAlias(")
        self.assertIn('idStr::Icmp( leafName, "dw_worldMap" ) == 0', alias)
        self.assertIn("CM_IsWorldModelAlias( leafName )", self.full_model_name)
        self.assertIn("canonicalModelName += WORLD_MODEL_NAME", self.full_model_name)
        self.assertIn("canonicalModelName = WORLD_MODEL_NAME", self.full_model_name)
        self.assertIn(
            "GetFullModelName( fileName, token.c_str(), fullModelName )", self.cm_files_source
        )
        self.assertIn("GetFullModelName( mapName, modelName, fullModelName )", self.load_model)

    def test_world_fallback_remains_scoped_and_reports_a_true_miss(self) -> None:
        # The resolved name is qualified with the active map; slot zero or any
        # retained model from an older map must never be used as a fallback.
        self.assertIn("mapName.IsEmpty()", self.world_model)
        self.assertIn(
            "GetFullModelName( mapName.c_str(), WORLD_MODEL_NAME, worldModelName )",
            self.world_model,
        )
        self.assertNotIn("models[0]", self.world_model.replace(" ", ""))

        for body in (self.contents, self.translation):
            with self.subTest(function="Contents" if body is self.contents else "Translation"):
                self.assertIn("model = GetWorldModel();", body)
                self.assertIn("no current world model is available", body)
                self.assertIn("common->Warning", body)


if __name__ == "__main__":
    unittest.main(verbosity=2)
