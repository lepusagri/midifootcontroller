#include "Controller.h"
#include "ControllerLogic.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>

#define NETWORK_MAGIC 0x4C45504Eu
#define NETWORK_VERSION 1u
#define NETWORK_RETRY_MS 30000u
#define NETWORK_RESTART_MS 1500u
#define AP_PASSWORD "lepus-fm3"

typedef struct {
  uint32_t uiMagic;
  uint8_t uiVersion;
  uint8_t uiAccessPoint;
  uint8_t uiDhcp;
  char sSsid[33];
  char sPassword[65];
  char sAddress[16];
  char sGateway[16];
  char sSubnet[16];
  char sDns[16];
} T_NETWORK_SETTINGS;

static T_NETWORK_SETTINGS stSettings = {NETWORK_MAGIC, NETWORK_VERSION, 1, 1, {}, {}, {}, {}, {}, {}};
static String ssAccessPointSsid;
static bool sbFallbackAccessPoint = false;
static bool sbMdnsStarted = false;
static unsigned long suiStationStart = 0;
static unsigned long suiLastRetry = 0;
static unsigned long suiRestartAt = 0;

//=================================================================================================
// Function     : loadNetworkSettings
// Purpose      : Load valid network settings from persistent ESP32 storage.
// Return Value : void
//=================================================================================================
static void loadNetworkSettings() {
  // +++++++++++++++++++++++++++++++++
  Preferences tPreferences;
  T_NETWORK_SETTINGS tLoaded = {};
  bool bValid = false;
  // +++++++++++++++++++++++++++++++++
  if (!tPreferences.begin("lepus-network", true)) return;
  bValid = tPreferences.getBytesLength("settings") == sizeof(tLoaded) &&
           tPreferences.getBytes("settings", &tLoaded, sizeof(tLoaded)) == sizeof(tLoaded);
  tPreferences.end();
  if (!bValid || tLoaded.uiMagic != NETWORK_MAGIC || tLoaded.uiVersion != NETWORK_VERSION ||
      tLoaded.uiAccessPoint > 1 || tLoaded.uiDhcp > 1 ||
      !memchr(tLoaded.sSsid, 0, sizeof(tLoaded.sSsid)) ||
      !memchr(tLoaded.sPassword, 0, sizeof(tLoaded.sPassword))) return;
  stSettings = tLoaded;
}

//=================================================================================================
// Function     : storeNetworkSettings
// Purpose      : Persist one complete settings record.
// Return Value : bool
//=================================================================================================
static bool storeNetworkSettings(const T_NETWORK_SETTINGS& tSettings) {
  // +++++++++++++++++++++++++++++++++
  Preferences tPreferences;
  bool bSaved = false;
  // +++++++++++++++++++++++++++++++++
  if (!tPreferences.begin("lepus-network", false)) return false;
  bSaved = tPreferences.putBytes("settings", &tSettings, sizeof(tSettings)) == sizeof(tSettings);
  tPreferences.end();
  return bSaved;
}

//=================================================================================================
// Function     : startAccessPoint
// Purpose      : Start the phone-accessible network with the ESP32 default address.
// Return Value : bool
//=================================================================================================
static bool startAccessPoint(bool bWithStation) {
  WiFi.mode(bWithStation ? WIFI_AP_STA : WIFI_AP);
  if (!WiFi.softAP(ssAccessPointSsid.c_str(), AP_PASSWORD)) return false;
  Serial.print("Access Point: ");
  Serial.print(ssAccessPointSsid);
  Serial.print(" / ");
  Serial.println(WiFi.softAPIP());
  return true;
}

