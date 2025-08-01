/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim:expandtab:shiftwidth=4:tabstop=4:
 */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Logging.h"

#include "nsGtkKeyUtils.h"

#include <gdk/gdkkeysyms.h>
#include <algorithm>
#include <gdk/gdk.h>
#include <dlfcn.h>
#include <gdk/gdkkeysyms-compat.h>
#ifdef MOZ_X11
#  include <gdk/gdkx.h>
#  include <X11/XKBlib.h>
#  include "X11UndefineNone.h"
#endif
#include "IMContextWrapper.h"
#include "WidgetUtils.h"
#include "WidgetUtilsGtk.h"
#include "x11/keysym2ucs.h"
#include "nsContentUtils.h"
#include "nsGtkUtils.h"
#include "nsIBidiKeyboard.h"
#include "nsPrintfCString.h"
#include "nsReadableUtils.h"
#include "nsServiceManagerUtils.h"
#include "nsWindow.h"

#include "mozilla/ArrayUtils.h"
#include "mozilla/Maybe.h"
#include "mozilla/MouseEvents.h"
#include "mozilla/StaticPrefs_dom.h"
#include "mozilla/TextEventDispatcher.h"
#include "mozilla/TextEvents.h"

#ifdef MOZ_WAYLAND
#  include <sys/mman.h>
#  include "nsWaylandDisplay.h"
#endif

// For collecting other people's log, tell them `MOZ_LOG=KeyboardHandler:4,sync`
// rather than `MOZ_LOG=KeyboardHandler:5,sync` since using `5` may create too
// big file.
// Therefore you shouldn't use `LogLevel::Verbose` for logging usual behavior.
mozilla::LazyLogModule gKeyLog("KeyboardHandler");

namespace mozilla {
namespace widget {

#define IS_ASCII_ALPHABETICAL(key) \
  ((('a' <= key) && (key <= 'z')) || (('A' <= key) && (key <= 'Z')))

#define MOZ_MODIFIER_KEYS "MozKeymapWrapper"

KeymapWrapper* KeymapWrapper::sInstance = nullptr;
guint KeymapWrapper::sLastRepeatableHardwareKeyCode = 0;
#ifdef MOZ_X11
Time KeymapWrapper::sLastRepeatableKeyTime = 0;
#endif
KeymapWrapper::RepeatState KeymapWrapper::sRepeatState =
    KeymapWrapper::NOT_PRESSED;

#ifdef MOZ_WAYLAND
uint32_t KeymapWrapper::sLastRepeatableSerial = 0;
#endif

static const char* GetStatusName(nsEventStatus aStatus) {
  switch (aStatus) {
    case nsEventStatus_eConsumeDoDefault:
      return "nsEventStatus_eConsumeDoDefault";
    case nsEventStatus_eConsumeNoDefault:
      return "nsEventStatus_eConsumeNoDefault";
    case nsEventStatus_eIgnore:
      return "nsEventStatus_eIgnore";
    case nsEventStatus_eSentinel:
      return "nsEventStatus_eSentinel";
    default:
      return "Illegal value";
  }
}

static const nsCString GetKeyLocationName(uint32_t aLocation) {
  switch (aLocation) {
    case eKeyLocationLeft:
      return "KEY_LOCATION_LEFT"_ns;
    case eKeyLocationRight:
      return "KEY_LOCATION_RIGHT"_ns;
    case eKeyLocationStandard:
      return "KEY_LOCATION_STANDARD"_ns;
    case eKeyLocationNumpad:
      return "KEY_LOCATION_NUMPAD"_ns;
    default:
      return nsPrintfCString("Unknown (0x%04X)", aLocation);
  }
}

static const nsCString GetCharacterCodeName(char16_t aChar) {
  switch (aChar) {
    case 0x0000:
      return "NULL (0x0000)"_ns;
    case 0x0008:
      return "BACKSPACE (0x0008)"_ns;
    case 0x0009:
      return "CHARACTER TABULATION (0x0009)"_ns;
    case 0x000A:
      return "LINE FEED (0x000A)"_ns;
    case 0x000B:
      return "LINE TABULATION (0x000B)"_ns;
    case 0x000C:
      return "FORM FEED (0x000C)"_ns;
    case 0x000D:
      return "CARRIAGE RETURN (0x000D)"_ns;
    case 0x0018:
      return "CANCEL (0x0018)"_ns;
    case 0x001B:
      return "ESCAPE (0x001B)"_ns;
    case 0x0020:
      return "SPACE (0x0020)"_ns;
    case 0x007F:
      return "DELETE (0x007F)"_ns;
    case 0x00A0:
      return "NO-BREAK SPACE (0x00A0)"_ns;
    case 0x00AD:
      return "SOFT HYPHEN (0x00AD)"_ns;
    case 0x2000:
      return "EN QUAD (0x2000)"_ns;
    case 0x2001:
      return "EM QUAD (0x2001)"_ns;
    case 0x2002:
      return "EN SPACE (0x2002)"_ns;
    case 0x2003:
      return "EM SPACE (0x2003)"_ns;
    case 0x2004:
      return "THREE-PER-EM SPACE (0x2004)"_ns;
    case 0x2005:
      return "FOUR-PER-EM SPACE (0x2005)"_ns;
    case 0x2006:
      return "SIX-PER-EM SPACE (0x2006)"_ns;
    case 0x2007:
      return "FIGURE SPACE (0x2007)"_ns;
    case 0x2008:
      return "PUNCTUATION SPACE (0x2008)"_ns;
    case 0x2009:
      return "THIN SPACE (0x2009)"_ns;
    case 0x200A:
      return "HAIR SPACE (0x200A)"_ns;
    case 0x200B:
      return "ZERO WIDTH SPACE (0x200B)"_ns;
    case 0x200C:
      return "ZERO WIDTH NON-JOINER (0x200C)"_ns;
    case 0x200D:
      return "ZERO WIDTH JOINER (0x200D)"_ns;
    case 0x200E:
      return "LEFT-TO-RIGHT MARK (0x200E)"_ns;
    case 0x200F:
      return "RIGHT-TO-LEFT MARK (0x200F)"_ns;
    case 0x2029:
      return "PARAGRAPH SEPARATOR (0x2029)"_ns;
    case 0x202A:
      return "LEFT-TO-RIGHT EMBEDDING (0x202A)"_ns;
    case 0x202B:
      return "RIGHT-TO-LEFT EMBEDDING (0x202B)"_ns;
    case 0x202D:
      return "LEFT-TO-RIGHT OVERRIDE (0x202D)"_ns;
    case 0x202E:
      return "RIGHT-TO-LEFT OVERRIDE (0x202E)"_ns;
    case 0x202F:
      return "NARROW NO-BREAK SPACE (0x202F)"_ns;
    case 0x205F:
      return "MEDIUM MATHEMATICAL SPACE (0x205F)"_ns;
    case 0x2060:
      return "WORD JOINER (0x2060)"_ns;
    case 0x2066:
      return "LEFT-TO-RIGHT ISOLATE (0x2066)"_ns;
    case 0x2067:
      return "RIGHT-TO-LEFT ISOLATE (0x2067)"_ns;
    case 0x3000:
      return "IDEOGRAPHIC SPACE (0x3000)"_ns;
    case 0xFEFF:
      return "ZERO WIDTH NO-BREAK SPACE (0xFEFF)"_ns;
    default: {
      if (aChar < ' ' || (aChar >= 0x80 && aChar < 0xA0)) {
        return nsPrintfCString("control (0x%04X)", aChar);
      }
      if (NS_IS_HIGH_SURROGATE(aChar)) {
        return nsPrintfCString("high surrogate (0x%04X)", aChar);
      }
      if (NS_IS_LOW_SURROGATE(aChar)) {
        return nsPrintfCString("low surrogate (0x%04X)", aChar);
      }
      return nsPrintfCString("'%s' (0x%04X)",
                             NS_ConvertUTF16toUTF8(nsAutoString(aChar)).get(),
                             aChar);
    }
  }
}

static const nsCString GetCharacterCodeNames(const char16_t* aChars,
                                             uint32_t aLength) {
  if (!aLength) {
    return "\"\""_ns;
  }
  nsCString result;
  result.AssignLiteral("\"");
  StringJoinAppend(result, ", "_ns, Span{aChars, aLength},
                   [](nsACString& dest, const char16_t charValue) {
                     dest.Append(GetCharacterCodeName(charValue));
                   });
  result.AppendLiteral("\"");
  return result;
}

static const nsCString GetCharacterCodeNames(const nsAString& aString) {
  return GetCharacterCodeNames(aString.BeginReading(), aString.Length());
}

/* static */
const char* KeymapWrapper::GetModifierName(MappedModifier aModifier) {
  switch (aModifier) {
    case CAPS_LOCK:
      return "CapsLock";
    case NUM_LOCK:
      return "NumLock";
    case SCROLL_LOCK:
      return "ScrollLock";
    case SHIFT:
      return "Shift";
    case CTRL:
      return "Ctrl";
    case ALT:
      return "Alt";
    case SUPER:
      return "Super";
    case HYPER:
      return "Hyper";
    case META:
      return "Meta";
    case LEVEL3:
      return "Level3";
    case LEVEL5:
      return "Level5";
    case NOT_MODIFIER:
      return "NotModifier";
    default:
      return "InvalidValue";
  }
}

/* static */
KeymapWrapper::MappedModifier KeymapWrapper::GetModifierForGDKKeyval(
    guint aGdkKeyval) {
  switch (aGdkKeyval) {
    case GDK_Caps_Lock:
      return CAPS_LOCK;
    case GDK_Num_Lock:
      return NUM_LOCK;
    case GDK_Scroll_Lock:
      return SCROLL_LOCK;
    case GDK_Shift_Lock:
    case GDK_Shift_L:
    case GDK_Shift_R:
      return SHIFT;
    case GDK_Control_L:
    case GDK_Control_R:
      return CTRL;
    case GDK_Alt_L:
    case GDK_Alt_R:
      return ALT;
    case GDK_Super_L:
    case GDK_Super_R:
      return SUPER;
    case GDK_Hyper_L:
    case GDK_Hyper_R:
      return HYPER;
    case GDK_Meta_L:
    case GDK_Meta_R:
      return META;
    case GDK_ISO_Level3_Shift:
    case GDK_Mode_switch:
      return LEVEL3;
    case GDK_ISO_Level5_Shift:
      return LEVEL5;
    default:
      return NOT_MODIFIER;
  }
}

guint KeymapWrapper::GetGdkModifierMask(MappedModifier aModifier) const {
  switch (aModifier) {
    case CAPS_LOCK:
      return GDK_LOCK_MASK;
    case NUM_LOCK:
      return mModifierMasks[INDEX_NUM_LOCK];
    case SCROLL_LOCK:
      return mModifierMasks[INDEX_SCROLL_LOCK];
    case SHIFT:
      return GDK_SHIFT_MASK;
    case CTRL:
      return GDK_CONTROL_MASK;
    case ALT:
      return mModifierMasks[INDEX_ALT];
    case SUPER:
      return GDK_SUPER_MASK;
    case HYPER:
      return mModifierMasks[INDEX_HYPER];
    case META:
      return mModifierMasks[INDEX_META];
    case LEVEL3:
      return mModifierMasks[INDEX_LEVEL3];
    case LEVEL5:
      return mModifierMasks[INDEX_LEVEL5];
    default:
      return 0;
  }
}

KeymapWrapper::ModifierKey* KeymapWrapper::GetModifierKey(
    guint aHardwareKeycode) {
  for (uint32_t i = 0; i < mModifierKeys.Length(); i++) {
    ModifierKey& key = mModifierKeys[i];
    if (key.mHardwareKeycode == aHardwareKeycode) {
      return &key;
    }
  }
  return nullptr;
}

/* static */
KeymapWrapper* KeymapWrapper::GetInstance() {
  if (!sInstance) {
    sInstance = new KeymapWrapper();
    sInstance->Init();
  }
  return sInstance;
}

#ifdef MOZ_WAYLAND
void KeymapWrapper::EnsureInstance() { (void)GetInstance(); }

void KeymapWrapper::InitBySystemSettingsWayland() {
  MOZ_UNUSED(WaylandDisplayGet());
}
#endif

/* static */
void KeymapWrapper::Shutdown() {
  if (sInstance) {
    delete sInstance;
    sInstance = nullptr;
  }
}

KeymapWrapper::KeymapWrapper()
    : mInitialized(false),
      mGdkKeymap(gdk_keymap_get_default()),
      mXKBBaseEventCode(0),
      mOnKeysChangedSignalHandle(0),
      mOnDirectionChangedSignalHandle(0) {
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p Constructor, mGdkKeymap=%p", this, mGdkKeymap));

  g_object_ref(mGdkKeymap);

#ifdef MOZ_X11
  if (GdkIsX11Display()) {
    InitXKBExtension();
  }
#endif
}

void KeymapWrapper::Init() {
  if (mInitialized) {
    return;
  }
  mInitialized = true;

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p Init, mGdkKeymap=%p", this, mGdkKeymap));

  mModifierKeys.Clear();
  memset(mModifierMasks, 0, sizeof(mModifierMasks));

#ifdef MOZ_X11
  if (GdkIsX11Display()) {
    InitBySystemSettingsX11();
  }
#endif
#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay()) {
    InitBySystemSettingsWayland();
  }
#endif

#ifdef MOZ_X11
  gdk_window_add_filter(nullptr, FilterEvents, this);
#endif

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p Init, CapsLock=0x%X, NumLock=0x%X, "
           "ScrollLock=0x%X, Level3=0x%X, Level5=0x%X, "
           "Shift=0x%X, Ctrl=0x%X, Alt=0x%X, Meta=0x%X, Super=0x%X, Hyper=0x%X",
           this, GetGdkModifierMask(CAPS_LOCK), GetGdkModifierMask(NUM_LOCK),
           GetGdkModifierMask(SCROLL_LOCK), GetGdkModifierMask(LEVEL3),
           GetGdkModifierMask(LEVEL5), GetGdkModifierMask(SHIFT),
           GetGdkModifierMask(CTRL), GetGdkModifierMask(ALT),
           GetGdkModifierMask(META), GetGdkModifierMask(SUPER),
           GetGdkModifierMask(HYPER)));
}

