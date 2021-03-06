/***
 * Copyright (C) Microsoft. All rights reserved.
 * Licensed under the MIT license. See LICENSE.txt file in the project root for full license
 *information.
 *
 * =+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+=+
 *
 * Oauth2Client.cpp : Defines the entry point for the console application
 *
 * =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-
 ****/

/*

INSTRUCTIONS

This sample performs authorization code grant flow on various OAuth 2.0
services and then requests basic user information.

This sample is for Windows Desktop, OS X and Linux.
Execute with administrator privileges.

Set the app key & secret strings below (i.e. s_dropbox_key, s_dropbox_secret, etc.)
To get key & secret, register an app in the corresponding service.

Set following entry in the hosts file:
127.0.0.1    testhost.local

*/
#include "cpprest/http_client.h"
#include <filesystem>
#include <fstream>
#include <ios>
#include <mutex>
#include <sstream>

#if defined(_WIN32) && !defined(__cplusplus_winrt)
// Extra includes for Windows desktop.
#include <windows.h>

#include <Shellapi.h>
#endif

#include "cpprest/filestream.h"
#include "cpprest/http_client.h"
#include "cpprest/http_listener.h"

using namespace utility;
using namespace web;
using namespace web::http;
using namespace web::http::client;
using namespace web::http::oauth2::experimental;
using namespace web::http::experimental::listener;

//
// Utility method to open browser on Windows, OS X and Linux systems.
//
static void open_browser(utility::string_t auth_uri) {
#if defined(_WIN32) && !defined(__cplusplus_winrt)
  // NOTE: Windows desktop only.
  auto r = ShellExecuteA(NULL, "open", conversions::utf16_to_utf8(auth_uri).c_str(), NULL, NULL,
                         SW_SHOWNORMAL);
#elif defined(__APPLE__)
  // NOTE: OS X only.
  string_t browser_cmd(U("open \"") + auth_uri + U("\""));
  (void)system(browser_cmd.c_str());
#else
  // NOTE: Linux/X11 only.
  string_t browser_cmd(U("xdg-open \"") + auth_uri + U("\""));
  (void)system(browser_cmd.c_str());
#endif
}

//
// A simple listener class to capture OAuth 2.0 HTTP redirect to localhost.
// The listener captures redirected URI and obtains the token.
// This type of listener can be implemented in the back-end to capture and store tokens.
//
class oauth2_code_listener {
public:
  oauth2_code_listener(uri listen_uri, oauth2_config& config)
      : m_listener(new http_listener(listen_uri)), m_config(config) {
    m_listener->support([this](http::http_request request) -> void {
      if(request.request_uri().path() == U("/") && !request.request_uri().query().empty()) {
        m_resplock.lock();

        m_config.token_from_redirected_uri(request.request_uri())
            .then([this, request](pplx::task<void> token_task) -> void {
              try {
                token_task.wait();
                m_tce.set(true);
              } catch(const oauth2_exception& e) {
                ucout << "Error: " << e.what() << std::endl;
                m_tce.set(false);
              }
            });

        request.reply(status_codes::OK, U("Ok."));

        m_resplock.unlock();
      } else {
        request.reply(status_codes::NotFound, U("Not found."));
      }
    });

    m_listener->open().wait();
  }

  ~oauth2_code_listener() { m_listener->close().wait(); }

  pplx::task<bool> listen_for_code() { return pplx::create_task(m_tce); }

private:
  std::unique_ptr<http_listener> m_listener;
  pplx::task_completion_event<bool> m_tce;
  oauth2_config& m_config;
  std::mutex m_resplock;
};

//
// Base class for OAuth 2.0 sessions of this sample.
//
class oauth2_session_sample {
public:
  oauth2_session_sample(utility::string_t name, utility::string_t client_key,
                        utility::string_t client_secret, utility::string_t auth_endpoint,
                        utility::string_t token_endpoint, utility::string_t redirect_uri)
      : m_oauth2_config(client_key, client_secret, auth_endpoint, token_endpoint, redirect_uri),
        m_name(name), m_listener(new oauth2_code_listener(redirect_uri, m_oauth2_config)) {}

  void run() {
    if(is_enabled()) {

      if(!m_oauth2_config.token().is_valid_access_token()) {
        if(authorization_code_flow().get()) {
          m_http_config.set_oauth2(m_oauth2_config);
        } else {
          ucout << "Authorization failed for " << m_name.c_str() << "." << std::endl;
        }
      }

      run_internal();
    } else {
      ucout << "Skipped " << m_name.c_str()
            << " session sample because app key or secret is empty. Please see instructions."
            << std::endl;
    }
  }

protected:
  virtual void run_internal() = 0;

  pplx::task<bool> authorization_code_flow() {
    open_browser_auth();
    return m_listener->listen_for_code();
  }

