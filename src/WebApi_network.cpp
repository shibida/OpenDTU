// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2024 Thomas Basler and others
 */
#include "WebApi_network.h"
#include "Configuration.h"
#include "NetworkSettings.h"
#include "WebApi.h"
#include "WebApi_errors.h"
#include "helper.h"
#include <AsyncJson.h>

WebApiNetworkClass::WebApiNetworkClass()
    : _applyDataTask(500 * TASK_MILLISECOND, TASK_ONCE, std::bind(&WebApiNetworkClass::applyDataTaskCb, this))
{
}

void WebApiNetworkClass::init(AsyncWebServer& server, Scheduler& scheduler)
{
    using std::placeholders::_1;

    server.on("/api/network/status", HTTP_GET, std::bind(&WebApiNetworkClass::onNetworkStatus, this, _1));
    server.on("/api/network/config", HTTP_GET, std::bind(&WebApiNetworkClass::onNetworkAdminGet, this, _1));
    server.on("/api/network/config", HTTP_POST, std::bind(&WebApiNetworkClass::onNetworkAdminPost, this, _1));

    scheduler.addTask(_applyDataTask);
}

void WebApiNetworkClass::onNetworkStatus(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentialsReadonly(request)) {
        return;
    }

    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();

    root["sta_status"] = ((WiFi.getMode() & WIFI_STA) != 0);
    root["sta_ssid"] = WiFi.SSID();
    root["sta_bssid"] = WiFi.BSSIDstr();
    root["sta_rssi"] = WiFi.RSSI();
    root["network_hostname"] = NetworkSettings.getHostname();
    root["network_ip"] = NetworkSettings.localIP().toString();
    root["network_netmask"] = NetworkSettings.subnetMask().toString();
    root["network_gateway"] = NetworkSettings.gatewayIP().toString();
    root["network_dns1"] = NetworkSettings.dnsIP(0).toString();
    root["network_dns2"] = NetworkSettings.dnsIP(1).toString();
    root["network_mac"] = NetworkSettings.macAddress();
    root["network_mode"] = NetworkSettings.NetworkMode() == network_mode::WiFi ? "Station" : "Ethernet";
    root["ap_status"] = ((WiFi.getMode() & WIFI_AP) != 0);
    root["ap_ssid"] = NetworkSettings.getApName();
    root["ap_ip"] = WiFi.softAPIP().toString();
    root["ap_mac"] = WiFi.softAPmacAddress();
    root["ap_stationnum"] = WiFi.softAPgetStationNum();

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiNetworkClass::onNetworkAdminGet(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) {
        return;
    }

    AsyncJsonResponse* response = new AsyncJsonResponse();
    auto& root = response->getRoot();
    const CONFIG_T& config = Configuration.get();

    root["hostname"] = config.WiFi.Hostname;
    root["dhcp"] = config.WiFi.Dhcp;
    root["ipaddress"] = IPAddress(config.WiFi.Ip).toString();
    root["netmask"] = IPAddress(config.WiFi.Netmask).toString();
    root["gateway"] = IPAddress(config.WiFi.Gateway).toString();
    root["dns1"] = IPAddress(config.WiFi.Dns1).toString();
    root["dns2"] = IPAddress(config.WiFi.Dns2).toString();
    root["ssid"] = config.WiFi.Ssid;
    root["password"] = config.WiFi.Password;
    root["aptimeout"] = config.WiFi.ApTimeout;
    root["mdnsenabled"] = config.Mdns.Enabled;
    root["syslogenabled"] = config.Syslog.Enabled;
    root["sysloghostname"] = config.Syslog.Hostname;
    root["syslogport"] = config.Syslog.Port;

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
}