#ifdef MOZ_X11
void KeymapWrapper::InitXKBExtension() {
  PodZero(&mKeyboardState);

  int xkbMajorVer = XkbMajorVersion;
  int xkbMinorVer = XkbMinorVersion;
  if (!XkbLibraryVersion(&xkbMajorVer, &xkbMinorVer)) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p InitXKBExtension failed due to failure of "
             "XkbLibraryVersion()",
             this));
    return;
  }

  Display* display = gdk_x11_display_get_xdisplay(gdk_display_get_default());

  // XkbLibraryVersion() set xkbMajorVer and xkbMinorVer to that of the
  // library, which may be newer than what is required of the server in
  // XkbQueryExtension(), so these variables should be reset to
  // XkbMajorVersion and XkbMinorVersion before the XkbQueryExtension call.
  xkbMajorVer = XkbMajorVersion;
  xkbMinorVer = XkbMinorVersion;
  int opcode, baseErrorCode;
  if (!XkbQueryExtension(display, &opcode, &mXKBBaseEventCode, &baseErrorCode,
                         &xkbMajorVer, &xkbMinorVer)) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   InitXKBExtension failed due to failure of "
             "XkbQueryExtension(), display=0x%p",
             this, display));
    return;
  }

  if (!XkbSelectEventDetails(display, XkbUseCoreKbd, XkbStateNotify,
                             XkbModifierStateMask, XkbModifierStateMask)) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   InitXKBExtension failed due to failure of "
             "XkbSelectEventDetails() for XModifierStateMask, display=0x%p",
             this, display));
    return;
  }

  if (!XkbSelectEventDetails(display, XkbUseCoreKbd, XkbControlsNotify,
                             XkbPerKeyRepeatMask, XkbPerKeyRepeatMask)) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   InitXKBExtension failed due to failure of "
             "XkbSelectEventDetails() for XkbControlsNotify, display=0x%p",
             this, display));
    return;
  }

  if (!XGetKeyboardControl(display, &mKeyboardState)) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   InitXKBExtension failed due to failure of "
             "XGetKeyboardControl(), display=0x%p",
             this, display));
    return;
  }

  MOZ_LOG(gKeyLog, LogLevel::Info, ("%p   InitXKBExtension, Succeeded", this));
}

void KeymapWrapper::InitBySystemSettingsX11() {
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p InitBySystemSettingsX11, mGdkKeymap=%p", this, mGdkKeymap));

  if (!mOnKeysChangedSignalHandle) {
    mOnKeysChangedSignalHandle = g_signal_connect(
        mGdkKeymap, "keys-changed", (GCallback)OnKeysChanged, this);
  }
  if (!mOnDirectionChangedSignalHandle) {
    mOnDirectionChangedSignalHandle = g_signal_connect(
        mGdkKeymap, "direction-changed", (GCallback)OnDirectionChanged, this);
  }

  Display* display = gdk_x11_display_get_xdisplay(gdk_display_get_default());

  int min_keycode = 0;
  int max_keycode = 0;
  XDisplayKeycodes(display, &min_keycode, &max_keycode);

  int keysyms_per_keycode = 0;
  KeySym* xkeymap =
      XGetKeyboardMapping(display, min_keycode, max_keycode - min_keycode + 1,
                          &keysyms_per_keycode);
  if (!xkeymap) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   InitBySystemSettings, "
             "Failed due to null xkeymap",
             this));
    return;
  }

  XModifierKeymap* xmodmap = XGetModifierMapping(display);
  if (!xmodmap) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   InitBySystemSettings, "
             "Failed due to null xmodmap",
             this));
    XFree(xkeymap);
    return;
  }
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p   InitBySystemSettings, min_keycode=%d, "
           "max_keycode=%d, keysyms_per_keycode=%d, max_keypermod=%d",
           this, min_keycode, max_keycode, keysyms_per_keycode,
           xmodmap->max_keypermod));

  // The modifiermap member of the XModifierKeymap structure contains 8 sets
  // of max_keypermod KeyCodes, one for each modifier in the order Shift,
  // Lock, Control, Mod1, Mod2, Mod3, Mod4, and Mod5.
  // Only nonzero KeyCodes have meaning in each set, and zero KeyCodes are
  // ignored.

  // Note that two or more modifiers may use one modifier flag.  E.g.,
  // on Ubuntu 10.10, Alt and Meta share the Mod1 in default settings.
  // And also Super and Hyper share the Mod4. In such cases, we need to
  // decide which modifier flag means one of DOM modifiers.

  // mod[0] is Modifier introduced by Mod1.
  MappedModifier mod[5];
  int32_t foundLevel[5];
  for (uint32_t i = 0; i < std::size(mod); i++) {
    mod[i] = NOT_MODIFIER;
    foundLevel[i] = INT32_MAX;
  }
  const uint32_t map_size = 8 * xmodmap->max_keypermod;
  for (uint32_t i = 0; i < map_size; i++) {
    KeyCode keycode = xmodmap->modifiermap[i];
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p   InitBySystemSettings, "
             "  i=%d, keycode=0x%08X",
             this, i, keycode));
    if (!keycode || keycode < min_keycode || keycode > max_keycode) {
      continue;
    }

    ModifierKey* modifierKey = GetModifierKey(keycode);
    if (!modifierKey) {
      modifierKey = mModifierKeys.AppendElement(ModifierKey(keycode));
    }

    const KeySym* syms =
        xkeymap + (keycode - min_keycode) * keysyms_per_keycode;
    const uint32_t bit = i / xmodmap->max_keypermod;
    modifierKey->mMask |= 1 << bit;

    // We need to know the meaning of Mod1, Mod2, Mod3, Mod4 and Mod5.
    // Let's skip if current map is for others.
    if (bit < 3) {
      continue;
    }

    const int32_t modIndex = bit - 3;
    for (int32_t j = 0; j < keysyms_per_keycode; j++) {
      MappedModifier modifier = GetModifierForGDKKeyval(syms[j]);
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("%p   InitBySystemSettings, "
               "    Mod%d, j=%d, syms[j]=%s(0x%lX), modifier=%s",
               this, modIndex + 1, j, gdk_keyval_name(syms[j]), syms[j],
               GetModifierName(modifier)));

      switch (modifier) {
        case NOT_MODIFIER:
          // Don't overwrite the stored information with
          // NOT_MODIFIER.
          break;
        case CAPS_LOCK:
        case SHIFT:
        case CTRL:
        case SUPER:
          // Ignore the modifiers defined in GDK spec. They shouldn't
          // be mapped to Mod1-5 because they must not work on native
          // GTK applications.
          break;
        default:
          // If new modifier is found in higher level than stored
          // value, we don't need to overwrite it.
          if (j > foundLevel[modIndex]) {
            break;
          }
          // If new modifier is more important than stored value,
          // we should overwrite it with new modifier.
          if (j == foundLevel[modIndex]) {
            mod[modIndex] = std::min(modifier, mod[modIndex]);
            break;
          }
          foundLevel[modIndex] = j;
          mod[modIndex] = modifier;
          break;
      }
    }
  }

  for (uint32_t i = 0; i < COUNT_OF_MODIFIER_INDEX; i++) {
    MappedModifier modifier;
    switch (i) {
      case INDEX_NUM_LOCK:
        modifier = NUM_LOCK;
        break;
      case INDEX_SCROLL_LOCK:
        modifier = SCROLL_LOCK;
        break;
      case INDEX_ALT:
        modifier = ALT;
        break;
      case INDEX_META:
        modifier = META;
        break;
      case INDEX_HYPER:
        modifier = HYPER;
        break;
      case INDEX_LEVEL3:
        modifier = LEVEL3;
        break;
      case INDEX_LEVEL5:
        modifier = LEVEL5;
        break;
      default:
        MOZ_CRASH("All indexes must be handled here");
    }
    for (uint32_t j = 0; j < std::size(mod); j++) {
      if (modifier == mod[j]) {
        mModifierMasks[i] |= 1 << (j + 3);
      }
    }
  }

  XFreeModifiermap(xmodmap);
  XFree(xkeymap);
}
#endif

#ifdef MOZ_WAYLAND
void KeymapWrapper::SetModifierMask(xkb_keymap* aKeymap,
                                    ModifierIndex aModifierIndex,
                                    const char* aModifierName) {
  xkb_mod_index_t index = xkb_keymap_mod_get_index(aKeymap, aModifierName);
  if (index == XKB_MOD_INVALID) {
    return;
  }
  struct xkb_state* xkb_state = xkb_state_new(aKeymap);
  if (!xkb_state) {
    return;
  }
  xkb_mod_mask_t mask = 1u << index;
  xkb_state_update_mask(xkb_state, mask, 0, 0, 0, 0, 0);
  xkb_mod_mask_t res =
      xkb_state_serialize_mods(xkb_state, XKB_STATE_MODS_EFFECTIVE);
  xkb_state_unref(xkb_state);
  if (res == mask) {
    // Either this already was an rmod, or it's an unmapped vmod. In the former
    // case we obviously want to use it, in the latter this allows us to handle
    // synthesized key events using that vmod, even if that vmod cannot be
    // generated by keyboard (with a slight risk of GdkModifierType collisions).
    mModifierMasks[aModifierIndex] = res;
  } else {
    // We found a mapped rmod for the vmod. Make sure to remove the opaque `1u
    // << index` mask, which does not get removed by xkb_state_serialize_mods,
    // because it can quite easily collide with the bitwise-incompatible parts
    // of the GdkModifierType enum (most likely GDK_BUTTON{N}_MASK).
    mModifierMasks[aModifierIndex] = res & ~(mask);
  }
}

void KeymapWrapper::SetModifierMasks(xkb_keymap* aKeymap) {
  KeymapWrapper* keymapWrapper = GetInstance();

  // These mappings are derived from get_xkb_modifiers() at gdkkeys-wayland.c,
  // as well as libxkbcommon. The first two are practically fixed.
  keymapWrapper->SetModifierMask(aKeymap, INDEX_ALT, XKB_MOD_NAME_ALT);
  keymapWrapper->SetModifierMask(aKeymap, INDEX_NUM_LOCK, XKB_MOD_NAME_NUM);
  // We have to find the rmods for these vmods, which are not supported by GDK.
  keymapWrapper->SetModifierMask(aKeymap, INDEX_LEVEL3, XKB_VMOD_NAME_LEVEL3);
  keymapWrapper->SetModifierMask(aKeymap, INDEX_LEVEL5, XKB_VMOD_NAME_LEVEL5);
  keymapWrapper->SetModifierMask(aKeymap, INDEX_SCROLL_LOCK,
                                 XKB_VMOD_NAME_SCROLL);
  // GDK specific vmods we don't want to map to rmods to preserve behavior.
  keymapWrapper->mModifierMasks[INDEX_HYPER] = GDK_HYPER_MASK;
  keymapWrapper->mModifierMasks[INDEX_META] = GDK_META_MASK;

  keymapWrapper->SetKeymap(aKeymap);

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p KeymapWrapper::SetModifierMasks, CapsLock=0x%X, NumLock=0x%X, "
           "ScrollLock=0x%X, Level3=0x%X, Level5=0x%X, "
           "Shift=0x%X, Ctrl=0x%X, Alt=0x%X, Meta=0x%X, Super=0x%X, Hyper=0x%X",
           keymapWrapper, keymapWrapper->GetGdkModifierMask(CAPS_LOCK),
           keymapWrapper->GetGdkModifierMask(NUM_LOCK),
           keymapWrapper->GetGdkModifierMask(SCROLL_LOCK),
           keymapWrapper->GetGdkModifierMask(LEVEL3),
           keymapWrapper->GetGdkModifierMask(LEVEL5),
           keymapWrapper->GetGdkModifierMask(SHIFT),
           keymapWrapper->GetGdkModifierMask(CTRL),
           keymapWrapper->GetGdkModifierMask(ALT),
           keymapWrapper->GetGdkModifierMask(META),
           keymapWrapper->GetGdkModifierMask(SUPER),
           keymapWrapper->GetGdkModifierMask(HYPER)));
}

/* This keymap routine is derived from weston-2.0.0/clients/simple-im.c
 */
void KeymapWrapper::HandleKeymap(uint32_t format, int fd, uint32_t size) {
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("KeymapWrapper::HandleKeymap() format %d fd %d size %d", format, fd,
           size));
  KeymapWrapper::ResetKeyboard();

  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("KeymapWrapper::HandleKeymap(): format is not "
             "WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1!"));
    close(fd);
    return;
  }

  char* mapString = (char*)mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  if (mapString == MAP_FAILED) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("KeymapWrapper::HandleKeymap(): failed to allocate shm!"));
    close(fd);
    return;
  }

  struct xkb_context* xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  if (!xkb_context) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("KeymapWrapper::HandleKeymap(): failed to get xkb_context!"));
    close(fd);
    return;
  }

  struct xkb_keymap* keymap = xkb_keymap_new_from_string(
      xkb_context, mapString, XKB_KEYMAP_FORMAT_TEXT_V1,
      XKB_KEYMAP_COMPILE_NO_FLAGS);

  munmap(mapString, size);
  close(fd);

  if (!keymap) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("KeymapWrapper::HandleKeymap(): Failed to compile keymap!"));
    xkb_context_unref(xkb_context);
    return;
  }

  KeymapWrapper::SetModifierMasks(keymap);

  xkb_keymap_unref(keymap);

  xkb_context_unref(xkb_context);
}
#endif

KeymapWrapper::~KeymapWrapper() {
#ifdef MOZ_X11
  gdk_window_remove_filter(nullptr, FilterEvents, this);
#endif
#ifdef MOZ_WAYLAND
  if (mXkbKeymap) {
    xkb_keymap_unref(mXkbKeymap);
  }
#endif
  if (mOnKeysChangedSignalHandle) {
    g_signal_handler_disconnect(mGdkKeymap, mOnKeysChangedSignalHandle);
  }
  if (mOnDirectionChangedSignalHandle) {
    g_signal_handler_disconnect(mGdkKeymap, mOnDirectionChangedSignalHandle);
  }
  g_object_unref(mGdkKeymap);
  MOZ_LOG(gKeyLog, LogLevel::Info, ("%p Destructor", this));
}

