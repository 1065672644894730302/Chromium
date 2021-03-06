// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NATIVE_CLIENT_SRC_SHARED_PPAPI_PROXY_PLUGIN_UDP_SOCKET_PRIVATE_H_
#define NATIVE_CLIENT_SRC_SHARED_PPAPI_PROXY_PLUGIN_UDP_SOCKET_PRIVATE_H_

#include "native_client/src/include/nacl_macros.h"
#include "ppapi/c/private/ppb_udp_socket_private.h"

namespace ppapi_proxy {

// Implements the untrusted side of the PPB_UDPSocket_Private interface.
class PluginUDPSocketPrivate {
 public:
  static const PPB_UDPSocket_Private_0_2* GetInterface0_2();
  static const PPB_UDPSocket_Private_0_3* GetInterface0_3();

 private:
  NACL_DISALLOW_COPY_AND_ASSIGN(PluginUDPSocketPrivate);
};

}  // namespace ppapi_proxy

#endif  // NATIVE_CLIENT_SRC_SHARED_PPAPI_PROXY_PLUGIN_UDP_SOCKET_PRIVATE_H_
