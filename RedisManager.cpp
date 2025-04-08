#include "RedisManager.h"

thread_local std::mt19937 RedisManager::gen(std::random_device{}());

void RedisManager::init(const uint16_t RedisThreadCnt_, const uint16_t maxClientCount_, HANDLE sIOCPHandle_) {

    // ---------- SET PACKET PROCESS ---------- 
    packetIDTable = std::unordered_map<uint16_t, RECV_PACKET_FUNCTION>();

    //SYSTEM
    packetIDTable[(uint16_t)CHANNEL_ID::USER_CONNECT_REQUEST] = &RedisManager::UserConnect;
    packetIDTable[(uint16_t)CHANNEL_ID::USER_DISCONNECT_REQUEST] = &RedisManager::UserDisConnect;

    // USER STATUS
    packetIDTable[(UINT16)CHANNEL_ID::EXP_UP_REQUEST] = &RedisManager::ExpUp;

    // INVENTORY
    packetIDTable[(uint16_t)CHANNEL_ID::ADD_ITEM_REQUEST] = &RedisManager::AddItem;
    packetIDTable[(uint16_t)CHANNEL_ID::DEL_ITEM_REQUEST] = &RedisManager::DeleteItem;
    packetIDTable[(uint16_t)CHANNEL_ID::MOD_ITEM_REQUEST] = &RedisManager::ModifyItem;
    packetIDTable[(uint16_t)CHANNEL_ID::MOV_ITEM_REQUEST] = &RedisManager::MoveItem;

    //INVENTORY:EQUIPMENT
    packetIDTable[(uint16_t)CHANNEL_ID::ADD_EQUIPMENT_REQUEST] = &RedisManager::AddEquipment;
    packetIDTable[(uint16_t)CHANNEL_ID::DEL_EQUIPMENT_REQUEST] = &RedisManager::DeleteEquipment;
    packetIDTable[(uint16_t)CHANNEL_ID::ENH_EQUIPMENT_REQUEST] = &RedisManager::EnhanceEquipment;
    packetIDTable[(uint16_t)CHANNEL_ID::MOV_EQUIPMENT_REQUEST] = &RedisManager::MoveEquipment;

    //RAID
    packetIDTable[(uint16_t)CHANNEL_ID::RAID_RANKING_REQUEST] = &RedisManager::GetRanking;

    RedisRun(RedisThreadCnt_);
    channelManager = new ChannelManager;
}

void RedisManager::RedisRun(const uint16_t RedisThreadCnt_) { // Connect Redis Server
    try {
        connection_options.host = "127.0.0.1";  // Redis Cluster IP
        connection_options.port = 7001;  // Redis Cluster Master Node Port
        connection_options.socket_timeout = std::chrono::seconds(10);
        connection_options.keep_alive = true;

        redis = std::make_unique<sw::redis::RedisCluster>(connection_options);
        std::cout << "Redis Cluster Connect Success !" << std::endl;

        CreateRedisThread(RedisThreadCnt_);
    }
    catch (const  sw::redis::Error& err) {
        std::cout << "Redis Connect Error : " << err.what() << std::endl;
    }
}

void RedisManager::Disconnect(uint16_t connObjNum_) {
    UserDisConnect(connObjNum_);
}

void RedisManager::SetManager(ConnUsersManager* connUsersManager_, InGameUserManager* inGameUserManager_) {
    connUsersManager = connUsersManager_;
    inGameUserManager = inGameUserManager_;
}

bool RedisManager::CreateRedisThread(const uint16_t RedisThreadCnt_) {
    redisRun = true;
    for (int i = 0; i < RedisThreadCnt_; i++) {
        redisThreads.emplace_back(std::thread([this]() {RedisThread(); }));
    }
    return true;
}