#ifdef MOZ_X11
/* static */
GdkFilterReturn KeymapWrapper::FilterEvents(GdkXEvent* aXEvent,
                                            GdkEvent* aGdkEvent,
                                            gpointer aData) {
  XEvent* xEvent = static_cast<XEvent*>(aXEvent);
  switch (xEvent->type) {
    case KeyPress: {
      // If the key doesn't support auto repeat, ignore the event because
      // even if such key (e.g., Shift) is pressed during auto repeat of
      // anoter key, it doesn't stop the auto repeat.
      KeymapWrapper* self = static_cast<KeymapWrapper*>(aData);
      if (!self->IsAutoRepeatableKey(xEvent->xkey.keycode)) {
        break;
      }
      if (sRepeatState == NOT_PRESSED) {
        sRepeatState = FIRST_PRESS;
        MOZ_LOG(gKeyLog, LogLevel::Info,
                ("FilterEvents(aXEvent={ type=KeyPress, "
                 "xkey={ keycode=0x%08X, state=0x%08X, time=%lu } }, "
                 "aGdkEvent={ state=0x%08X }), "
                 "detected first keypress",
                 xEvent->xkey.keycode, xEvent->xkey.state, xEvent->xkey.time,
                 reinterpret_cast<GdkEventKey*>(aGdkEvent)->state));
      } else if (sLastRepeatableHardwareKeyCode == xEvent->xkey.keycode) {
        if (sLastRepeatableKeyTime == xEvent->xkey.time &&
            sLastRepeatableHardwareKeyCode ==
                IMContextWrapper::
                    GetWaitingSynthesizedKeyPressHardwareKeyCode()) {
          // On some environment, IM may generate duplicated KeyPress event
          // without any special state flags.  In such case, we shouldn't
          // treat the event as "repeated".
          MOZ_LOG(gKeyLog, LogLevel::Info,
                  ("FilterEvents(aXEvent={ type=KeyPress, "
                   "xkey={ keycode=0x%08X, state=0x%08X, time=%lu } }, "
                   "aGdkEvent={ state=0x%08X }), "
                   "igored keypress since it must be synthesized by IME",
                   xEvent->xkey.keycode, xEvent->xkey.state, xEvent->xkey.time,
                   reinterpret_cast<GdkEventKey*>(aGdkEvent)->state));
          break;
        }
        sRepeatState = REPEATING;
        MOZ_LOG(gKeyLog, LogLevel::Info,
                ("FilterEvents(aXEvent={ type=KeyPress, "
                 "xkey={ keycode=0x%08X, state=0x%08X, time=%lu } }, "
                 "aGdkEvent={ state=0x%08X }), "
                 "detected repeating keypress",
                 xEvent->xkey.keycode, xEvent->xkey.state, xEvent->xkey.time,
                 reinterpret_cast<GdkEventKey*>(aGdkEvent)->state));
      } else {
        // If a different key is pressed while another key is pressed,
        // auto repeat system repeats only the last pressed key.
        // So, setting new keycode and setting repeat state as first key
        // press should work fine.
        sRepeatState = FIRST_PRESS;
        MOZ_LOG(gKeyLog, LogLevel::Info,
                ("FilterEvents(aXEvent={ type=KeyPress, "
                 "xkey={ keycode=0x%08X, state=0x%08X, time=%lu } }, "
                 "aGdkEvent={ state=0x%08X }), "
                 "detected different keypress",
                 xEvent->xkey.keycode, xEvent->xkey.state, xEvent->xkey.time,
                 reinterpret_cast<GdkEventKey*>(aGdkEvent)->state));
      }
      sLastRepeatableHardwareKeyCode = xEvent->xkey.keycode;
      sLastRepeatableKeyTime = xEvent->xkey.time;
      break;
    }
    case KeyRelease: {
      if (sLastRepeatableHardwareKeyCode != xEvent->xkey.keycode) {
        // This case means the key release event is caused by
        // a non-repeatable key such as Shift or a repeatable key that
        // was pressed before sLastRepeatableHardwareKeyCode was
        // pressed.
        break;
      }
      sRepeatState = NOT_PRESSED;
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("FilterEvents(aXEvent={ type=KeyRelease, "
               "xkey={ keycode=0x%08X, state=0x%08X, time=%lu } }, "
               "aGdkEvent={ state=0x%08X }), "
               "detected key release",
               xEvent->xkey.keycode, xEvent->xkey.state, xEvent->xkey.time,
               reinterpret_cast<GdkEventKey*>(aGdkEvent)->state));
      break;
    }
    case FocusOut: {
      // At moving focus, we should reset keyboard repeat state.
      // Strictly, this causes incorrect behavior.  However, this
      // correctness must be enough for web applications.
      sRepeatState = NOT_PRESSED;
      break;
    }
    default: {
      KeymapWrapper* self = static_cast<KeymapWrapper*>(aData);
      if (xEvent->type != self->mXKBBaseEventCode) {
        break;
      }
      XkbEvent* xkbEvent = (XkbEvent*)xEvent;
      if (xkbEvent->any.xkb_type != XkbControlsNotify ||
          !(xkbEvent->ctrls.changed_ctrls & XkbPerKeyRepeatMask)) {
        break;
      }
      if (!XGetKeyboardControl(xkbEvent->any.display, &self->mKeyboardState)) {
        MOZ_LOG(gKeyLog, LogLevel::Info,
                ("%p FilterEvents failed due to failure "
                 "of XGetKeyboardControl(), display=0x%p",
                 self, xkbEvent->any.display));
      }
      break;
    }
  }

  return GDK_FILTER_CONTINUE;
}
#endif

#ifdef MOZ_WAYLAND
// static
void KeymapWrapper::KeyboardHandlerForWayland(uint32_t aSerial,
                                              uint32_t aHardwareKeycode,
                                              uint32_t aState) {
  KeymapWrapper* keymapWrapper = GetInstance();
  if (!keymapWrapper->IsAutoRepeatableKey(aHardwareKeycode)) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("KeyboardHandlerForWayland(aSerial=%u, aHardwareKeycode=0x%08X, "
             "aState=%s), no repeat key",
             aSerial, aHardwareKeycode,
             aState == WL_KEYBOARD_KEY_STATE_PRESSED
                 ? "WL_KEYBOARD_KEY_STATE_PRESSED"
                 : "WL_KEYBOARD_KEY_STATE_RELEASED"));
    return;
  }

  if (aState == WL_KEYBOARD_KEY_STATE_PRESSED) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("KeyboardHandlerForWayland(aSerial=%u, aHardwareKeycode=0x%08X, "
             "aState=WL_KEYBOARD_KEY_STATE_PRESSED), first key pressed",
             aSerial, aHardwareKeycode));

    sLastRepeatableSerial = aSerial;
    sLastRepeatableHardwareKeyCode = aHardwareKeycode;
    sRepeatState = FIRST_PRESS;

    // This runnable will be run after GDK's key event.
    //
    // Next key press of GDK will be after repeat's delay ms.
    // But event time in next key press won't updated.
    //
    // The delay's default is 400ms in GTK/wayland. Even if we can get this
    // information from repeat_info, if we wait for this time, it is too late.
    // We guess that 10ms will be enough duration to set repeat state.

    NS_DelayedDispatchToCurrentThread(
        NS_NewRunnableFunction(
            __func__,
            [aSerial] {
              if (sLastRepeatableSerial != aSerial) {
                // We already receive newer key event. Don't set repeat state.
                return;
              }

              MOZ_LOG(gKeyLog, LogLevel::Info,
                      ("KeyboardHandlerForWayland(aSerial=%u, "
                       "aState=WL_KEYBOARD_KEY_STATE_PRESSED), repeating",
                       aSerial));
              sRepeatState = REPEATING;
            }),
        10);
    return;
  }

  if (sLastRepeatableHardwareKeyCode != aHardwareKeycode) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("KeyboardHandlerForWayland(aSerial=%u, aHardwareKeycode=0x%08X "
             "aState=WL_KEYBOARD_KEY_STATE_RELEASED), released key isn't "
             "matched",
             aSerial, aHardwareKeycode));
    return;
  }

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("KeyboardHandlerForWayland(aSerial=%u, aHardwareKeycode=0x%08X"
           "aState=WL_KEYBOARD_KEY_STATE_RELEASED), not pressed",
           aSerial, aHardwareKeycode));

  sLastRepeatableSerial = aSerial;
  sRepeatState = NOT_PRESSED;
}

void KeymapWrapper::SetKeymap(xkb_keymap* aKeymap) {
  if (mXkbKeymap) {
    xkb_keymap_unref(mXkbKeymap);
  }
  mXkbKeymap = xkb_keymap_ref(aKeymap);
}
#endif

static void ResetBidiKeyboard() {
  // Reset the bidi keyboard settings for the new GdkKeymap
  nsCOMPtr<nsIBidiKeyboard> bidiKeyboard = nsContentUtils::GetBidiKeyboard();
  if (bidiKeyboard) {
    bidiKeyboard->Reset();
  }
  WidgetUtils::SendBidiKeyboardInfoToContent();
}

/* static */
void KeymapWrapper::ResetKeyboard() {
  if (sInstance) {
    sInstance->mInitialized = false;
    ResetBidiKeyboard();
  }
}

/* static */
void KeymapWrapper::OnKeysChanged(GdkKeymap* aGdkKeymap,
                                  KeymapWrapper* aKeymapWrapper) {
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("OnKeysChanged, aGdkKeymap=%p, aKeymapWrapper=%p", aGdkKeymap,
           aKeymapWrapper));

  MOZ_ASSERT(sInstance == aKeymapWrapper,
             "This instance must be the singleton instance");

  // We cannot reintialize here becasue we don't have GdkWindow which is using
  // the GdkKeymap.  We'll reinitialize it when next GetInstance() is called.
  ResetKeyboard();
}

// static
void KeymapWrapper::OnDirectionChanged(GdkKeymap* aGdkKeymap,
                                       KeymapWrapper* aKeymapWrapper) {
  // XXX
  // A lot of diretion-changed signal might be fired on switching bidi
  // keyboard when using both ibus (with arabic layout) and fcitx (with IME).
  // See https://github.com/fcitx/fcitx/issues/257
  //
  // Also, when using ibus, switching to IM might not cause this signal.
  // See https://github.com/ibus/ibus/issues/1848

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("OnDirectionChanged, aGdkKeymap=%p, aKeymapWrapper=%p", aGdkKeymap,
           aKeymapWrapper));

  ResetBidiKeyboard();
}

/* static */
guint KeymapWrapper::GetCurrentModifierState() {
  GdkModifierType modifiers;
  GdkDisplay* display = gdk_display_get_default();
  GdkScreen* screen = gdk_display_get_default_screen(display);
  GdkWindow* window = gdk_screen_get_root_window(screen);
  gdk_window_get_device_position(window, GdkGetPointer(), nullptr, nullptr,
                                 &modifiers);
  return static_cast<guint>(modifiers);
}

/* static */
bool KeymapWrapper::AreModifiersActive(MappedModifiers aModifiers,
                                       guint aGdkModifierState) {
  NS_ENSURE_TRUE(aModifiers, false);

  KeymapWrapper* keymapWrapper = GetInstance();
  for (uint32_t i = 0; i < sizeof(MappedModifier) * 8 && aModifiers; i++) {
    MappedModifier modifier = static_cast<MappedModifier>(1 << i);
    // Is the binary position used by modifier?
    if (!(aModifiers & modifier)) {
      continue;
    }
    // Is the modifier active?
    if (!(aGdkModifierState & keymapWrapper->GetGdkModifierMask(modifier))) {
      return false;
    }
    aModifiers &= ~modifier;
  }
  return true;
}

/* static */
uint32_t KeymapWrapper::ComputeCurrentKeyModifiers() {
  return ComputeKeyModifiers(GetCurrentModifierState());
}

/* static */
uint32_t KeymapWrapper::ComputeKeyModifiers(guint aGdkModifierState) {
  uint32_t keyModifiers = 0;
  if (!aGdkModifierState) {
    return keyModifiers;
  }

  // DOM Meta key should be TRUE only on Mac.  We need to discuss this
  // issue later.
  KeymapWrapper* keymapWrapper = GetInstance();
  if (keymapWrapper->AreModifiersActive(SHIFT, aGdkModifierState)) {
    keyModifiers |= MODIFIER_SHIFT;
  }
  if (keymapWrapper->AreModifiersActive(CTRL, aGdkModifierState)) {
    keyModifiers |= MODIFIER_CONTROL;
  }
  if (keymapWrapper->AreModifiersActive(ALT, aGdkModifierState)) {
    keyModifiers |= MODIFIER_ALT;
  }
  if (keymapWrapper->AreModifiersActive(SUPER, aGdkModifierState) ||
      keymapWrapper->AreModifiersActive(HYPER, aGdkModifierState) ||
      // "Meta" state is typically mapped to `Alt` + `Shift`, but we ignore the
      // state if `Alt` is mapped to "Alt" state.  Additionally it's mapped to
      // `Win` in Sun/Solaris keyboard layout.  In this case, we want to treat
      // them as DOM Meta modifier keys like "Super" state in the major Linux
      // environments.
      keymapWrapper->AreModifiersActive(META, aGdkModifierState)) {
    keyModifiers |= MODIFIER_META;
  }
  if (keymapWrapper->AreModifiersActive(LEVEL3, aGdkModifierState) ||
      keymapWrapper->AreModifiersActive(LEVEL5, aGdkModifierState)) {
    keyModifiers |= MODIFIER_ALTGRAPH;
  }
  if (keymapWrapper->AreModifiersActive(CAPS_LOCK, aGdkModifierState)) {
    keyModifiers |= MODIFIER_CAPSLOCK;
  }
  if (keymapWrapper->AreModifiersActive(NUM_LOCK, aGdkModifierState)) {
    keyModifiers |= MODIFIER_NUMLOCK;
  }
  if (keymapWrapper->AreModifiersActive(SCROLL_LOCK, aGdkModifierState)) {
    keyModifiers |= MODIFIER_SCROLLLOCK;
  }
  return keyModifiers;
}

/* static */
guint KeymapWrapper::ConvertWidgetModifierToGdkState(
    nsIWidget::Modifiers aNativeModifiers) {
  if (!aNativeModifiers) {
    return 0;
  }
  struct ModifierMapEntry {
    nsIWidget::Modifiers mWidgetModifier;
    MappedModifier mModifier;
  };
  // TODO: Currently, we don't treat L/R of each modifier on Linux.
  // TODO: No proper native modifier for Level5.
  static constexpr ModifierMapEntry sModifierMap[] = {
      {nsIWidget::CAPS_LOCK, MappedModifier::CAPS_LOCK},
      {nsIWidget::NUM_LOCK, MappedModifier::NUM_LOCK},
      {nsIWidget::SHIFT_L, MappedModifier::SHIFT},
      {nsIWidget::SHIFT_R, MappedModifier::SHIFT},
      {nsIWidget::CTRL_L, MappedModifier::CTRL},
      {nsIWidget::CTRL_R, MappedModifier::CTRL},
      {nsIWidget::ALT_L, MappedModifier::ALT},
      {nsIWidget::ALT_R, MappedModifier::ALT},
      {nsIWidget::ALTGRAPH, MappedModifier::LEVEL3},
      {nsIWidget::COMMAND_L, MappedModifier::SUPER},
      {nsIWidget::COMMAND_R, MappedModifier::SUPER}};

  guint state = 0;
  KeymapWrapper* instance = GetInstance();
  for (const ModifierMapEntry& entry : sModifierMap) {
    if (aNativeModifiers & entry.mWidgetModifier) {
      state |= instance->GetGdkModifierMask(entry.mModifier);
    }
  }
  return state;
}

