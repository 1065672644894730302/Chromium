// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef JINGLE_NOTIFIER_LISTENER_FAKE_PUSH_CLIENT_H_
#define JINGLE_NOTIFIER_LISTENER_FAKE_PUSH_CLIENT_H_

#include <vector>

#include "base/basictypes.h"
#include "base/compiler_specific.h"
#include "base/observer_list.h"
#include "jingle/notifier/listener/push_client.h"

namespace notifier {

// PushClient implementation that can be used for testing.
class FakePushClient : public PushClient {
 public:
  FakePushClient();
  virtual ~FakePushClient();

  // PushClient implementation.
  virtual void AddObserver(PushClientObserver* observer) OVERRIDE;
  virtual void RemoveObserver(PushClientObserver* observer) OVERRIDE;
  virtual void UpdateSubscriptions(
      const SubscriptionList& subscriptions) OVERRIDE;
  virtual void UpdateCredentials(
      const std::string& email, const std::string& token) OVERRIDE;
  virtual void SendNotification(const Notification& notification) OVERRIDE;

  // Triggers OnNotificationStateChange on all observers.
  void SimulateNotificationStateChange(bool notifications_enabled);

  // Triggers OnIncomingNotification on all observers.
  void SimulateIncomingNotification(const Notification& notification);

  const SubscriptionList& subscriptions() const;
  const std::string& email() const;
  const std::string& token() const;
  const std::vector<Notification>& sent_notifications() const;

 private:
  ObserverList<PushClientObserver> observers_;
  SubscriptionList subscriptions_;
  std::string email_;
  std::string token_;
  std::vector<Notification> sent_notifications_;

  DISALLOW_COPY_AND_ASSIGN(FakePushClient);
};

}  // namespace notifier

#endif  // JINGLE_NOTIFIER_LISTENER_FAKE_PUSH_CLIENT_H_