bool RedisManager::EquipmentEnhance(uint16_t currentEnhanceCount_) {
    if (currentEnhanceCount_ < 0 || currentEnhanceCount_ >= enhanceProbabilities.size()) {
        return false;
    }

    std::uniform_int_distribution<int> dist(1, 100);
    return dist(gen) <= enhanceProbabilities[currentEnhanceCount_];
}

void RedisManager::RedisThread() {
    DataPacket tempD(0, 0);
    ConnUser* TempConnUser = nullptr;
    char tempData[1024] = { 0 };

    while (redisRun) {
        if (procSktQueue.pop(tempD)) {
            std::memset(tempData, 0, sizeof(tempData));
            TempConnUser = connUsersManager->FindUser(tempD.connObjNum); // Find User
            PacketInfo packetInfo = TempConnUser->ReadRecvData(tempData, tempD.dataSize); // GetData
            (this->*packetIDTable[packetInfo.packetId])(packetInfo.connObjNum, packetInfo.dataSize, packetInfo.pData); // Proccess Packet
        }
        else { // Empty Queue
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

void RedisManager::PushRedisPacket(const uint16_t connObjNum_, const uint32_t size_, char* recvData_) {
    ConnUser* TempConnUser = connUsersManager->FindUser(connObjNum_);
    TempConnUser->WriteRecvData(recvData_, size_); // Push Data in Circualr Buffer
    DataPacket tempD(size_, connObjNum_);
    procSktQueue.push(tempD);
}

// ============================== PACKET ==============================

//  ---------------------------- SYSTEM  ----------------------------

void RedisManager::UserConnect(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto userConn = reinterpret_cast<USER_CONNECT_REQUEST_PACKET*>(pPacket_);
    std::string key = "jwtcheck:{" + (std::string)userConn->userId + "}";

    USER_CONNECT_RESPONSE_PACKET ucReq;
    ucReq.PacketId = (uint16_t)CHANNEL_ID::USER_CONNECT_RESPONSE;
    ucReq.PacketLength = sizeof(USER_CONNECT_RESPONSE_PACKET);

    try {
        auto pk = static_cast<uint32_t>(std::stoul(*redis->hget(key, (std::string)userConn->userToken)));
        if (pk) {
            std::string userInfokey = "userinfo:{" + std::to_string(pk) + "}";
            std::unordered_map<std::string, std::string> userData;
            redis->hgetall(userInfokey, std::inserter(userData, userData.begin()));

            connUsersManager->FindUser(connObjNum_)->SetPk(pk);
            inGameUserManager->Set(connObjNum_, (std::string)userConn->userId, pk, std::stoul(userData["exp"]),
                static_cast<uint16_t>(std::stoul(userData["level"])), std::stoul(userData["raidScore"]));

            ucReq.isSuccess = true;
            connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(USER_CONNECT_RESPONSE_PACKET), (char*)&ucReq);
            std::cout << (std::string)userConn->userId << " Connect" << std::endl;
        }
        else {
            ucReq.isSuccess = false;
            connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(USER_CONNECT_RESPONSE_PACKET), (char*)&ucReq);
            std::cout << (std::string)userConn->userId << " JWT Check Fail" << std::endl;
        }
    }
    catch (const sw::redis::Error& e) {
        ucReq.isSuccess = false;
        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(USER_CONNECT_RESPONSE_PACKET), (char*)&ucReq);
        std::cerr << "Redis error: " << e.what() << std::endl;
        return;
    }
}

void RedisManager::UserDisConnect(uint16_t connObjNum_) {
    InGameUser* tempUser = inGameUserManager->GetInGameUserByObjNum(connObjNum_);

    USER_DISCONNECT_REQUEST_PACKET userDisconnReqPacket;
    userDisconnReqPacket.PacketId = (uint16_t)CHANNEL_ID::USER_DISCONNECT_REQUEST;
    userDisconnReqPacket.PacketLength = sizeof(USER_DISCONNECT_REQUEST_PACKET);
    userDisconnReqPacket.userPk = tempUser->GetPk();
    connUsersManager->FindUser(centerServerObjNum)-> PushSendMsg(sizeof(USER_DISCONNECT_REQUEST_PACKET), (char*)&userDisconnReqPacket);
    std::cout << "User PK:" << tempUser->GetPk() << " Disconnect" << std::endl;
}