void WebApiNetworkClass::onNetworkAdminPost(AsyncWebServerRequest* request)
{
    if (!WebApi.checkCredentials(request)) {
        return;
    }

    AsyncJsonResponse* response = new AsyncJsonResponse();
    JsonDocument root;
    if (!WebApi.parseRequestData(request, response, root)) {
        return;
    }

    auto& retMsg = response->getRoot();

    if (!(root["ssid"].is<String>()
            && root["password"].is<String>()
            && root["hostname"].is<String>()
            && root["dhcp"].is<bool>()
            && root["ipaddress"].is<String>()
            && root["netmask"].is<String>()
            && root["gateway"].is<String>()
            && root["dns1"].is<String>()
            && root["dns2"].is<String>()
            && root["aptimeout"].is<uint>())) {
        retMsg["message"] = "Values are missing!";
        retMsg["code"] = WebApiError::GenericValueMissing;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    IPAddress ipaddress;
    if (!ipaddress.fromString(root["ipaddress"].as<String>())) {
        retMsg["message"] = "IP address is invalid!";
        retMsg["code"] = WebApiError::NetworkIpInvalid;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }
    IPAddress netmask;
    if (!netmask.fromString(root["netmask"].as<String>())) {
        retMsg["message"] = "Netmask is invalid!";
        retMsg["code"] = WebApiError::NetworkNetmaskInvalid;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }
    IPAddress gateway;
    if (!gateway.fromString(root["gateway"].as<String>())) {
        retMsg["message"] = "Gateway is invalid!";
        retMsg["code"] = WebApiError::NetworkGatewayInvalid;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }
    IPAddress dns1;
    if (!dns1.fromString(root["dns1"].as<String>())) {
        retMsg["message"] = "DNS Server IP 1 is invalid!";
        retMsg["code"] = WebApiError::NetworkDns1Invalid;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }
    IPAddress dns2;
    if (!dns2.fromString(root["dns2"].as<String>())) {
        retMsg["message"] = "DNS Server IP 2 is invalid!";
        retMsg["code"] = WebApiError::NetworkDns2Invalid;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }

    if (root["hostname"].as<String>().length() == 0 || root["hostname"].as<String>().length() > WIFI_MAX_HOSTNAME_STRLEN) {
        retMsg["message"] = "Hostname must between 1 and " STR(WIFI_MAX_HOSTNAME_STRLEN) " characters long!";
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }
    if (NetworkSettings.NetworkMode() == network_mode::WiFi) {
        if (root["ssid"].as<String>().length() == 0 || root["ssid"].as<String>().length() > WIFI_MAX_SSID_STRLEN) {
            retMsg["message"] = "SSID must between 1 and " STR(WIFI_MAX_SSID_STRLEN) " characters long!";
            WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
            return;
        }
    }
    if (root["password"].as<String>().length() > WIFI_MAX_PASSWORD_STRLEN - 1) {
        retMsg["message"] = "Password must not be longer than " STR(WIFI_MAX_PASSWORD_STRLEN) " characters long!";
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }
    if (root["aptimeout"].as<uint>() > 99999) {
        retMsg["message"] = "ApTimeout must be a number between 0 and 99999!";
        retMsg["code"] = WebApiError::NetworkApTimeoutInvalid;
        WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
        return;
    }
    if (root["syslogenabled"].as<bool>()) {
        if (root["sysloghostname"].as<String>().length() == 0 || root["sysloghostname"].as<String>().length() > SYSLOG_MAX_HOSTNAME_STRLEN) {
            retMsg["message"] = "Syslog Server must between 1 and " STR(SYSLOG_MAX_HOSTNAME_STRLEN) " characters long!";
            retMsg["code"] = WebApiError::NetworkSyslogHostnameLength;
            retMsg["param"]["max"] = SYSLOG_MAX_HOSTNAME_STRLEN;
            WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
            return;
        }

        if (root["syslogport"].as<uint>() == 0 || root["syslogport"].as<uint>() > 65535) {
            retMsg["message"] = "Port must be a number between 1 and 65535!";
            retMsg["code"] = WebApiError::NetworkSyslogPort;
            WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);
            return;
        }
    }

    {
        auto guard = Configuration.getWriteGuard();
        auto& config = guard.getConfig();

        config.WiFi.Ip[0] = ipaddress[0];
        config.WiFi.Ip[1] = ipaddress[1];
        config.WiFi.Ip[2] = ipaddress[2];
        config.WiFi.Ip[3] = ipaddress[3];
        config.WiFi.Netmask[0] = netmask[0];
        config.WiFi.Netmask[1] = netmask[1];
        config.WiFi.Netmask[2] = netmask[2];
        config.WiFi.Netmask[3] = netmask[3];
        config.WiFi.Gateway[0] = gateway[0];
        config.WiFi.Gateway[1] = gateway[1];
        config.WiFi.Gateway[2] = gateway[2];
        config.WiFi.Gateway[3] = gateway[3];
        config.WiFi.Dns1[0] = dns1[0];
        config.WiFi.Dns1[1] = dns1[1];
        config.WiFi.Dns1[2] = dns1[2];
        config.WiFi.Dns1[3] = dns1[3];
        config.WiFi.Dns2[0] = dns2[0];
        config.WiFi.Dns2[1] = dns2[1];
        config.WiFi.Dns2[2] = dns2[2];
        config.WiFi.Dns2[3] = dns2[3];
        strlcpy(config.WiFi.Ssid, root["ssid"].as<String>().c_str(), sizeof(config.WiFi.Ssid));
        strlcpy(config.WiFi.Password, root["password"].as<String>().c_str(), sizeof(config.WiFi.Password));
        strlcpy(config.WiFi.Hostname, root["hostname"].as<String>().c_str(), sizeof(config.WiFi.Hostname));
        if (root["dhcp"].as<bool>()) {
            config.WiFi.Dhcp = true;
        } else {
            config.WiFi.Dhcp = false;
        }
        config.WiFi.ApTimeout = root["aptimeout"].as<uint>();
        config.Mdns.Enabled = root["mdnsenabled"].as<bool>();
        config.Syslog.Enabled = root["syslogenabled"].as<bool>();
        strlcpy(config.Syslog.Hostname, root["sysloghostname"].as<String>().c_str(), sizeof(config.Syslog.Hostname));
        config.Syslog.Port = root["syslogport"].as<uint>();
    }

    WebApi.writeConfig(retMsg);

    WebApi.sendJsonResponse(request, response, __FUNCTION__, __LINE__);

    _applyDataTask.enable();
    _applyDataTask.restart();
}

void WebApiNetworkClass::applyDataTaskCb()
{
    NetworkSettings.enableAdminMode();
    NetworkSettings.applyConfig();
}
