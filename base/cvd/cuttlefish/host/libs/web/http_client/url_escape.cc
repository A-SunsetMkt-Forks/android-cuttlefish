//
// Copyright (C) 2019 The Android Open Source Project
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "cuttlefish/host/libs/web/http_client/url_escape.h"

#include <curl/curl.h>

#include <string>
#include <string_view>

namespace cuttlefish {

std::string UrlEscape(std::string_view text) {
  char* escaped_str = curl_escape(text.data(), text.size());
  std::string ret{escaped_str};
  curl_free(escaped_str);
  return ret;
}

}  // namespace cuttlefish