/* static */
void KeymapWrapper::InitInputEvent(WidgetInputEvent& aInputEvent,
                                   guint aGdkModifierState, bool isEraser) {
  KeymapWrapper* keymapWrapper = GetInstance();

  aInputEvent.mModifiers = ComputeKeyModifiers(aGdkModifierState);

  // Don't log this method for non-important events because e.g., eMouseMove is
  // just noisy and there is no reason to log it.
  bool doLog = aInputEvent.mMessage != eMouseMove;
  if (doLog) {
    MOZ_LOG(gKeyLog, LogLevel::Debug,
            ("%p InitInputEvent, aGdkModifierState=0x%08X, "
             "aInputEvent={ mMessage=%s, mModifiers=0x%04X (Shift: %s, "
             "Control: %s, Alt: %s, Meta: %s, AltGr: %s, "
             "CapsLock: %s, NumLock: %s, ScrollLock: %s })",
             keymapWrapper, aGdkModifierState, ToChar(aInputEvent.mMessage),
             aInputEvent.mModifiers,
             GetBoolName(aInputEvent.mModifiers & MODIFIER_SHIFT),
             GetBoolName(aInputEvent.mModifiers & MODIFIER_CONTROL),
             GetBoolName(aInputEvent.mModifiers & MODIFIER_ALT),
             GetBoolName(aInputEvent.mModifiers & MODIFIER_META),
             GetBoolName(aInputEvent.mModifiers & MODIFIER_ALTGRAPH),
             GetBoolName(aInputEvent.mModifiers & MODIFIER_CAPSLOCK),
             GetBoolName(aInputEvent.mModifiers & MODIFIER_NUMLOCK),
             GetBoolName(aInputEvent.mModifiers & MODIFIER_SCROLLLOCK)));
  }

  switch (aInputEvent.mClass) {
    case eMouseEventClass:
    case ePointerEventClass:
    case eMouseScrollEventClass:
    case eWheelEventClass:
    case eDragEventClass:
    case eSimpleGestureEventClass:
      break;
    default:
      return;
  }

  WidgetMouseEventBase& mouseEvent = *aInputEvent.AsMouseEventBase();
  mouseEvent.mButtons = 0;
  if (aGdkModifierState & GDK_BUTTON1_MASK) {
    if (isEraser) {
      mouseEvent.mButtons |= MouseButtonsFlag::eEraserFlag;
    } else {
      mouseEvent.mButtons |= MouseButtonsFlag::ePrimaryFlag;
    }
  }
  if (aGdkModifierState & GDK_BUTTON3_MASK) {
    mouseEvent.mButtons |= MouseButtonsFlag::eSecondaryFlag;
  }
  if (aGdkModifierState & GDK_BUTTON2_MASK) {
    mouseEvent.mButtons |= MouseButtonsFlag::eMiddleFlag;
  }

  if (doLog) {
    MOZ_LOG(
        gKeyLog, LogLevel::Debug,
        ("%p   InitInputEvent, aInputEvent has mButtons, "
         "aInputEvent.mButtons=0x%04X (Left: %s, Right: %s, Middle: %s, "
         "4th (BACK): %s, 5th (FORWARD): %s)",
         keymapWrapper, mouseEvent.mButtons,
         GetBoolName(mouseEvent.mButtons & MouseButtonsFlag::ePrimaryFlag),
         GetBoolName(mouseEvent.mButtons & MouseButtonsFlag::eSecondaryFlag),
         GetBoolName(mouseEvent.mButtons & MouseButtonsFlag::eMiddleFlag),
         GetBoolName(mouseEvent.mButtons & MouseButtonsFlag::e4thFlag),
         GetBoolName(mouseEvent.mButtons & MouseButtonsFlag::e5thFlag)));
  }
}

/* static */
uint32_t KeymapWrapper::ComputeDOMKeyCode(const GdkEventKey* aGdkKeyEvent) {
  // If the keyval indicates it's a modifier key, we should use unshifted
  // key's modifier keyval.
  guint keyval = aGdkKeyEvent->keyval;
  if (GetModifierForGDKKeyval(keyval)) {
    // But if the keyval without modifiers isn't a modifier key, we
    // shouldn't use it.  E.g., Japanese keyboard layout's
    // Shift + Eisu-Toggle key is CapsLock.  This is an actual rare case,
    // Windows uses different keycode for a physical key for different
    // shift key state.
    guint keyvalWithoutModifier = GetGDKKeyvalWithoutModifier(aGdkKeyEvent);
    if (GetModifierForGDKKeyval(keyvalWithoutModifier)) {
      keyval = keyvalWithoutModifier;
    }
    // Note that the modifier keycode and activating or deactivating
    // modifier flag may be mismatched, but it's okay.  If a DOM key
    // event handler is testing a keydown event, it's more likely being
    // used to test which key is being pressed than to test which
    // modifier will become active.  So, if we computed DOM keycode
    // from modifier flag which were changing by the physical key, then
    // there would be no other way for the user to generate the original
    // keycode.
    uint32_t DOMKeyCode = GetDOMKeyCodeFromKeyPairs(keyval);
    NS_ASSERTION(DOMKeyCode, "All modifier keys must have a DOM keycode");
    return DOMKeyCode;
  }

  // If the key isn't printable, let's look at the key pairs.
  uint32_t charCode = GetCharCodeFor(aGdkKeyEvent);
  if (!charCode) {
    // Note that any key may be a function key because of some unusual keyboard
    // layouts.  I.e., even if the pressed key is a printable key of en-US
    // keyboard layout, we should expose the function key's keyCode value to
    // web apps because web apps should handle the keydown/keyup events as
    // inputted by usual keyboard layout.  For example, Hatchak keyboard
    // maps Tab key to "Digit3" key and Level3 Shift makes it "Backspace".
    // In this case, we should expose DOM_VK_BACK_SPACE (8).
    uint32_t DOMKeyCode = GetDOMKeyCodeFromKeyPairs(keyval);
    if (DOMKeyCode) {
      // XXX If DOMKeyCode is a function key's keyCode value, it might be
      //     better to consume necessary modifiers.  For example, if there is
      //     no Control Pad section on keyboard like notebook, Delete key is
      //     available only with Level3 Shift+"Backspace" key if using Hatchak.
      //     If web apps accept Delete key operation only when no modifiers are
      //     active, such users cannot use Delete key to do it.  However,
      //     Chromium doesn't consume such necessary modifiers.  So, our default
      //     behavior should keep not touching modifiers for compatibility, but
      //     it might be better to add a pref to consume necessary modifiers.
      return DOMKeyCode;
    }
    // If aGdkKeyEvent cannot be mapped to a DOM keyCode value, we should
    // refer keyCode value without modifiers because web apps should be
    // able to identify the key as far as possible.
    guint keyvalWithoutModifier = GetGDKKeyvalWithoutModifier(aGdkKeyEvent);
    return GetDOMKeyCodeFromKeyPairs(keyvalWithoutModifier);
  }

  // printable numpad keys should be resolved here.
  switch (keyval) {
    case GDK_KP_Multiply:
      return NS_VK_MULTIPLY;
    case GDK_KP_Add:
      return NS_VK_ADD;
    case GDK_KP_Separator:
      return NS_VK_SEPARATOR;
    case GDK_KP_Subtract:
      return NS_VK_SUBTRACT;
    case GDK_KP_Decimal:
      return NS_VK_DECIMAL;
    case GDK_KP_Divide:
      return NS_VK_DIVIDE;
    case GDK_KP_0:
      return NS_VK_NUMPAD0;
    case GDK_KP_1:
      return NS_VK_NUMPAD1;
    case GDK_KP_2:
      return NS_VK_NUMPAD2;
    case GDK_KP_3:
      return NS_VK_NUMPAD3;
    case GDK_KP_4:
      return NS_VK_NUMPAD4;
    case GDK_KP_5:
      return NS_VK_NUMPAD5;
    case GDK_KP_6:
      return NS_VK_NUMPAD6;
    case GDK_KP_7:
      return NS_VK_NUMPAD7;
    case GDK_KP_8:
      return NS_VK_NUMPAD8;
    case GDK_KP_9:
      return NS_VK_NUMPAD9;
  }

  KeymapWrapper* keymapWrapper = GetInstance();

  // Ignore all modifier state except NumLock.
  guint baseState =
      (aGdkKeyEvent->state & keymapWrapper->GetGdkModifierMask(NUM_LOCK));

  // Basically, we should use unmodified character for deciding our keyCode.
  uint32_t unmodifiedChar = keymapWrapper->GetCharCodeFor(
      aGdkKeyEvent, baseState, aGdkKeyEvent->group);
  if (IsBasicLatinLetterOrNumeral(unmodifiedChar)) {
    // If the unmodified character is an ASCII alphabet or an ASCII
    // numeric, it's the best hint for deciding our keyCode.
    return WidgetUtils::ComputeKeyCodeFromChar(unmodifiedChar);
  }

  // If the unmodified character is not an ASCII character, that means we
  // couldn't find the hint. We should reset it.
  if (!IsPrintableASCIICharacter(unmodifiedChar)) {
    unmodifiedChar = 0;
  }

  // Retry with shifted keycode.
  guint shiftState = (baseState | keymapWrapper->GetGdkModifierMask(SHIFT));
  uint32_t shiftedChar = keymapWrapper->GetCharCodeFor(aGdkKeyEvent, shiftState,
                                                       aGdkKeyEvent->group);
  if (IsBasicLatinLetterOrNumeral(shiftedChar)) {
    // A shifted character can be an ASCII alphabet on Hebrew keyboard
    // layout. And also shifted character can be an ASCII numeric on
    // AZERTY keyboad layout.  Then, it's a good hint for deciding our
    // keyCode.
    return WidgetUtils::ComputeKeyCodeFromChar(shiftedChar);
  }

  // If the shifted unmodified character isn't an ASCII character, we should
  // discard it too.
  if (!IsPrintableASCIICharacter(shiftedChar)) {
    shiftedChar = 0;
  }

  // If current keyboard layout isn't ASCII alphabet inputtable layout,
  // look for ASCII alphabet inputtable keyboard layout.  If the key
  // inputs an ASCII alphabet or an ASCII numeric, we should use it
  // for deciding our keyCode.
  uint32_t unmodCharLatin = 0;
  uint32_t shiftedCharLatin = 0;
  if (!keymapWrapper->IsLatinGroup(aGdkKeyEvent->group)) {
    gint minGroup = keymapWrapper->GetFirstLatinGroup();
    if (minGroup >= 0) {
      unmodCharLatin =
          keymapWrapper->GetCharCodeFor(aGdkKeyEvent, baseState, minGroup);
      if (IsBasicLatinLetterOrNumeral(unmodCharLatin)) {
        // If the unmodified character is an ASCII alphabet or
        // an ASCII numeric, we should use it for the keyCode.
        return WidgetUtils::ComputeKeyCodeFromChar(unmodCharLatin);
      }
      // If the unmodified character in the alternative ASCII capable
      // keyboard layout isn't an ASCII character, that means we couldn't
      // find the hint. We should reset it.
      if (!IsPrintableASCIICharacter(unmodCharLatin)) {
        unmodCharLatin = 0;
      }
      shiftedCharLatin =
          keymapWrapper->GetCharCodeFor(aGdkKeyEvent, shiftState, minGroup);
      if (IsBasicLatinLetterOrNumeral(shiftedCharLatin)) {
        // If the shifted character is an ASCII alphabet or an ASCII
        // numeric, we should use it for the keyCode.
        return WidgetUtils::ComputeKeyCodeFromChar(shiftedCharLatin);
      }
      // If the shifted unmodified character in the alternative ASCII
      // capable keyboard layout isn't an ASCII character, we should
      // discard it too.
      if (!IsPrintableASCIICharacter(shiftedCharLatin)) {
        shiftedCharLatin = 0;
      }
    }
  }

  // If the key itself or with Shift state on active keyboard layout produces
  // an ASCII punctuation character, we should decide keyCode value with it.
  if (unmodifiedChar || shiftedChar) {
    return WidgetUtils::ComputeKeyCodeFromChar(unmodifiedChar ? unmodifiedChar
                                                              : shiftedChar);
  }

  // If the key itself or with Shift state on alternative ASCII capable
  // keyboard layout produces an ASCII punctuation character, we should
  // decide keyCode value with it.  Note that We've returned 0 for long
  // time if keyCode isn't for an alphabet keys or a numeric key even in
  // alternative ASCII capable keyboard layout because we decided that we
  // should avoid setting same keyCode value to 2 or more keys since active
  // keyboard layout may have a key to input the punctuation with different
  // key.  However, setting keyCode to 0 makes some web applications which
  // are aware of neither KeyboardEvent.key nor KeyboardEvent.code not work
  // with Firefox when user selects non-ASCII capable keyboard layout such
  // as Russian and Thai.  So, if alternative ASCII capable keyboard layout
  // has keyCode value for the key, we should use it.  In other words, this
  // behavior means that non-ASCII capable keyboard layout overrides some
  // keys' keyCode value only if the key produces ASCII character by itself
  // or with Shift key.
  if (unmodCharLatin || shiftedCharLatin) {
    return WidgetUtils::ComputeKeyCodeFromChar(
        unmodCharLatin ? unmodCharLatin : shiftedCharLatin);
  }

  // Otherwise, let's decide keyCode value from the hardware_keycode
  // value on major keyboard layout.
  CodeNameIndex code = ComputeDOMCodeNameIndex(aGdkKeyEvent);
  return WidgetKeyboardEvent::GetFallbackKeyCodeOfPunctuationKey(code);
}

KeyNameIndex KeymapWrapper::ComputeDOMKeyNameIndex(
    const GdkEventKey* aGdkKeyEvent) {
  switch (aGdkKeyEvent->keyval) {
#define NS_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX(aNativeKey, aKeyNameIndex) \
  case aNativeKey:                                                     \
    return aKeyNameIndex;

#include "NativeKeyToDOMKeyName.h"

#undef NS_NATIVE_KEY_TO_DOM_KEY_NAME_INDEX

    default:
      break;
  }

  return KEY_NAME_INDEX_Unidentified;
}