  http_client_config m_http_config;
  oauth2_config m_oauth2_config;

private:
  bool is_enabled() const {
    return !m_oauth2_config.client_key().empty() && !m_oauth2_config.client_secret().empty();
  }

  void open_browser_auth() {
    auto auth_uri(m_oauth2_config.build_authorization_uri(true));
    open_browser(auth_uri);
  }

  utility::string_t m_name;
  std::unique_ptr<oauth2_code_listener> m_listener;
};

class live_session_sample : public oauth2_session_sample {
  std::string filename;

public:
  live_session_sample(std::string filename, std::string s_live_key, std::string s_live_secret)
      : oauth2_session_sample(U("Live"), s_live_key, s_live_secret,
                              U("https://login.microsoftonline.com/common/oauth2/v2.0/authorize"),
                              U("https://login.microsoftonline.com/common/oauth2/v2.0//token"),
                              U("http://localhost:8890/")),
        filename(filename) {
    m_oauth2_config.set_scope(U("Files.Read Files.ReadWrite Files.Read.All Files.ReadWrite.All "
                                "Sites.Read.All Sites.ReadWrite.All"));
  }

  std::optional<unsigned long> getNextRangeOffset(http_client& api, uri uploadUrl) {
    auto uploadDescription =
        api.request(methods::GET, uploadUrl.to_string()).get().extract_json().get();
    if(!uploadDescription.has_field("nextExpectedRanges")) {
      return {};
    }
    auto rangestream =
        std::stringstream(uploadDescription.at("nextExpectedRanges").at(0).as_string());
    unsigned long rangeStart = 0;
    rangestream >> rangeStart;
    return rangeStart;
  }

protected:
  constexpr static const auto* const dbPath = ".onestream.json";
  void run_internal() override {
    http_client api(U("https://graph.microsoft.com/v1.0/"), m_http_config);

    auto file =
        concurrency::streams::file_stream<uint8_t>::open_istream(filename, std::ios::binary).get();
    file.seek(0, std::ios_base::end);
    auto size = long(file.tell());
    auto uploadUrl = [](auto& api, auto& filename) {
      auto uploadDatabase = json::value();
      if(std::filesystem::exists(std::filesystem::path(dbPath))) {
        utility::ifstream_t database_stream(dbPath);
        uploadDatabase = json::value::parse(database_stream);
      }

      auto remoteFileURL =
          "me/drive/root:/uploads/" + filename.substr(filename.find_last_of("/\\") + 1);
      if(!uploadDatabase.has_field(remoteFileURL)) {
        std::cout << "starting upload of " << filename << std::endl;
        uploadDatabase[remoteFileURL] =
            api.request(methods::PUT, remoteFileURL + ":/createUploadSession")
                .get()
                .extract_json()
                .get();
      } else {
        std::cout << "resuming upload of" << filename << std::endl;
      }
      auto db = utility::ofstream_t(dbPath);
      db << uploadDatabase << std::endl;
      db.close();
      return uri(uploadDatabase[remoteFileURL].at("uploadUrl").as_string());
    }(api, filename);
    http_client api2(uploadUrl.scheme() + "://" + uploadUrl.host(), m_http_config);
    for(auto rangeStart = getNextRangeOffset(api2, uploadUrl); rangeStart.has_value();
        rangeStart = getNextRangeOffset(api2, uploadUrl)) {

      file.seek(rangeStart.value(), std::ios_base::beg);
      auto chunkSize = std::min(64 * 1024 * 1024UL, size - rangeStart.value());
      std::cout << "uploading range " << rangeStart.value() << "-"
                << rangeStart.value() + chunkSize - 1UL << ": " << (rangeStart.value() * 100 / size)
                << "%" << std::endl;

      auto uploadRequest = http_request(methods::PUT);
      uploadRequest.set_request_uri(uploadUrl);
      uploadRequest.headers().add("Content-Length", size);
      uploadRequest.headers().add("Content-Range",
                                  std::string("bytes ") + std::to_string(rangeStart.value()) + "-" +
                                      std::to_string(rangeStart.value() + chunkSize - 1UL) + "/" +
                                      std::to_string(size));
      uploadRequest.set_body(file, chunkSize);
      try {
        api2.request(uploadRequest).get().extract_json().get();
      } catch(web::http::http_exception const& e) {
        std::cout << e.what() << std::endl;
      }
    }

    std::cout
        << api.request(methods::GET, "me/drive/root:/uploads:/children").get().extract_json().get()
        << std::endl;
  }
};

#ifdef _WIN32
int wmain(int argc, wchar_t* argv[])
#else
int main(int argc, char* argv[])
#endif
{
  if(argc != 4) {
    std::cout << "usage: OneStream ${filename} ${oauth app key} ${oauth app secret}" << std::endl;
    return 1;
  }
  auto live = live_session_sample(argv[1], argv[2], argv[3]);
  live.run();

  return 0;
}
