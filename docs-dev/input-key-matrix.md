# OpenPrey SDL3 Input Key Matrix Audit (2026-02-20)

This checklist audits SDL3 input parity against the legacy Win32 path for:

- console
- GUI edit fields
- chat
- binds
- numpad
- modifiers

## Method

- Reviewed event generation in `src/sys/win32/win_sdl3.cpp`.
- Compared behavior to legacy `WM_KEY*` + `WM_CHAR`/DirectInput paths in:
  - `src/sys/win32/win_wndproc.cpp`
  - `src/sys/win32/win_input.cpp`
- Reviewed consumer paths:
  - `src/framework/Console.cpp`
  - `src/framework/EditField.cpp`
  - `src/ui/EditWindow.cpp`
  - `src/ui/Window.cpp`
  - `src/framework/Session.cpp`

## Checklist

| Area | Coverage | Result | Notes |
|---|---|---|---|
| Console input | Toggle key, enter/submit, tab-complete, history, paging, backspace, ctrl shortcuts | Pass | `SE_KEY` + `SE_CHAR` paths align for core behavior. |
| GUI edit fields | Backspace/delete, cursor movement, enter handling, ctrl-h/ctrl-a/ctrl-e style chars | Pass | Control-char synthesis parity verified on SDL3 path. |
| Chat input | Text entry/editing in GUI-driven chat fields | Pass (ASCII/control) | Same event flow as edit controls. |
| Binds | Key-down bind execution in session loop (including function keys and mouse/wheel) | Pass (core) | Common bindable keys align. |
| Numpad | KP enter/arrows/home/end/ins/del, KP operators, KP equals | Pass (core) | SDL3 mapping covers key families used by id key enums. |
| Modifiers | Ctrl/Shift/Alt/RightAlt behavior | Pass (core) | Locale-sensitive RightAlt behavior retained. |

## Remaining Gaps

1. Non-English layout parity is still incomplete.
2. Console key localization parity is incomplete outside default mapping.
3. Text input queueing remains byte-oriented for UTF-8 `SDL_EVENT_TEXT_INPUT` payloads.

## Next Steps

1. Complete layout-aware key translation parity for non-English keyboard maps.
2. Make `Sys_GetConsoleKey` layout-aware for SDL3 path.
3. Decode UTF-8 before emitting `SE_CHAR` events.