/* static */
CodeNameIndex KeymapWrapper::ComputeDOMCodeNameIndex(
    const GdkEventKey* aGdkKeyEvent) {
  switch (aGdkKeyEvent->hardware_keycode) {
#define NS_NATIVE_KEY_TO_DOM_CODE_NAME_INDEX(aNativeKey, aCodeNameIndex) \
  case aNativeKey:                                                       \
    return aCodeNameIndex;

#include "NativeKeyToDOMCodeName.h"

#undef NS_NATIVE_KEY_TO_DOM_CODE_NAME_INDEX

    default:
      break;
  }

  return CODE_NAME_INDEX_UNKNOWN;
}

/* static */
bool KeymapWrapper::DispatchKeyDownOrKeyUpEvent(nsWindow* aWindow,
                                                GdkEventKey* aGdkKeyEvent,
                                                bool aIsProcessedByIME,
                                                bool* aIsCancelled) {
  MOZ_ASSERT(aIsCancelled, "aIsCancelled must not be nullptr");

  *aIsCancelled = false;

  if (aGdkKeyEvent->type == GDK_KEY_PRESS && aGdkKeyEvent->keyval == GDK_Tab &&
      AreModifiersActive(CTRL | ALT, aGdkKeyEvent->state)) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("  DispatchKeyDownOrKeyUpEvent(), didn't dispatch keyboard events "
             "because it's Ctrl + Alt + Tab"));
    return false;
  }

  EventMessage message =
      aGdkKeyEvent->type == GDK_KEY_PRESS ? eKeyDown : eKeyUp;
  WidgetKeyboardEvent keyEvent(true, message, aWindow);
  KeymapWrapper::InitKeyEvent(keyEvent, aGdkKeyEvent, aIsProcessedByIME);
  return DispatchKeyDownOrKeyUpEvent(aWindow, keyEvent, aIsCancelled);
}

/* static */
bool KeymapWrapper::DispatchKeyDownOrKeyUpEvent(
    nsWindow* aWindow, WidgetKeyboardEvent& aKeyboardEvent,
    bool* aIsCancelled) {
  MOZ_ASSERT(aIsCancelled, "aIsCancelled must not be nullptr");

  *aIsCancelled = false;

  RefPtr<TextEventDispatcher> dispatcher = aWindow->GetTextEventDispatcher();
  nsresult rv = dispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gKeyLog, LogLevel::Error,
            ("  DispatchKeyDownOrKeyUpEvent(), stopped dispatching %s event "
             "because of failed to initialize TextEventDispatcher",
             ToChar(aKeyboardEvent.mMessage)));
    return FALSE;
  }

  nsEventStatus status = nsEventStatus_eIgnore;
  bool dispatched = dispatcher->DispatchKeyboardEvent(
      aKeyboardEvent.mMessage, aKeyboardEvent, status, nullptr);
  *aIsCancelled = (status == nsEventStatus_eConsumeNoDefault);
  return dispatched;
}

/* static */
bool KeymapWrapper::MaybeDispatchContextMenuEvent(nsWindow* aWindow,
                                                  const GdkEventKey* aEvent) {
  KeyNameIndex keyNameIndex = ComputeDOMKeyNameIndex(aEvent);

  // Shift+F10 and ContextMenu should cause eContextMenu event.
  if (keyNameIndex != KEY_NAME_INDEX_F10 &&
      keyNameIndex != KEY_NAME_INDEX_ContextMenu) {
    return false;
  }

  WidgetPointerEvent contextMenuEvent(true, eContextMenu, aWindow,
                                      WidgetMouseEvent::eContextMenuKey);
  contextMenuEvent.mRefPoint = LayoutDeviceIntPoint(0, 0);
  contextMenuEvent.AssignEventTime(aWindow->GetWidgetEventTime(aEvent->time));
  contextMenuEvent.mClickCount = 1;
  KeymapWrapper::InitInputEvent(contextMenuEvent, aEvent->state);

  if (contextMenuEvent.IsControl() || contextMenuEvent.IsMeta() ||
      contextMenuEvent.IsAlt()) {
    return false;
  }

  // If the key is ContextMenu, then an eContextMenu mouse event is
  // dispatched regardless of the state of the Shift modifier.  When it is
  // pressed without the Shift modifier, a web page can prevent the default
  // context menu action.  When pressed with the Shift modifier, the web page
  // cannot prevent the default context menu action.
  // (PresShell::HandleEventInternal() sets mOnlyChromeDispatch to true.)

  // If the key is F10, it needs Shift state because Shift+F10 is well-known
  // shortcut key on Linux.  However, eContextMenu with Shift state is
  // special.  It won't fire "contextmenu" event in the web content for
  // blocking web page to prevent its default.  Therefore, this combination
  // should work same as ContextMenu key.
  // XXX Should we allow to block web page to prevent its default with
  //     Ctrl+Shift+F10 or Alt+Shift+F10 instead?
  if (keyNameIndex == KEY_NAME_INDEX_F10) {
    if (!contextMenuEvent.IsShift()) {
      return false;
    }
    contextMenuEvent.mModifiers &= ~MODIFIER_SHIFT;
  }

  aWindow->DispatchInputEvent(&contextMenuEvent);
  return true;
}

/* static*/
void KeymapWrapper::HandleKeyPressEvent(nsWindow* aWindow,
                                        GdkEventKey* aGdkKeyEvent) {
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("HandleKeyPressEvent(aWindow=%p, aGdkKeyEvent={ type=%s, "
           "keyval=%s(0x%X), state=0x%08X, hardware_keycode=0x%08X, "
           "time=%u, is_modifier=%s })",
           aWindow,
           ((aGdkKeyEvent->type == GDK_KEY_PRESS) ? "GDK_KEY_PRESS"
                                                  : "GDK_KEY_RELEASE"),
           gdk_keyval_name(aGdkKeyEvent->keyval), aGdkKeyEvent->keyval,
           aGdkKeyEvent->state, aGdkKeyEvent->hardware_keycode,
           aGdkKeyEvent->time, GetBoolName(aGdkKeyEvent->is_modifier)));

  // if we are in the middle of composing text, XIM gets to see it
  // before mozilla does.
  // FYI: Don't dispatch keydown event before notifying IME of the event
  //      because IME may send a key event synchronously and consume the
  //      original event.
  bool IMEWasEnabled = false;
  KeyHandlingState handlingState = KeyHandlingState::eNotHandled;
  RefPtr<IMContextWrapper> imContext = aWindow->GetIMContext();
  if (imContext) {
    IMEWasEnabled = imContext->IsEnabled();
    handlingState = imContext->OnKeyEvent(aWindow, aGdkKeyEvent);
    if (handlingState == KeyHandlingState::eHandled) {
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyPressEvent(), the event was handled by "
               "IMContextWrapper"));
      return;
    }
  }

  // work around for annoying things.
  if (aGdkKeyEvent->keyval == GDK_Tab &&
      AreModifiersActive(CTRL | ALT, aGdkKeyEvent->state)) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("  HandleKeyPressEvent(), didn't dispatch keyboard events "
             "because it's Ctrl + Alt + Tab"));
    return;
  }

  // Dispatch keydown event always.  At auto repeating, we should send
  // KEYDOWN -> KEYPRESS -> KEYDOWN -> KEYPRESS ... -> KEYUP
  // However, old distributions (e.g., Ubuntu 9.10) sent native key
  // release event, so, on such platform, the DOM events will be:
  // KEYDOWN -> KEYPRESS -> KEYUP -> KEYDOWN -> KEYPRESS -> KEYUP...

  bool isKeyDownCancelled = false;
  if (handlingState == KeyHandlingState::eNotHandled) {
    if (DispatchKeyDownOrKeyUpEvent(aWindow, aGdkKeyEvent, false,
                                    &isKeyDownCancelled) &&
        (MOZ_UNLIKELY(aWindow->IsDestroyed()) || isKeyDownCancelled)) {
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyPressEvent(), dispatched eKeyDown event and "
               "stopped handling the event because %s",
               aWindow->IsDestroyed() ? "the window has been destroyed"
                                      : "the event was consumed"));
      return;
    }
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("  HandleKeyPressEvent(), dispatched eKeyDown event and "
             "it wasn't consumed"));
    handlingState = KeyHandlingState::eNotHandledButEventDispatched;
  }

  // If a keydown event handler causes to enable IME, i.e., it moves
  // focus from IME unusable content to IME usable editor, we should
  // send the native key event to IME for the first input on the editor.
  imContext = aWindow->GetIMContext();
  if (!IMEWasEnabled && imContext && imContext->IsEnabled()) {
    // Notice our keydown event was already dispatched.  This prevents
    // unnecessary DOM keydown event in the editor.
    handlingState = imContext->OnKeyEvent(aWindow, aGdkKeyEvent, true);
    if (handlingState == KeyHandlingState::eHandled) {
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyPressEvent(), the event was handled by "
               "IMContextWrapper which was enabled by the preceding eKeyDown "
               "event"));
      return;
    }
  }

  // Look for specialized app-command keys
  switch (aGdkKeyEvent->keyval) {
    case GDK_Back:
      aWindow->DispatchCommandEvent(nsGkAtoms::Back);
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyPressEvent(), dispatched \"Back\" command event"));
      return;
    case GDK_Forward:
      aWindow->DispatchCommandEvent(nsGkAtoms::Forward);
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyPressEvent(), dispatched \"Forward\" command "
               "event"));
      return;
    case GDK_Reload:
    case GDK_Refresh:
      aWindow->DispatchCommandEvent(nsGkAtoms::Reload);
      return;
    case GDK_Stop:
      aWindow->DispatchCommandEvent(nsGkAtoms::Stop);
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyPressEvent(), dispatched \"Stop\" command event"));
      return;
    case GDK_Search:
      aWindow->DispatchCommandEvent(nsGkAtoms::Search);
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyPressEvent(), dispatched \"Search\" command event"));
      return;
    case GDK_Favorites:
      aWindow->DispatchCommandEvent(nsGkAtoms::Bookmarks);
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyPressEvent(), dispatched \"Bookmarks\" command "
               "event"));
      return;
    case GDK_HomePage:
      aWindow->DispatchCommandEvent(nsGkAtoms::Home);
      return;
    case GDK_Copy:
    case GDK_F16:  // F16, F20, F18, F14 are old keysyms for Copy Cut Paste Undo
      aWindow->DispatchContentCommandEvent(eContentCommandCopy);
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyPressEvent(), dispatched \"Copy\" content command "
               "event"));
      return;
    case GDK_Cut:
    case GDK_F20:
      aWindow->DispatchContentCommandEvent(eContentCommandCut);
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyPressEvent(), dispatched \"Cut\" content command "
               "event"));
      return;
    case GDK_Paste:
    case GDK_F18:
      aWindow->DispatchContentCommandEvent(eContentCommandPaste);
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyPressEvent(), dispatched \"Paste\" content command "
               "event"));
      return;
    case GDK_Redo:
      aWindow->DispatchContentCommandEvent(eContentCommandRedo);
      return;
    case GDK_Undo:
    case GDK_F14:
      aWindow->DispatchContentCommandEvent(eContentCommandUndo);
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyPressEvent(), dispatched \"Undo\" content command "
               "event"));
      return;
    default:
      break;
  }

  // before we dispatch a key, check if it's the context menu key.
  // If so, send a context menu key event instead.
  if (MaybeDispatchContextMenuEvent(aWindow, aGdkKeyEvent)) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("  HandleKeyPressEvent(), stopped dispatching eKeyPress event "
             "because eContextMenu event was dispatched"));
    return;
  }

  RefPtr<TextEventDispatcher> textEventDispatcher =
      aWindow->GetTextEventDispatcher();
  nsresult rv = textEventDispatcher->BeginNativeInputTransaction();
  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_LOG(gKeyLog, LogLevel::Error,
            ("  HandleKeyPressEvent(), stopped dispatching eKeyPress event "
             "because of failed to initialize TextEventDispatcher"));
    return;
  }

  // If the character code is in the BMP, send the key press event.
  // Otherwise, send a compositionchange event with the equivalent UTF-16
  // string.
  // TODO: Investigate other browser's behavior in this case because
  //       this hack is odd for UI Events.
  WidgetKeyboardEvent keypressEvent(true, eKeyPress, aWindow);
  KeymapWrapper::InitKeyEvent(keypressEvent, aGdkKeyEvent, false);
  nsEventStatus status = nsEventStatus_eIgnore;
  if (keypressEvent.mKeyNameIndex != KEY_NAME_INDEX_USE_STRING ||
      keypressEvent.mKeyValue.Length() == 1) {
    if (textEventDispatcher->MaybeDispatchKeypressEvents(keypressEvent, status,
                                                         aGdkKeyEvent)) {
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyPressEvent(), dispatched eKeyPress event "
               "(status=%s)",
               GetStatusName(status)));
    } else {
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyPressEvent(), didn't dispatch eKeyPress event "
               "(status=%s)",
               GetStatusName(status)));
    }
  } else {
    WidgetEventTime eventTime = aWindow->GetWidgetEventTime(aGdkKeyEvent->time);
    textEventDispatcher->CommitComposition(status, &keypressEvent.mKeyValue,
                                           &eventTime);
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("  HandleKeyPressEvent(), dispatched a set of composition "
             "events"));
  }
}

/* static */
bool KeymapWrapper::HandleKeyReleaseEvent(nsWindow* aWindow,
                                          GdkEventKey* aGdkKeyEvent) {
  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("HandleKeyReleaseEvent(aWindow=%p, aGdkKeyEvent={ type=%s, "
           "keyval=%s(0x%X), state=0x%08X, hardware_keycode=0x%08X, "
           "time=%u, is_modifier=%s })",
           aWindow,
           ((aGdkKeyEvent->type == GDK_KEY_PRESS) ? "GDK_KEY_PRESS"
                                                  : "GDK_KEY_RELEASE"),
           gdk_keyval_name(aGdkKeyEvent->keyval), aGdkKeyEvent->keyval,
           aGdkKeyEvent->state, aGdkKeyEvent->hardware_keycode,
           aGdkKeyEvent->time, GetBoolName(aGdkKeyEvent->is_modifier)));

  RefPtr<IMContextWrapper> imContext = aWindow->GetIMContext();
  if (imContext) {
    KeyHandlingState handlingState =
        imContext->OnKeyEvent(aWindow, aGdkKeyEvent);
    if (handlingState != KeyHandlingState::eNotHandled) {
      MOZ_LOG(gKeyLog, LogLevel::Info,
              ("  HandleKeyReleaseEvent(), the event was handled by "
               "IMContextWrapper"));
      return true;
    }
  }

  bool isCancelled = false;
  if (NS_WARN_IF(!DispatchKeyDownOrKeyUpEvent(aWindow, aGdkKeyEvent, false,
                                              &isCancelled))) {
    MOZ_LOG(gKeyLog, LogLevel::Error,
            ("  HandleKeyReleaseEvent(), didn't dispatch eKeyUp event"));
    return false;
  }

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("  HandleKeyReleaseEvent(), dispatched eKeyUp event "
           "(isCancelled=%s)",
           GetBoolName(isCancelled)));
  return true;
}