void RedisManager::MoveCenterServer(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
	MOVE_CENTER_SERVER_REQUEST moveCenterReqPacket;
	moveCenterReqPacket.PacketId = (uint16_t)CHANNEL_ID::MOVE_CENTER_SERVER_REQUEST;
	moveCenterReqPacket.PacketLength = sizeof(MOVE_CENTER_SERVER_REQUEST);
	moveCenterReqPacket.userPk = inGameUserManager->GetInGameUserByObjNum(connObjNum_)->GetPk();

	connUsersManager->FindUser(centerServerObjNum)->PushSendMsg(sizeof(MOVE_CENTER_SERVER_REQUEST), (char*)&moveCenterReqPacket);
}

void RedisManager::MoveChannel(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto expUpReqPacket = reinterpret_cast<MOVE_CHANNEL_REQUEST*>(pPacket_);

}

//  ---------------------------- USER_STATUS  ----------------------------

void RedisManager::ExpUp(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto expUpReqPacket = reinterpret_cast<EXP_UP_REQUEST*>(pPacket_);
    InGameUser* tempUser = inGameUserManager->GetInGameUserByObjNum(connObjNum_);

    std::string key = "userinfo:{" + std::to_string(tempUser->GetPk()) + "}";

    auto userExp = tempUser->ExpUp(mobExp[expUpReqPacket->mobNum]); // Increase Level Cnt , Current Exp

    EXP_UP_RESPONSE expUpResPacket;
    expUpResPacket.PacketId = (uint16_t)CHANNEL_ID::EXP_UP_RESPONSE;
    expUpResPacket.PacketLength = sizeof(EXP_UP_RESPONSE);

    if (userExp.first != 0) { // Level Up
        try {
            auto pipe = redis->pipeline(std::to_string(tempUser->GetPk()));

            pipe.hset(key, "exp", std::to_string(userExp.second))
                .hincrby(key, "level", userExp.first);

            pipe.exec();

            expUpResPacket.increaseLevel = userExp.first;
            expUpResPacket.currentExp = userExp.second;

            connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(EXP_UP_RESPONSE), (char*)&expUpResPacket);
        }
        catch (const sw::redis::Error& e) {
            expUpResPacket.increaseLevel = 0;
            expUpResPacket.currentExp = 0;
            connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(EXP_UP_RESPONSE), (char*)&expUpResPacket);
            std::cerr << "Redis error: " << e.what() << std::endl;
            return;
        }
    }

    else { // Just Exp Up
        try {
            if (redis->hincrby(key, "exp", userExp.second)) { // Exp Up Success
                expUpResPacket.increaseLevel = userExp.first;
                expUpResPacket.currentExp = userExp.second;
                connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(EXP_UP_RESPONSE), (char*)&expUpResPacket);
            }
            else { // Exp Up Fail
                expUpResPacket.increaseLevel = 0;
                expUpResPacket.currentExp = 0;
                connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(EXP_UP_RESPONSE), (char*)&expUpResPacket);
            }
        }
        catch (const sw::redis::Error& e) {
            expUpResPacket.increaseLevel = 0;
            expUpResPacket.currentExp = 0;
            connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(EXP_UP_RESPONSE), (char*)&expUpResPacket);
            std::cerr << "Redis error: " << e.what() << std::endl;
            return;
        }
    }
}


//  ---------------------------- INVENTORY  ----------------------------

