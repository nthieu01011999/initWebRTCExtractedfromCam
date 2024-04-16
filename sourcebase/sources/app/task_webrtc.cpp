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
#include "helpers.hpp"
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
std::shared_ptr<PeerConnection> pc = nullptr;  // AK_MSG_NULL has been did with #define AK_MSG_NULL ((ak_msg_t *)0)
// #ifdef TEST_USE_WEB_SOCKET

static int8_t loadWsocketSignalingServerConfigFile(string &wsUrl);
// #endif

static Configuration rtcConfig;
static int8_t loadIceServersConfigFile(Configuration &rtcConfig);
void handleWebSocketMessage(const std::string& message);
// shared_ptr<Client> createPeerConnection(const Configuration &rtcConfig, string id);
void handleClientRequest(const std::string& clientId);
void onPeerConnectionStateChange(const string& clientId, PeerConnection::State state);
void onDataChannelOpen(const string& clientId);
void onDataChannelMessage(const string& clientId, const binary& data);
void onDataChannelStringMessage(const string& clientId, const string& message);
void sendLocalDescription(const Description &description);
void sendIceCandidate(const Candidate &candidate);

shared_ptr<Client> createPeerConnection(const Configuration &rtcConfig, const string& clientId);



void *gw_task_webrtc_entry(void *) {
	ak_msg_t *msg = AK_MSG_NULL;

	wait_all_tasks_started();
	APP_DBG("[STARTED_1] gw_task_webrtc_entry\n");
    if (loadIceServersConfigFile(rtcConfig) != APP_CONFIG_SUCCESS) {
        APP_PRINT("Failed to load ICE servers configuration.\n");
        return AK_MSG_NULL;  // Exit if configuration fails
    }

    rtcConfig.disableAutoNegotiation = false;  // Set localDescription automatically
	
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
		APP_PRINT("WebSocket connection @ ws->onMessage\n");
		if (holds_alternative<string>(data)) {
			string msg = get<string>(data);
			APP_DBG("%s\n", msg.data());
			handleWebSocketMessage(msg);  // Pass the message to the handler
			// If needed, you can post the message here as well, depending on your application's architecture.
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
        case GW_WEBRTC_ERASE_CLIENT_REQ: {
            APP_DBG_SIG("GW_WEBRTC_ERASE_CLIENT_REQ\n");
            // string id((char *)msg->header->payload);
            // APP_PRINT("clear client id: %s\n", id.c_str());
            // Client::setSignalingStatus(true);
            // lockMutexListClients();
            // clients.erase(id);
            // unlockMutexListClients();
            // Client::setSignalingStatus(false);

            // printAllClients();
		} break;
			
		default:
		break;
		}

		/* free message */
		ak_msg_free(msg);
	}

    // rtcConfig.disableAutoNegotiation = false;
    pc = make_shared<PeerConnection>(rtcConfig);

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
void handleWebSocketMessage(const std::string& message) {
    APP_DBG("[WebSocket Message Received]: %s\n", message.c_str());

    try {
        json messageJson = json::parse(message);
        APP_DBG("Parsed WebSocket Message: %s\n", messageJson.dump(4).c_str()); // Use pretty printing for better readability

        auto typeIt = messageJson.find("Type");
        if (typeIt != messageJson.end()) {
            string type = typeIt->get<string>();
            APP_DBG("Processing Message Type: %s\n", type.c_str());

            if (type == "request") {
                auto clientIdIt = messageJson.find("ClientId");
                if (clientIdIt != messageJson.end()) {
                    string clientId = clientIdIt->get<string>();
                    APP_DBG("Handling request for Client ID: %s\n", clientId.c_str());

                    // Handle the client's request, e.g., create a new peer connection
                    handleClientRequest(clientId);
                } else {
                    APP_DBG("Error: ClientId not found in request message\n");
                }
            } else if (type == "candidate") {
                string sdp = messageJson["candidate"]["sdp"].get<string>();
                string mid = messageJson["candidate"]["mid"].get<string>();
                APP_DBG("Adding ICE Candidate: sdp=%s, mid=%s\n", sdp.c_str(), mid.c_str());
                
                pc->addRemoteCandidate(Candidate(sdp, mid));
            } else if (type == "answer") {
                string sdp = messageJson["answer"]["sdp"].get<string>();
                APP_DBG("Setting Remote Description with SDP: %s\n", sdp.c_str());
                
                pc->setRemoteDescription(Description(sdp, "answer"));
            }
            // Other message types can be handled here
        } else {
            APP_DBG("Error: Message type not specified\n");
        }
    } catch (const json::exception& e) {
        APP_DBG("[JSON Parsing Error] %s\n", e.what());
    }

    // Continue to post the message to the task or message queue
    task_post_dynamic_msg(GW_TASK_WEBRTC_ID, GW_WEBRTC_SIGNALING_SOCKET_REQ, (uint8_t *)message.data(), message.length() + 1);
}