//=================================================================================================
// Function     : setupNetwork
// Purpose      : Start the stored network mode; a fresh controller starts as an access point.
// Return Value : void
//=================================================================================================
void setupNetwork() {
  // +++++++++++++++++++++++++++++++++
  uint64_t uiMac = ESP.getEfuseMac();
  IPAddress tAddress;
  IPAddress tGateway;
  IPAddress tSubnet;
  IPAddress tDns;
  char sSuffix[7];
  // +++++++++++++++++++++++++++++++++
  loadNetworkSettings();
  snprintf(sSuffix, sizeof(sSuffix), "%06lX", static_cast<unsigned long>(uiMac & 0xFFFFFFu));
  ssAccessPointSsid = String("Lepus-") + sSuffix;
  if (stSettings.uiAccessPoint) {
    startAccessPoint(false);
    return;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setHostname("fm3-controller");
  WiFi.setAutoReconnect(true);
  if (!stSettings.uiDhcp) {
    tAddress.fromString(stSettings.sAddress);
    tGateway.fromString(stSettings.sGateway);
    tSubnet.fromString(stSettings.sSubnet);
    tDns.fromString(stSettings.sDns);
    WiFi.config(tAddress, tGateway, tSubnet, tDns);
  }
  WiFi.begin(stSettings.sSsid, stSettings.sPassword);
  suiStationStart = millis();
  suiLastRetry = suiStationStart;
}

//=================================================================================================
// Function     : updateNetwork
// Purpose      : Maintain station connection and provide access-point fallback after a timeout.
// Return Value : void
//=================================================================================================
void updateNetwork(unsigned long uiNow) {
  if (suiRestartAt && static_cast<long>(uiNow - suiRestartAt) >= 0) ESP.restart();
  if (stSettings.uiAccessPoint) return;
  if (WiFi.status() == WL_CONNECTED) {
    suiStationStart = uiNow;
    if (sbFallbackAccessPoint) {
      WiFi.softAPdisconnect(true);
      WiFi.mode(WIFI_STA);
      sbFallbackAccessPoint = false;
    }
    if (!sbMdnsStarted && MDNS.begin("fm3-controller")) {
      MDNS.addService("http", "tcp", 80);
      sbMdnsStarted = true;
    }
    return;
  }
  if (sbMdnsStarted) { MDNS.end(); sbMdnsStarted = false; }
  if (!sbFallbackAccessPoint && uiNow - suiStationStart >= NETWORK_RETRY_MS) {
    sbFallbackAccessPoint = startAccessPoint(true);
  }
  if (uiNow - suiLastRetry >= NETWORK_RETRY_MS) {
    suiLastRetry = uiNow;
    WiFi.reconnect();
  }
}

bool networkIsAccessPoint() { return stSettings.uiAccessPoint || sbFallbackAccessPoint; }
String networkAddress() { return networkIsAccessPoint() ? WiFi.softAPIP().toString() : WiFi.localIP().toString(); }
String networkSettingsJson() {
  // +++++++++++++++++++++++++++++++++
  String sJson = "{\"mode\":\"";
  const char* psKeys[] = {"ip", "gateway", "subnet", "dns"};
  const char* psValues[] = {stSettings.sAddress, stSettings.sGateway, stSettings.sSubnet, stSettings.sDns};
  // +++++++++++++++++++++++++++++++++
  sJson += stSettings.uiAccessPoint ? "ap" : "home";
  sJson += "\",\"activeMode\":\"";
  sJson += networkIsAccessPoint() ? "ap" : "home";
  sJson += "\",\"ssid\":";
  appendJsonString(sJson, stSettings.sSsid);
  sJson += ",\"apSsid\":";
  appendJsonString(sJson, ssAccessPointSsid.c_str());
  sJson += ",\"address\":";
  appendJsonString(sJson, networkAddress().c_str());
  sJson += ",\"dhcp\":";
  sJson += stSettings.uiDhcp ? "true" : "false";
  sJson += ",\"hasPassword\":";
  sJson += stSettings.sPassword[0] ? "true" : "false";
  for (int iIndex = 0; iIndex < 4; ++iIndex) {
    sJson += ",\"";
    sJson += psKeys[iIndex];
    sJson += "\":";
    appendJsonString(sJson, psValues[iIndex]);
  }
  sJson += '}';
  return sJson;
}

//=================================================================================================
// Function     : saveNetworkSettings
// Purpose      : Validate and persist web settings before scheduling a restart.
// Return Value : bool
//=================================================================================================
bool saveNetworkSettings(bool bAccessPoint, const String& sSsid, const String& sPassword,
                         bool bDhcp, const String& sAddress, const String& sGateway,
                         const String& sSubnet, const String& sDns, String& sError) {
  // +++++++++++++++++++++++++++++++++
  T_NETWORK_SETTINGS tNext = stSettings;
  IPAddress tParsed;
  // +++++++++++++++++++++++++++++++++
  if (!bAccessPoint && (sSsid.isEmpty() || sSsid.length() > 32 || sPassword.length() > 64)) {
    sError = "SSID oder Passwort ist ungueltig";
    return false;
  }
  if (!bAccessPoint && !bDhcp) {
    const String* psValues[] = {&sAddress, &sGateway, &sSubnet, &sDns};
    for (int iIndex = 0; iIndex < 4; ++iIndex) {
      if ((*psValues[iIndex]).length() > 15 || !tParsed.fromString(*psValues[iIndex]) ||
          tParsed == IPAddress(0, 0, 0, 0)) {
        sError = "IP-Adresse, Gateway, Maske und DNS muessen gueltig sein";
        return false;
      }
    }
  }
  tNext.uiAccessPoint = bAccessPoint ? 1 : 0;
  tNext.uiDhcp = bDhcp ? 1 : 0;
  if (!bAccessPoint) {
    sSsid.toCharArray(tNext.sSsid, sizeof(tNext.sSsid));
    if (!sPassword.isEmpty() || sSsid != stSettings.sSsid)
      sPassword.toCharArray(tNext.sPassword, sizeof(tNext.sPassword));
    sAddress.toCharArray(tNext.sAddress, sizeof(tNext.sAddress));
    sGateway.toCharArray(tNext.sGateway, sizeof(tNext.sGateway));
    sSubnet.toCharArray(tNext.sSubnet, sizeof(tNext.sSubnet));
    sDns.toCharArray(tNext.sDns, sizeof(tNext.sDns));
  }
  if (!storeNetworkSettings(tNext)) {
    sError = "Netzwerkeinstellungen konnten nicht gespeichert werden";
    return false;
  }
  stSettings = tNext;
  suiRestartAt = millis() + NETWORK_RESTART_MS;
  return true;
}

void resetNetworkToAccessPoint() {
  // +++++++++++++++++++++++++++++++++
  T_NETWORK_SETTINGS tNext = stSettings;
  // +++++++++++++++++++++++++++++++++
  if (stSettings.uiAccessPoint) return;
  tNext.uiAccessPoint = 1;
  if (storeNetworkSettings(tNext)) {
    stSettings = tNext;
    ESP.restart();
  } else {
    lastOperation = "Access Point konnte nicht gespeichert werden";
  }
}
