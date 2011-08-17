// Copyright 2007-2010 Google Inc.
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
// ========================================================================

// TODO(omaha): might need to remove dependency on winhttp.h when implementing
// support for wininet; see http://b/1119232

#ifndef OMAHA_NET_NETWORK_CONFIG_H__
#define OMAHA_NET_NETWORK_CONFIG_H__

#include <windows.h>
#include <winhttp.h>
#include <atlstr.h>
#include <map>
#include <vector>
#include "base/basictypes.h"
#include "base/scoped_ptr.h"
#include "omaha/base/scoped_any.h"
#include "omaha/base/synchronized.h"
#include "omaha/net/detector.h"
#include "omaha/net/http_client.h"
#include "omaha/net/proxy_auth.h"

namespace ATL {

class CSecurityDesc;

}  // namespace ATL

namespace omaha {

class RegKey;

// The cup credentials are persisted across sessions. The sk is encrypted
// while on the disk so only a user with the same login credentials as
// the encryptor can decrypt it. The credentials are protected
// using the system default security, so users can't modify each other's
// credentials. In case of elevated administrators, the credentials are
// protected from the non-elevated administrators, so the latter can't
// read the keys and attack the elevated administrator.
//
// Cup credentials can be negotiated using either production keys or
// test keys. There is a registry value override to specify that test keys
// be used. For the change to be effective, the old credentials must be cleared.
struct CupCredentials {
  std::vector<uint8> sk;             // shared key (sk)
  CStringA c;                        // client cookie (c)
};

// There are three ways by which an application could connect to the Internet:
// 1. Direct connection.
//    The config for the direction connection must not specify WPAD information
//    nor named proxy information.
// 2. Named proxy.
//    The config for named proxy only includes proxy and proxy_bypass.
// 3. Proxy auto detection.
//    The config for proxy auto detection should include either the auto-detect
//    flag or the auto configuration url. Named proxy information is discarded
//    if present.
struct ProxyConfig {
  ProxyConfig() : auto_detect(false), priority(PROXY_PRIORITY_DEFAULT_NORMAL) {}

  // Used to uniquely identify a proxy.
  CString source;

  // Specifies the configuration is WPAD.
  bool auto_detect;

  // The url of the proxy configuration script, if known.
  CString auto_config_url;

  // Named proxy information.
  // The proxy string is usually something as "http=foo:80;https=bar:8080".
  // According to the documentation for WINHTTP_PROXY_INFO, multiple proxies
  // are separated by semicolons or whitespace. The documentation for
  // IBackgroundCopyJob::SetProxySettings says that the list is
  // space-delimited.
  // TODO(omaha): our proxy information is semicolon-separated. This may
  // result in compatibility problems with BITS. Fix this.
  CString proxy;
  CString proxy_bypass;

  // Suggested priority of the proxy config. When establishing network
  // connections, it is a good idea to try higher priority proxy first.
  enum Priority {
    PROXY_PRIORITY_DEFAULT_NORMAL = 0,
    PROXY_PRIORITY_DEFAULT_BROWSER = 1,
    PROXY_PRIORITY_LAST_KNOWN_GOOD = 2,
    PROXY_PRIORITY_OVERRIDE = 3,
  } priority;
};

// Manages the network configurations.
class NetworkConfig {
 public:
  // Abstracts the Internet session, as provided by winhttp or wininet.
  // A winhttp session should map to one and only one identity. in other words,
  // a winhttp session is used to manage the network traffic of a single
  // authenticated user, or a group of anonymous users.
  struct Session {
    Session() : session_handle(NULL) {}

    HINTERNET session_handle;
  };

  // Hooks up a proxy detector. The class takes ownership of the detector.
  void Add(ProxyDetectorInterface* detector);

  // Clears all detectors and configurations. It does not clear the session.
  // TODO(omaha): rename to avoid the confusion that Clear clears the sessions
  // as well.
  void Clear();

  // Detects the network configuration for each of the registered detectors.
  HRESULT Detect();

