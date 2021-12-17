

#include "EXICrowdControl.h"

#include "Core/HW/Memmap.h"
#include "Common/Logging/Log.h"
#include "Common/Logging/LogManager.h"
#include "Common/CommonPaths.h"
#include "Common/CommonTypes.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/MemoryUtil.h"
#include "Common/MsgHandler.h"
#include "Common/StringUtil.h"
#include "Common/Thread.h"
#include <chrono>


CEXICrowdControl::CEXICrowdControl()
{
  INFO_LOG(CROWDCONTROL, "CROWDCONTROL exi ctor");
}

CEXICrowdControl::~CEXICrowdControl()
{

}

void CEXICrowdControl::clearReadQueue()
{
  for (int i = 0; i < m_read_queue.size(); i++)
  {
    m_read_queue[i] = 0;
  }
}

bool CEXICrowdControl::connectCrowdControl()
{
  sf::IpAddress ipAddress("127.0.0.1");
  sf::Socket::Status status = socket.connect(ipAddress, 43384);
  if (status != sf::Socket::Done)
  {
    // error...
    return false;
  }
  socket.setBlocking(false);

  INFO_LOG(CROWDCONTROL, "Connected To Crowd Control");
  return true;
}

bool CEXICrowdControl::disconnectCrowdControl()
{
  socket.disconnect();
  clearReadQueue();

  return true;
}

int CEXICrowdControl::paramToID(json jParam)
{
  if (jParam.is_number())
  {
    return jParam.get<int>();
  }
  else if (jParam.is_string()) 
  {

    // number at the end after last underscore is used as the id
    // e.g. item_reg_9 -> 9

    std::string paramStr = jParam.get<std::string>();
    std::string delimiter = "_";
    
    size_t pos = 0;

    while ((pos = paramStr.find(delimiter)) != std::string::npos)
    {
      paramStr.erase(0, pos + delimiter.length());
    }
    return std::stoi(paramStr);
  }
  return 0;
}

bool CEXICrowdControl::retrieveRequest()
{
  char data[300];
  std::size_t received;

  // TCP socket:
  if (socket.receive(data, 200, received) != sf::Socket::Done)
  {
    m_read_queue[0] = 1; // EFFECT_NONE
    return false;
    // error...
  }

  INFO_LOG(CROWDCONTROL, (char*)data);

  const std::string buf(data);
  // Example json request
  // {"id":3,"code":"give_stocks_3","parameters":["player_2",3],"targets":[],"viewer":"sdk","type":1}

  // deserialize json
  json j = json::parse(buf);

  // check if json is invalid
  if (!(j.contains("id") && j.contains("code") && j.contains("parameters")))
  {
    m_read_queue[0] = 2; // EFFECT_UNKNOWN
    return false;
    //error
  }

  // parse into ExiRequest depending on type of request
  json jParams = j["parameters"];
  currentRequestID = j["id"].get<int>();
  m_read_queue[0] = paramToID(j["code"]);

  for (int i = 0; i < jParams.size(); i++)
  {
    m_read_queue[1 + i] = Common::swap16(paramToID(jParams[i]));
  }

  return true;
}

bool CEXICrowdControl::sendResponse(int result)
{
  json j;
  j["id"] = currentRequestID;
  j["message"] = "Response sent from game";
  j["status"] = result;
  //j["type"] = 0;  // EffectRequest

  std::string data = j.dump() + "\00";

  INFO_LOG(CROWDCONTROL, &data[0]);
  if (socket.send(&data[0], data.size() + 1) != sf::Socket::Done)
  {
    INFO_LOG(CROWDCONTROL, "Could not send response message");
    return false;
    // error
  }

  return true;
}

// recieve data from game into emulator
void CEXICrowdControl::DMAWrite(u32 address, u32 size)
{
  INFO_LOG(CROWDCONTROL, "DMAWrite size: %u\n", size);
  u8* mem = Memory::GetPointer(address);

  if (!mem)
  {
    INFO_LOG(CROWDCONTROL, "Invalid address in DMAWrite!");
    return;
  }

  u8 command_byte = mem[0];  // first byte is always cmd byte
  //u8* payload = &mem[1];     // rest is payload

  switch (command_byte)
  {

    case STATUS_UNKNOWN:
      INFO_LOG(CROWDCONTROL, "Unknown DMAWrite command byte!");
      break;
    case STATUS_GAME_STARTED:
      INFO_LOG(CROWDCONTROL, "DMAWrite: STATUS_GAME_STARTED");
      isConnected = connectCrowdControl();
      break;
    case STATUS_GAME_ENDED:
      INFO_LOG(CROWDCONTROL, "DMAWrite: STATUS_GAME_ENDED");
      disconnectCrowdControl();
      isConnected = false;
      break;
    case STATUS_MATCH_STARTED:
      INFO_LOG(CROWDCONTROL, "DMAWrite: STATUS_MATCH_STARTED");
      if (!isConnected)
        isConnected = connectCrowdControl();
      isInMatch = true;
      break;
    case STATUS_MATCH_ENDED:
      INFO_LOG(CROWDCONTROL, "DMAWrite: STATUS_MATCH_ENDED");
      isInMatch = false;
      break;
    case RESULT_EFFECT_SUCCESS:
      INFO_LOG(CROWDCONTROL, "DMAWrite: EFFECT_RESULT_SUCCESS");
      sendResponse(0);
      break;
    case RESULT_EFFECT_FAILURE:
      INFO_LOG(CROWDCONTROL, "DMAWrite: EFFECT_RESULT_FAILURE");
      sendResponse(1);
      break;
    case RESULT_EFFECT_UNAVAILABLE:
      INFO_LOG(CROWDCONTROL, "DMAWrite: EFFECT_RESULT_UNAVAILABLE");
      sendResponse(2);
      break;
    case RESULT_EFFECT_RETRY:
      INFO_LOG(CROWDCONTROL, "DMAWrite: EFFECT_RESULT_RETRY");
      sendResponse(3);
      break;
    default:
      INFO_LOG(CROWDCONTROL, "DMAWrite: DEFAULT");
      IEXIDevice::DMAWrite(address, size);
      break;
  }
}

// send data from emulator to game
void CEXICrowdControl::DMARead(u32 address, u32 size)
{
  if (isConnected)
  {
    clearReadQueue();
    retrieveRequest();

    int effectID = m_read_queue[0];

    m_read_queue[0] = Common::swap16(m_read_queue[0]);

    if (effectID >= 3)
    {
      INFO_LOG(CROWDCONTROL, "DMARead CCEffect: %u\n", effectID);
      if (!isInMatch)
      {
        m_read_queue[0] = Common::swap16(1);
        INFO_LOG(CROWDCONTROL, "Not in match so can't grant effect");
        sendResponse(1);
      }      
    }
    
  }

  u16* qAddr = &this->m_read_queue[0];
  Memory::CopyToEmu(address, qAddr, size);

  /*else
  {
    INFO_LOG(CROWDCONTROL, "Empty DMARead since not connected");
  }*/
  
}

bool CEXICrowdControl::IsPresent() const
{
  return true;
}
