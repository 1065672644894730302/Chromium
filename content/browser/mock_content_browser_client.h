// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CONTENT_BROWSER_MOCK_CONTENT_BROWSER_CLIENT_H_
#define CONTENT_BROWSER_MOCK_CONTENT_BROWSER_CLIENT_H_
#pragma once

#include <string>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/scoped_temp_dir.h"
#include "content/public/browser/content_browser_client.h"

namespace content {

// Base for unit tests that need to mock the ContentBrowserClient.
class MockContentBrowserClient : public ContentBrowserClient {
 public:
  MockContentBrowserClient();
  virtual ~MockContentBrowserClient();

  virtual WebContentsView* OverrideCreateWebContentsView(
      WebContents* web_contents,
      RenderViewHostDelegateView** render_view_host_delegate_view) OVERRIDE;
  virtual FilePath GetDefaultDownloadDirectory() OVERRIDE;

 private:
  // Temporary directory for GetDefaultDownloadDirectory.
  ScopedTempDir download_dir_;

  DISALLOW_COPY_AND_ASSIGN(MockContentBrowserClient);
};

}  // namespace content

#endif  // CONTENT_BROWSER_MOCK_CONTENT_BROWSER_CLIENT_H_
