#!/usr/bin/env python3
"""Regression checks for macOS renderer startup guards from issue #73."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
ISSUE_COMMENTS = ("4730727130", "4749280134", "4749378065", "4874832470")


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def function_body(source: str, signature: str) -> str:
    start = source.find(signature)
    if start == -1:
        raise AssertionError(f"Missing function signature {signature!r}")

    depth = 0
    for index in range(start, len(source)):
        char = source[index]
        if char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[start : index + 1]

    raise AssertionError(f"Could not find end of function {signature!r}")


def source_section(source: str, start_marker: str, end_marker: str) -> str:
    start = source.find(start_marker)
    if start == -1:
        raise AssertionError(f"Missing section marker {start_marker!r}")

    end = source.find(end_marker, start)
    if end == -1:
        raise AssertionError(f"Missing section end marker {end_marker!r}")

    return source[start:end]


def validate_renderer_entry_point_guards() -> None:
    source = read("src/renderer/RenderSystem_init.cpp")
    multitexture_guard = function_body(source, "static bool R_HasARBMultitextureEntryPoints() {")
    buffer_guard = function_body(source, "static bool R_HasARBBufferObjectEntryPoints() {")
    vbo_guard = function_body(source, "static bool R_HasARBVertexBufferObjectEntryPoints() {")
    pbo_guard = function_body(source, "static bool R_HasARBPixelBufferObjectEntryPoints() {")
    arb_guard = function_body(source, "static bool R_HasARBProgramEntryPoints() {")
    glsl_guard = function_body(source, "static bool R_HasGLSLProgramEntryPoints() {")
    glsl_cap = function_body(source, "static bool R_CanUseGLSLPrograms() {")

    for token in (
        "glActiveTextureARB != NULL",
        "glClientActiveTextureARB != NULL",
        "glMultiTexCoord2fARB != NULL",
    ):
        require(multitexture_guard, token, "ARB multitexture entry-point guard")

    for token in (
        "glBindBufferARB != NULL",
        "glDeleteBuffersARB != NULL",
        "glGenBuffersARB != NULL",
        "glBufferDataARB != NULL",
    ):
        require(buffer_guard, token, "ARB buffer-object entry-point guard")

    for token in (
        "R_HasARBBufferObjectEntryPoints()",
        "glBufferSubDataARB != NULL",
    ):
        require(vbo_guard, token, "ARB VBO entry-point guard")

    for token in (
        "R_HasARBBufferObjectEntryPoints()",
        "glMapBufferARB != NULL",
        "glUnmapBufferARB != NULL",
    ):
        require(pbo_guard, token, "ARB PBO entry-point guard")

    for token in (
        "glBindProgramARB != NULL",
        "glProgramStringARB != NULL",
        "glProgramEnvParameter4fvARB != NULL",
        "glProgramLocalParameter4fvARB != NULL",
        "glVertexAttribPointerARB != NULL",
        "glEnableVertexAttribArrayARB != NULL",
        "glDisableVertexAttribArrayARB != NULL",
    ):
        require(arb_guard, token, "ARB program entry-point guard")

    for token in (
        "glCreateShaderObjectARB != NULL",
        "glShaderSourceARB != NULL",
        "glCompileShaderARB != NULL",
        "glGetObjectParameterivARB != NULL",
        "glGetInfoLogARB != NULL",
        "glCreateProgramObjectARB != NULL",
        "glAttachObjectARB != NULL",
        "glDetachObjectARB != NULL",
        "glBindAttribLocationARB != NULL",
        "glLinkProgramARB != NULL",
        "glUseProgramObjectARB != NULL",
        "glGetUniformLocationARB != NULL",
        "glUniform1fARB != NULL",
        "glUniform1fvARB != NULL",
        "glUniform1iARB != NULL",
        "glUniform2fvARB != NULL",
        "glUniform3fvARB != NULL",
        "glUniform4fvARB != NULL",
        "glUniformMatrix4fvARB != NULL",
        "glVertexAttribPointerARB != NULL",
        "glEnableVertexAttribArrayARB != NULL",
        "glDisableVertexAttribArrayARB != NULL",
        "glDeleteObjectARB != NULL",
    ):
        require(glsl_guard, token, "GLSL entry-point guard")

    require(glsl_cap, "R_HasGLSLProgramEntryPoints()", "GLSL capability gate")

    for token in (
        "glConfig.multitextureAvailable && !R_HasARBMultitextureEntryPoints()",
        'R_RecordMissingRequiredOpenGLFeature( "GL_ARB_multitexture entry points" )',
        "glConfig.ARBVertexBufferObjectAvailable && !R_HasARBVertexBufferObjectEntryPoints()",
        "using virtual-memory vertex cache",
        "pixelBufferObjectAdvertised && !glConfig.pixelBufferObjectAvailable",
        "async readbacks disabled",
        "glConfig.ARBVertexProgramAvailable && !R_HasARBProgramEntryPoints()",
        'R_RecordMissingRequiredOpenGLFeature( "GL_ARB_vertex_program entry points" )',
        "glConfig.ARBFragmentProgramAvailable && !R_HasARBProgramEntryPoints()",
        'R_RecordMissingRequiredOpenGLFeature( "GL_ARB_fragment_program entry points" )',
        "glConfig.ARBVertexBufferObjectAvailable && !glConfig.backendCaps.hasVBO",
        "GL_ARB_vertex_buffer_object disabled by renderer driver quirk",
    ):
        require(source, token, "renderer startup capability fallback")


def validate_apple_gl21_vbo_quirk() -> None:
    header = read("src/renderer/RendererCaps.h")
    source = read("src/renderer/RendererCaps.cpp")
    apply_body = function_body(source, "void RendererDriverQuirks_Apply( renderBackendCaps_t &caps, const rendererDriverInfo_t &driverInfo ) {")
    self_test = function_body(source, "bool RendererCompatibilityGates_RunSelfTest( void ) {")

    for token in (
        "RENDERER_DRIVER_QUIRK_DISABLE_VBO",
        "1u << 6",
        "RENDERER_DRIVER_QUIRK_PREFER_SIMPLE_INTERACTION",
        "1u << 7",
        "RENDERER_DRIVER_QUIRK_DISABLE_ARB2_INTERACTIONS",
        "1u << 8",
    ):
        require(header, token, "renderer driver quirk flags")

    for token in (
        '"Apple"',
        "RendererDriverQuirk_ParseGLVersionPrefix",
        "RendererDriverQuirk_IsAppleGL21CompatibilityFallback",
        "selectedCompatibility",
        "Apple OpenGL 2.1 compatibility path uses a CPU-backed vertex cache with automatic stock GLSL interactions, simple ARB per-surface fallback, and an emergency interaction bypass",
        '{ RENDERER_DRIVER_QUIRK_DISABLE_VBO, "disableVBO" }',
        '{ RENDERER_DRIVER_QUIRK_PREFER_SIMPLE_INTERACTION, "preferSimpleInteraction" }',
        '{ RENDERER_DRIVER_QUIRK_DISABLE_ARB2_INTERACTIONS, "disableARB2Interactions" }',
        "selected=%s baseline=%d VBO=%d PBO=%d",
        "selectedContext=%d.%d %s fixedFunction=%d VBO:%d->%d PBO:%d->%d simpleInteraction=%d ARB2:%d->%d",
    ):
        require(source, token, "Apple GL 2.1 VBO driver quirk")

    require(apply_body, "caps.hasVBO = false", "Apple GL 2.1 VBO driver quirk application")
    for token in (
        '"Apple M4 Max"',
        '"2.1 Metal"',
        "RENDERER_DRIVER_QUIRK_DISABLE_VBO",
        "RENDERER_DRIVER_QUIRK_PREFER_SIMPLE_INTERACTION",
        "RENDERER_DRIVER_QUIRK_DISABLE_ARB2_INTERACTIONS",
        "expectedFlags",
        "caps.hasVBO != quirkCasesTable[i].expectedVBO",
    ):
        require(self_test, token, "renderer compatibility self-test Apple GL 2.1 VBO quirk")


def validate_disabled_upload_bridge_state() -> None:
    source = read("src/renderer/RendererUpload.cpp")
    init_body = function_body(source, "void idUploadManager::Init( const renderBackendCaps_t &caps ) {")
    delete_helper = function_body(source, "static void R_RendererUpload_DeleteBufferName( unsigned int &vbo ) {")

    for token in (
        "if ( requestedPath == UPLOAD_PATH_DISABLED )",
        "frameBufferCount = 0;",
        "const int activeRingBytes = requestedPath == UPLOAD_PATH_DISABLED ? 0 : ringBytes;",
        "allocator.Init( activeRingBytes, requestedPath == UPLOAD_PATH_PERSISTENT )",
        "ring.Init( activeRingBytes, requestedPath == UPLOAD_PATH_PERSISTENT )",
        "hasSync = requestedPath != UPLOAD_PATH_DISABLED",
        'const char *bridgeMode = "disabled"',
        'bridgeMode = "streaming"',
        "activeRingBytes / 1024",
    ):
        require(init_body, token, "disabled renderer upload bridge state")

    for token in (
        "if ( glDeleteBuffersARB != NULL )",
        "glDeleteBuffersARB( 1, &vbo )",
        "vbo = 0;",
    ):
        require(delete_helper, token, "renderer upload guarded buffer delete")

    if source.count("glDeleteBuffersARB") != 2:
        raise AssertionError("Renderer upload cleanup must route buffer deletes through R_RendererUpload_DeleteBufferName")
    if source.count("R_RendererUpload_DeleteBufferName(") < 5:
        raise AssertionError("Renderer upload cleanup no longer covers every expected buffer delete site")


def validate_vertex_cache_bind_entry_point_guards() -> None:
    source = read("src/renderer/VertexCache.cpp")
    bind_array = function_body(source, "void idVertexCache::BindArrayBuffer( GLuint vbo ) {")
    bind_index = function_body(source, "void idVertexCache::BindIndexBuffer( GLuint vbo ) {")

    for body, context, shadow in (
        (bind_array, "array-buffer bind wrapper", "vc_boundArrayBuffer = VERTCACHE_BIND_UNKNOWN;"),
        (bind_index, "index-buffer bind wrapper", "vc_boundIndexBuffer = VERTCACHE_BIND_UNKNOWN;"),
    ):
        for token in (
            "glBindBufferARB == NULL",
            shadow,
            "return;",
        ):
            require(body, token, context)


def validate_hdr_pbo_uses_portable_guard() -> None:
    source = read("src/renderer/draw_common.cpp")
    readback = source_section(
        source,
        "const bool asyncReadbackSupported = glConfig.pixelBufferObjectAvailable;",
        "rbHDRExposureReadbackIndex = readIndex;",
    )
    shutdown = source_section(
        source,
        "if ( rbHDRExposureReadbackPBOs[0] != 0 ) {",
        "rbHDRExposureReadbackPBOs[0] = 0;",
    )

    for token in (
        "glConfig.pixelBufferObjectAvailable",
        "bool hdrAsyncReadbackActive",
        "glGenBuffersARB",
        "rbHDRExposureReadbackPBOs[i] == 0",
        "hdrAsyncReadbackActive = false",
        "glDeleteBuffersARB( 2, rbHDRExposureReadbackPBOs )",
        "rbHDRExposureReadbackPBOs[0] = 0",
        "rbHDRExposureReadbackPBOs[1] = 0",
        "glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, 0 )",
        "glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB",
        "glBufferDataARB( GL_PIXEL_PACK_BUFFER_ARB",
        "glMapBufferARB( GL_PIXEL_PACK_BUFFER_ARB",
        "glUnmapBufferARB( GL_PIXEL_PACK_BUFFER_ARB )",
    ):
        require(readback, token, "HDR auto-exposure portable PBO guard")

    for token in (
        "glDeleteBuffersARB != NULL",
        "glDeleteBuffersARB( 2, rbHDRExposureReadbackPBOs )",
    ):
        require(shutdown, token, "HDR auto-exposure guarded PBO teardown")

    for token in (
        "GLEW_VERSION_2_1",
        "GLEW_ARB_pixel_buffer_object",
        "glGenBuffers( 2, rbHDRExposureReadbackPBOs )",
        "glBindBuffer( GL_PIXEL_PACK_BUFFER",
        "glMapBuffer( GL_PIXEL_PACK_BUFFER",
        "glDeleteBuffers( 2, rbHDRExposureReadbackPBOs )",
    ):
        if token in readback or token in shutdown:
            raise AssertionError(f"HDR auto-exposure PBO path bypasses portable guard: {token!r}")


def validate_lightgrid_pbo_fails_closed() -> None:
    source = read("src/renderer/RenderWorld_lightgrid.cpp")
    use_async = function_body(source, "static bool LightGrid_UseAsyncReadback( int captureSize, int blends ) {")
    constructor = function_body(source, "LightGridBakeReadbackPool( int requestedSlotCount, int captureSize, int captureBytes )")
    destructor = function_body(source, "~LightGridBakeReadbackPool() {")

    require(use_async, "glConfig.pixelBufferObjectAvailable", "light-grid async readback PBO gate")
    for token in (
        "glGenBuffersARB( slotCount, pboIds.Ptr() )",
        "if ( pboIds[ i ] == 0 )",
        "glDeleteBuffersARB != NULL",
        "slots.Clear()",
        "glBindBufferARB( GL_PIXEL_PACK_BUFFER_ARB, 0 )",
        "return;",
    ):
        require(constructor, token, "light-grid async readback PBO allocation fallback")

    for token in (
        "glDeleteBuffersARB != NULL",
        "glDeleteBuffersARB( slots.Num(), pboIds.Ptr() )",
    ):
        require(destructor, token, "light-grid async readback guarded PBO teardown")


def validate_classic_arb2_vbo_offset_pointers() -> None:
    source = read("src/renderer/draw_arb2.cpp")
    vertex_cache_header = read("src/renderer/VertexCache.h")
    helper = function_body(vertex_cache_header, "ID_INLINE void *RB_DrawVertAttributePointer( const void *base, const size_t byteOffset ) {")
    classic_interactions = source_section(
        source,
        "// set the vertex pointers",
        "// this may cause RB_ARB2_DrawInteraction",
    )

    require(helper, "reinterpret_cast<uintptr_t>( base ) + byteOffset", "draw-vertex VBO offset helper")
    for token in (
        "RB_DrawVertAttributePointer( ac, DRAWVERT_COLOR_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_NORMAL_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT1_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_TANGENT0_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_ST_OFFSET )",
        "RB_DrawVertAttributePointer( ac, DRAWVERT_XYZ_OFFSET )",
    ):
        require(classic_interactions, token, "classic ARB2 interaction VBO attribute pointers")

    for token in (
        "ac->color",
        "ac->normal.ToFloatPtr()",
        "ac->tangents[1].ToFloatPtr()",
        "ac->tangents[0].ToFloatPtr()",
        "ac->st.ToFloatPtr()",
        "ac->xyz.ToFloatPtr()",
    ):
        if token in classic_interactions:
            raise AssertionError(f"Classic ARB2 interactions must not form field pointers through VBO offsets: {token!r}")


def validate_apple_gl21_simple_interaction_fallback() -> None:
    renderer_header = read("src/renderer/RenderSystem.h")
    arb2 = read("src/renderer/draw_arb2.cpp")
    common = read("src/renderer/draw_common.cpp")
    gfx_info = read("src/renderer/RenderSystem_init.cpp")

    for token in (
        "preferSimpleInteraction",
        "driver quirk fallback for fragile ARB interaction uploads",
        "disableARB2Interactions",
        "driver quirk fallback for fragile ARB2 light-interaction draws",
    ):
        require(renderer_header, token, "glconfig simple interaction fallback state")

    for token in (
        "static bool RB_DriverPrefersSimpleInteraction( void )",
        "RENDERER_DRIVER_QUIRK_PREFER_SIMPLE_INTERACTION",
        "static bool RB_ShouldSkipFullInteractionUpload( const progDef_t &prog )",
        "skipped by renderer driver quirk; using SimpleInteraction.vfp",
        "prog.ident == VPROG_SIMPLE_INTERACTION",
        "prog.ident == FPROG_SIMPLE_INTERACTION",
        "idStr::Icmp( prog.name, \"SimpleInteraction.vfp\" ) == 0",
        "glConfig.preferSimpleInteraction = true",
        "prefers simple ARB interaction shader for compatibility",
        "g_appleGL21AutomaticInteractionPath = true",
        "automatic stock GLSL interactions with simple ARB per-surface fallback",
        "RB_SurfaceEligibleForAppleGL21StockGLSLInteraction",
        "RB_GLSLMaterial_CreateDrawInteractions( &singleSurf, true )",
        "RB_ARB2_CreateDrawInteractions( &singleSurf )",
        "glConfig.disableARB2Interactions = true",
        "emergency ARB2 light-interaction bypass",
        "Apple OpenGL 2.1 compatibility path bypassing ARB2 light interactions",
        "RENDERER_STARTUP_PHASE_ARB2_INTERACTION_DRIVER_BYPASS",
    ):
        require(arb2, token, "Apple GL 2.1 simple interaction fallback")

    force_ambient = function_body(common, "static void RB_STD_ForceAmbient( void ) {")
    for token in (
        "glConfig.preferSimpleInteraction",
        "glConfig.disableARB2Interactions",
        "VPROG_SIMPLE_INTERACTION",
        "FPROG_SIMPLE_INTERACTION",
    ):
        require(force_ambient, token, "force ambient interaction rescue family selection")

    require(
        gfx_info,
        "Apple GL 2.1 automatic stock GLSL interactions active with simple ARB per-surface fallback",
        "gfxInfo automatic Apple GL 2.1 interactions",
    )
    require(
        gfx_info,
        "Simple ARB interaction per-surface fallback armed for this renderer",
        "gfxInfo simple interaction fallback",
    )
    require(
        gfx_info,
        "ARB2 light interaction compatibility bypass active for this renderer",
        "gfxInfo ARB2 interaction bypass",
    )


def validate_docs_and_validation() -> None:
    validator = read("tools/validation/openq4_validate.py")
    platform = read("docs/dev/platform-support.md")
    status_ledger = read("docs/dev/prey-rebase/status-ledger.md")

    require(validator, "macos_renderer_startup_guard.py", "validation runner")
    require(platform, "OpenGL is the first compatibility target", "platform support docs")
    require(platform, "Prey runtime and package evidence pending", "platform support docs")
    require(status_ledger, "TODO-RELEASE-LANES", "macOS lane deferral")


def main() -> None:
    validate_renderer_entry_point_guards()
    validate_apple_gl21_vbo_quirk()
    validate_disabled_upload_bridge_state()
    validate_vertex_cache_bind_entry_point_guards()
    validate_hdr_pbo_uses_portable_guard()
    validate_lightgrid_pbo_fails_closed()
    validate_classic_arb2_vbo_offset_pointers()
    validate_apple_gl21_simple_interaction_fallback()
    validate_docs_and_validation()
    print("macos_renderer_startup_guard: ok")


if __name__ == "__main__":
    main()
