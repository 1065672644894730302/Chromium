// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ash/accelerators/accelerator_controller.h"

#include "ash/accelerators/accelerator_table.h"
#include "ash/ash_switches.h"
#include "ash/caps_lock_delegate.h"
#include "ash/desktop_background/desktop_background_controller.h"
#include "ash/focus_cycler.h"
#include "ash/ime_control_delegate.h"
#include "ash/launcher/launcher.h"
#include "ash/launcher/launcher_delegate.h"
#include "ash/launcher/launcher_model.h"
#include "ash/monitor/multi_monitor_manager.h"
#include "ash/screenshot_delegate.h"
#include "ash/shell.h"
#include "ash/shell_delegate.h"
#include "ash/shell_window_ids.h"
#include "ash/system/brightness/brightness_control_delegate.h"
#include "ash/system/tray/system_tray.h"
#include "ash/volume_control_delegate.h"
#include "ash/wm/property_util.h"
#include "ash/wm/window_cycle_controller.h"
#include "ash/wm/window_util.h"
#include "ash/wm/workspace/snap_sizer.h"
#include "base/command_line.h"
#include "ui/aura/event.h"
#include "ui/aura/root_window.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/accelerators/accelerator_manager.h"
#include "ui/compositor/debug_utils.h"
#include "ui/compositor/layer.h"
#include "ui/compositor/layer_animation_sequence.h"
#include "ui/compositor/layer_animator.h"
#include "ui/compositor/screen_rotation.h"
#include "ui/oak/oak.h"

#if defined(OS_CHROMEOS)
#include "chromeos/monitor/output_configurator.h"
#endif  // defined(OS_CHROMEOS)