void RedisManager::AddItem(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto addItemReqPacket = reinterpret_cast<ADD_ITEM_REQUEST*>(pPacket_);
    InGameUser* tempUser = inGameUserManager->GetInGameUserByObjNum(connObjNum_);

    ADD_ITEM_RESPONSE addItemResPacket;
    addItemResPacket.PacketId = (uint16_t)CHANNEL_ID::ADD_ITEM_RESPONSE;
    addItemResPacket.PacketLength = sizeof(ADD_ITEM_RESPONSE);

    std::string tag = "{" + std::to_string(tempUser->GetPk()) + "}";
    std::string inventory_slot = itemType[addItemReqPacket->itemType] + ":" + tag;

    try { // AddItem Success (ItemCode:slotposition, count)
        redis->hset(inventory_slot, std::to_string(addItemReqPacket->itemPosition),
            std::to_string(addItemReqPacket->itemCode) + ":" + std::to_string(addItemReqPacket->itemCount));
    }
    catch (const sw::redis::Error& e) {
        addItemResPacket.isSuccess = false;
        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(ADD_ITEM_RESPONSE), (char*)&addItemResPacket);
        std::cerr << "Redis error: " << e.what() << std::endl;
        return;
    }

    addItemResPacket.isSuccess = true;
    connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(ADD_ITEM_RESPONSE), (char*)&addItemResPacket);
}

void RedisManager::DeleteItem(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto delItemReqPacket = reinterpret_cast<DEL_ITEM_REQUEST*>(pPacket_);
    InGameUser* tempUser = inGameUserManager->GetInGameUserByObjNum(connObjNum_);

    DEL_ITEM_RESPONSE delItemResPacket;
    delItemResPacket.PacketId = (uint16_t)CHANNEL_ID::DEL_ITEM_RESPONSE;
    delItemResPacket.PacketLength = sizeof(DEL_ITEM_RESPONSE);

    std::string inventory_slot = itemType[delItemReqPacket->itemType] + ":";
    std::string tag = "{" + std::to_string(tempUser->GetPk()) + "}";

    try {
        redis->hset(inventory_slot, std::to_string(delItemReqPacket->itemPosition), std::to_string(0) + ":" + std::to_string(0));
    }
    catch (const sw::redis::Error& e) {
        delItemResPacket.isSuccess = false;
        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(DEL_ITEM_RESPONSE), (char*)&delItemResPacket);
        std::cerr << "Redis error: " << e.what() << std::endl;
        return;
    }

    delItemResPacket.isSuccess = true;
    connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(DEL_ITEM_RESPONSE), (char*)&delItemResPacket);
}

void RedisManager::ModifyItem(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto modItemReqPacket = reinterpret_cast<MOD_ITEM_REQUEST*>(pPacket_);
    InGameUser* tempUser = inGameUserManager->GetInGameUserByObjNum(connObjNum_);

    MOD_ITEM_RESPONSE modItemResPacket;
    modItemResPacket.PacketId = (uint16_t)CHANNEL_ID::MOD_ITEM_RESPONSE;
    modItemResPacket.PacketLength = sizeof(MOD_ITEM_RESPONSE);

    std::string inventory_slot = itemType[modItemReqPacket->itemType] + ":";
    std::string tag = "{" + std::to_string(tempUser->GetPk()) + "}";

    try {
        redis->hset(inventory_slot, itemType[modItemReqPacket->itemType] + std::to_string(modItemReqPacket->itemCode) + std::to_string(modItemReqPacket->itemPosition), std::to_string(modItemReqPacket->itemCount));
    }
    catch (const sw::redis::Error& e) {
        modItemResPacket.isSuccess = false;
        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(MOD_ITEM_RESPONSE), (char*)&modItemResPacket);
        std::cerr << "Redis error: " << e.what() << std::endl;
        return;
    }

    modItemResPacket.isSuccess = true;
    connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(MOD_ITEM_RESPONSE), (char*)&modItemResPacket);
}