guint KeymapWrapper::GetModifierState(GdkEventKey* aGdkKeyEvent,
                                      KeymapWrapper* aWrapper) {
  guint state = aGdkKeyEvent->state;
  if (!aGdkKeyEvent->is_modifier) {
    return state;
  }
#ifdef MOZ_X11
  // NOTE: The state of given key event indicates adjacent state of
  // modifier keys.  E.g., even if the event is Shift key press event,
  // the bit for Shift is still false.  By the same token, even if the
  // event is Shift key release event, the bit for Shift is still true.
  // Unfortunately, gdk_keyboard_get_modifiers() returns current modifier
  // state.  It means if there're some pending modifier key press or
  // key release events, the result isn't what we want.
  GdkDisplay* gdkDisplay = gdk_display_get_default();
  if (GdkIsX11Display(gdkDisplay)) {
    GdkDisplay* gdkDisplay = gdk_display_get_default();
    Display* display = gdk_x11_display_get_xdisplay(gdkDisplay);
    if (XEventsQueued(display, QueuedAfterReading)) {
      XEvent nextEvent;
      XPeekEvent(display, &nextEvent);
      if (nextEvent.type == aWrapper->mXKBBaseEventCode) {
        XkbEvent* XKBEvent = (XkbEvent*)&nextEvent;
        if (XKBEvent->any.xkb_type == XkbStateNotify) {
          XkbStateNotifyEvent* stateNotifyEvent =
              (XkbStateNotifyEvent*)XKBEvent;
          state &= ~0xFF;
          state |= stateNotifyEvent->lookup_mods;
        }
      }
    }
    return state;
  }
#endif
#ifdef MOZ_WAYLAND
  guint mask = 0;
  switch (aGdkKeyEvent->keyval) {
    case GDK_Shift_L:
    case GDK_Shift_R:
      mask = aWrapper->GetGdkModifierMask(SHIFT);
      break;
    case GDK_Control_L:
    case GDK_Control_R:
      mask = aWrapper->GetGdkModifierMask(CTRL);
      break;
    case GDK_Alt_L:
    case GDK_Alt_R:
      mask = aWrapper->GetGdkModifierMask(ALT);
      break;
    case GDK_Super_L:
    case GDK_Super_R:
      mask = aWrapper->GetGdkModifierMask(SUPER);
      break;
    case GDK_Hyper_L:
    case GDK_Hyper_R:
      mask = aWrapper->GetGdkModifierMask(HYPER);
      break;
    case GDK_Meta_L:
    case GDK_Meta_R:
      mask = aWrapper->GetGdkModifierMask(META);
      break;
    case GDK_ISO_Level3_Shift:
    case GDK_Mode_switch:
      mask = aWrapper->GetGdkModifierMask(LEVEL3);
      break;
    case GDK_ISO_Level5_Shift:
      mask = aWrapper->GetGdkModifierMask(LEVEL5);
      break;
    default:
      break;
  }
  if (aGdkKeyEvent->type == GDK_KEY_PRESS) {
    state |= mask;
  } else {
    state &= ~mask;
  }
#endif
  return state;
}

/* static */
void KeymapWrapper::InitKeyEvent(WidgetKeyboardEvent& aKeyEvent,
                                 GdkEventKey* aGdkKeyEvent,
                                 bool aIsProcessedByIME) {
  MOZ_ASSERT(
      !aIsProcessedByIME || aKeyEvent.mMessage != eKeyPress,
      "If the key event is handled by IME, keypress event shouldn't be fired");

  KeymapWrapper* keymapWrapper = GetInstance();

  aKeyEvent.mCodeNameIndex = ComputeDOMCodeNameIndex(aGdkKeyEvent);
  MOZ_ASSERT(aKeyEvent.mCodeNameIndex != CODE_NAME_INDEX_USE_STRING);
  aKeyEvent.mKeyNameIndex =
      aIsProcessedByIME ? KEY_NAME_INDEX_Process
                        : keymapWrapper->ComputeDOMKeyNameIndex(aGdkKeyEvent);
  if (aKeyEvent.mKeyNameIndex == KEY_NAME_INDEX_Unidentified) {
    uint32_t charCode = GetCharCodeFor(aGdkKeyEvent);
    if (!charCode) {
      charCode = keymapWrapper->GetUnmodifiedCharCodeFor(aGdkKeyEvent);
    }
    if (charCode) {
      aKeyEvent.mKeyNameIndex = KEY_NAME_INDEX_USE_STRING;
      MOZ_ASSERT(aKeyEvent.mKeyValue.IsEmpty(),
                 "Uninitialized mKeyValue must be empty");
      AppendUCS4ToUTF16(charCode, aKeyEvent.mKeyValue);
    }
  }

  if (aIsProcessedByIME) {
    aKeyEvent.mKeyCode = NS_VK_PROCESSKEY;
  } else if (aKeyEvent.mKeyNameIndex != KEY_NAME_INDEX_USE_STRING ||
             aKeyEvent.mMessage != eKeyPress) {
    aKeyEvent.mKeyCode = ComputeDOMKeyCode(aGdkKeyEvent);
  } else {
    aKeyEvent.mKeyCode = 0;
  }

  const guint modifierState = GetModifierState(aGdkKeyEvent, keymapWrapper);
  InitInputEvent(aKeyEvent, modifierState);

  switch (aGdkKeyEvent->keyval) {
    case GDK_Shift_L:
    case GDK_Control_L:
    case GDK_Alt_L:
    case GDK_Super_L:
    case GDK_Hyper_L:
    case GDK_Meta_L:
      aKeyEvent.mLocation = eKeyLocationLeft;
      break;

    case GDK_Shift_R:
    case GDK_Control_R:
    case GDK_Alt_R:
    case GDK_Super_R:
    case GDK_Hyper_R:
    case GDK_Meta_R:
      aKeyEvent.mLocation = eKeyLocationRight;
      break;

    case GDK_KP_0:
    case GDK_KP_1:
    case GDK_KP_2:
    case GDK_KP_3:
    case GDK_KP_4:
    case GDK_KP_5:
    case GDK_KP_6:
    case GDK_KP_7:
    case GDK_KP_8:
    case GDK_KP_9:
    case GDK_KP_Space:
    case GDK_KP_Tab:
    case GDK_KP_Enter:
    case GDK_KP_F1:
    case GDK_KP_F2:
    case GDK_KP_F3:
    case GDK_KP_F4:
    case GDK_KP_Home:
    case GDK_KP_Left:
    case GDK_KP_Up:
    case GDK_KP_Right:
    case GDK_KP_Down:
    case GDK_KP_Prior:  // same as GDK_KP_Page_Up
    case GDK_KP_Next:   // same as GDK_KP_Page_Down
    case GDK_KP_End:
    case GDK_KP_Begin:
    case GDK_KP_Insert:
    case GDK_KP_Delete:
    case GDK_KP_Equal:
    case GDK_KP_Multiply:
    case GDK_KP_Add:
    case GDK_KP_Separator:
    case GDK_KP_Subtract:
    case GDK_KP_Decimal:
    case GDK_KP_Divide:
      aKeyEvent.mLocation = eKeyLocationNumpad;
      break;

    default:
      aKeyEvent.mLocation = eKeyLocationStandard;
      break;
  }

  // The transformations above and in gdk for the keyval are not invertible
  // so link to the GdkEvent (which will vanish soon after return from the
  // event callback) to give plugins access to hardware_keycode and state.
  // (An XEvent would be nice but the GdkEvent is good enough.)
  aKeyEvent.mNativeKeyEvent = static_cast<void*>(aGdkKeyEvent);
  aKeyEvent.mIsRepeat =
      sRepeatState == REPEATING &&
      aGdkKeyEvent->hardware_keycode == sLastRepeatableHardwareKeyCode;

  MOZ_LOG(
      gKeyLog, LogLevel::Info,
      ("%p InitKeyEvent, modifierState=0x%08X "
       "aKeyEvent={ mMessage=%s, isShift=%s, isControl=%s, "
       "isAlt=%s, isMeta=%s, isAltGraph=%s mKeyCode=0x%02X, mCharCode=%s, "
       "mKeyNameIndex=%s, mKeyValue=%s, mCodeNameIndex=%s, mCodeValue=%s, "
       "mLocation=%s, mIsRepeat=%s }",
       keymapWrapper, modifierState, ToChar(aKeyEvent.mMessage),
       GetBoolName(aKeyEvent.IsShift()), GetBoolName(aKeyEvent.IsControl()),
       GetBoolName(aKeyEvent.IsAlt()), GetBoolName(aKeyEvent.IsMeta()),
       GetBoolName(aKeyEvent.IsAltGraph()), aKeyEvent.mKeyCode,
       GetCharacterCodeName(static_cast<char16_t>(aKeyEvent.mCharCode)).get(),
       ToString(aKeyEvent.mKeyNameIndex).get(),
       GetCharacterCodeNames(aKeyEvent.mKeyValue).get(),
       ToString(aKeyEvent.mCodeNameIndex).get(),
       GetCharacterCodeNames(aKeyEvent.mCodeValue).get(),
       GetKeyLocationName(aKeyEvent.mLocation).get(),
       GetBoolName(aKeyEvent.mIsRepeat)));
}

/* static */
uint32_t KeymapWrapper::GetCharCodeFor(const GdkEventKey* aGdkKeyEvent) {
  // Anything above 0xf000 is considered a non-printable
  // Exception: directly encoded UCS characters
  if (aGdkKeyEvent->keyval > 0xf000 &&
      (aGdkKeyEvent->keyval & 0xff000000) != 0x01000000) {
    // Keypad keys are an exception: they return a value different
    // from their non-keypad equivalents, but mozilla doesn't distinguish.
    switch (aGdkKeyEvent->keyval) {
      case GDK_KP_Space:
        return ' ';
      case GDK_KP_Equal:
        return '=';
      case GDK_KP_Multiply:
        return '*';
      case GDK_KP_Add:
        return '+';
      case GDK_KP_Separator:
        return ',';
      case GDK_KP_Subtract:
        return '-';
      case GDK_KP_Decimal:
        return '.';
      case GDK_KP_Divide:
        return '/';
      case GDK_KP_0:
        return '0';
      case GDK_KP_1:
        return '1';
      case GDK_KP_2:
        return '2';
      case GDK_KP_3:
        return '3';
      case GDK_KP_4:
        return '4';
      case GDK_KP_5:
        return '5';
      case GDK_KP_6:
        return '6';
      case GDK_KP_7:
        return '7';
      case GDK_KP_8:
        return '8';
      case GDK_KP_9:
        return '9';
      default:
        return 0;  // non-printables
    }
  }

  static const long MAX_UNICODE = 0x10FFFF;

  // we're supposedly printable, let's try to convert
  long ucs = keysym2ucs(aGdkKeyEvent->keyval);
  if ((ucs != -1) && (ucs < MAX_UNICODE)) {
    return ucs;
  }

  // I guess we couldn't convert
  return 0;
}

uint32_t KeymapWrapper::GetCharCodeFor(const GdkEventKey* aGdkKeyEvent,
                                       guint aGdkModifierState, gint aGroup) {
  guint keyval;
  if (!gdk_keymap_translate_keyboard_state(
          mGdkKeymap, aGdkKeyEvent->hardware_keycode,
          GdkModifierType(aGdkModifierState), aGroup, &keyval, nullptr, nullptr,
          nullptr)) {
    return 0;
  }
  GdkEventKey tmpEvent = *aGdkKeyEvent;
  tmpEvent.state = aGdkModifierState;
  tmpEvent.keyval = keyval;
  tmpEvent.group = aGroup;
  return GetCharCodeFor(&tmpEvent);
}

uint32_t KeymapWrapper::GetUnmodifiedCharCodeFor(
    const GdkEventKey* aGdkKeyEvent) {
  guint state =
      aGdkKeyEvent->state &
      (GetGdkModifierMask(SHIFT) | GetGdkModifierMask(CAPS_LOCK) |
       GetGdkModifierMask(NUM_LOCK) | GetGdkModifierMask(SCROLL_LOCK) |
       GetGdkModifierMask(LEVEL3) | GetGdkModifierMask(LEVEL5));
  uint32_t charCode =
      GetCharCodeFor(aGdkKeyEvent, GdkModifierType(state), aGdkKeyEvent->group);
  if (charCode) {
    return charCode;
  }
  // If no character is mapped to the key when Level3 Shift or Level5 Shift
  // is active, let's return a character which is inputted by the key without
  // Level3 nor Level5 Shift.
  guint stateWithoutAltGraph =
      state & ~(GetGdkModifierMask(LEVEL3) | GetGdkModifierMask(LEVEL5));
  if (state == stateWithoutAltGraph) {
    return 0;
  }
  return GetCharCodeFor(aGdkKeyEvent, GdkModifierType(stateWithoutAltGraph),
                        aGdkKeyEvent->group);
}

gint KeymapWrapper::GetKeyLevel(GdkEventKey* aGdkKeyEvent) {
  gint level;
  if (!gdk_keymap_translate_keyboard_state(
          mGdkKeymap, aGdkKeyEvent->hardware_keycode,
          GdkModifierType(aGdkKeyEvent->state), aGdkKeyEvent->group, nullptr,
          nullptr, &level, nullptr)) {
    return -1;
  }
  return level;
}

gint KeymapWrapper::GetFirstLatinGroup() {
  GdkKeymapKey* keys;
  gint count;
  gint minGroup = -1;
  if (gdk_keymap_get_entries_for_keyval(mGdkKeymap, GDK_a, &keys, &count)) {
    // find the minimum number group for latin inputtable layout
    for (gint i = 0; i < count && minGroup != 0; ++i) {
      if (keys[i].level != 0 && keys[i].level != 1) {
        continue;
      }
      if (minGroup >= 0 && keys[i].group > minGroup) {
        continue;
      }
      minGroup = keys[i].group;
    }
    g_free(keys);
  }
  return minGroup;
}

