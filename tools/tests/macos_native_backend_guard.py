#!/usr/bin/env python3
"""Regression checks for native macOS backend crash guards."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]


def read(relative_path: str) -> str:
    return (ROOT / relative_path).read_text(encoding="utf-8")


def require(haystack: str, needle: str, context: str) -> None:
    if needle not in haystack:
        raise AssertionError(f"Missing {needle!r} in {context}")


def reject(haystack: str, needle: str, context: str) -> None:
    if needle in haystack:
        raise AssertionError(f"Unexpected {needle!r} in {context}")


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


def strip_if0_blocks(source: str) -> str:
    kept: list[str] = []
    disabled_depth = 0
    for line in source.splitlines(keepends=True):
        stripped = line.strip()
        if disabled_depth > 0:
            if stripped.startswith("#if"):
                disabled_depth += 1
            elif stripped.startswith("#endif"):
                disabled_depth -= 1
            continue
        if stripped.startswith("#if 0"):
            disabled_depth = 1
            continue
        kept.append(line)
    return "".join(kept)


def validate_macos_source_manifest() -> None:
    sources = read("tools/build/meson_sources.py")

    for token in (
        "SDL3_DARWIN_SOURCES",
        "DARWIN_PLATFORM_SOURCES",
        '"sys/osx/macosx_compat.mm"',
        '"sys/osx/macosx_event.mm"',
        '"sys/osx/macosx_glimp.mm"',
        '"sys/osx/macosx_misc.mm"',
        '"sys/osx/macosx_sys.mm"',
        '"sys/osx/macosx_sdl3.cpp"',
        '"sys/osx/macosx_sdl3_main.cpp"',
    ):
        require(sources, token, "macOS Meson source manifest")
    for legacy_source in (
        "DOOMController.mm",
        "PreferencesDialog.cpp",
        "PickMonitor.cpp",
        "macosx_sound.cpp",
        "macosx_utils.mm",
    ):
        reject(sources, legacy_source, "macOS Meson source manifest")


def validate_no_live_raw_string_builders() -> None:
    for relative_path in (
        "src/sys/osx/macosx_compat.mm",
        "src/sys/osx/macosx_event.mm",
        "src/sys/osx/macosx_glimp.mm",
        "src/sys/osx/macosx_misc.mm",
        "src/sys/osx/macosx_sys.mm",
        "src/sys/osx/macosx_sdl3.cpp",
        "src/sys/osx/macosx_sdl3_main.cpp",
    ):
        live_source = strip_if0_blocks(read(relative_path))
        for pattern in (r"\bstrcpy\s*\(", r"\bstrcat\s*\(", r"\bsprintf\s*\(", r"\bvsprintf\s*\(", r"\balloca\s*\("):
            if re.search(pattern, live_source):
                raise AssertionError(f"Live raw string builder {pattern!r} found in {relative_path}")


def validate_native_cocoa_context() -> None:
    source = read("src/sys/osx/macosx_glimp.mm")
    accessor = function_body(source, "- (CGLContextObj) cglContext;")
    ladder = function_body(source, "static bool OSX_CreateContextWithLadder( unsigned int &selectedMultisamples, NSOpenGLPixelFormat **selectedPixelFormat ) {")
    require(accessor, "return [self CGLContextObj];", "native macOS CGL context accessor")
    require(ladder, "context != nil && [context cglContext] != NULL", "native macOS CGL context validation")
    require(ladder, "[context release];", "native macOS failed context cleanup")

    set_context_macro = read("src/sys/osx/macosx_native_gl.h")
    require(set_context_macro, "glw_state._cgl_ctx = [_context cglContext];", "native macOS context storage")
    require(set_context_macro, "glw_state._ctx_is_current = NO;", "native macOS context current-state reset")


def validate_native_fullscreen_failures() -> None:
    source = read("src/sys/osx/macosx_glimp.mm")
    create_window = function_body(source, "static bool CreateGameWindow(  glimpParms_t parms ) {")
    pixel_attrs = function_body(source, "static NSOpenGLPixelFormatAttribute *GetPixelAttributes( unsigned int multisamples, const rendererContextCandidate_t &candidate ) {")
    pixel_format = function_body(source, "static NSOpenGLPixelFormat *OSX_CreatePixelFormatForCandidate( unsigned int &multisamples, const rendererContextCandidate_t &candidate ) {")
    desktop_resolution = function_body(source, "bool Sys_GetDesktopResolution( int *width, int *height ) {")
    check_errors = function_body(source, "void CheckErrors( void ) {")

    for token in (
        "if ( !glw_state.desktopMode ) {",
        "if (!glw_state.gameMode) {",
        "return false;",
        "if ( err != CGDisplayNoErr )",
        "CGLSetFullScreen(OSX_GetCGLContext())",
        "OSX_ReleaseGLContext();",
        "glw_state.window = [[NSWindow alloc] initWithContentRect",
        "No NSWindow/content view available for OpenGL context",
    ):
        require(create_window, token, "native macOS window creation failure handling")
    require(create_window, "if ( glw_state.display == 0 )", "native macOS window creation display guard")
    require(create_window, "NSArray *screens = [NSScreen screens];", "native macOS NSScreen array guard")
    require(create_window, "displayCount = [screens count];", "native macOS NSScreen array guard")
    require(create_window, "screen = [screens objectAtIndex:static_cast<NSUInteger>( displayIndex )];", "native macOS NSScreen index guard")
    require(create_window, "if ( screen == nil && displayCount > 0 )", "native macOS NSScreen fallback guard")
    reject(create_window, "[[NSScreen screens] objectAtIndex", "native macOS NSScreen index guard")
    require(create_window, 'OSX_EnsureGLContextCurrent( "create window" )', "native macOS window creation current-context guard")
    require(create_window, 'if ( !OSX_EnsureGLContextCurrent( "create window" ) ) {\n\t\tOSX_ReleaseGLContext();', "native macOS window creation failed-current context release")

    require(desktop_resolution, "if ( display == 0 ) {", "native macOS desktop-resolution display guard")
    require(check_errors, 'common->Error( "glGetError: 0x%04x\\n"', "native macOS GL error report")
    reject(check_errors, "glGetString( err )", "native macOS GL error string guard")

    for token in (
        "const CGDirectDisplayID display = glw_state.display != 0 ? glw_state.display : Sys_DisplayToUse();",
        "if ( display == 0 )",
        "CGDisplayIDToOpenGLDisplayMask(display)",
        "if ( pixelAttributes == NULL )",
        "return NULL;",
    ):
        require(pixel_attrs, token, "native macOS pixel attribute allocation guard")
    for token in (
        "newPixelAttributes == NULL",
        "NSZoneFree(NULL, pixelAttributes);",
        "return NULL;",
    ):
        require(source, token, "native macOS pixel attribute allocation macro guard")
    require(pixel_format, "if ( pixelAttributes == NULL )", "native macOS pixel format allocation guard")
    require(pixel_format, "return nil;", "native macOS pixel format allocation guard")

    release_displays = function_body(source, "static void ReleaseAllDisplays() {")
    require(release_displays, "glw_state.originalDisplayGammaTables == NULL", "native macOS safe display release")
    require(release_displays, "display != 0", "native macOS safe display release")

    capture_displays = function_body(source, "CGDisplayErr Sys_CaptureActiveDisplays(void) {")
    require(capture_displays, "for ( CGDisplayCount releaseIndex = 0; releaseIndex < displayIndex; ++releaseIndex )", "native macOS partial display-capture unwind")
    require(capture_displays, "CGDisplayRelease( capturedDisplay );", "native macOS partial display-capture unwind")

    release_context = function_body(source, "static void OSX_ReleaseGLContext() {")
    require(release_context, "OSX_SetGLContext((id)nil);", "native macOS context release reset")
    require(release_context, "OSX_GetCGLContext() != NULL", "native macOS context release reset")
    require(release_context, "[OSX_GetNSGLContext() release];", "native macOS context release reset")

    ensure_context = function_body(source, "static bool OSX_EnsureGLContextCurrent( const char *operation ) {")
    require(ensure_context, "OSX_GetNSGLContext() == nil || OSX_GetCGLContext() == NULL", "native macOS current-context helper")
    require(ensure_context, "[NSOpenGLContext currentContext] == OSX_GetNSGLContext()", "native macOS current-context helper")
    require(ensure_context, "[OSX_GetNSGLContext() makeCurrentContext];", "native macOS current-context helper")
    require(ensure_context, "glw_state._ctx_is_current = false;", "native macOS current-context helper")
    require(ensure_context, "glw_state._ctx_is_current = true;", "native macOS current-context helper")


def validate_hide_unhide_and_stubs() -> None:
    source = read("src/sys/osx/macosx_glimp.mm")
    pause = function_body(source, "void Sys_PauseGL () {")
    resume = function_body(source, "void Sys_ResumeGL () {")
    restore = function_body(source, "static void _GLimp_RestoreOriginalVideoSettings() {")
    swap = function_body(source, "void GLimp_SwapBuffers( void ) {")
    shutdown = function_body(source, "void GLimp_Shutdown( void ) {")
    hide = function_body(source, "bool Sys_Hide() {")
    unhide = function_body(source, "bool Sys_Unhide() {")
    spawn = function_body(source, "bool GLimp_SpawnRenderThread( void (*function)( void ) ) {")
    renderer_sleep = function_body(source, "void *GLimp_RendererSleep(void) {")
    backend_sleep = function_body(source, "void *GLimp_BackEndSleep( void ) {")
    screen_parms = function_body(source, "bool GLimp_SetScreenParms( glimpParms_t parms ) {")
    activate = function_body(source, "void GLimp_ActivateContext( void ) {")
    deactivate = function_body(source, "void GLimp_DeactivateContext( void ) {")

    for token in ("return true;", "return false;", "OSX_GetCGLContext() == NULL"):
        require(hide, token, "native macOS hide guard")
        require(unhide, token, "native macOS unhide guard")
    require(unhide, "glw_state.display == 0", "native macOS unhide display guard")

    for body, context in ((pause, "native macOS GL pause guard"), (resume, "native macOS GL resume guard")):
        require(body, "OSX_GetNSGLContext() == nil || OSX_GetCGLContext() == NULL", context)
        require(body, "return;", context)
    require(pause, 'OSX_EnsureGLContextCurrent( "pause OpenGL" )', "native macOS GL pause current-context guard")
    require(resume, 'OSX_EnsureGLContextCurrent( "resume OpenGL" )', "native macOS GL resume current-context guard")
    require(restore, "glw_state.display != 0 && glw_state.desktopMode != nil", "native macOS restore display guard")
    require(swap, 'OSX_EnsureGLContextCurrent( "swap buffers" )', "native macOS swap context guard")
    require(swap, "return;", "native macOS swap context guard")
    require(shutdown, "OSX_ReleaseGLContext();", "native macOS shutdown context release")
    require(shutdown, "OSX_ResetGammaTable( &glw_state.tempTable );", "native macOS shutdown gamma reset")
    require(shutdown, "glw_state.originalDisplayGammaTables = NULL;", "native macOS shutdown gamma reset")
    require(hide, 'OSX_EnsureGLContextCurrent( "hide OpenGL" )', "native macOS hide current-context guard")
    require(unhide, 'OSX_EnsureGLContextCurrent( "unhide OpenGL" )', "native macOS unhide current-context guard")
    require(activate, 'OSX_EnsureGLContextCurrent( "activate context" )', "native macOS activate context guard")
    require(deactivate, "OSX_GetNSGLContext() == nil || OSX_GetCGLContext() == NULL", "native macOS deactivate context guard")
    require(deactivate, "[NSOpenGLContext currentContext] == OSX_GetNSGLContext()", "native macOS deactivate context ownership guard")
    require(deactivate, "[NSOpenGLContext clearCurrentContext];", "native macOS deactivate context ownership guard")
    require(deactivate, "glw_state._ctx_is_current = false;", "native macOS deactivate context state reset")

    reject(hide, "ReleaseAllDisplays();", "native macOS hide duplicate display release")
    require(spawn, "return false;", "native macOS render-thread unsupported stub")
    require(renderer_sleep, "return NULL;", "native macOS renderer sleep stub")
    require(backend_sleep, "return NULL;", "native macOS backend sleep stub")
    require(screen_parms, "return false;", "native macOS screen-parms unsupported stub")


def validate_display_and_gamma_guards() -> None:
    source = read("src/sys/osx/macosx_glimp.mm")
    glimp_init = function_body(source, "bool GLimp_Init( glimpParms_t parms ) {")
    matching_mode = function_body(source, "NSDictionary *Sys_GetMatchingDisplayMode( glimpParms_t parms ) {")
    get_gamma = function_body(source, "void Sys_GetGammaTable(glwgamma_t *table) {")
    store_gamma = function_body(source, "void Sys_StoreGammaTables() {")
    set_gamma = function_body(source, "void GLimp_SetGamma(unsigned short red[256],")
    fade = function_body(source, "void Sys_SetScreenFade(glwgamma_t *table, float fraction) {")
    fade_screen = function_body(source, "void Sys_FadeScreen(CGDirectDisplayID display) {")
    unfade_screen = function_body(source, "void Sys_UnfadeScreen(CGDirectDisplayID display, glwgamma_t *table) {")
    display = function_body(source, "CGDirectDisplayID Sys_DisplayToUse(void) {")
    extension = function_body(source, "void *GLimp_ExtensionPointer(const char *name) {")
    query_vram = function_body(source, "unsigned long Sys_QueryVideoMemory() {")

    require(glimp_init, "video memory query unavailable; continuing with OpenGL context creation", "native macOS VRAM query soft failure")
    reject(glimp_init, "Could not initialize OpenGL.  There does not appear to be an OpenGL-supported video card", "native macOS VRAM query soft failure")

    require(matching_mode, "return nil;", "native macOS display-mode lookup")
    require(matching_mode, "if ( glw_state.display == 0 ) {", "native macOS display-mode lookup")
    require(matching_mode, "if ( modeCount == 0 ) {", "native macOS display-mode empty-list guard")
    require(matching_mode, "bestModeIndex = NSNotFound;", "native macOS display-mode best-index guard")
    require(matching_mode, "bestModeIndex == NSNotFound || bestModeIndex >= modeCount", "native macOS display-mode best-index guard")
    require(matching_mode, "modeText != NULL ? modeText : \"\"", "native macOS display-mode description guard")
    require(matching_mode, "return [displayModes objectAtIndex: bestModeIndex];", "native macOS display-mode lookup")
    reject(matching_mode, "bestModeIndex = 0xFFFFFFFF;", "native macOS display-mode best-index guard")

    require(get_gamma, "if ( table->display == 0 ) {", "native macOS gamma table display guard")
    require(get_gamma, "OSX_FreeGammaTableStorage( table );", "native macOS gamma table cleanup")
    require(store_gamma, "if (err != CGDisplayNoErr || glw_state.displayCount == 0) {", "native macOS gamma table storage")
    require(store_gamma, "if ( glw_state.originalDisplayGammaTables != NULL ) {", "native macOS repeated gamma table storage")
    require(store_gamma, "OSX_ResetGammaTable( &glw_state.originalDisplayGammaTables[displayIndex] );", "native macOS repeated gamma table storage")
    require(store_gamma, "glw_state.originalDisplayGammaTables = (glwgamma_t *)calloc", "native macOS gamma table storage")
    require(store_gamma, "glw_state.displayCount = 0;", "native macOS gamma allocation failure")
    require(set_gamma, "red == NULL || green == NULL || blue == NULL || glw_state.display == 0", "native macOS gamma setter guard")

    for token in (
        "table == NULL",
        "table->display == 0",
        "table->red == NULL",
        "table->green == NULL",
        "table->blue == NULL",
        "CGGammaValue *newRed = (CGGammaValue *)malloc",
        "free(glw_state.tempTable.red);",
    ):
        require(fade, token, "native macOS gamma fade guard")
    require(fade_screen, "if ( display == 0 ) {", "native macOS single-display fade guard")
    require(unfade_screen, "if ( display == 0 ) {", "native macOS single-display unfade guard")

    require(display, "if ( gotDisplay ) {\n\t\treturn displayToUse;", "native macOS display cache return")
    require(display, "displayIndex >= 0 && displayIndex < static_cast<int>( displayCount ) && displays[displayIndex] != 0", "native macOS requested-display guard")
    require(display, "mainDisplay = CGMainDisplayID();", "native macOS display fallback")
    require(display, "if ( mainDisplay != 0 )", "native macOS display fallback")
    require(display, "for ( CGDisplayCount i = 0; i < displayCount; ++i )", "native macOS first-active-display fallback")
    require(display, "return displayToUse;", "native macOS display return")
    require(extension, "name == NULL || name[0] == '\\0'", "native macOS extension lookup guard")
    require(extension, "idStr symbolName;", "native macOS extension lookup storage")
    require(extension, "idMath::INT_MAX - 2", "native macOS extension lookup length guard")
    require(extension, "symbolName += name;", "native macOS extension lookup storage")
    require(extension, "symbolName.c_str()", "native macOS extension lookup storage")
    reject(extension, "alloca", "native macOS extension lookup stack allocation")
    reject(extension, "strcpy", "native macOS extension lookup raw copy")
    reject(extension, "glGenFragmentShadersATI", "native macOS extension lookup stale ATI shim")
    require(query_vram, "const CGDirectDisplayID display = Sys_DisplayToUse();", "native macOS renderer info guard")
    require(query_vram, "if ( display == 0 ) {", "native macOS renderer info guard")
    require(query_vram, "CGDisplayIDToOpenGLDisplayMask(display)", "native macOS renderer info guard")
    require(query_vram, "rendererInfo == NULL", "native macOS renderer info guard")
    require(query_vram, "unsigned long maxVRAM = 0;", "native macOS VRAM overflow guard")
    require(query_vram, "static_cast<unsigned long>(vramValue) * 1024UL * 1024UL", "native macOS VRAM overflow guard")


def validate_native_input_capture_guards() -> None:
    source = read("src/sys/osx/macosx_event.mm")
    update_rect = read("src/sys/osx/macosx_glimp.mm")

    require(source, "static CGRect\tinputRect\t\t= CGRectZero;", "native macOS input rect initialization")

    process_event = function_body(source, "void processEvent( NSEvent *event ) {")
    process_key = function_body(source, "void OSX_ProcessKeyEvent( NSEvent *keyEvent, bool keyDownFlag ) {")
    process_flags = function_body(source, "void processFlagsChangedEvent( NSEvent *flagsChangedEvent ) {")
    process_system = function_body(source, "void processSystemDefinedEvent( NSEvent *systemDefinedEvent ) {")
    post_wheel = function_body(source, "static void OSX_PostMouseWheelSteps( int wheelSteps ) {")
    lookup_character = function_body(source, "inline bool OSX_LookupCharacter(unsigned short vkey, unsigned int modifiers, bool keyDownFlag, unsigned char *outChar)")
    usable_rect = function_body(source, "static bool Sys_InputRectIsUsable( CGRect rect ) {")
    prevent = function_body(source, "bool Sys_PreventMouseMovement( CGPoint point ) {")
    reenable = function_body(source, "bool Sys_ReenableMouseMovement() {")
    lock = function_body(source, "bool Sys_LockMouseInInputRect(CGRect rect) {")
    set_rect = function_body(source, "void Sys_SetMouseInputRect(CGRect newRect) {")
    activate = function_body(source, "void IN_ActivateMouse( void ) {")
    deactivate = function_body(source, "void IN_DeactivateMouse( void ) {")
    update_window_rect = function_body(update_rect, "void Sys_UpdateWindowMouseInputRect(void) {")

    require(process_event, "if ( event == nil )", "native macOS nil event guard")
    require(process_key, "if ( keyEvent == nil )", "native macOS key-event guard")
    require(process_flags, "if ( flagsChangedEvent == nil )", "native macOS flags-event guard")
    require(process_system, "if ( systemDefinedEvent == nil )", "native macOS system-event guard")
    require(source, "static const int MAX_MOUSE_WHEEL_STEPS_PER_EVENT = 64;", "native macOS wheel-step flood guard")
    require(process_event, "if ( !isfinite( deltaY ) )", "native macOS scroll delta finite guard")
    require(process_event, "idMath::ClampFloat(", "native macOS scroll delta clamp")
    require(post_wheel, "wheelSteps == idMath::INT_MIN", "native macOS wheel-step INT_MIN guard")
    require(post_wheel, "queuedWheelSteps", "native macOS wheel-step clamp")
    reject(post_wheel, "abs( wheelSteps )", "native macOS wheel-step overflow guard")
    require(lookup_character, "if ( outChar == NULL )", "native macOS character-output guard")
    require(usable_rect, "isfinite( rect.origin.x )", "native macOS input rect validation")
    require(usable_rect, "rect.size.width > 0.0 && rect.size.height > 0.0", "native macOS input rect validation")

    for body, context in ((prevent, "native macOS mouse capture guard"), (reenable, "native macOS mouse release guard")):
        require(body, "common->Printf", context)
        require(body, "return false;", context)
        reject(body, "common->Error", context)

    require(prevent, "CGAssociateMouseAndMouseCursorPosition( true )", "native macOS mouse capture rollback")
    require(lock, "!Sys_InputRectIsUsable( rect )", "native macOS input rect guard")
    require(lock, "return Sys_PreventMouseMovement(center);", "native macOS mouse lock status")
    require(set_rect, "inputRectValid = NO;", "native macOS invalid input rect guard")
    require(set_rect, "IN_DeactivateMouse();", "native macOS invalid input rect guard")
    require(activate, "const CGDirectDisplayID display = Sys_DisplayToUse();", "native macOS input rect fallback")
    require(activate, "display != 0", "native macOS input rect fallback")
    require(activate, "CGDisplayBounds( display )", "native macOS input rect fallback")
    require(activate, "Mouse capture unavailable on this macOS display state", "native macOS mouse capture soft failure")
    require(activate, "CGDisplayHideCursor( display )", "native macOS cursor hide display guard")
    require(deactivate, "CGDisplayShowCursor", "native macOS cursor restore")
    require(deactivate, "if ( display != 0 )", "native macOS cursor restore display guard")
    require(update_window_rect, "glw_state.display != 0 ? glw_state.display : Sys_DisplayToUse()", "native macOS window input rect display fallback")


def validate_objc_text_bridge_guards() -> None:
    misc = read("src/sys/osx/macosx_misc.mm")
    sys = read("src/sys/osx/macosx_sys.mm")

    localized = function_body(misc, "const char* OSX_GetLocalizedString( const char* key )")
    clipboard = function_body(sys, "char *Sys_GetClipboardData(void)")
    sys_error = function_body(sys, "void Sys_Error(const char *error, ...)")
    sys_print = function_body(sys, "void Sys_Print(const char *text)")

    require(localized, "static idStr localizedStrings[8];", "native macOS localized key-name storage")
    require(localized, "idStr &localizedString = localizedStrings[localizedStringIndex];", "native macOS localized key-name storage")
    require(localized, "localizedText != NULL", "native macOS localized key-name UTF-8 guard")
    reject(localized, "return [string UTF8String];", "native macOS localized key-name transient pointer")
    reject(localized, "return key;", "native macOS localized key-name caller-storage fallback")

    require(clipboard, "pasteboard == nil", "native macOS clipboard pasteboard guard")
    require(clipboard, "utf8ClipboardString != NULL", "native macOS clipboard UTF-8 guard")
    require(clipboard, "Mem_Alloc", "native macOS clipboard engine allocator")
    require(clipboard, "clipboardLength > static_cast<size_t>( idMath::INT_MAX - 1 )", "native macOS clipboard size guard")
    reject(clipboard, "strdup([clipboardString UTF8String])", "native macOS clipboard transient pointer")
    reject(clipboard, "return strdup", "native macOS clipboard allocator mismatch")

    require(sys_error, "formatString = error != NULL ? [NSString stringWithUTF8String:error] : nil;", "native macOS Sys_Error UTF-8 guard")
    require(sys_error, "[NSString stringWithCString:error encoding:NSISOLatin1StringEncoding]", "native macOS Sys_Error fallback encoding")
    require(sys_error, "if (formattedString == nil)", "native macOS Sys_Error formatting fallback")
    require(sys_error, "NSAlert *alert = [[NSAlert alloc] init];", "native macOS Sys_Error modern alert")
    require(sys_error, '[alert setMessageText:@"openPREY Error"];', "native macOS Sys_Error modern alert")
    require(sys_error, "[alert setInformativeText:formattedString];", "native macOS Sys_Error modern alert")
    require(sys_error, "[alert setAlertStyle:NSAlertStyleCritical];", "native macOS Sys_Error modern alert")
    require(sys_error, "[alert runModal];", "native macOS Sys_Error modern alert")
    require(sys_error, "[alert release];", "native macOS Sys_Error modern alert")
    require(sys_error, "[formattedString release];", "native macOS Sys_Error owned string release")
    reject(sys_error, "initWithFormat:[NSString stringWithUTF8String:error]", "native macOS Sys_Error nil format hazard")
    reject(sys_error, "NSRunAlertPanel", "native macOS deprecated alert API")
    reject(sys_error, 'NSRunAlertPanel(@"openPREY Error", formattedString', "native macOS Sys_Error alert format guard")

    require(sys_print, "if ( text == NULL ) {", "native macOS Sys_Print null guard")


def validate_default_savepath_guards() -> None:
    source = read("src/sys/osx/macosx_compat.mm")
    default_savepath = function_body(source, "const char *Sys_DefaultSavePath( void ) {")
    normalize = function_body(source, "static void Sys_NormalizeMacOSDirectoryPath( idStr &path ) {")
    ensure_tree = function_body(source, "static bool Sys_EnsureMacOSDirectoryTree( const idStr &path ) {")
    usable = function_body(source, "static bool Sys_SetUsableMacOSSavePath( const idStr &candidate, const char *label ) {")
    writable = function_body(source, "static bool Sys_DirectoryIsWritable( const idStr &path ) {")
    home = function_body(source, "static bool Sys_GetMacOSHomeDirectory( idStr &homePath ) {")

    require(normalize, "path.BackSlashesToSlashes();", "native macOS directory path normalization")
    require(normalize, "path.Length() > 1", "native macOS root-preserving slash trim")
    require(normalize, "path.CapLength( path.Length() - 1 );", "native macOS root-preserving slash trim")

    require(ensure_tree, "!Sys_IsAbsoluteMacOSPath( path )", "native macOS save path absolute guard")
    require(ensure_tree, "refusing to create non-absolute macOS directory path", "native macOS save path absolute guard")
    require(ensure_tree, "parent.StripFilename();", "native macOS save path parent walk")
    require(ensure_tree, "!Sys_DirectoryExists( parent.c_str() ) && !Sys_EnsureMacOSDirectoryTree( parent )", "native macOS recursive save path creation")
    require(ensure_tree, "mkdir( path.c_str(), 0700 )", "native macOS private save path permissions")
    require(ensure_tree, "errno != EEXIST", "native macOS save path race guard")
    require(ensure_tree, "!Sys_DirectoryExists( path.c_str() )", "native macOS save path directory verification")

    require(usable, "Sys_NormalizeMacOSDirectoryPath( savepath );", "native macOS save path normalization")
    require(usable, "!Sys_IsAbsoluteMacOSPath( savepath )", "native macOS save path absolute guard")
    require(usable, "!Sys_EnsureMacOSDirectoryTree( savepath )", "native macOS save path creation guard")
    require(usable, "!Sys_DirectoryIsWritable( savepath )", "native macOS save path writable guard")
    require(writable, "W_OK | X_OK", "native macOS save path write/search guard")

    require(home, "[NSHomeDirectory() fileSystemRepresentation]", "native macOS home directory lookup")
    require(home, 'home = getenv( "HOME" );', "native macOS HOME fallback")
    require(home, "home[0] != '/'", "native macOS absolute home guard")
    require(home, "realpath( home, resolvedPath )", "native macOS home path canonicalization")
    require(home, "Sys_NormalizeMacOSDirectoryPath( homePath );", "native macOS home path normalization")

    require(default_savepath, 'candidate.AppendPath( "Library" );', "native macOS Application Support save path")
    require(default_savepath, 'candidate.AppendPath( "Application Support" );', "native macOS Application Support save path")
    require(default_savepath, 'candidate.AppendPath( "openPREY" );', "native macOS product save path")
    require(default_savepath, 'Sys_SetUsableMacOSSavePath( candidate, "Application Support" )', "native macOS verified save path")
    require(default_savepath, "idStr cwd = Posix_Cwd();", "native macOS cwd save path fallback")
    require(default_savepath, 'Sys_SetUsableMacOSSavePath( cwd, "cwd fallback" )', "native macOS verified cwd save path fallback")
    require(default_savepath, "using current directory as unverified macOS save path fallback", "native macOS final save path diagnostic")


def main() -> None:
    validate_macos_source_manifest()
    validate_no_live_raw_string_builders()
    validate_native_cocoa_context()
    validate_native_fullscreen_failures()
    validate_hide_unhide_and_stubs()
    validate_display_and_gamma_guards()
    validate_native_input_capture_guards()
    validate_objc_text_bridge_guards()
    validate_default_savepath_guards()
    print("macos_native_backend_guard: ok")


if __name__ == "__main__":
    main()
