// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/extensions/extension_message_handler.h"

#include "chrome/browser/extensions/extension_message_service.h"
#include "chrome/browser/extensions/extension_system.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/view_type_utils.h"
#include "chrome/common/extensions/extension_messages.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/web_contents.h"

using content::WebContents;

ExtensionMessageHandler::ExtensionMessageHandler(
    content::RenderViewHost* render_view_host)
    : content::RenderViewHostObserver(render_view_host) {
}

ExtensionMessageHandler::~ExtensionMessageHandler() {
}

bool ExtensionMessageHandler::OnMessageReceived(
    const IPC::Message& message) {
  bool handled = true;
  IPC_BEGIN_MESSAGE_MAP(ExtensionMessageHandler, message)
    IPC_MESSAGE_HANDLER(ExtensionHostMsg_PostMessage, OnPostMessage)
    IPC_MESSAGE_UNHANDLED(handled = false)
  IPC_END_MESSAGE_MAP()
  return handled;
}

void ExtensionMessageHandler::RenderViewHostInitialized() {
  WebContents* web_contents =
      WebContents::FromRenderViewHost(render_view_host());
  Send(new ExtensionMsg_NotifyRenderViewType(
      routing_id(), chrome::GetViewType(web_contents)));
}

void ExtensionMessageHandler::OnPostMessage(int port_id,
                                            const std::string& message) {
  Profile* profile = Profile::FromBrowserContext(
      render_view_host()->GetProcess()->GetBrowserContext());
  ExtensionMessageService* message_service =
      ExtensionSystem::Get(profile)->message_service();
  if (message_service) {
    message_service->PostMessageFromRenderer(port_id, message);
  }
}