namespace {

bool HandleCycleWindowMRU(ash::WindowCycleController::Direction direction,
                          bool is_alt_down) {
  ash::Shell::GetInstance()->
      window_cycle_controller()->HandleCycleWindow(direction, is_alt_down);
  // Always report we handled the key, even if the window didn't change.
  return true;
}

void HandleCycleWindowLinear(ash::CycleDirection direction) {
  ash::Shell::GetInstance()->launcher()->CycleWindowLinear(direction);
}

#if defined(OS_CHROMEOS)
bool HandleLock() {
  ash::Shell::GetInstance()->delegate()->LockScreen();
  return true;
}

bool HandleFileManager() {
  ash::Shell::GetInstance()->delegate()->OpenFileManager();
  return true;
}

bool HandleCrosh() {
  ash::Shell::GetInstance()->delegate()->OpenCrosh();
  return true;
}

bool HandleToggleSpokenFeedback() {
  ash::Shell::GetInstance()->delegate()->ToggleSpokenFeedback();
  return true;
}
#endif

bool HandleExit() {
  ash::ShellDelegate* delegate = ash::Shell::GetInstance()->delegate();
  if (!delegate)
    return false;
  delegate->Exit();
  return true;
}

bool HandleNewTab() {
  ash::Shell::GetInstance()->delegate()->NewTab();
  return true;
}

bool HandleNewWindow(bool is_incognito) {
  ash::ShellDelegate* delegate = ash::Shell::GetInstance()->delegate();
  if (!delegate)
    return false;
  delegate->NewWindow(is_incognito);
  return true;
}

bool HandleRestoreTab() {
  ash::Shell::GetInstance()->delegate()->RestoreTab();
  return true;
}

bool HandleShowTaskManager() {
  ash::Shell::GetInstance()->delegate()->ShowTaskManager();
  return true;
}

// Rotates the default window container.
bool HandleRotateWindows() {
  aura::Window* target = ash::Shell::GetInstance()->GetContainer(
        ash::internal::kShellWindowId_DefaultContainer);
  scoped_ptr<ui::LayerAnimationSequence> screen_rotation(
      new ui::LayerAnimationSequence(new ui::ScreenRotation(360)));
  target->layer()->GetAnimator()->StartAnimation(
      screen_rotation.release());
  return true;
}

#if !defined(NDEBUG)
// Rotates the screen.
bool HandleRotateScreen() {
  static int i = 0;
  int delta = 0;
  switch (i) {
    case 0: delta = 90; break;
    case 1: delta = 90; break;
    case 2: delta = 90; break;
    case 3: delta = 90; break;
    case 4: delta = -90; break;
    case 5: delta = -90; break;
    case 6: delta = -90; break;
    case 7: delta = -90; break;
    case 8: delta = -90; break;
    case 9: delta = 180; break;
    case 10: delta = 180; break;
    case 11: delta = 90; break;
    case 12: delta = 180; break;
    case 13: delta = 180; break;
  }
  i = (i + 1) % 14;
  ash::Shell::GetPrimaryRootWindow()->layer()->GetAnimator()->
      set_preemption_strategy(ui::LayerAnimator::REPLACE_QUEUED_ANIMATIONS);
  scoped_ptr<ui::LayerAnimationSequence> screen_rotation(
      new ui::LayerAnimationSequence(new ui::ScreenRotation(delta)));
  screen_rotation->AddObserver(ash::Shell::GetPrimaryRootWindow());
  ash::Shell::GetPrimaryRootWindow()->layer()->GetAnimator()->StartAnimation(
      screen_rotation.release());
  return true;
}

bool HandleToggleDesktopBackgroundMode() {
  ash::DesktopBackgroundController* desktop_background_controller =
      ash::Shell::GetInstance()->desktop_background_controller();
  if (desktop_background_controller->desktop_background_mode() ==
      ash::DesktopBackgroundController::BACKGROUND_IMAGE)
    desktop_background_controller->SetDesktopBackgroundSolidColorMode();
  else
    desktop_background_controller->SetLoggedInUserWallpaper();
  return true;
}

bool HandleToggleRootWindowFullScreen() {
  ash::Shell::GetPrimaryRootWindow()->ToggleFullScreen();
  return true;
}

bool HandlePrintLayerHierarchy() {
  aura::RootWindow* root_window = ash::Shell::GetPrimaryRootWindow();
  ui::PrintLayerHierarchy(root_window->layer(),
                          root_window->last_mouse_location());
  return true;
}

void PrintWindowHierarchy(aura::Window* window, int indent) {
  std::string indent_str(indent, ' ');
  VLOG(1) << indent_str << window->name() << " type " << window->type()
      << (ash::wm::IsActiveWindow(window) ? "active" : "");
  for (size_t i = 0; i < window->children().size(); ++i)
    PrintWindowHierarchy(window->children()[i], indent + 3);
}

bool HandlePrintWindowHierarchy() {
  VLOG(1) << "Window hierarchy:";
  aura::Window* container = ash::Shell::GetInstance()->GetContainer(
      ash::internal::kShellWindowId_DefaultContainer);
  PrintWindowHierarchy(container, 0);
  return true;
}

#endif

}  // namespace