bool KeymapWrapper::IsLatinGroup(guint8 aGroup) {
  GdkKeymapKey* keys;
  gint count;
  bool result = false;
  if (gdk_keymap_get_entries_for_keyval(mGdkKeymap, GDK_a, &keys, &count)) {
    for (gint i = 0; i < count; ++i) {
      if (keys[i].level != 0 && keys[i].level != 1) {
        continue;
      }
      if (keys[i].group == aGroup) {
        result = true;
        break;
      }
    }
    g_free(keys);
  }
  return result;
}

bool KeymapWrapper::IsAutoRepeatableKey(guint aHardwareKeyCode) {
  GdkDisplay* gdkDisplay = gdk_display_get_default();
#ifdef MOZ_X11
  if (GdkIsX11Display(gdkDisplay)) {
    uint8_t indexOfArray = aHardwareKeyCode / 8;
    MOZ_ASSERT(indexOfArray < std::size(mKeyboardState.auto_repeats),
               "invalid index");
    char bitMask = 1 << (aHardwareKeyCode % 8);
    return (mKeyboardState.auto_repeats[indexOfArray] & bitMask) != 0;
  }
#endif
#ifdef MOZ_WAYLAND
  if (GdkIsWaylandDisplay(gdkDisplay)) {
    if (MOZ_UNLIKELY(!mXkbKeymap)) {
#  ifdef DEBUG
      static bool sWarned = false;
      NS_WARNING_ASSERTION(sWarned, "no keymap!");
      sWarned = true;
#  endif
      return false;
    }
    return !!xkb_keymap_key_repeats(mXkbKeymap, aHardwareKeyCode);
  }
#endif
  return false;
}

/* static */
bool KeymapWrapper::IsBasicLatinLetterOrNumeral(uint32_t aCharCode) {
  return (aCharCode >= 'a' && aCharCode <= 'z') ||
         (aCharCode >= 'A' && aCharCode <= 'Z') ||
         (aCharCode >= '0' && aCharCode <= '9');
}

/* static */
guint KeymapWrapper::GetGDKKeyvalWithoutModifier(
    const GdkEventKey* aGdkKeyEvent) {
  KeymapWrapper* keymapWrapper = GetInstance();
  guint state =
      (aGdkKeyEvent->state & keymapWrapper->GetGdkModifierMask(NUM_LOCK));
  guint keyval;
  if (!gdk_keymap_translate_keyboard_state(
          keymapWrapper->mGdkKeymap, aGdkKeyEvent->hardware_keycode,
          GdkModifierType(state), aGdkKeyEvent->group, &keyval, nullptr,
          nullptr, nullptr)) {
    return 0;
  }
  return keyval;
}

struct KeyCodeData {
  const char* str;
  size_t strlength;
  uint32_t keycode;
};