  // Detects the network configuration for the given source.
  HRESULT Detect(const CString& proxy_source, ProxyConfig* config) const;

  static HRESULT ConfigFromIdentifier(const CString& id, ProxyConfig* config);

  static bool ProxySortPredicate(const ProxyConfig& config1,
                                 const ProxyConfig& config2) {
    return config1.priority > config2.priority;
  }

  // Sort the proxy configs based on their priorities. Proxy with higher
  // priority precedes that with lower priority.
  static void SortProxies(std::vector<ProxyConfig>* configurations);

  void AppendLastKnownGoodProxyConfig(
      std::vector<ProxyConfig>* configurations) const;

  // Adds static configurations (WPAD & direct) to current detected network
  // configuration list.
  static void AppendStaticProxyConfigs(
      std::vector<ProxyConfig>* configurations);

  // Returns the detected configurations.
  std::vector<ProxyConfig> GetConfigurations() const;

  // Gets the persisted CUP credentials.
  HRESULT GetCupCredentials(CupCredentials* cup_credentials) const;

  // Saves the CUP credentials in persistent storage. If the parameter is null,
  // it clears the credentials.
  HRESULT SetCupCredentials(const CupCredentials* cup_credentials) const;

  // Prompts for credentials, or gets cached credentials if they exist.
  bool GetProxyCredentials(bool allow_ui,
                           bool force_ui,
                           const CString& proxy_settings,
                           const ProxyAuthConfig& proxy_auth_config,
                           bool is_https,
                           CString* username,
                           CString* password,
                           uint32* auth_scheme);

  // Once a auth scheme has been verified against a proxy, this allows a client
  // to record the auth scheme that was used and was successful, so it can be
  // cached for future use within this process.
  HRESULT SetProxyAuthScheme(const CString& proxy_settings,
                             bool is_https,
                             uint32 auth_scheme);

  // Runs the WPAD protocol to compute the proxy information to be used
  // for the given url. The ProxyInfo pointer members must be freed using
  // GlobalFree.
  HRESULT GetProxyForUrl(const CString& url,
                         const CString& auto_config_url,
                         HttpClient::ProxyInfo* proxy_info);

  Session session() const { return session_; }

  // Returns the global configuration override if available.
  HRESULT GetConfigurationOverride(ProxyConfig* configuration_override);

  // Sets the global configuration override. The function clears the existing
  // configuration if the parameter is NULL.
  void SetConfigurationOverride(const ProxyConfig* configuration_override);

  // True if the CUP test keys are being used to negotiate the CUP
  // credentials.
  bool static IsUsingCupTestKeys();

  // Returns the prefix of the user agent string.
  static CString GetUserAgent();

  // Returns the MID value under UpdateDev.
  static CString GetMID();

  // Eliminates the redundant configurations, for example, if multiple
  // direct connection or proxy auto-detect occur.
  static void RemoveDuplicates(std::vector<ProxyConfig>*);

  // Saves/loads a proxy source and auto_detect information to the registry
  // so that that proxy can be tried with high priority when establishing
  // network connections later on.
  static HRESULT SaveProxyConfig(const ProxyConfig& config);
  HRESULT LoadProxyConfig(ProxyConfig* config) const;

  // Parses a network configuration string. The format of the string is:
  // wpad=[false|true];script=script_url;proxy=host:port
  // Ignores the names and the values it does not understand.
  static ProxyConfig ParseNetConfig(const CString& net_config);

  // Serializes configurations for debugging purposes.
  static CString ToString(const std::vector<ProxyConfig>& configurations);
  static CString ToString(const ProxyConfig& configuration);

  static int GetAccessType(const ProxyConfig& config);

  // Returns s1 + delim + s2. Consider making it an utility function if
  // more usage patterns are found.
  static CString JoinStrings(const TCHAR* s1,
                             const TCHAR* s2,
                             const TCHAR* delim);

  // Uses jsproxy to use a PAC proxy configuration file stored on the local
  // drive, instead of one sourced from WPAD.
  static HRESULT GetProxyForUrlLocal(const CString& url,
                                     const CString& path_to_pac_file,
                                     HttpClient::ProxyInfo* proxy_info);

