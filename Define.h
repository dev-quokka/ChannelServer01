#pragma once
#define WIN32_LEAN_AND_MEAN 

#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <cstdint>
#include <string>
#include <unordered_map>

const uint32_t MAX_RECV_SIZE = 1024; // Set Max RECV Buf
const uint32_t MAX_CIRCLE_SIZE = 8096;

enum class ServerType : uint16_t {
	// Channel Server (11~)
	ChannelServer01 = 1,
	ChannelServer02 = 2,

	// Game Server (51~)
	RaidGameServer01 = 51,

	// Server Type (101~)
	GatewayServer = 101,
	MatchingServer = 102,
};

enum class ChannelType : uint16_t {
	CH_011 = 1, // 1���� 1 ä��
	CH_012 = 2, // 1���� 2 ä��
	CH_013 = 3, // 1���� 3 ä��

};

//  ---------------------------- SYSTEM  ----------------------------

enum class TaskType {
	ACCEPT,
	RECV,
	SEND,
	NEWRECV, // ������ Ǯ �� �Ἥ ���� ���� ����Ѱ�. (�̰� �� ���� ����)
	NEWSEND
};

struct OverlappedEx {
	WSAOVERLAPPED wsaOverlapped;
	// 16 bytes
	WSABUF wsaBuf; // TCP Buffer
	// 4 bytes
	TaskType taskType; // ACCPET, RECV, SEND INFO
	// 2 bytes
	uint16_t connObjNum;
};

