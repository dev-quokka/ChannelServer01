#pragma once
#include <vector>

#include "Channel.h"

constexpr int MAX_CHANNEL = 4; // �ִ� ä�� �� + 1
constexpr int MAX_CHANNEL_USERS = 10; // �ش� ä�� ���Ӱ��� ���� ��

class ChannelManager {
public:
	bool init();
	bool InsertChannel(uint16_t channelNum, uint16_t userObjNum_, InGameUser* user_);
	void LeaveChannel(uint16_t channelNum, uint16_t userObjNum_);

private:
	std::vector<Channel*> channels; // IDX 1���� 1ä��
};