void RedisManager::MoveItem(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto movItemReqPacket = reinterpret_cast<MOV_ITEM_REQUEST*>(pPacket_);
    InGameUser* tempUser = inGameUserManager->GetInGameUserByObjNum(connObjNum_);

    std::string tag = "{" + std::to_string(tempUser->GetPk()) + "}";
    std::string inventory_slot = itemType[movItemReqPacket->ItemType] + ":" + tag;

    MOV_ITEM_RESPONSE movItemResPacket;
    movItemResPacket.PacketId = (uint16_t)CHANNEL_ID::MOV_ITEM_RESPONSE;
    movItemResPacket.PacketLength = sizeof(MOV_ITEM_RESPONSE);

    try {
        auto pipe = redis->pipeline(tag);
        pipe.hset(inventory_slot, std::to_string(movItemReqPacket->dragItemPos),
            std::to_string(movItemReqPacket->dragItemCode) + ":" + std::to_string(movItemReqPacket->dragItemCount))
            .hset(inventory_slot, std::to_string(movItemReqPacket->targetItemPos),
                std::to_string(movItemReqPacket->targetItemCode) + ":" + std::to_string(movItemReqPacket->targetItemCount));
        pipe.exec();
    }
    catch (const sw::redis::Error& e) {
        movItemResPacket.isSuccess = false;
        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(MOV_ITEM_RESPONSE), (char*)&movItemResPacket);
        std::cerr << "Redis error: " << e.what() << std::endl;
        return;
    }

    movItemResPacket.isSuccess = true;
    connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(MOV_ITEM_RESPONSE), (char*)&movItemResPacket);
}


//  ---------------------------- INVENTORY:EQUIPMENT  ----------------------------

void RedisManager::AddEquipment(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto addEquipReqPacket = reinterpret_cast<ADD_EQUIPMENT_REQUEST*>(pPacket_);
    InGameUser* tempUser = inGameUserManager->GetInGameUserByObjNum(connObjNum_);

    ADD_EQUIPMENT_RESPONSE addEquipResPacket;
    addEquipResPacket.PacketId = (uint16_t)CHANNEL_ID::ADD_EQUIPMENT_RESPONSE;
    addEquipResPacket.PacketLength = sizeof(ADD_EQUIPMENT_RESPONSE);

    std::string tag = "{" + std::to_string(tempUser->GetPk()) + "}";
    std::string inventory_slot = itemType[0] + ":" + tag;

    try {
        redis->hset(inventory_slot, std::to_string(addEquipReqPacket->itemPosition),
            std::to_string(addEquipReqPacket->itemCode) + ":" + std::to_string(addEquipReqPacket->Enhancement));
    }
    catch (const sw::redis::Error& e) {
        addEquipResPacket.isSuccess = false;
        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(ADD_EQUIPMENT_RESPONSE), (char*)&addEquipResPacket);
        std::cerr << "Redis error: " << e.what() << std::endl;
        return;
    }

    addEquipResPacket.isSuccess = true;
    connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(ADD_EQUIPMENT_RESPONSE), (char*)&addEquipResPacket);
}

void RedisManager::DeleteEquipment(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto delEquipReqPacket = reinterpret_cast<DEL_EQUIPMENT_REQUEST*>(pPacket_);
    InGameUser* tempUser = inGameUserManager->GetInGameUserByObjNum(connObjNum_);

    DEL_EQUIPMENT_RESPONSE delEquipResPacket;
    delEquipResPacket.PacketId = (uint16_t)CHANNEL_ID::DEL_EQUIPMENT_RESPONSE;
    delEquipResPacket.PacketLength = sizeof(DEL_EQUIPMENT_RESPONSE);

    std::string tag = "{" + std::to_string(tempUser->GetPk()) + "}";
    std::string inventory_slot = itemType[0] + ":" + tag;

    try {
        redis->hset(inventory_slot, std::to_string(delEquipReqPacket->itemPosition), std::to_string(0) + ":" + std::to_string(0));
    }
    catch (const sw::redis::Error& e) {
        delEquipResPacket.isSuccess = false;
        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(DEL_EQUIPMENT_RESPONSE), (char*)&delEquipResPacket);
        std::cerr << "Redis error: " << e.what() << std::endl;
        return;
    }
    delEquipResPacket.isSuccess = true;
    connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(DEL_EQUIPMENT_RESPONSE), (char*)&delEquipResPacket);
}

