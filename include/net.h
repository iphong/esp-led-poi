#include <Arduino.h>
#include <ESP8266WiFi.h>
// #include <WiFiServer.h>
#include <MeshRC.h>
// #include <ESP8266mDNS.h>
// #include <DNSServer.h>
// #include <WiFiUdp.h>
#include "led.h"

#ifndef __NET_H__
#define __NET_H__

namespace Net {

Ticker timeSyncInterrupt;
Ticker nodePingInterrupt;

bool receivingFiles;
bool sendingFiles;
bool IS_WIFI_ON = false;

String staSSID;
String staPSK;
String apSSID;
String apPSK;

IPAddress apAddr;
IPAddress apMask;

Dir dir;
File file;
String tmpName = "/tmp/receiving";
String name = "";
u8 broadcastWildCard[] = "******";
u8 crc = 0x00;

void wifiOn() {
	IS_WIFI_ON = true;
	WiFi.softAPConfig(apAddr, apAddr, apMask);
	WiFi.softAP(apSSID, apPSK, 0, 0, 8);
}

void wifiOff() {
	IS_WIFI_ON = false;
	WiFi.softAPdisconnect();
}
void wifiToggle() {
	!IS_WIFI_ON ? wifiOn() : wifiOff();
}

struct SyncData {
	u8 show;
	u32 time;
	bool ended;
	bool paused;
};
u8 syncBuffer[32];
void sendSync() {
#ifdef MASTER
	if (!sendingFiles && !App::isPairing()) {
		SyncData tmp = {
			App::data.show,
			LED::getTime(),
			LED::ended,
			LED::paused};
		memcpy(syncBuffer, &tmp, sizeof(tmp));
		MeshRC::send("#>SYNC", syncBuffer, sizeof(tmp));
		MeshRC::wait();
#ifdef SYNC_LOGS
		LOGD("Sync sent: show=%u e=%i p=%i sync=%u play=%u\n", tmp.show, tmp.ended, tmp.paused, tmp.time, LED::getTime());
#endif
	}
#endif
}
void recvSync(u8* data, u8 size) {
#ifndef MASTER
	if (!receivingFiles && !App::isPairing()) {
		// if (IS_WIFI_ON) {
		// 	wifiOff();
		// 	WiFi.disconnect();
		// }
		SyncData tmp;
		memcpy(&tmp, data, size);
#ifdef SYNC_LOGS
		LOGD("Sync received: show=%u e=%i p=%i sync=%u play=%u diff=%i\n", tmp.show, tmp.ended, tmp.paused, tmp.time, LED::getTime(), LED::getTime() - tmp.time);
#endif
		if (tmp.show == 0 && App::data.show != 0) {
			LED::end();
			LED::begin();
		}
		App::data.show = tmp.show;
		if (tmp.show > 0) {
			if (LED::isRunning() && tmp.ended) {
				LED::end();
			} else if (!LED::isRunning() && !tmp.ended) {
				LED::begin();
			} else if (tmp.paused && !LED::isPaused()) {
				LED::pause();
			} else if (!tmp.paused && LED::isPaused()) {
				LED::resume();
			}
			if (!tmp.ended && !tmp.paused) {
				LED::setTime(tmp.time);
			}
		}
	}
#endif
}
struct NodeInfo {
	char id[6];
	String ip;
};
NodeInfo nodesList[255];
size_t nodesCount = 0;

void sendPing() {
	LOGL("sent ping");
	NodeInfo node;
	u8 buf[sizeof(node)];
	node.ip = WiFi.localIP().toString();
	memcpy(node.id, App::chipID, 6);
	memcpy(buf, &node, sizeof(node));
	MeshRC::send("#>PING", buf, sizeof(node));
}
void recvPing(u8* data, u8 size) {
	LOGL("received ping");
	bool isNew = true;
	NodeInfo node;
	memcpy(&node, data, size);
	for (auto node : nodesList) {
		if (MeshRC::equals((u8*)node.id, (u8*)node.id, 6)) {
			isNew = false;
		}
	}
	if (isNew) {
		nodesList[nodesCount++] = node;
	}
}

void sendPair(u8 channel = 255) {
	if (channel != 255) {
		LOGD("Sending pair with channel: %u\n", channel);
		MeshRC::send("#>PAIR", &channel, 1);
	} else {
		LOGD("Sending pair without channel\n");
		MeshRC::send("#>PAIR");
	}
}

void recvPair(u8* data, u8 size) {
	if (App::isPairing() && !receivingFiles) {
		// App::stopBlink();
		if (size >= 1) App::setChannel(data[0]);
		App::setMaster(MeshRC::sender);
		App::setMode(App::SHOW);
		App::save();
		App::lED_LOW();
	}
}

void waitDelay(u32 time) {
	static u32 delayUntil;
	while (MeshRC::sending)
		yield();  // Wait while sending
	delayUntil = millis() + time;
	while (millis() < delayUntil)
		yield();
}

void sendFile(String path, String targetID = "******") {
	// static u8 delayTime;
	LOGD("send file: %s -- ", path.c_str());
	if (!App::fs->exists(path)) {
		LOGD("NOT EXISTS\n");
		return;
	}
	sendingFiles = true;
	App::lED_LOW();
	LOGD("OK\n");
	file = App::fs->open(path, "r");
	crc = 0x00;
	// delayTime = 105;
	for (auto i = 0; i < 1; i++) {
		MeshRC::send("#>FILE^" + targetID + String(file.fullName()));
		waitDelay(500);
	}
	while (file.available()) {
		u16 pos = file.position();
		u8 len = _min(240, file.available());
		u8 data[len + 2];
		data[0] = pos >> 8;
		data[1] = pos & 0xff;
#ifdef SEND_FILE_LOGS
		LOGD("%04X :: ", pos);
#endif
		for (auto i = 0; i < len; i++) {
			data[i + 2] = file.read();
			crc += data[i + 2];
#ifdef SEND_FILE_LOGS
			LOGD("%02X ", data[i + 2]);
#endif
		}
#ifdef SEND_FILE_LOGS
		LOGD("\n");
#endif
		for (auto i = 0; i < 1; i++) {
			MeshRC::send("#>FILE+", data, sizeof(data));
			App::lED_BLINK();
			// if (delayTime > 5) delayTime -= 10;
			waitDelay(100);
		}
	}

	file.close();
	for (auto i = 0; i < 1; i++) {
		MeshRC::send("#>FILE$" + String((const char)crc));
		waitDelay(500);
	}
	App::lED_HIGH();
	LOGD("Sent\n\n");
	sendingFiles = false;
}

void sendDir(String path) {
	LOGD("Send DIR: %s\n", path.c_str());
	dir = App::fs->openDir(path);
	while (dir.next()) {
		if (dir.isFile()) {
			sendFile(path + "/" + dir.fileName());
		} else if (dir.isDirectory() && dir.fileName() != "." && dir.fileName() != "..") {
			sendDir(path + "/" + dir.fileName());
		}
	}
}

void recvFile(u8* buf, u8 len) {
	if (!App::isPaired()) return;
	char type = buf[0];
	u8* data = &buf[1];
	u8 size = len - 1;
	u32 tmr = millis();
	switch (type) {
		// FILE STREAM HEADER
		case '^':
			if (MeshRC::equals(data, (u8*)App::chipID, 6) || MeshRC::equals(data, broadcastWildCard, 6)) {
				receivingFiles = true;
				LED::end();
				// wifiOff();
				// WiFi.disconnect();
				// App::stopBlink();
				App::lED_LOW();
				name = "";
				crc = 0x00;
				for (auto i = 6; i < size; i++) {
					name.concat((const char)data[i]);
				}
				if (file)
					file.close();
				file = App::fs->open(tmpName, "w");
				file.write((const char*)0);
				file.seek(0);
				if (!file) {
					LOG(F("CREATE FAILED - "));
					receivingFiles = false;
				} else {
					LOGD("receiving file: %s - ", name.c_str());
				}
				LOG(millis() - tmr);
				LOGL(" us");
			}
			break;

		// FILE STREAM DATA
		case '+':
			if (file) {
				App::lED_BLINK();
				u16 pos = data[0] << 8 | data[1];
				u16 pos2 = file.position();
#ifdef RECV_FILE_LOGS
				LOGD("%04X %04X :: %u bytes :: ", pos, pos2, size);
#endif
				for (auto i = 2; i < size; i++) {
					crc += data[i];
					file.write(data[i]);
					// LOGD("%02X ", data[i]);
				}
#ifdef RECV_FILE_LOGS
				LOG(millis() - tmr);
				LOGL(" us");
#endif
			} else {
				receivingFiles = false;
			}
			break;

		// FILE STREAM END
		case '$':
			if (receivingFiles && file) {
				// wifiOn();
				App::lED_LOW();
				LOGD("EOF %02X = %02X - ", crc, data[0]);
				file.close();
				if (data[0] == crc) {
					if (App::fs->exists(name)) {
						App::fs->remove(name);
					}
					App::fs->rename(tmpName, name);
					if (App::fs->exists(tmpName)) {
						App::fs->remove(tmpName);
					}
				}
				LOG(millis() - tmr);
				LOGL(" us");
			}
			receivingFiles = false;
			break;
		default:
			break;
	}
}

void handleRestartMsg() {
	ESP.restart();
}

void handleWifiMsg(u8* data, u8 size) {
	if (size) {
		u8 turnOn = data[0];
		if (turnOn)
			wifiOn();
		else
			wifiOff();
	}
}

struct WiFiConnection {
	String ssid;
	String pass;
};
void sendWiFiConnect(String ssid, String pass) {
	WiFiConnection msg = {ssid, pass};
	u8 buffer[sizeof(msg)];
	memcpy(buffer, &msg, sizeof(msg));
	MeshRC::send("$>WIFI+", buffer, sizeof(msg));
}
void recvWiFiConnect(u8* data, u8 size) {
	WiFiConnection conn;
	memcpy(&conn, data, size);
	WiFi.begin(conn.ssid, conn.pass);
	wifiOn();
}

void setup() {
	WiFi.mode(WIFI_AP_STA);
	WiFi.setAutoConnect(false);
	WiFi.setAutoReconnect(false);
	WiFi.disconnect();

	ArduinoOTA.onProgress([](int percent, int total) { App::lED_BLINK(); });
	ArduinoOTA.begin();

#ifndef MASTER
	MeshRC::on("$>WIFI+", recvWiFiConnect);
	MeshRC::on("$>WIFI-", []() { WiFi.disconnect(); });
#endif

	MeshRC::on("#>PING", Net::recvPing);
	MeshRC::on("#>SYNC", Net::recvSync);
	MeshRC::on("#>PAIR", Net::recvPair);
	MeshRC::on("#>FILE", Net::recvFile);

	MeshRC::on("#>RESTART", handleRestartMsg);
	MeshRC::on("#>WIFI", handleWifiMsg);

	MeshRC::on("#>BEGIN", LED::begin);
	MeshRC::on("#>PAUSE", LED::pause);
	MeshRC::on("#>RESUME", LED::resume);
	MeshRC::on("#>TOGGLE", LED::toggle);
	MeshRC::on("#>END", LED::end);

	MeshRC::begin();
#ifdef MASTER
	timeSyncInterrupt.attach_ms_scheduled_accurate(100, sendSync);
#endif
}

void loop() {
	ArduinoOTA.handle();
}
}  // namespace Net

#endif