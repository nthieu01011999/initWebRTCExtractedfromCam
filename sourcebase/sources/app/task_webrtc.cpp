#include <unistd.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <iostream>
#include <iomanip>
#include <cstdio>
#include <vector>

#include <algorithm>
#include <future>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <chrono>
#include <pthread.h>

#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <atomic>

#include "app.h"
#include "app_data.h"
#include "app_config.h"
#include "app_dbg.h"
// #include "datachannel_hdl.h"

#include "task_list.h"

// #include "dispatchqueue.hpp"
// #include "helpers.hpp"
#include "rtc/rtc.hpp"
#include "json.hpp"
// #include "stream.hpp"

#ifdef BUILD_ARM_VVTK
// #include "h26xsource.hpp"
// #include "audiosource.hpp"
#endif

// #include "mtce_audio.hpp"
#include "parser_json.h"
#include "utils.h"

#define CLIENT_SIGNALING_MAX 20
#define CLIENT_MAX			 10

using namespace rtc;
using namespace std;
using namespace chrono_literals;
using namespace chrono;
using json = nlohmann::json;

q_msg_t gw_task_webrtc_mailbox;

// #ifdef TEST_USE_WEB_SOCKET

static int8_t loadWsocketSignalingServerConfigFile(string &wsUrl);
// #endif

static Configuration rtcConfig;
static int8_t loadIceServersConfigFile(Configuration &rtcConfig);



void *gw_task_webrtc_entry(void *) {
	ak_msg_t *msg = AK_MSG_NULL;

	wait_all_tasks_started();
	APP_DBG("[STARTED_1] gw_task_webrtc_entry\n");
    if (loadIceServersConfigFile(rtcConfig) != APP_CONFIG_SUCCESS) {
        APP_PRINT("Failed to load ICE servers configuration.\n");
        return AK_MSG_NULL;  // Exit if configuration fails
    }

    rtcConfig.disableAutoNegotiation = false;  // Set localDescription automatically
	
	
// #ifdef TEST_USE_WEB_SOCKET
	/* init websocket */
	// auto ws = make_shared<WebSocket>();	   // init poll serivce and threadpool = 4
	// ws->onOpen([]() {
	// 	APP_PRINT("WebSocket connected, signaling ready\n");
	// 	timer_remove_attr(GW_TASK_WEBRTC_ID, GW_WEBRTC_TRY_CONNECT_SOCKET_REQ);
	// });

	// ws->onClosed([]() {
	// 	APP_PRINT("WebSocket closed\n");
	// 	timer_set(GW_TASK_WEBRTC_ID, GW_WEBRTC_TRY_CONNECT_SOCKET_REQ, GW_WEBRTC_TRY_CONNECT_SOCKET_INTERVAL, TIMER_ONE_SHOT);
	// });

	// ws->onError([](const string &error) { APP_PRINT("WebSocket failed: %s\n", error.c_str()); });

	// ws->onMessage([&](variant<binary, string> data) {
	// 	if (!holds_alternative<string>(data)) {
	// 		return;
	// 	}
	// 	string msg = get<string>(data);
	// 	APP_DBG("%s\n", msg.data());
	// 	task_post_dynamic_msg(GW_TASK_WEBRTC_ID, GW_WEBRTC_SIGNALING_SOCKET_REQ, (uint8_t *)msg.data(), msg.length() + 1);
	// });

	// std::string wsUrl;
	// loadWsocketSignalingServerConfigFile(wsUrl);

	// /* For Debugging */
	// wsUrl = "ws://sig.espitek.com:8089/" + mtce_getSerialInfo();
	// std::cout << "wsURL: " << wsUrl << std::endl;

	// if (!wsUrl.empty()) {
	// 	timer_set(GW_TASK_WEBRTC_ID, GW_WEBRTC_TRY_CONNECT_SOCKET_REQ, 3000, TIMER_ONE_SHOT);
	// }

    /* init websocket */
    auto ws = make_shared<WebSocket>(); // init poll service and threadpool = 4

    // Guard flag to check WebSocket connection status
    atomic<bool> isConnected(false);

    ws->onOpen([&]() {
        isConnected.store(true);
        APP_PRINT("WebSocket connected, signaling ready\n");
        timer_remove_attr(GW_TASK_WEBRTC_ID, GW_WEBRTC_TRY_CONNECT_SOCKET_REQ);
    });

    ws->onClosed([&]() {
        isConnected.store(false);
        APP_PRINT("WebSocket closed\n");
        timer_set(GW_TASK_WEBRTC_ID, GW_WEBRTC_TRY_CONNECT_SOCKET_REQ, GW_WEBRTC_TRY_CONNECT_SOCKET_INTERVAL, TIMER_ONE_SHOT);
    });

    ws->onError([&](const string &error) {
        isConnected.store(false);
        APP_PRINT("WebSocket connection failed: %s\n", error.c_str());
        // Depending on your application logic, you might want to attempt a reconnection here.
    });

    ws->onMessage([&](variant<binary, string> data) {
        if (holds_alternative<string>(data)) {
            string msg = get<string>(data);
            APP_DBG("%s\n", msg.data());
            task_post_dynamic_msg(GW_TASK_WEBRTC_ID, GW_WEBRTC_SIGNALING_SOCKET_REQ, (uint8_t *)msg.data(), msg.length() + 1);
        }
	});

    std::string wsUrl;


    // Debugging output
    wsUrl = "ws://sig.espitek.com:8089/" + mtce_getSerialInfo();
    std::cout << "Attempting to connect WebSocket server at URL: " << wsUrl << std::endl;

    if (loadWsocketSignalingServerConfigFile(wsUrl) != APP_CONFIG_SUCCESS || wsUrl.empty()) {
        APP_PRINT("Failed to load WebSocket URL configuration or URL is empty.\n");
        return AK_MSG_NULL;  // Exit if configuration fails or URL is empty
    }
    // Attempt to connect
    ws->open(wsUrl);

    // Wait a bit to see if the connection succeeds (or modify based on your app logic)
    std::this_thread::sleep_for(3s); // Adjust the timing as necessary

    // Check the connection status
    if (isConnected.load()) {
        APP_PRINT("WebSocket is successfully connected.\n");
    } else {
        APP_PRINT("WebSocket connection failed or is still in progress.\n");
    }

// #endif
	APP_DBG("[STARTED] gw_task_webrtc_entry\n");

	while (1) {
		/* get messge */
		msg = ak_msg_rev(GW_TASK_WEBRTC_ID);

		switch (msg->header->sig) {
			
		default:
		break;
		}

		/* free message */
		ak_msg_free(msg);
	}

	return (void *)0;
}