void handleClientRequest(const std::string& clientId) {
    APP_DBG("Initiating peer connection for Client ID: %s\n", clientId.c_str());

    // Check client limits
    if (Client::totalClientsConnectSuccess <= CLIENT_MAX && clients.size() <= CLIENT_SIGNALING_MAX) {
        Client::setSignalingStatus(true);
        APP_DBG("Client limits OK, proceeding to create peer connection\n");

        // Create a new peer connection for the client
        std::shared_ptr<Client> newClient = createPeerConnection(rtcConfig, clientId);
        lockMutexListClients();
        clients.emplace(clientId, newClient);
        // printAllClients();
        auto cl = clients.at(clientId);
        cl->startTimeoutConnect();
        APP_DBG("Started timeout connect for client: %s\n", clientId.c_str());
        unlockMutexListClients();
        Client::setSignalingStatus(false);
    } else {
        APP_DBG("Reached max client limit. Cannot handle new request for client: %s\n", clientId.c_str());
    }
}


shared_ptr<Client> createPeerConnection(const Configuration &rtcConfig, const string& clientId) {
    APP_DBG("[NEW] Starting to create PeerConnection for client ID: %s\n", clientId.c_str());
    auto pc		= make_shared<PeerConnection>(rtcConfig);
	auto client = make_shared<Client>(pc);
	client->setId(clientId);

    pc->onStateChange([clientId](PeerConnection::State state) {
        APP_DBG("State: %d\n", (int)state);
        if (state == PeerConnection::State::Disconnected || state == PeerConnection::State::Failed || state == PeerConnection::State::Closed) {
            // remove disconnected client
            APP_DBG("call erase client from lib\n");
            systemTimer.add(milliseconds(100),
                            [clientId](CppTime::timer_id) { task_post_dynamic_msg(GW_TASK_WEBRTC_ID, GW_WEBRTC_ERASE_CLIENT_REQ, (uint8_t *)clientId.c_str(), clientId.length() + 1); });
        }
	});

    task_post_dynamic_msg(GW_TASK_HELLO_ID, GW_HELLO_PRINT, (uint8_t *)clientId.c_str(), clientId.length() + 1);
	// pc->onStateChange([clientId](PeerConnection::State state) {
	// 	APP_DBG("State: %d\n", (int)state);
	// 	if (state == PeerConnection::State::Disconnected || state == PeerConnection::State::Failed || state == PeerConnection::State::Closed) {
	// 		// remove disconnected client
	// 		APP_DBG("call erase client from lib\n");
	// 		systemTimer.add(milliseconds(100),
	// 						[clientId](CppTime::timer_id) { task_post_dynamic_msg(GW_TASK_WEBRTC_ID, GW_WEBRTC_ERASE_CLIENT_REQ, (uint8_t *)clientId.c_str(), clientId.length() + 1); });
	// 	}
	// });
    // // Create a new PeerConnection with the given configuration
    // auto peerConnection = make_shared<PeerConnection>(rtcConfig);
    
    // // Set up event handlers for the PeerConnection
    // peerConnection->onStateChange([clientId](PeerConnection::State state) {
    //     APP_DBG("PeerConnection state change for client %s: %d\n", clientId.c_str(), static_cast<int>(state));
    //     onPeerConnectionStateChange(clientId, state);
    // });

    // // Create and configure a new DataChannel
    // auto dataChannel = peerConnection->createDataChannel("control");
    // dataChannel->onOpen([clientId]() {
    //     APP_DBG("DataChannel opened for client %s\n", clientId.c_str());
    //     onDataChannelOpen(clientId);
    // });

    // dataChannel->onMessage([clientId](variant<binary, string> message) {
    //     if (holds_alternative<binary>(message)) {
    //         onDataChannelMessage(clientId, get<binary>(message));
    //     } else if (holds_alternative<string>(message)) {
    //         onDataChannelStringMessage(clientId, get<string>(message));
    //     }
    // });

    // // Create a Client object that encapsulates the PeerConnection and the DataChannel
    // auto client = make_shared<Client>(peerConnection);
    // client->setId(clientId);
    // client->dataChannel = dataChannel;

    // APP_DBG("PeerConnection and DataChannel setup complete for client %s\n", clientId.c_str());

    return client;
}

// void onPeerConnectionStateChange(const string& clientId, PeerConnection::State state) {
//     // Logic for when the PeerConnection's state changes
//     APP_DBG("Client %s PeerConnection state changed: %d\n", clientId.c_str(), static_cast<int>(state));
// }

// void onDataChannelOpen(const string& clientId) {
//     // Logic for when the DataChannel is open
//     APP_DBG("DataChannel for client %s is open\n", clientId.c_str());
// }

// void onDataChannelMessage(const string& clientId, const binary& data) {
//     // Logic for when a message is received on the DataChannel
//     APP_DBG("Message received from client %s on DataChannel\n", clientId.c_str());
// }

// void onDataChannelStringMessage(const string& clientId, const string& message) {
//     APP_DBG("String message received from client %s: %s\n", clientId.c_str(), message.c_str());
// }