void RedisManager::EnhanceEquipment(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto enhEquipReqPacket = reinterpret_cast<ENH_EQUIPMENT_REQUEST*>(pPacket_);
    InGameUser* tempUser = inGameUserManager->GetInGameUserByObjNum(connObjNum_);

    ENH_EQUIPMENT_RESPONSE enhEquipResPacket;
    enhEquipResPacket.PacketId = (uint16_t)CHANNEL_ID::ENH_EQUIPMENT_RESPONSE;
    enhEquipResPacket.PacketLength = sizeof(ENH_EQUIPMENT_RESPONSE);

    std::string tag = "{" + std::to_string(tempUser->GetPk()) + "}";
    std::string inventory_slot = itemType[0] + ":" + tag;

    try {
        auto tempE = redis->hget(inventory_slot, std::to_string(enhEquipReqPacket->itemPosition));
        if (tempE) {
            std::string value = *tempE;
            for (int i = 0; i < value.size(); i++) {
                if (value[i] == ':') {
                    std::string first = value.substr(0, i);
                    std::string second = value.substr(i + 1);

                    // uint16_t�� ��ȯ
                    uint16_t f = static_cast<uint16_t>(std::stoi(first));
                    uint16_t s = static_cast<uint16_t>(std::stoi(second));

                    std::cout << tempUser->GetId() << " ���� " << enhanceProbabilities[s] << "% Ȯ�� ��ȭ �õ�" << std::endl;

                    if (EquipmentEnhance(s)) { // Enhance Success
                        redis->hset(inventory_slot, std::to_string(enhEquipReqPacket->itemPosition),
                            first + ":" + std::to_string(s + 1)); // ��ȭ ����
                        enhEquipResPacket.isSuccess = true;
                        enhEquipResPacket.Enhancement = s + 1;
                        std::cout << "��ȭ ����" << std::endl;
                    }
                    else { // Enhance Success
                        enhEquipResPacket.isSuccess = false;
                        std::cout << "��ȭ ����" << std::endl;
                    }

                    connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(ENH_EQUIPMENT_RESPONSE), (char*)&enhEquipResPacket);
                    return;
                }
            }

            // �߸��� �����͸� �޾�����
            enhEquipResPacket.isSuccess = false;
            connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(ENH_EQUIPMENT_RESPONSE), (char*)&enhEquipResPacket);
        }
        else { // ���𽺿� �ش� ��� �� ã������
            enhEquipResPacket.isSuccess = false;
            std::cout << "��ȭ ����" << std::endl;
            connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(ENH_EQUIPMENT_RESPONSE), (char*)&enhEquipResPacket);
            return;
        }
    }
    catch (const sw::redis::Error& e) {
        enhEquipResPacket.isSuccess = false;
        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(ENH_EQUIPMENT_RESPONSE), (char*)&enhEquipResPacket);
        std::cerr << "Redis error: " << e.what() << std::endl;
        return;
    }
}