int8_t loadIceServersConfigFile(Configuration &rtcConfig) {
	rtcServersConfig_t rtcServerCfg;
	int8_t ret = configGetRtcServers(&rtcServerCfg);
	try {
		if (ret == APP_CONFIG_SUCCESS) {
			rtcConfig.iceServers.clear();

			APP_DBG("List stun server:\n");
			string url = "";
			int size   = rtcServerCfg.arrStunServerUrl.size();
			for (int idx = 0; idx < size; idx++) {
				url = rtcServerCfg.arrStunServerUrl.at(idx);
				APP_DBG("\t[%d] url: %s\n", idx + 1, url.c_str());
				if (url != "") {
					rtcConfig.iceServers.emplace_back(url);
				}
			}
			APP_DBG("\n");
			APP_DBG("List turn server:\n");
			size = rtcServerCfg.arrTurnServerUrl.size();
			for (int idx = 0; idx < size; idx++) {
				url = rtcServerCfg.arrTurnServerUrl.at(idx);
				APP_DBG("\t[%d] url: %s\n", idx + 1, url.c_str());
				if (url != "") {
					rtcConfig.iceServers.emplace_back(url);
				}
			}
			APP_DBG("\n");
		}
	}
	catch (const exception &error) {
		APP_DBG("loadIceServersConfigFile %s\n", error.what());
		ret = APP_CONFIG_ERROR_DATA_INVALID;
	}

	return ret;
}

// int8_t loadWsocketSignalingServerConfigFile(string &wsUrl) {
// 	rtcServersConfig_t rtcServerCfg;
// 	int8_t ret = configGetRtcServers(&rtcServerCfg);
// 	if (ret == APP_CONFIG_SUCCESS) {
// 		wsUrl.clear();
// 		if (rtcServerCfg.wSocketServerCfg 	!= "") {
// 			wsUrl = rtcServerCfg.wSocketServerCfg + "/" + mtce_getSerialInfo();
// 		}
// 	}
// 	return ret;
// }

int8_t loadWsocketSignalingServerConfigFile(string &wsUrl) {
    rtcServersConfig_t rtcServerCfg;
    int8_t ret = configGetRtcServers(&rtcServerCfg);
    
    APP_PRINT("[DEBUG] Loading WebSocket Signaling Server Config...\n");
    
    if (ret == APP_CONFIG_SUCCESS) {
        APP_PRINT("[DEBUG] Successfully retrieved RTC server configuration.\n");
        
        wsUrl.clear();
        // if (!rtcServerCfg.wSocketServerCfg.empty()) {
		if (rtcServerCfg.wSocketServerCfg 	!= "") {
            wsUrl = rtcServerCfg.wSocketServerCfg + "/" + mtce_getSerialInfo();
            APP_PRINT("[DEBUG] WebSocket URL constructed: %s\n", wsUrl.c_str());
        } else {
            APP_PRINT("[ERROR] WebSocket server configuration is empty.\n");
        }
    } else {
        APP_PRINT("[ERROR] Failed to retrieve RTC server configuration. Error code: %d\n", ret);
    }
    
    return ret;
}

// "Exchange of Offer and Answer"