 private:
  explicit NetworkConfig(bool is_machine);
  ~NetworkConfig();

  HRESULT Initialize();

  // Configures the proxy auth credentials options. Called by Initialize().
  void ConfigureProxyAuth();

  // Creates the proxy configuration registry key for the calling user
  // identified by the token.
  static HRESULT CreateProxyConfigRegKey(RegKey* key);

  // Converts a response string from a PAC script into an WinHTTP proxy
  // descriptor struct.
  static void ConvertPacResponseToProxyInfo(const CStringA& response,
                                            HttpClient::ProxyInfo* proxy_info);

  static const TCHAR* const kUserAgent;

  static const TCHAR* const kRegKeyProxy;
  static const TCHAR* const kRegValueSource;

  static const TCHAR* const kWPADIdentifier;
  static const TCHAR* const kDirectConnectionIdentifier;

  bool is_machine_;     // True if the instance is initialized for machine.

  std::vector<ProxyConfig> configurations_;
  std::vector<ProxyDetectorInterface*> detectors_;

  // Synchronizes access to per-process instance data, which includes
  // the detectors and configurations.
  LLock lock_;

  bool is_initialized_;

  scoped_ptr<ProxyConfig> configuration_override_;

  Session session_;
  scoped_ptr<HttpClient> http_client_;

  // Manages the proxy auth credentials. Typically a http client tries to
  // use autologon via Negotiate/NTLM with a proxy server. If that fails, the
  // Http client then calls GetProxyCredentials() on NetworkConfig.
  // GetProxyCredentials() gets credentials by either prompting the user, or
  // cached credentials. Then the http client tries again. Options are set via
  // ConfigureProxyAuth().
  ProxyAuth proxy_auth_;

  friend class NetworkConfigManager;
  DISALLOW_EVIL_CONSTRUCTORS(NetworkConfig);
};

class NetworkConfigManager {
 public:
  static NetworkConfigManager& Instance();
  static void DeleteInstance();

  // Directs this singleton class to create machine or user instance.
  static void set_is_machine(bool is_machine);

  HRESULT GetUserNetworkConfig(NetworkConfig** network_config);

  // Gets the persisted CUP credentials.
  HRESULT GetCupCredentials(CupCredentials* cup_credentials);

  // Saves the CUP credentials in persistent storage.
  HRESULT SetCupCredentials(const CupCredentials& cup_credentials);

  void ClearCupCredentials();

 private:
  explicit NetworkConfigManager();
  ~NetworkConfigManager();

  static HRESULT CreateInstance();

  void DeleteInstanceInternal();

  HRESULT InitializeLock();
  HRESULT InitializeRegistryKey();

  HRESULT CreateNetworkConfigInstance(NetworkConfig** network_config_ptr,
                                      bool is_machine);
  HRESULT LoadCupCredentialsFromRegistry();
  HRESULT SaveCupCredentialsToRegistry();

  std::map<CString, NetworkConfig*> user_network_config_map_;
  scoped_ptr<CupCredentials> cup_credentials_;

  LLock lock_;

  // Synchronizes access to CUP registry.
  GLock global_lock_;

  // Registry sub key where network configuration is persisted.
  static const TCHAR* const kNetworkSubkey;

  // Registry sub key where CUP configuration is persisted.
  static const TCHAR* const kNetworkCupSubkey;

  // The secret key must be encrypted by the caller. This class does not do any
  // encryption.
  static const TCHAR* const kCupClientSecretKey;      // CUP sk.
  static const TCHAR* const kCupClientCookie;         // CUP c.

  static const NetworkConfigManager* const kInvalidInstance;
  static NetworkConfigManager* instance_;
  static LLock instance_lock_;
  static bool is_machine_;

  DISALLOW_EVIL_CONSTRUCTORS(NetworkConfigManager);
};

}   // namespace omaha

#endif  // OMAHA_NET_NETWORK_CONFIG_H__

