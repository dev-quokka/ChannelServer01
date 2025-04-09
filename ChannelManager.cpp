#include "ChannelManager.h"

bool ChannelManager::init() {
	channels.resize(MAX_CHANNEL);
	channels[0] = nullptr;// IDX 0�� ������� ����

	for (int i = 1; i < MAX_CHANNEL; ++i) {
		channels[i] = new Channel();

	}
	return true;
}

bool ChannelManager::InsertChannel(uint16_t channelNum, uint16_t userObjNum_, InGameUser* user_) {
	if (channels[channelNum]->GetUserCount() > MAX_CHANNEL_USERS) { // �ش� ä�� ���� �� ���� ���� ���� �Ұ���
		return false;
	}
		
	channels[channelNum]->InsertUser(userObjNum_, user_);
	return true;
}

void ChannelManager::LeaveChannel(uint16_t channelNum, uint16_t userObjNum_) {
	channels[channelNum]->RemoveUser(userObjNum_);
}