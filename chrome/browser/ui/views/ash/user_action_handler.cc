// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/ash/user_action_handler.h"

#include "ash/wm/window_util.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/browser_window.h"
#include "chrome/browser/ui/tab_contents/tab_contents_wrapper.h"
#include "content/public/browser/web_contents.h"

// Returns the currently-active WebContents belonging to the active browser, or
// NULL if there's no currently-active browser.
content::WebContents* GetActiveWebContents() {
  Browser* browser = BrowserList::GetLastActive();
  if (!browser)
    return NULL;
  if (!ash::wm::IsActiveWindow(browser->window()->GetNativeHandle()))
    return NULL;

  TabContentsWrapper* wrapper = browser->GetSelectedTabContentsWrapper();
  if (!wrapper)
    return NULL;
  return wrapper->web_contents();
}

UserActionHandler::UserActionHandler() {}

UserActionHandler::~UserActionHandler() {}

bool UserActionHandler::OnUserAction(
    aura::client::UserActionClient::Command command) {
  switch (command) {
    case aura::client::UserActionClient::BACK: {
      content::WebContents* contents = GetActiveWebContents();
      if (contents && contents->GetController().CanGoBack()) {
        contents->GetController().GoBack();
        return true;
      }
      break;
    }
    case aura::client::UserActionClient::FORWARD: {
      content::WebContents* contents = GetActiveWebContents();
      if (contents && contents->GetController().CanGoForward()) {
        contents->GetController().GoForward();
        return true;
      }
      break;
    }
    default:
      break;
  }
  return false;
}