namespace ash {

////////////////////////////////////////////////////////////////////////////////
// AcceleratorController, public:

AcceleratorController::AcceleratorController()
    : accelerator_manager_(new ui::AcceleratorManager) {
  Init();
}

AcceleratorController::~AcceleratorController() {
}

void AcceleratorController::Init() {
  for (size_t i = 0; i < kActionsAllowedAtLoginOrLockScreenLength; ++i) {
    actions_allowed_at_login_screen_.insert(
        kActionsAllowedAtLoginOrLockScreen[i]);
    actions_allowed_at_lock_screen_.insert(
        kActionsAllowedAtLoginOrLockScreen[i]);
  }
  for (size_t i = 0; i < kActionsAllowedAtLockScreenLength; ++i) {
    actions_allowed_at_lock_screen_.insert(kActionsAllowedAtLockScreen[i]);
  }

  for (size_t i = 0; i < kAcceleratorDataLength; ++i) {
    ui::Accelerator accelerator(kAcceleratorData[i].keycode,
                                kAcceleratorData[i].modifiers);
    accelerator.set_type(kAcceleratorData[i].trigger_on_press ?
                         ui::ET_KEY_PRESSED : ui::ET_KEY_RELEASED);
    Register(accelerator, this);
    accelerators_.insert(
        std::make_pair(accelerator, kAcceleratorData[i].action));
  }
}

void AcceleratorController::Register(const ui::Accelerator& accelerator,
                                     ui::AcceleratorTarget* target) {
  accelerator_manager_->Register(accelerator,
                                 ui::AcceleratorManager::kNormalPriority,
                                 target);
}

void AcceleratorController::Unregister(const ui::Accelerator& accelerator,
                                       ui::AcceleratorTarget* target) {
  accelerator_manager_->Unregister(accelerator, target);
}

void AcceleratorController::UnregisterAll(ui::AcceleratorTarget* target) {
  accelerator_manager_->UnregisterAll(target);
}

bool AcceleratorController::Process(const ui::Accelerator& accelerator) {
  if (ime_control_delegate_.get()) {
    return accelerator_manager_->Process(
        ime_control_delegate_->RemapAccelerator(accelerator));
  }
  return accelerator_manager_->Process(accelerator);
}

bool AcceleratorController::IsRegistered(
    const ui::Accelerator& accelerator) const {
  return accelerator_manager_->GetCurrentTarget(accelerator) != NULL;
}

void AcceleratorController::SetBrightnessControlDelegate(
    scoped_ptr<BrightnessControlDelegate> brightness_control_delegate) {
  brightness_control_delegate_.swap(brightness_control_delegate);
}

void AcceleratorController::SetCapsLockDelegate(
    scoped_ptr<CapsLockDelegate> caps_lock_delegate) {
  caps_lock_delegate_.swap(caps_lock_delegate);
}

void AcceleratorController::SetImeControlDelegate(
    scoped_ptr<ImeControlDelegate> ime_control_delegate) {
  ime_control_delegate_.swap(ime_control_delegate);
}

void AcceleratorController::SetScreenshotDelegate(
    scoped_ptr<ScreenshotDelegate> screenshot_delegate) {
  screenshot_delegate_.swap(screenshot_delegate);
}

void AcceleratorController::SetVolumeControlDelegate(
    scoped_ptr<VolumeControlDelegate> volume_control_delegate) {
  volume_control_delegate_.swap(volume_control_delegate);
}

////////////////////////////////////////////////////////////////////////////////
// AcceleratorController, ui::AcceleratorTarget implementation:

bool AcceleratorController::AcceleratorPressed(
    const ui::Accelerator& accelerator) {
  std::map<ui::Accelerator, int>::const_iterator it =
      accelerators_.find(accelerator);
  DCHECK(it != accelerators_.end());
  AcceleratorAction action = static_cast<AcceleratorAction>(it->second);

  ash::Shell* shell = ash::Shell::GetInstance();
  bool at_login_screen = false;
#if defined(OS_CHROMEOS)
  at_login_screen = (shell->delegate() && !shell->delegate()->IsUserLoggedIn());
#endif
  bool at_lock_screen = shell->IsScreenLocked();

  if (at_login_screen &&
      actions_allowed_at_login_screen_.find(action) ==
      actions_allowed_at_login_screen_.end()) {
    return false;
  }
  if (at_lock_screen &&
      actions_allowed_at_lock_screen_.find(action) ==
      actions_allowed_at_lock_screen_.end()) {
    return false;
  }

  switch (action) {
    case CYCLE_BACKWARD_MRU:
      return HandleCycleWindowMRU(WindowCycleController::BACKWARD,
                                  accelerator.IsAltDown());
    case CYCLE_FORWARD_MRU:
      return HandleCycleWindowMRU(WindowCycleController::FORWARD,
                                  accelerator.IsAltDown());
    case CYCLE_BACKWARD_LINEAR:
      HandleCycleWindowLinear(CYCLE_BACKWARD);
      return true;
    case CYCLE_FORWARD_LINEAR:
      HandleCycleWindowLinear(CYCLE_FORWARD);
      return true;
#if defined(OS_CHROMEOS)
    case LOCK_SCREEN:
      return HandleLock();
    case OPEN_FILE_MANAGER:
      return HandleFileManager();
    case OPEN_CROSH:
      return HandleCrosh();
    case TOGGLE_SPOKEN_FEEDBACK:
      return HandleToggleSpokenFeedback();
    case CYCLE_DISPLAY_MODE:
      ash::Shell::GetInstance()->output_configurator()->CycleDisplayMode();
      return true;
#endif
    case EXIT:
      return HandleExit();
    case NEW_INCOGNITO_WINDOW:
      return HandleNewWindow(true /* is_incognito */);
    case NEW_TAB:
      return HandleNewTab();
    case NEW_WINDOW:
      return HandleNewWindow(false /* is_incognito */);
    case RESTORE_TAB:
      return HandleRestoreTab();
    case TAKE_SCREENSHOT:
      if (screenshot_delegate_.get()) {
        // TODO(oshima): Use the active window for now. Use the mouse
        // location if there is no active window. crbug.com/130718.
        aura::RootWindow* root_window = Shell::GetActiveRootWindow();
        screenshot_delegate_->HandleTakeScreenshot(root_window);
      }
      // Return true to prevent propagation of the key event.
      return true;
    case TAKE_PARTIAL_SCREENSHOT:
      if (screenshot_delegate_.get())
        ash::Shell::GetInstance()->delegate()->
            StartPartialScreenshot(screenshot_delegate_.get());
      // Return true to prevent propagation of the key event because
      // this key combination is reserved for partial screenshot.
      return true;
    case SEARCH_KEY:
      ash::Shell::GetInstance()->delegate()->Search();
      return true;
    case TOGGLE_APP_LIST:
      ash::Shell::GetInstance()->ToggleAppList();
      return true;
    case TOGGLE_CAPS_LOCK:
      if (caps_lock_delegate_.get())
        return caps_lock_delegate_->HandleToggleCapsLock();
      break;
    case BRIGHTNESS_DOWN:
      if (brightness_control_delegate_.get())
        return brightness_control_delegate_->HandleBrightnessDown(accelerator);
      break;
    case BRIGHTNESS_UP:
      if (brightness_control_delegate_.get())
        return brightness_control_delegate_->HandleBrightnessUp(accelerator);
      break;
    case VOLUME_MUTE:
      if (volume_control_delegate_.get())
        return volume_control_delegate_->HandleVolumeMute(accelerator);
      break;
    case VOLUME_DOWN:
      if (volume_control_delegate_.get())
        return volume_control_delegate_->HandleVolumeDown(accelerator);
      break;
    case VOLUME_UP:
      if (volume_control_delegate_.get())
        return volume_control_delegate_->HandleVolumeUp(accelerator);
      break;
    case FOCUS_LAUNCHER:
      if (shell->launcher())
        return shell->focus_cycler()->FocusWidget(shell->launcher()->widget());
      break;
    case FOCUS_SYSTEM_TRAY:
      if (shell->system_tray())
        return shell->focus_cycler()->FocusWidget(
            shell->system_tray()->GetWidget());
      break;
    case SHOW_KEYBOARD_OVERLAY:
      ash::Shell::GetInstance()->delegate()->ShowKeyboardOverlay();
      break;
    case SHOW_OAK:
      if (CommandLine::ForCurrentProcess()->HasSwitch(
              switches::kAshEnableOak)) {
        oak::ShowOakWindow();
        return true;
      }
      break;
    case SHOW_TASK_MANAGER:
      return HandleShowTaskManager();
    case NEXT_IME:
      if (ime_control_delegate_.get())
        return ime_control_delegate_->HandleNextIme();
      break;
    case PREVIOUS_IME:
      if (ime_control_delegate_.get())
        return ime_control_delegate_->HandlePreviousIme();
      break;
    case SWITCH_IME:
      if (ime_control_delegate_.get())
        return ime_control_delegate_->HandleSwitchIme(accelerator);
      break;
    case SELECT_WIN_0:
      SwitchToWindow(0);
      return true;
    case SELECT_WIN_1:
      SwitchToWindow(1);
      return true;
    case SELECT_WIN_2:
      SwitchToWindow(2);
      return true;
    case SELECT_WIN_3:
      SwitchToWindow(3);
      return true;
    case SELECT_WIN_4:
      SwitchToWindow(4);
      return true;
    case SELECT_WIN_5:
      SwitchToWindow(5);
      return true;
    case SELECT_WIN_6:
      SwitchToWindow(6);
      return true;
    case SELECT_WIN_7:
      SwitchToWindow(7);
      return true;
    case SELECT_LAST_WIN:
      SwitchToWindow(-1);
      return true;
    case WINDOW_SNAP_LEFT:
    case WINDOW_SNAP_RIGHT: {
      aura::Window* window = wm::GetActiveWindow();
      if (!window)
        break;
      internal::SnapSizer sizer(window,
          gfx::Point(),
          action == WINDOW_SNAP_LEFT ? internal::SnapSizer::LEFT_EDGE :
                                       internal::SnapSizer::RIGHT_EDGE,
          shell->GetGridSize());
      window->SetBounds(sizer.GetSnapBounds(window->bounds()));
      return true;
    }
    case WINDOW_MINIMIZE: {
      aura::Window* window = wm::GetActiveWindow();
      if (window) {
        wm::MinimizeWindow(window);
        return true;
      }
      break;
    }
    case WINDOW_MAXIMIZE_RESTORE: {
      aura::Window* window = wm::GetActiveWindow();
      if (window) {
        if (wm::IsWindowMaximized(window))
          wm::RestoreWindow(window);
        else
          wm::MaximizeWindow(window);
        return true;
      }
      break;
    }
    case WINDOW_POSITION_CENTER: {
      aura::Window* window = wm::GetActiveWindow();
      if (window) {
        wm::CenterWindow(window);
        return true;
      }
      break;
    }
    case ROTATE_WINDOWS:
      return HandleRotateWindows();
#if !defined(NDEBUG)
    case ROTATE_SCREEN:
      return HandleRotateScreen();
    case TOGGLE_DESKTOP_BACKGROUND_MODE:
      return HandleToggleDesktopBackgroundMode();
    case TOGGLE_ROOT_WINDOW_FULL_SCREEN:
      return HandleToggleRootWindowFullScreen();
    case PRINT_LAYER_HIERARCHY:
      return HandlePrintLayerHierarchy();
    case PRINT_WINDOW_HIERARCHY:
      return HandlePrintWindowHierarchy();
    case MONITOR_ADD_REMOVE:
      internal::MultiMonitorManager::AddRemoveMonitor();
      return true;
    case MONITOR_CYCLE:
      internal::MultiMonitorManager::CycleMonitor();
      return true;
    case MONITOR_TOGGLE_SCALE:
      internal::MultiMonitorManager::ToggleMonitorScale();
      return true;
#endif
    default:
      NOTREACHED() << "Unhandled action " << it->second;
  }
  return false;
}

void AcceleratorController::SwitchToWindow(int window) {
  const LauncherItems& items =
      Shell::GetInstance()->launcher()->model()->items();
  int item_count =
      Shell::GetInstance()->launcher()->model()->item_count();
  int indexes_left = window >= 0 ? window : item_count;
  int found_index = -1;

  // Iterating until we have hit the index we are interested in which
  // is true once indexes_left becomes negative.
  for (int i = 0; i < item_count && indexes_left >= 0; i++) {
    if (items[i].type != TYPE_APP_LIST &&
        items[i].type != TYPE_BROWSER_SHORTCUT) {
      found_index = i;
      indexes_left--;
    }
  }

  // There are two ways how found_index can be valid: a.) the nth item was
  // found (which is true when indexes_left is -1) or b.) the last item was
  // requested (which is true when index was passed in as a negative number).
  if (found_index >= 0 && (indexes_left == -1 || window < 0) &&
      items[found_index].status == ash::STATUS_RUNNING) {
    // Then set this one as active.
    Shell::GetInstance()->launcher()->ActivateLauncherItem(found_index);
  }
}

bool AcceleratorController::CanHandleAccelerators() const {
  return true;
}

}  // namespace ash
