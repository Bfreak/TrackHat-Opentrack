/* Trackhat: capture a shortcut (keyboard key, mouse, joystick or gamepad button) into a key_opts,
 * the same way Options > Shortcuts does. Used by the first-run setup wizard. */
#pragma once

#include "export.hpp"
#include "input/key-opts.hpp"

#include <functional>
#include <QString>

namespace key_capture {

// Opens the "press a key" dialog and stores what was pressed in `k` (cleared when nothing usable was
// pressed). `pause_keybindings(true)` is called while the dialog is open so the main window's
// shortcuts don't fire. Returns true when a key or button was bound.
OTR_GUI_EXPORT bool capture(key_opts& k, const std::function<void(bool)>& pause_keybindings);

// "Ctrl+F1", "Gamepad button 3", "Joy button 7", "Mouse 4" or "None".
OTR_GUI_EXPORT QString describe(const key_opts& k);

OTR_GUI_EXPORT void clear(key_opts& k);

} // ns key_capture
