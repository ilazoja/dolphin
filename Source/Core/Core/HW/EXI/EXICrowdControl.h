#pragma once
#include "Core/HW/EXI/EXI_Device.h"
#include <string>
#include <array>
#include <vector>
#include <iostream>
#include <fstream>
#include <memory>
#include <deque>
#include "Core/Brawlback/Savestate.h"
#include "Core/Brawlback/Brawltypes.h"
#include <SFML/Network/TcpSocket.hpp>
#include <SFML/Network/IpAddress.hpp>
#include "json.hpp"

using json = nlohmann::json;

class CEXICrowdControl : public ExpansionInterface::IEXIDevice
{

  public:
    CEXICrowdControl();
    ~CEXICrowdControl() override;

    
    void DMAWrite(u32 address, u32 size) override;
    void DMARead(u32 address, u32 size) override;

    bool IsPresent() const;


  private:
    // halfword vector for sending into to the game
    // first entry is effect id (EFFECT_NOT_CONNECTED = 0, EFFECT_NONE = 1, EFFECT_UNKNOWN = 2, EFFECT_ACTUAL >= 3
    // second and above entries are parameters for the effect
    std::vector<u16> m_read_queue = {0, 0, 0, 0, 0};
    

    sf::TcpSocket socket;
    bool isConnected = false;
    bool isInMatch = false;
    int currentRequestID = 0;

    enum EXIStatus
    {
      STATUS_UNKNOWN = 0,
      STATUS_GAME_STARTED = 1,
      STATUS_GAME_ENDED = 2,
      STATUS_MATCH_STARTED = 3,
      STATUS_MATCH_ENDED = 4,
      RESULT_EFFECT_SUCCESS = 5,
      RESULT_EFFECT_FAILURE = 6,
      RESULT_EFFECT_UNAVAILABLE = 7,
      RESULT_EFFECT_RETRY = 8,
    };

    void clearReadQueue();
    bool connectCrowdControl();
    bool disconnectCrowdControl();
    int paramToID(json jParam);
    bool retrieveRequest();
    bool sendResponse(int result);

};
