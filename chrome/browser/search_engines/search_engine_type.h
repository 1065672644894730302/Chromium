// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SEARCH_ENGINES_SEARCH_ENGINE_TYPE_H_
#define CHROME_BROWSER_SEARCH_ENGINES_SEARCH_ENGINE_TYPE_H_
#pragma once

// Enum to record the user's default search engine choice in UMA.  Add new
// search engines at the bottom and do not delete from this list, so as not
// to disrupt UMA data already recorded.
enum SearchEngineType {
  SEARCH_ENGINE_OTHER = 0,  // At the top in case of future list changes.
  SEARCH_ENGINE_GOOGLE,
  SEARCH_ENGINE_YAHOO,
  SEARCH_ENGINE_YAHOOJP,
  SEARCH_ENGINE_BING,
  SEARCH_ENGINE_ASK,
  SEARCH_ENGINE_YANDEX,
  SEARCH_ENGINE_SEZNAM,
  SEARCH_ENGINE_CENTRUM,
  SEARCH_ENGINE_NETSPRINT,
  SEARCH_ENGINE_VIRGILIO,
  SEARCH_ENGINE_MAILRU,
  SEARCH_ENGINE_ABCSOK,
  SEARCH_ENGINE_ALTAVISTA,
  SEARCH_ENGINE_BAIDU,
  SEARCH_ENGINE_DAUM,
  SEARCH_ENGINE_DELFI,
  SEARCH_ENGINE_DIRI,
  SEARCH_ENGINE_GOO,
  SEARCH_ENGINE_IN,
  SEARCH_ENGINE_NAJDI,
  SEARCH_ENGINE_NAVER,
  SEARCH_ENGINE_NETI,
  SEARCH_ENGINE_OK,
  SEARCH_ENGINE_POGODAK,
  SEARCH_ENGINE_POGODOK_MK,  // Defunct. Deletion would corrupt UMA stats.
  SEARCH_ENGINE_RAMBLER,
  SEARCH_ENGINE_SANOOK,
  SEARCH_ENGINE_SAPO,
  SEARCH_ENGINE_TUT,
  SEARCH_ENGINE_WALLA,
  SEARCH_ENGINE_ZOZNAM,
  SEARCH_ENGINE_YAHOOQC,
  SEARCH_ENGINE_NONE,  // Used by Protector. Putting it at the beginning would
                       // corrupt UMA stats. Add new search engines below.
  SEARCH_ENGINE_CONDUIT,
  SEARCH_ENGINE_ALL_BY,
  SEARCH_ENGINE_APORT,
  SEARCH_ENGINE_ICQ,
  SEARCH_ENGINE_METABOT_RU,
  SEARCH_ENGINE_META_UA,
  SEARCH_ENGINE_NIGMA,
  SEARCH_ENGINE_QIP,
  SEARCH_ENGINE_UKR_NET,
  SEARCH_ENGINE_WEBALTA,
  SEARCH_ENGINE_MAX  // Bounding max value needed for UMA histogram macro.
};

#endif  // CHROME_BROWSER_SEARCH_ENGINES_SEARCH_ENGINE_TYPE_H_