static struct KeyCodeData gKeyCodes[] = {
#define NS_DEFINE_VK(aDOMKeyName, aDOMKeyCode) \
  {#aDOMKeyName, sizeof(#aDOMKeyName) - 1, aDOMKeyCode},
#include "mozilla/VirtualKeyCodeList.h"
#undef NS_DEFINE_VK
    {nullptr, 0, 0}};

struct KeyPair {
  uint32_t DOMKeyCode;
  guint GDKKeyval;
};

//
// Netscape keycodes are defined in widget/public/nsGUIEvent.h
// GTK keycodes are defined in <gdk/gdkkeysyms.h>
//
static const KeyPair gKeyPairs[] = {
    {NS_VK_CANCEL, GDK_Cancel},
    {NS_VK_BACK, GDK_BackSpace},
    {NS_VK_TAB, GDK_Tab},
    {NS_VK_CLEAR, GDK_Clear},
    {NS_VK_RETURN, GDK_Return},
    {NS_VK_SHIFT, GDK_Shift_L},
    {NS_VK_CONTROL, GDK_Control_L},
    {NS_VK_ALT, GDK_Alt_L},
    {NS_VK_META, GDK_Meta_L},

    // Assume that Super or Hyper is always mapped to physical Win key.
    {NS_VK_WIN, GDK_Super_L},

    // GTK's AltGraph key is similar to Mac's Option (Alt) key.  However,
    // unfortunately, browsers on Mac are using NS_VK_ALT for it even though
    // it's really different from Alt key on Windows.
    // On the other hand, GTK's AltGrapsh keys are really different from
    // Alt key.  However, there is no AltGrapsh key on Windows.  On Windows,
    // both Ctrl and Alt keys are pressed internally when AltGr key is pressed.
    // For some languages' users, AltGraph key is important, so, web
    // applications on such locale may want to know AltGraph key press.
    // Therefore, we should map AltGr keycode for them only on GTK.
    {NS_VK_ALTGR, GDK_ISO_Level3_Shift},

    {NS_VK_PAUSE, GDK_Pause},
    {NS_VK_CAPS_LOCK, GDK_Caps_Lock},
    {NS_VK_ESCAPE, GDK_Escape},
    // { NS_VK_ACCEPT,     GDK_XXX },
    // { NS_VK_MODECHANGE, GDK_XXX },
    {NS_VK_SPACE, GDK_space},
    {NS_VK_PAGE_UP, GDK_Page_Up},
    {NS_VK_PAGE_DOWN, GDK_Page_Down},
    {NS_VK_END, GDK_End},
    {NS_VK_HOME, GDK_Home},
    {NS_VK_LEFT, GDK_Left},
    {NS_VK_UP, GDK_Up},
    {NS_VK_RIGHT, GDK_Right},
    {NS_VK_DOWN, GDK_Down},
    {NS_VK_SELECT, GDK_Select},
    {NS_VK_PRINT, GDK_Print},
    {NS_VK_EXECUTE, GDK_Execute},
    {NS_VK_PRINTSCREEN, GDK_Print},
    {NS_VK_INSERT, GDK_Insert},
    {NS_VK_DELETE, GDK_Delete},
    {NS_VK_HELP, GDK_Help},

    {NS_VK_NUM_LOCK, GDK_Num_Lock},
    {NS_VK_SCROLL_LOCK, GDK_Scroll_Lock},

    // Function keys
    {NS_VK_F1, GDK_F1},
    {NS_VK_F2, GDK_F2},
    {NS_VK_F3, GDK_F3},
    {NS_VK_F4, GDK_F4},
    {NS_VK_F5, GDK_F5},
    {NS_VK_F6, GDK_F6},
    {NS_VK_F7, GDK_F7},
    {NS_VK_F8, GDK_F8},
    {NS_VK_F9, GDK_F9},
    {NS_VK_F10, GDK_F10},
    {NS_VK_F11, GDK_F11},
    {NS_VK_F12, GDK_F12},
    {NS_VK_F13, GDK_F13},
    {NS_VK_F14, GDK_F14},
    {NS_VK_F15, GDK_F15},
    {NS_VK_F16, GDK_F16},
    {NS_VK_F17, GDK_F17},
    {NS_VK_F18, GDK_F18},
    {NS_VK_F19, GDK_F19},
    {NS_VK_F20, GDK_F20},
    {NS_VK_F21, GDK_F21},
    {NS_VK_F22, GDK_F22},
    {NS_VK_F23, GDK_F23},
    {NS_VK_F24, GDK_F24},

    // context menu key, keysym 0xff67, typically keycode 117 on 105-key
    // (Microsoft) x86 keyboards, located between right 'Windows' key and right
    // Ctrl key
    {NS_VK_CONTEXT_MENU, GDK_Menu},
    {NS_VK_SLEEP, GDK_Sleep},

    {NS_VK_ATTN, GDK_3270_Attn},
    {NS_VK_CRSEL, GDK_3270_CursorSelect},
    {NS_VK_EXSEL, GDK_3270_ExSelect},
    {NS_VK_EREOF, GDK_3270_EraseEOF},
    {NS_VK_PLAY, GDK_3270_Play},
    //{ NS_VK_ZOOM,       GDK_XXX },
    {NS_VK_PA1, GDK_3270_PA1},

    {NS_VK_MULTIPLY, GDK_KP_Multiply},
    {NS_VK_ADD, GDK_KP_Add},
    {NS_VK_SEPARATOR, GDK_KP_Separator},
    {NS_VK_SUBTRACT, GDK_KP_Subtract},
    {NS_VK_DECIMAL, GDK_KP_Decimal},
    {NS_VK_DIVIDE, GDK_KP_Divide},
    {NS_VK_NUMPAD0, GDK_KP_0},
    {NS_VK_NUMPAD1, GDK_KP_1},
    {NS_VK_NUMPAD2, GDK_KP_2},
    {NS_VK_NUMPAD3, GDK_KP_3},
    {NS_VK_NUMPAD4, GDK_KP_4},
    {NS_VK_NUMPAD5, GDK_KP_5},
    {NS_VK_NUMPAD6, GDK_KP_6},
    {NS_VK_NUMPAD7, GDK_KP_7},
    {NS_VK_NUMPAD8, GDK_KP_8},
    {NS_VK_NUMPAD9, GDK_KP_9},
    {NS_VK_SPACE, GDK_space},
    {NS_VK_COLON, GDK_colon},
    {NS_VK_SEMICOLON, GDK_semicolon},
    {NS_VK_LESS_THAN, GDK_less},
    {NS_VK_EQUALS, GDK_equal},
    {NS_VK_GREATER_THAN, GDK_greater},
    {NS_VK_QUESTION_MARK, GDK_question},
    {NS_VK_AT, GDK_at},
    {NS_VK_CIRCUMFLEX, GDK_asciicircum},
    {NS_VK_EXCLAMATION, GDK_exclam},
    {NS_VK_DOUBLE_QUOTE, GDK_quotedbl},
    {NS_VK_HASH, GDK_numbersign},
    {NS_VK_DOLLAR, GDK_dollar},
    {NS_VK_PERCENT, GDK_percent},
    {NS_VK_AMPERSAND, GDK_ampersand},
    {NS_VK_UNDERSCORE, GDK_underscore},
    {NS_VK_OPEN_PAREN, GDK_parenleft},
    {NS_VK_CLOSE_PAREN, GDK_parenright},
    {NS_VK_ASTERISK, GDK_asterisk},
    {NS_VK_PLUS, GDK_plus},
    {NS_VK_PIPE, GDK_bar},
    {NS_VK_HYPHEN_MINUS, GDK_minus},
    {NS_VK_OPEN_CURLY_BRACKET, GDK_braceleft},
    {NS_VK_CLOSE_CURLY_BRACKET, GDK_braceright},
    {NS_VK_TILDE, GDK_asciitilde},
    {NS_VK_COMMA, GDK_comma},
    {NS_VK_PERIOD, GDK_period},
    {NS_VK_SLASH, GDK_slash},
    {NS_VK_BACK_QUOTE, GDK_grave},
    {NS_VK_OPEN_BRACKET, GDK_bracketleft},
    {NS_VK_BACK_SLASH, GDK_backslash},
    {NS_VK_CLOSE_BRACKET, GDK_bracketright},
    {NS_VK_QUOTE, GDK_apostrophe},
};

/* static */
guint KeymapWrapper::ConvertGeckoKeyCodeToGDKKeyval(const nsAString& aKeyCode) {
  NS_ConvertUTF16toUTF8 keyName(aKeyCode);
  ToUpperCase(keyName);  // We want case-insensitive comparison with data
                         // stored as uppercase.

  uint32_t keyCode = 0;

  uint32_t keyNameLength = keyName.Length();
  const char* keyNameStr = keyName.get();
  for (const auto& code : gKeyCodes) {
    if (keyNameLength == code.strlength &&
        !nsCRT::strcmp(code.str, keyNameStr)) {
      keyCode = code.keycode;
      break;
    }
  }

  // First, try to handle alphanumeric input, not listed in nsKeycodes:
  // most likely, more letters will be getting typed in than things in
  // the key list, so we will look through these first.

  if (keyCode >= NS_VK_A && keyCode <= NS_VK_Z) {
    // gdk and DOM both use the ASCII codes for these keys.
    return keyCode;
  }

  // numbers
  if (keyCode >= NS_VK_0 && keyCode <= NS_VK_9) {
    // gdk and DOM both use the ASCII codes for these keys.
    return keyCode - NS_VK_0 + GDK_0;
  }

  // misc other things
  for (const auto& pair : gKeyPairs) {
    if (pair.DOMKeyCode == keyCode) {
      return pair.GDKKeyval;
    }
  }

  return 0;
}

/* static */
uint32_t KeymapWrapper::GetDOMKeyCodeFromKeyPairs(guint aGdkKeyval) {
  switch (aGdkKeyval) {
    case GDK_Cancel:
      return NS_VK_CANCEL;
    case GDK_BackSpace:
      return NS_VK_BACK;
    case GDK_Tab:
    case GDK_ISO_Left_Tab:
      return NS_VK_TAB;
    case GDK_Clear:
      return NS_VK_CLEAR;
    case GDK_Return:
      return NS_VK_RETURN;
    case GDK_Shift_L:
    case GDK_Shift_R:
    case GDK_Shift_Lock:
      return NS_VK_SHIFT;
    case GDK_Control_L:
    case GDK_Control_R:
      return NS_VK_CONTROL;
    case GDK_Alt_L:
    case GDK_Alt_R:
      return NS_VK_ALT;
    case GDK_Meta_L:
    case GDK_Meta_R:
      return NS_VK_META;

    // Assume that Super or Hyper is always mapped to physical Win key.
    case GDK_Super_L:
    case GDK_Super_R:
    case GDK_Hyper_L:
    case GDK_Hyper_R:
      return NS_VK_WIN;

    // GTK's AltGraph key is similar to Mac's Option (Alt) key.  However,
    // unfortunately, browsers on Mac are using NS_VK_ALT for it even though
    // it's really different from Alt key on Windows.
    // On the other hand, GTK's AltGrapsh keys are really different from
    // Alt key.  However, there is no AltGrapsh key on Windows.  On Windows,
    // both Ctrl and Alt keys are pressed internally when AltGr key is
    // pressed.  For some languages' users, AltGraph key is important, so,
    // web applications on such locale may want to know AltGraph key press.
    // Therefore, we should map AltGr keycode for them only on GTK.
    case GDK_ISO_Level3_Shift:
    case GDK_ISO_Level5_Shift:
    // We assume that Mode_switch is always used for level3 shift.
    case GDK_Mode_switch:
      return NS_VK_ALTGR;

    case GDK_Pause:
      return NS_VK_PAUSE;
    case GDK_Caps_Lock:
      return NS_VK_CAPS_LOCK;
    case GDK_Kana_Lock:
    case GDK_Kana_Shift:
      return NS_VK_KANA;
    case GDK_Hangul:
      return NS_VK_HANGUL;
    // case GDK_XXX:                   return NS_VK_JUNJA;
    // case GDK_XXX:                   return NS_VK_FINAL;
    case GDK_Hangul_Hanja:
      return NS_VK_HANJA;
    case GDK_Kanji:
      return NS_VK_KANJI;
    case GDK_Escape:
      return NS_VK_ESCAPE;
    case GDK_Henkan:
      return NS_VK_CONVERT;
    case GDK_Muhenkan:
      return NS_VK_NONCONVERT;
    // case GDK_XXX:                   return NS_VK_ACCEPT;
    // case GDK_XXX:                   return NS_VK_MODECHANGE;
    case GDK_Page_Up:
      return NS_VK_PAGE_UP;
    case GDK_Page_Down:
      return NS_VK_PAGE_DOWN;
    case GDK_End:
      return NS_VK_END;
    case GDK_Home:
      return NS_VK_HOME;
    case GDK_Left:
      return NS_VK_LEFT;
    case GDK_Up:
      return NS_VK_UP;
    case GDK_Right:
      return NS_VK_RIGHT;
    case GDK_Down:
      return NS_VK_DOWN;
    case GDK_Select:
      return NS_VK_SELECT;
    case GDK_Print:
      return NS_VK_PRINT;
    case GDK_Execute:
      return NS_VK_EXECUTE;
    case GDK_Insert:
      return NS_VK_INSERT;
    case GDK_Delete:
      return NS_VK_DELETE;
    case GDK_Help:
      return NS_VK_HELP;

    // keypad keys
    case GDK_KP_Left:
      return NS_VK_LEFT;
    case GDK_KP_Right:
      return NS_VK_RIGHT;
    case GDK_KP_Up:
      return NS_VK_UP;
    case GDK_KP_Down:
      return NS_VK_DOWN;
    case GDK_KP_Page_Up:
      return NS_VK_PAGE_UP;
    // Not sure what these are
    // case GDK_KP_Prior:              return NS_VK_;
    // case GDK_KP_Next:               return NS_VK_;
    case GDK_KP_Begin:
      return NS_VK_CLEAR;  // Num-unlocked 5
    case GDK_KP_Page_Down:
      return NS_VK_PAGE_DOWN;
    case GDK_KP_Home:
      return NS_VK_HOME;
    case GDK_KP_End:
      return NS_VK_END;
    case GDK_KP_Insert:
      return NS_VK_INSERT;
    case GDK_KP_Delete:
      return NS_VK_DELETE;
    case GDK_KP_Enter:
      return NS_VK_RETURN;

    case GDK_Num_Lock:
      return NS_VK_NUM_LOCK;
    case GDK_Scroll_Lock:
      return NS_VK_SCROLL_LOCK;

    // Function keys
    case GDK_F1:
      return NS_VK_F1;
    case GDK_F2:
      return NS_VK_F2;
    case GDK_F3:
      return NS_VK_F3;
    case GDK_F4:
      return NS_VK_F4;
    case GDK_F5:
      return NS_VK_F5;
    case GDK_F6:
      return NS_VK_F6;
    case GDK_F7:
      return NS_VK_F7;
    case GDK_F8:
      return NS_VK_F8;
    case GDK_F9:
      return NS_VK_F9;
    case GDK_F10:
      return NS_VK_F10;
    case GDK_F11:
      return NS_VK_F11;
    case GDK_F12:
      return NS_VK_F12;
    case GDK_F13:
      return NS_VK_F13;
    case GDK_F14:
      return NS_VK_F14;
    case GDK_F15:
      return NS_VK_F15;
    case GDK_F16:
      return NS_VK_F16;
    case GDK_F17:
      return NS_VK_F17;
    case GDK_F18:
      return NS_VK_F18;
    case GDK_F19:
      return NS_VK_F19;
    case GDK_F20:
      return NS_VK_F20;
    case GDK_F21:
      return NS_VK_F21;
    case GDK_F22:
      return NS_VK_F22;
    case GDK_F23:
      return NS_VK_F23;
    case GDK_F24:
      return NS_VK_F24;

    // context menu key, keysym 0xff67, typically keycode 117 on 105-key
    // (Microsoft) x86 keyboards, located between right 'Windows' key and
    // right Ctrl key
    case GDK_Menu:
      return NS_VK_CONTEXT_MENU;
    case GDK_Sleep:
      return NS_VK_SLEEP;

    case GDK_3270_Attn:
      return NS_VK_ATTN;
    case GDK_3270_CursorSelect:
      return NS_VK_CRSEL;
    case GDK_3270_ExSelect:
      return NS_VK_EXSEL;
    case GDK_3270_EraseEOF:
      return NS_VK_EREOF;
    case GDK_3270_Play:
      return NS_VK_PLAY;
    // case GDK_XXX:                   return NS_VK_ZOOM;
    case GDK_3270_PA1:
      return NS_VK_PA1;

    // map Sun Keyboard special keysyms on to NS_VK keys

    // Sun F11 key generates SunF36(0x1005ff10) keysym
    case 0x1005ff10:
      return NS_VK_F11;
    // Sun F12 key generates SunF37(0x1005ff11) keysym
    case 0x1005ff11:
      return NS_VK_F12;
    default:
      return 0;
  }
}

void KeymapWrapper::WillDispatchKeyboardEvent(WidgetKeyboardEvent& aKeyEvent,
                                              GdkEventKey* aGdkKeyEvent) {
  GetInstance()->WillDispatchKeyboardEventInternal(aKeyEvent, aGdkKeyEvent);
}

void KeymapWrapper::WillDispatchKeyboardEventInternal(
    WidgetKeyboardEvent& aKeyEvent, GdkEventKey* aGdkKeyEvent) {
  if (!aGdkKeyEvent) {
    // If aGdkKeyEvent is nullptr, we're trying to dispatch a fake keyboard
    // event in such case, we don't need to set alternative char codes.
    // So, we don't need to do nothing here.  This case is typically we're
    // dispatching eKeyDown or eKeyUp event during composition.
    return;
  }

  uint32_t charCode = GetCharCodeFor(aGdkKeyEvent);
  if (!charCode) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p WillDispatchKeyboardEventInternal, "
             "mKeyCode=0x%02X, charCode=0x%08X",
             this, aKeyEvent.mKeyCode, aKeyEvent.mCharCode));
    return;
  }

  // The mCharCode was set from mKeyValue. However, for example, when Ctrl key
  // is pressed, its value should indicate an ASCII character for backward
  // compatibility rather than inputting character without the modifiers.
  // Therefore, we need to modify mCharCode value here.
  aKeyEvent.SetCharCode(charCode);

  gint level = GetKeyLevel(aGdkKeyEvent);
  if (level != 0 && level != 1) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p WillDispatchKeyboardEventInternal, "
             "mKeyCode=0x%02X, mCharCode=0x%08X, level=%d",
             this, aKeyEvent.mKeyCode, aKeyEvent.mCharCode, level));
    return;
  }

  guint baseState = aGdkKeyEvent->state &
                    ~(GetGdkModifierMask(SHIFT) | GetGdkModifierMask(CTRL) |
                      GetGdkModifierMask(ALT) | GetGdkModifierMask(META) |
                      GetGdkModifierMask(SUPER) | GetGdkModifierMask(HYPER) |
                      GDK_META_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK);

  // We shold send both shifted char and unshifted char, all keyboard layout
  // users can use all keys.  Don't change event.mCharCode. On some keyboard
  // layouts, Ctrl/Alt/Meta keys are used for inputting some characters.
  AlternativeCharCode altCharCodes(0, 0);
  // unshifted charcode of current keyboard layout.
  altCharCodes.mUnshiftedCharCode =
      GetCharCodeFor(aGdkKeyEvent, baseState, aGdkKeyEvent->group);
  bool isLatin = (altCharCodes.mUnshiftedCharCode <= 0xFF);
  // shifted charcode of current keyboard layout.
  altCharCodes.mShiftedCharCode = GetCharCodeFor(
      aGdkKeyEvent, baseState | GetGdkModifierMask(SHIFT), aGdkKeyEvent->group);
  isLatin = isLatin && (altCharCodes.mShiftedCharCode <= 0xFF);
  if (altCharCodes.mUnshiftedCharCode || altCharCodes.mShiftedCharCode) {
    aKeyEvent.mAlternativeCharCodes.AppendElement(altCharCodes);
  }

  bool needLatinKeyCodes = !isLatin;
  if (!needLatinKeyCodes) {
    needLatinKeyCodes =
        (IS_ASCII_ALPHABETICAL(altCharCodes.mUnshiftedCharCode) !=
         IS_ASCII_ALPHABETICAL(altCharCodes.mShiftedCharCode));
  }

  // If current keyboard layout can input Latin characters, we don't need
  // more information.
  if (!needLatinKeyCodes) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p WillDispatchKeyboardEventInternal, "
             "mKeyCode=0x%02X, mCharCode=0x%08X, level=%d, altCharCodes={ "
             "mUnshiftedCharCode=0x%08X, mShiftedCharCode=0x%08X }",
             this, aKeyEvent.mKeyCode, aKeyEvent.mCharCode, level,
             altCharCodes.mUnshiftedCharCode, altCharCodes.mShiftedCharCode));
    return;
  }

  // Next, find Latin inputtable keyboard layout.
  gint minGroup = GetFirstLatinGroup();
  if (minGroup < 0) {
    MOZ_LOG(gKeyLog, LogLevel::Info,
            ("%p WillDispatchKeyboardEventInternal, "
             "Latin keyboard layout isn't found: "
             "mKeyCode=0x%02X, mCharCode=0x%08X, level=%d, "
             "altCharCodes={ mUnshiftedCharCode=0x%08X, "
             "mShiftedCharCode=0x%08X }",
             this, aKeyEvent.mKeyCode, aKeyEvent.mCharCode, level,
             altCharCodes.mUnshiftedCharCode, altCharCodes.mShiftedCharCode));
    return;
  }

  AlternativeCharCode altLatinCharCodes(0, 0);
  uint32_t unmodifiedCh = aKeyEvent.IsShift() ? altCharCodes.mShiftedCharCode
                                              : altCharCodes.mUnshiftedCharCode;

  // unshifted charcode of found keyboard layout.
  uint32_t ch = GetCharCodeFor(aGdkKeyEvent, baseState, minGroup);
  altLatinCharCodes.mUnshiftedCharCode =
      IsBasicLatinLetterOrNumeral(ch) ? ch : 0;
  // shifted charcode of found keyboard layout.
  ch = GetCharCodeFor(aGdkKeyEvent, baseState | GetGdkModifierMask(SHIFT),
                      minGroup);
  altLatinCharCodes.mShiftedCharCode = IsBasicLatinLetterOrNumeral(ch) ? ch : 0;
  if (altLatinCharCodes.mUnshiftedCharCode ||
      altLatinCharCodes.mShiftedCharCode) {
    aKeyEvent.mAlternativeCharCodes.AppendElement(altLatinCharCodes);
  }
  // If the mCharCode is not Latin, and the level is 0 or 1, we should
  // replace the mCharCode to Latin char if Alt keys are not pressed. (Alt
  // should be sent the localized char for accesskey like handling of Web
  // Applications.)
  ch = aKeyEvent.IsShift() ? altLatinCharCodes.mShiftedCharCode
                           : altLatinCharCodes.mUnshiftedCharCode;
  if (ch && !aKeyEvent.IsAlt() && charCode == unmodifiedCh) {
    aKeyEvent.SetCharCode(ch);
  }

  MOZ_LOG(gKeyLog, LogLevel::Info,
          ("%p WillDispatchKeyboardEventInternal, "
           "mKeyCode=0x%02X, mCharCode=0x%08X, level=%d, minGroup=%d, "
           "altCharCodes={ mUnshiftedCharCode=0x%08X, "
           "mShiftedCharCode=0x%08X } "
           "altLatinCharCodes={ mUnshiftedCharCode=0x%08X, "
           "mShiftedCharCode=0x%08X }",
           this, aKeyEvent.mKeyCode, aKeyEvent.mCharCode, level, minGroup,
           altCharCodes.mUnshiftedCharCode, altCharCodes.mShiftedCharCode,
           altLatinCharCodes.mUnshiftedCharCode,
           altLatinCharCodes.mShiftedCharCode));
}

#ifdef MOZ_WAYLAND
void KeymapWrapper::SetFocusIn(wl_surface* aFocusSurface,
                               uint32_t aFocusSerial) {
  LOGW("KeymapWrapper::SetFocusIn() surface %p ID %d serial %d", aFocusSurface,
       aFocusSurface ? wl_proxy_get_id((struct wl_proxy*)aFocusSurface) : 0,
       aFocusSerial);

  KeymapWrapper* keymapWrapper = KeymapWrapper::GetInstance();
  keymapWrapper->mFocusSurface = aFocusSurface;
  keymapWrapper->mFocusSerial = aFocusSerial;
}

// aFocusSurface can be null in case that focused surface is already destroyed.
void KeymapWrapper::SetFocusOut(wl_surface* aFocusSurface) {
  KeymapWrapper* keymapWrapper = KeymapWrapper::GetInstance();
  LOGW("KeymapWrapper::SetFocusOut surface %p ID %d", aFocusSurface,
       aFocusSurface ? wl_proxy_get_id((struct wl_proxy*)aFocusSurface) : 0);

  keymapWrapper->mFocusSurface = nullptr;
  keymapWrapper->mFocusSerial = 0;

  sRepeatState = NOT_PRESSED;
}

void KeymapWrapper::GetFocusInfo(wl_surface** aFocusSurface,
                                 uint32_t* aFocusSerial) {
  KeymapWrapper* keymapWrapper = KeymapWrapper::GetInstance();
  *aFocusSurface = keymapWrapper->mFocusSurface;
  *aFocusSerial = keymapWrapper->mFocusSerial;
}

void KeymapWrapper::ClearKeymap() {
  KeymapWrapper* keymapWrapper = KeymapWrapper::GetInstance();
  if (keymapWrapper->mXkbKeymap) {
    xkb_keymap_unref(keymapWrapper->mXkbKeymap);
    keymapWrapper->mXkbKeymap = nullptr;
  }
}
#endif

}  // namespace widget
}  // namespace mozilla