void RedisManager::MoveEquipment(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto movItemReqPacket = reinterpret_cast<MOV_EQUIPMENT_REQUEST*>(pPacket_);
    InGameUser* tempUser = inGameUserManager->GetInGameUserByObjNum(connObjNum_);

    std::string tag = "{" + std::to_string(tempUser->GetPk()) + "}";
    std::string inventory_slot = itemType[0] + ":" + tag;

    MOV_EQUIPMENT_RESPONSE movItemResPacket;
    movItemResPacket.PacketId = (uint16_t)CHANNEL_ID::MOV_EQUIPMENT_RESPONSE;
    movItemResPacket.PacketLength = sizeof(MOV_EQUIPMENT_RESPONSE);

    try {
        auto pipe = redis->pipeline(tag);
        pipe.hset(inventory_slot, std::to_string(movItemReqPacket->dragItemPos),
            std::to_string(movItemReqPacket->dragItemCode) + ":" + std::to_string(movItemReqPacket->dragItemEnhancement))
            .hset(inventory_slot, std::to_string(movItemReqPacket->targetItemPos),
                std::to_string(movItemReqPacket->targetItemCode) + ":" + std::to_string(movItemReqPacket->targetItemEnhancement));
        pipe.exec();
    }
    catch (const sw::redis::Error& e) {
        movItemResPacket.isSuccess = false;
        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(MOV_ITEM_RESPONSE), (char*)&movItemResPacket);
        std::cerr << "Redis error: " << e.what() << std::endl;
        return;
    }

    movItemResPacket.isSuccess = true;
    connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(MOV_ITEM_RESPONSE), (char*)&movItemResPacket);
}


//  ---------------------------- RAID  ----------------------------

void RedisManager::MatchStart(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    InGameUser* tempUser = inGameUserManager->GetInGameUserByObjNum(connObjNum_);

    MATCHING_REQUEST matchReqPacket;
    matchReqPacket.PacketId = (uint16_t)MATCHING_ID::MATCHING_REQUEST;
    matchReqPacket.PacketLength = sizeof(MATCHING_REQUEST);
    matchReqPacket.userObjNum = connObjNum_;
    matchReqPacket.userGroupNum = tempUser->GetLevel() / 3 + 1; // �����ص� �׷� ��ȣ ���� ����

    connUsersManager->FindUser(MatchingServerObjNum)->PushSendMsg(sizeof(MATCHING_REQUEST), (char*)&matchReqPacket);
}

void RedisManager::GetRanking(uint16_t connObjNum_, uint16_t packetSize_, char* pPacket_) {
    auto delEquipReqPacket = reinterpret_cast<RAID_RANKING_REQUEST*>(pPacket_);
    InGameUser* tempUser = inGameUserManager->GetInGameUserByObjNum(connObjNum_);

    RAID_RANKING_RESPONSE raidRankResPacket;
    raidRankResPacket.PacketId = (uint16_t)CHANNEL_ID::RAID_RANKING_RESPONSE;
    raidRankResPacket.PacketLength = sizeof(RAID_RANKING_RESPONSE);

    std::vector<std::pair<std::string, double>> scores;
    try {

        redis->zrevrange("ranking", delEquipReqPacket->startRank,
            delEquipReqPacket->startRank + RANKING_USER_COUNT, std::back_inserter(scores));

        char* tempC = new char[MAX_SCORE_SIZE + 1];
        char* tc = tempC;
        uint16_t cnt = scores.size();

        for (int i = 0; i < cnt; i++) {
            RANKING ranking;
            strncpy_s(ranking.userId, scores[i].first.c_str(), MAX_USER_ID_LEN);
            ranking.score = scores[i].second;
            memcpy(tc, (char*)&ranking, sizeof(RANKING));
            tc += sizeof(RANKING);
        }

        raidRankResPacket.rkCount = cnt;
        std::memcpy(raidRankResPacket.reqScore, tempC, MAX_SCORE_SIZE + 1);

        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(RAID_RANKING_RESPONSE), (char*)&raidRankResPacket);

        delete[] tempC;
    }
    catch (const sw::redis::Error& e) {
        raidRankResPacket.rkCount = 0;
        connUsersManager->FindUser(connObjNum_)->PushSendMsg(sizeof(RAID_RANKING_RESPONSE), (char*)&raidRankResPacket);
        std::cerr << "Redis error: " << e.what() << std::endl;
        return;
    }
}