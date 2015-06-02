/// Credit merydwin@gmail.com

#include "Session.hpp"
#include "Common.h"
#include "Database/DatabaseEnv.h"
#include "Configuration/Config.h"
#include "Log.h"
#include "Module.hpp"
#include "WoWModules/PasswordAuth.hpp"
#include "WoWModules/RiskFingerprintAuth.hpp"
#include "WoWModules/ThumbprintAuth.hpp"
#include "RealmList.h"
#include "AuthCodes.h"
#include "Cryptography/HMACSHA1.h"
#include "Cryptography/BigNumber.h"

#include <algorithm>

//#include <Reporting/Reporter.hpp>

void ToByteArray(const std::string & p_Str, uint8_t * p_Dest)
{
    char * l_Str = const_cast<char*>(p_Str.c_str());

    for (unsigned int l_I = 0; l_I < p_Str.size(); l_I += 2)
    {
        char l_Buffer[3] = { *(l_Str + l_I), *(l_Str + l_I + 1), '\0' };
        p_Dest[l_I / 2] = strtol(l_Buffer, NULL, 16);
    }
}

BigNumber MakeBigNumber(const std::string & p_Str)
{
    uint8_t * l_Buffer = new uint8_t[(p_Str.size() / 2) + 1];

    ToByteArray(p_Str, l_Buffer);
    l_Buffer[p_Str.size() / 2] = 0;

    BigNumber l_Result;
    l_Result.SetBinary(l_Buffer, (p_Str.size() / 2) + 1);

    delete[] l_Buffer;

    return l_Result;
}

namespace BNet2 {

    typedef struct AuthHandler
    {
        uint32_t Opcode;
        uint32_t Channel;
        bool (Session::*Handler)(BNet2::Packet*);
        char const* Name;
    } AuthHandler;

    const AuthHandler OpcodeTable[] =
    {
        /// BATTLENET2_CHANNEL_AUTHENTICATION
        { OPCODE_ID(CMSG_LOGON_REQUEST),                            OPCODE_CHANNEL(CMSG_LOGON_REQUEST),                             &Session::HandleNullPacket,                     "Authentication::CMSG_LOGON_REQUEST"                            },
        { OPCODE_ID(CMSG_RESUME_REQUEST),                           OPCODE_CHANNEL(CMSG_RESUME_REQUEST),                            &Session::Authentication_Handle_ResumeRequest,  "Authentication::CMSG_RESUME_REQUEST"                           },
        { OPCODE_ID(CMSG_PROOF_RESPONSE),                           OPCODE_CHANNEL(CMSG_PROOF_RESPONSE),                            &Session::Authentication_Handle_ProofResponse,  "Authentication::CMSG_PROOF_RESPONSE"                           },
        { OPCODE_ID(CMSG_GENERATE_SINGLE_SIGN_ON_TOKEN_REQUEST_2),  OPCODE_CHANNEL(CMSG_GENERATE_SINGLE_SIGN_ON_TOKEN_REQUEST_2),   &Session::HandleNullPacket,                     "Authentication::CMSG_GENERATE_SINGLE_SIGN_ON_TOKEN_REQUEST_2"  },
        { OPCODE_ID(CMSG_LOGON_REQUEST_3),                          OPCODE_CHANNEL(CMSG_LOGON_REQUEST_3),                           &Session::Authentication_Handle_LogonRequest3,  "Authentication::CMSG_LOGON_REQUEST_3"                          },
        { OPCODE_ID(CMSG_SINGLE_SIGN_ON_REQUEST_3),                 OPCODE_CHANNEL(CMSG_SINGLE_SIGN_ON_REQUEST_3),                  &Session::HandleNullPacket,                     "Authentication::CMSG_SINGLE_SIGN_ON_REQUEST_3"                 },

        /// BATTLENET2_CHANNEL_CONNECTION
        { OPCODE_ID(CMSG_PING),                                     OPCODE_CHANNEL(CMSG_PING),                                      &Session::Connection_Handle_Ping,               "Connection::CMSG_PING"                                         },
        { OPCODE_ID(CMSG_ENABLE_ENCRYPTION),                        OPCODE_CHANNEL(CMSG_ENABLE_ENCRYPTION),                         &Session::Connection_Handle_EnableEncryption,   "Connection::CMSG_ENABLE_ENCRYPTION"                            },
        { OPCODE_ID(CMSG_LOGOUT_REQUEST),                           OPCODE_CHANNEL(CMSG_LOGOUT_REQUEST),                            &Session::Connection_Handle_LogoutRequest,      "Connection::CMSG_LOGOUT_REQUEST"                               },
        { OPCODE_ID(CMSG_DISCONNECT_REQUEST),                       OPCODE_CHANNEL(CMSG_DISCONNECT_REQUEST),                        &Session::Connection_Handle_DisconnectRequest,  "Connection::CMSG_DISCONNECT_REQUEST"                           },
        { OPCODE_ID(CMSG_CONNECTION_CLOSING),                       OPCODE_CHANNEL(CMSG_CONNECTION_CLOSING),                        &Session::Connection_Handle_ConnectionClosing,  "Connection::CMSG_CONNECTION_CLOSING"                           },

        /// BATTLENET2_CHANNEL_WOWREALM
        { OPCODE_ID(CMSG_LIST_SUBSCRIBE_REQUEST),                   OPCODE_CHANNEL(CMSG_LIST_SUBSCRIBE_REQUEST),                    &Session::WoWRealm_Handle_RealmUpdate,          "WoWRealm::CMSG_LIST_SUBSCRIBE_REQUEST"                         },
        { OPCODE_ID(CMSG_LIST_UNSUBSCRIBE),                         OPCODE_CHANNEL(CMSG_LIST_UNSUBSCRIBE),                          &Session::HandleNullPacket,                     "WoWRealm::CMSG_LIST_UNSUBSCRIBE"                               },
        { OPCODE_ID(CMSG_JOIN_REQUEST_V2),                          OPCODE_CHANNEL(CMSG_JOIN_REQUEST_V2),                           &Session::WoW_Handle_JoinRequest,               "WoWRealm::CMSG_JOIN_REQUEST_V2"                                },
        { OPCODE_ID(CMSG_MULTI_LOGON_REQUEST_V2),                   OPCODE_CHANNEL(CMSG_MULTI_LOGON_REQUEST_V2),                    &Session::HandleNullPacket,                     "WoWRealm::CMSG_MULTI_LOGON_REQUEST_V2"                         },

        /// BATTLENET2_CHANNEL_FRIENDS

        /// BATTLENET2_CHANNEL_PRESENCE

        /// BATTLENET2_CHANNEL_CHAT

        /// BATTLENET2_CHANNEL_SUPPORT

        /// BATTLENET2_CHANNEL_ACHIEVEMENT

        /// BATTLENET2_CHANNEL_CACHE
        { OPCODE_ID(CMSG_GATEWAY_LOOKUP_REQUEST),                   OPCODE_CHANNEL(CMSG_GATEWAY_LOOKUP_REQUEST),                    &Session::HandleNullPacket,                     "Cache::CMSG_GATEWAY_LOOKUP_REQUEST"                            },
        { OPCODE_ID(CMSG_CONNECT_REQUEST),                          OPCODE_CHANNEL(CMSG_CONNECT_REQUEST),                           &Session::HandleNullPacket,                     "Cache::CMSG_CONNECT_REQUEST"                                   },
        { OPCODE_ID(CMSG_DATA_CHUNK),                               OPCODE_CHANNEL(CMSG_DATA_CHUNK),                                &Session::HandleNullPacket,                     "Cache::CMSG_DATA_CHUNK"                                        },
        { OPCODE_ID(CMSG_GET_STREAM_ITEMS_REQUEST),                 OPCODE_CHANNEL(CMSG_GET_STREAM_ITEMS_REQUEST),                  &Session::Cache_Handle_GetStreamItemsRequest,   "Cache::CMSG_GET_STREAM_ITEMS_REQUEST"                          },


    };

    #define AUTH_TOTAL_COMMANDS (sizeof(OpcodeTable) / sizeof(OpcodeTable[0]))

    //////////////////////////////////////////////////////////////////////////

    /// Constructor
    Session::Session(RealmSocket& p_Socket)
        : m_SRP(0), m_Platform(BNet2::BATTLENET2_PLATFORM_BASE), m_Socket(p_Socket), m_CurrentPacket(NULL), m_State(BATTLENET2_SESSION_STATE_NONE)
    {

    }
    /// Destructor
    Session::~Session()
    {
        if (m_SRP)
            delete m_SRP;
    }

    //////////////////////////////////////////////////////////////////////////

    /// On read
    void Session::OnRead(void)
    {
        while (1)
        {
            uint32_t l_Size = GetSocket().recv_len();

            if (l_Size < 2)
                return;

            uint8_t * l_Buffer = new uint8_t[l_Size];

            if (!GetSocket().recv((char *)l_Buffer, l_Size))
            {
                delete[] l_Buffer;
                return;
            }

            if (l_Buffer[0] == 0x45 && l_Buffer[1] == 0x01)
            {
                Connection_Handle_EnableEncryption(nullptr);

                if (l_Size > 2)
                {
                    uint8_t* l_SecondBuffer = new uint8_t[l_Size - 2];
                    memcpy(l_SecondBuffer, l_Buffer + 2, l_Size - 2);

                    delete[] l_Buffer;
                    l_Buffer = l_SecondBuffer;
                    l_Size -= 2;
                }
                else
                {
                    delete[] l_Buffer;
                    return;
                }
            }

            if (m_BNet2Crypt.IsInitialized())
                m_BNet2Crypt.Decrypt((uint8_t*)l_Buffer, l_Size);

            try
            {
                Packet l_Packet((char*)l_Buffer, l_Size);

                delete[] l_Buffer;

                uint32_t l_Opcode = l_Packet.GetOpcode();
                uint32_t l_Channel = l_Packet.GetChannel();
                uint32_t l_I;

                for (l_I = 0; l_I < AUTH_TOTAL_COMMANDS; ++l_I)
                {
                    if ((uint8)OpcodeTable[l_I].Opcode == l_Opcode && (uint8)OpcodeTable[l_I].Channel == l_Channel)
                    {
                        sLog->outDebug(LOG_FILTER_AUTHSERVER, "BNet2::Session::OnRead Got data for opcode %s channel %u", OpcodeTable[l_I].Name, l_Channel);

                        m_CurrentPacket = &l_Packet;

                        if (!(*this.*OpcodeTable[l_I].Handler)(&l_Packet))
                        {
                            sLog->outDebug(LOG_FILTER_AUTHSERVER, "BNet2::Session::OnRead Command handler failed for opcode %s channel %u", OpcodeTable[l_I].Name, l_Channel);
                            GetSocket().shutdown();
                            m_CurrentPacket = NULL;
                            return;
                        }
                        break;
                    }
                }

                // Report unknown packets in the error log
                if (l_I == AUTH_TOTAL_COMMANDS)
                {
                    sLog->outError(LOG_FILTER_AUTHSERVER, "BNet2::Session::OnRead Got unknown packet from '%s' opcode %u channel %u", GetSocket().getRemoteAddress().c_str(), l_Opcode, l_Channel);
                    m_CurrentPacket = NULL;
                    return;
                }

                m_CurrentPacket = NULL;
            }
            catch (...)
            {
                break;
            }
        }
    }
    /// On accept
    void Session::OnAccept(void)
    {
        sLog->outDebug(LOG_FILTER_AUTHSERVER, "BNet2::Session::OnAccept '%s:%d' Accepting connection", GetSocket().getRemoteAddress().c_str(), GetSocket().getRemotePort());
    }
    /// On close
    void Session::OnClose(void)
    {
        sLog->outDebug(LOG_FILTER_AUTHSERVER, "BNet2::Session::OnClose");
    }

    //////////////////////////////////////////////////////////////////////////

    /// Send packet
    void Session::Send(BNet2::Packet * p_Packet)
    {
        if (!p_Packet || !p_Packet->GetSize())
            return;

        uint8_t * l_Data = p_Packet->GetData();

        if (m_BNet2Crypt.IsInitialized())
            m_BNet2Crypt.Encrypt(l_Data, p_Packet->GetSize());

         GetSocket().send((char const*)l_Data, p_Packet->GetSize());

         sLog->outDebug(LOG_FILTER_AUTHSERVER, "BNet2::Session::Send opcode %u channel %u size %u", p_Packet->GetOpcode(), p_Packet->GetChannel(), p_Packet->GetSize());
    }

    //////////////////////////////////////////////////////////////////////////

    /// Set client platform
    void Session::SetClientPlatform(const std::string & p_Str)
    {
        if (p_Str == "Win")
            m_Platform = BNet2::BATTLENET2_PLATFORM_WIN;
        else if (p_Str == "Wn64")
            m_Platform = BNet2::BATTLENET2_PLATFORM_WIN64;
        else if (p_Str == "Mc64")
            m_Platform = BNet2::BATTLENET2_PLATFORM_MAC64;
    }
    /// Set SRP params
    void Session::SetSRPParams(const std::string & p_Salt, const std::string & p_AccountName, const std::string & p_PasswordHash)
    {
        if (m_SRP)
            delete m_SRP;

        m_SRP = new SRP6a(p_Salt, p_AccountName, p_PasswordHash);
    }

    //////////////////////////////////////////////////////////////////////////

    /// Get session state
    SessionState Session::GetState()
    {
        return m_State;
    }
    /// Get client platform
    Platforms Session::GetClientPlatform()
    {
        return m_Platform;
    }
    /// Get secure remote password computation helper
    SRP6a * Session::GetSRP()
    {
        return m_SRP;
    }

    //////////////////////////////////////////////////////////////////////////

    RealmSocket & Session::GetSocket(void)
    {
        return m_Socket;
    }

    //////////////////////////////////////////////////////////////////////////

    /// Send auth result
    void Session::SendAuthResult(BNet2::AuthResult p_Result, bool p_Failed)
    {
        BNet2::Packet l_Result(BNet2::SMSG_LOGON_RESPONSE);

        l_Result.WriteBits(p_Failed, 1);

        if (p_Failed)
        {
            l_Result.WriteBits(false, 1);       ///< false - disable optional modules
            l_Result.WriteBits(1, 2);           ///< 1 - enable AuthResults
            l_Result.WriteBits(p_Result, 16);   ///< AuthResults (Error codes)
            l_Result.WriteBits(0x80000000, 32); ///< Unknown*
        }
        else
        {
            bool l_HaveOptData = true;

            char l_Bufffer[31];
            sprintf(l_Bufffer, "%u", m_AccountID);

            std::string l_AccountName = l_Bufffer;

            l_Result.WriteBits(0, 3);
            l_Result.WriteBits(0x80005000, 32);         ///< Ping request, ~10 secs
            l_Result.WriteBits(l_HaveOptData, 1);

            if (l_HaveOptData)
            {
                bool l_HasConnectionInfo = true;

                l_Result.WriteBits(l_HasConnectionInfo, 1);

                if (l_HasConnectionInfo)                ///< Battlenet::Regulator::LeakyBucketParams
                {
                    l_Result.WriteBits(25000000, 32);   ///< Threshold
                    l_Result.WriteBits(1000, 32);       ///< Rate
                }
            }

            l_Result.WriteString("", 8, false);         ///< First Name
            l_Result.WriteString("", 8, false);         ///< Last Name
            l_Result.WriteBits(m_AccountID, 32);
            l_Result.WriteBits(0, 8);
            l_Result.WriteBits(0, 64);
            l_Result.WriteBits(0, 8);
            l_Result.WriteString(l_AccountName, 5, false, -1);
            l_Result.WriteBits(0, 64);
            l_Result.WriteBits(0, 32);
            l_Result.WriteBits(false, 1);
        }

        Send(&l_Result);
    }

    //////////////////////////////////////////////////////////////////////////

    /// Authentication resume request
    bool Session::Authentication_Handle_ResumeRequest(BNet2::Packet * p_Packet)
    {
        std::string l_Program   = p_Packet->ReadFourCC();
        std::string l_Platform  = p_Packet->ReadFourCC();
        std::string l_Locale    = p_Packet->ReadFourCC();

        SetClientPlatform(l_Platform);

        uint32_t l_ComponentCount = p_Packet->ReadBits<uint32_t>(6);

        for (uint32_t l_I = 0; l_I < l_ComponentCount; ++l_I)
        {
            AuthComponent l_Component;
            l_Component.Program     = p_Packet->ReadFourCC();
            l_Component.Platform    = p_Packet->ReadFourCC();
            l_Component.Build       = p_Packet->ReadBits<uint32>(32);

            BNet2::AuthResult l_Result = AuthComponentManager::GetSingleton()->Check(l_Component);

            if (l_Result != BNet2::BATTLENET2_AUTH_OK)
            {
                SendAuthResult(l_Result);
                sLog->outDebug(LOG_FILTER_AUTHSERVER, "BNet2::Session::None_Handle_InformationRequest Component(%s %s %u) not allowed, error code => %u", l_Component.Program.c_str(), l_Component.Platform.c_str(), l_Component.Build, l_Result);

                return false;
            }
        }

        std::string l_AccountName       = p_Packet->ReadString(p_Packet->ReadBits<uint32_t>(9) + 3);
        uint8_t     l_Region            = p_Packet->ReadBits<uint8_t>(8);
        std::string l_GameAccountName   = p_Packet->ReadString(p_Packet->ReadBits<uint32_t>(5) + 1);

        ///@TODO
        return true;
    }
    /// Authentication informations client request
    bool Session::Authentication_Handle_LogonRequest3(BNet2::Packet * p_Packet)
    {
        std::string l_Program   = p_Packet->ReadFourCC();
        std::string l_Platform  = p_Packet->ReadFourCC();
        std::string l_Locale    = p_Packet->ReadFourCC();

        SetClientPlatform(l_Platform);

        uint32_t l_ComponentCount = p_Packet->ReadBits<uint32_t>(6);

        for (uint32_t l_I = 0; l_I < l_ComponentCount; ++l_I)
        {
            AuthComponent l_Component;
            l_Component.Program     = p_Packet->ReadFourCC();
            l_Component.Platform    = p_Packet->ReadFourCC();
            l_Component.Build       = p_Packet->ReadBits<uint32>(32);

            BNet2::AuthResult l_Result = AuthComponentManager::GetSingleton()->Check(l_Component);

            if (l_Result != BNet2::BATTLENET2_AUTH_OK)
            {
                SendAuthResult(l_Result);
                sLog->outDebug(LOG_FILTER_AUTHSERVER, "BNet2::Session::None_Handle_InformationRequest Component(%s %s %u) not allowed, error code => %u", l_Component.Program.c_str(), l_Component.Platform.c_str(), l_Component.Build, l_Result);

                return false;
            }
        }

        std::string l_AccountName = "";
        /// Have login
        if (p_Packet->ReadBits<bool>(1))
            l_AccountName = p_Packet->ReadString(p_Packet->ReadBits<uint32_t>(9) + 3);

        uint64 l_Compatibility = p_Packet->ReadBits<uint32_t>(64);

        /// Have login
        if (!l_AccountName.empty())
        {
            std::string const & l_IPAddress = GetSocket().getRemoteAddress();

            PreparedStatement* l_Stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_IP_BANNED);
            l_Stmt->setString(0, l_IPAddress);

            PreparedQueryResult l_Result = LoginDatabase.Query(l_Stmt);

            if (l_Result)
            {
                SendAuthResult(BNet2::BATTLENET2_AUTH_ACCOUNT_TEMP_BANNED);
                sLog->outDebug(LOG_FILTER_AUTHSERVER, "BNet2::Session::None_Handle_InformationRequest '%s:%d' Banned ip tries to login!", GetSocket().getRemoteAddress().c_str(), GetSocket().getRemotePort());
                return false;
            }

            l_Stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_LOGONCHALLENGE);
            l_Stmt->setString(0, l_AccountName);

            PreparedQueryResult l_Result_2 = LoginDatabase.Query(l_Stmt);

            if (l_Result_2)
            {
                Field* l_Fields = l_Result_2->Fetch();

                // If the IP is 'locked', check that the player comes indeed from the correct IP address
                bool l_Locked = false;
                if (l_Fields[2].GetUInt16() == 1)                  // if ip is locked
                {
                    sLog->outDebug(LOG_FILTER_AUTHSERVER, "BNet2::Session::None_Handle_InformationRequest Account '%s' is locked to IP - '%s'", l_AccountName.c_str(), l_Fields[3].GetCString());
                    sLog->outDebug(LOG_FILTER_AUTHSERVER, "BNet2::Session::None_Handle_InformationRequest Player address is '%s'", l_IPAddress.c_str());

                    if (strcmp(l_Fields[4].GetCString(), l_IPAddress.c_str()) != 0)
                    {
                        sLog->outDebug(LOG_FILTER_AUTHSERVER, "[AuthChallenge] Account IP differs");
                        SendAuthResult(BNet2::BATTLENET2_AUTH_CONNECT_METHOD_CHANGED);
                        l_Locked = true;
                    }
                    else
                        sLog->outDebug(LOG_FILTER_AUTHSERVER, "BNet2::Session::None_Handle_InformationRequest Account IP matches");
                }

                if (!l_Locked)
                {
                    //set expired bans to inactive
                    LoginDatabase.Execute(LoginDatabase.GetPreparedStatement(LOGIN_UPD_EXPIRED_ACCOUNT_BANS));

                    // If the account is banned, reject the logon attempt
                    l_Stmt = LoginDatabase.GetPreparedStatement(LOGIN_SEL_ACCOUNT_BANNED);
                    l_Stmt->setUInt32(0, l_Fields[1].GetUInt32());

                    PreparedQueryResult l_BanResult = LoginDatabase.Query(l_Stmt);

                    if (l_BanResult)
                    {
                        if ((*l_BanResult)[0].GetUInt32() == (*l_BanResult)[1].GetUInt32())
                        {
                            SendAuthResult(BNet2::BATTLENET2_AUTH_ACCOUNT_TEMP_BANNED);
                            sLog->outDebug(LOG_FILTER_AUTHSERVER, "'%s:%d' BNet2::Session::None_Handle_InformationRequest Banned account %s tried to login!", GetSocket().getRemoteAddress().c_str(), GetSocket().getRemotePort(), l_AccountName.c_str());
                        }
                        else
                        {
                            SendAuthResult(BNet2::BATTLENET2_AUTH_ACCOUNT_TEMP_BANNED);
                            sLog->outDebug(LOG_FILTER_AUTHSERVER, "'%s:%d' BNet2::Session::None_Handle_InformationRequest Temporarily banned account %s tried to login!", GetSocket().getRemoteAddress().c_str(), GetSocket().getRemotePort(), l_AccountName.c_str());
                        }
                    }
                    else
                    {
                        SetSRPParams(l_Fields[9].GetString(), l_AccountName, l_Fields[8].GetString());
                        GetSRP()->ComputePublicB();

                        std::list<BNet2::Module::Ptr> l_Modules = BNet2::ModuleManager::GetSingleton()->GetPlatformModules(GetClientPlatform());

                        BNet2::Packet l_ProofRequest(BNet2::SMSG_PROOF_REQUEST);

                        l_ProofRequest.WriteBits(2, 3); ///< Modules count

                        for (std::list<BNet2::Module::Ptr>::iterator l_It = l_Modules.begin(); l_It != l_Modules.end(); l_It++)
                        {
                            switch ((*l_It)->GetID())
                            {
                                case WOW_PASSWORD_AUTH_MODULE_ID:
                                case WOW_THUMBPRINT_AUTH_MODULE_ID:
                                    l_ProofRequest.WriteFourCC((*l_It)->GetTypeStr());
                                    l_ProofRequest.WriteFourCC_BattleGroup("XX");
                                    l_ProofRequest.AppendByteArray((*l_It)->GetHashData(), (*l_It)->GetHashDataSize());
                                    l_ProofRequest.WriteBits((*l_It)->GetSize(this), 10);

                                    (*l_It)->Write(this, &l_ProofRequest);
                                    break;

                                default:
                                    break;
                            }
                        }

                        Send(&l_ProofRequest);

                        m_AccountName           = l_AccountName;
                        m_AccountID             = l_Fields[1].GetUInt32();
                        m_AccountSecurityLevel  = l_Fields[4].GetUInt8() <= SEC_ADMINISTRATOR ? AccountTypes(l_Fields[4].GetUInt8()) : SEC_ADMINISTRATOR;
                        m_Locale                = l_Locale;

                        sLog->outDebug(LOG_FILTER_AUTHSERVER, "'%s:%d' BNet2::Session::None_Handle_InformationRequest account %s is using '%s' locale (%u)", GetSocket().getRemoteAddress().c_str(), GetSocket().getRemotePort(),
                            l_AccountName.c_str(), l_Locale.c_str(), GetLocaleByName(l_Locale));
                    }
                }
            }
            else
            {
                SendAuthResult(BNet2::BATTLENET2_AUTH_BAD_INFOS);
            }
        }

        return true;
    }
    /// Authentication client request
    bool Session::Authentication_Handle_ProofResponse(BNet2::Packet * p_Packet)
    {
        uint32_t l_ModuleCount = p_Packet->ReadBits<uint32_t>(3);

        for (uint32_t l_I = 0; l_I < l_ModuleCount; ++l_I)
        {
            uint32_t l_DataSize = p_Packet->ReadBits<uint32_t>(10);
            uint32_t l_StateID  = p_Packet->Read<uint8_t>();

            switch (l_StateID)
            {
                case BNet2::BATTLENET2_SESSION_STATE_AUTHED:
                    SendAuthResult(BNet2::BATTLENET2_AUTH_OK, false);
                    break;

                case BNet2::BATTLENET2_SESSION_STATE_WAIT_PROOF_VERIFICATION:
                {
                    if (l_DataSize != 0x121)
                        return false;

                    uint8_t l_A[4 * SHA256_DIGEST_LENGTH];
                    uint8_t l_M1[SHA256_DIGEST_LENGTH];
                    uint8_t l_ClientChallenge[4 * SHA256_DIGEST_LENGTH];

                    p_Packet->ReadBytes(l_A,                4 * SHA256_DIGEST_LENGTH);
                    p_Packet->ReadBytes(l_M1,                   SHA256_DIGEST_LENGTH);
                    p_Packet->ReadBytes(l_ClientChallenge,  4 * SHA256_DIGEST_LENGTH);

                    GetSRP()->ComputeU(         l_A,    4 * SHA256_DIGEST_LENGTH);
                    GetSRP()->ComputeClientM(   l_A,    4 * SHA256_DIGEST_LENGTH);

                    if (GetSRP()->Compare(GetSRP()->ClientM, l_M1, SHA256_DIGEST_LENGTH))
                    {
                        GetSRP()->ComputeServerM(l_M1, SHA256_DIGEST_LENGTH);

                        m_State = BATTLENET2_SESSION_STATE_PROOF_VERIFICATION;

                        std::list<BNet2::Module::Ptr> l_Modules = BNet2::ModuleManager::GetSingleton()->GetPlatformModules(GetClientPlatform());

                        BNet2::Packet l_ProofVerification(BNet2::SMSG_PROOF_REQUEST);

                        l_ProofVerification.WriteBits(2, 3); ///< Modules count

                        for (std::list<BNet2::Module::Ptr>::iterator l_It = l_Modules.begin(); l_It != l_Modules.end(); l_It++)
                        {
                            switch ((*l_It)->GetID())
                            {
                                case WOW_PASSWORD_AUTH_MODULE_ID:
                                case WOW_RISKFINGERPRINT_AUTH_MODULE_ID:
                                    l_ProofVerification.WriteFourCC((*l_It)->GetTypeStr());
                                    l_ProofVerification.WriteFourCC_BattleGroup("XX");
                                    l_ProofVerification.AppendByteArray((*l_It)->GetHashData(), (*l_It)->GetHashDataSize());
                                    l_ProofVerification.WriteBits((*l_It)->GetSize(this), 10);

                                    (*l_It)->Write(this, &l_ProofVerification);
                                    break;

                                default:
                                    break;
                            }
                        }

                        Send(&l_ProofVerification);
                    }
                    else
                    {
                        SendAuthResult(BNet2::BATTLENET2_AUTH_BAD_INFOS);
                        return false;
                    }

                    break;
                }

                default:
                    break;
            }

        }

        return true;
    }

    //////////////////////////////////////////////////////////////////////////

    /// Ping request
    bool Session::Connection_Handle_Ping(BNet2::Packet * p_Packet)
    {
        BNet2::Packet l_Pong(BNet2::SMSG_PONG);
        Send(&l_Pong);

        /// @TODO temp hack
        WoWRealm_Handle_RealmUpdate(p_Packet);

        return true;
    }
    /// Enable encryption
    bool Session::Connection_Handle_EnableEncryption(BNet2::Packet * p_Packet)
    {
        if (m_SRP && !m_BNet2Crypt.IsInitialized())
        {
            m_BNet2Crypt.Init(&m_SRP->SessionKey);
            return true;
        }

        return false;
    }
    /// Disconnect notify
    bool Session::Connection_Handle_LogoutRequest(BNet2::Packet * p_Packet)
    {
        /// Clear session key
        PreparedStatement * l_Stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_LOGONPROOF);
        l_Stmt->setString(0, "");
        l_Stmt->setString(1, GetSocket().getRemoteAddress().c_str());
        l_Stmt->setUInt32(2, GetLocaleByName(m_Locale));

        switch (GetClientPlatform())
        {
            case BATTLENET2_PLATFORM_WIN:
                l_Stmt->setString(3, "Win");
                break;
            case BATTLENET2_PLATFORM_WIN64:
                l_Stmt->setString(3, "Wn64");
                break;
            case BATTLENET2_PLATFORM_MAC64:
                l_Stmt->setString(3, "Mc64");
                break;
            default:
                l_Stmt->setString(3, "unk");
                break;
        }

        l_Stmt->setString(4, m_AccountName);

        LoginDatabase.Query(l_Stmt);

        return true;
    }
    /// Disconnect request
    bool Session::Connection_Handle_DisconnectRequest(BNet2::Packet * p_Packet)
    {
        uint16 l_Timeout    = p_Packet->ReadBits<uint16>(16);
        uint32 l_Tick       = p_Packet->ReadBits<uint32>(32);

        sLog->outDebug(LOG_FILTER_AUTHSERVER, "Connection::DisconnectRequest[Timeout: %u, Tick: %u]", l_Timeout, l_Tick);

        return true;
    }
    /// Connection closing
    bool Session::Connection_Handle_ConnectionClosing(BNet2::Packet * p_Packet)
    {
        std::string ClosingReason[] =
        {
            "PACKET_TOO_LARGE",
            "PACKET_CORRUPT",
            "PACKET_INVALID",
            "PACKET_INCORRECT",
            "HEADER_CORRUPT",
            "HEADER_IGNORED",
            "HEADER_INCORRECT",
            "PACKET_REJECTED",
            "CHANNEL_UNHANDLED",
            "COMMAND_UNHANDLED",
            "COMMAND_BAD_PERMISSIONS",
            "DIRECT_CALL",
            "TIMEOUT",
        };

        uint32_t l_PacketCount = p_Packet->ReadBits<uint32_t>(6);

        for (uint32_t l_I = 0; l_I < l_PacketCount; ++l_I)
        {
            std::string l_CommandName   = p_Packet->ReadFourCC();
            uint32      l_Timestamp     = p_Packet->ReadBits<uint32>(32);
            uint16      l_Size          = p_Packet->ReadBits<uint16>(16);
            std::string l_Channel       = p_Packet->ReadFourCC();
            uint16      l_LayerID       = p_Packet->ReadBits<uint16>(16);

            sLog->outDebug(LOG_FILTER_AUTHSERVER, "Connection::ConnectionClosing[Command: %s, Channel: %s, Size: %u, LayerID: %u, Timestamp: %u]", l_CommandName.c_str(), l_Channel.c_str(), l_Size, l_LayerID, l_Timestamp);
        }

        uint8 l_ClosingReasonID = p_Packet->ReadBits<uint8>(4);

        sLog->outDebug(LOG_FILTER_AUTHSERVER, "Connection::ConnectionClosing[Reason: %s]", ClosingReason[l_ClosingReasonID].c_str());

        p_Packet->SkipBytes(p_Packet->ReadBits<uint8>(8));          ///< BadData

        if (p_Packet->ReadBits<bool>(1))                            ///< Has header
        {
            uint8 l_Opcode  = p_Packet->ReadBits<uint8>(6);
            uint8 l_Channel = 0;

            if (p_Packet->ReadBits<bool>(1))
                l_Channel = p_Packet->ReadBits<uint8>(8);

            sLog->outDebug(LOG_FILTER_AUTHSERVER, "Connection::ConnectionClosing[ErroredPacketOpcode: %u, ErroredPacketChannel: %u]", l_Opcode, l_Channel);
        }

        time_t l_Now = p_Packet->ReadBits<time_t>(32);
        sLog->outDebug(LOG_FILTER_AUTHSERVER, "Connection::ConnectionClosing[Now: %u]", l_Now);

        return true;
    }

    //////////////////////////////////////////////////////////////////////////

    /// Realm list client request
    bool Session::WoWRealm_Handle_RealmUpdate(BNet2::Packet * p_Packet)
    {
        // Update realm list if need
        sRealmList->UpdateIfNeed();

        {
            bool l_Success = true;

            BNet2::Packet l_Packet(BNet2::SMSG_LIST_SUBSCRIBE_RESPONSE);
            l_Packet.WriteBits(!l_Success, 1);

            if (l_Success)
            {
                uint32 l_CharacterCount = 0;

                l_Packet.WriteBits(l_CharacterCount, 7);

                for (uint32 l_I = 0; l_I < l_CharacterCount; ++l_I)
                {
                    l_Packet.WriteBits(0, 8);   ///< Region
                    l_Packet.WriteBits(0, 12);  ///< ?
                    l_Packet.WriteBits(0, 8);   ///< Battleground
                    l_Packet.WriteBits(0, 32);  ///< Realm ID
                    l_Packet.WriteBits(0, 16);  ///< Character count
                }
            }
            else
                l_Packet.WriteBits(26, 8);  ///< Responce code

            Send(&l_Packet);
        }

        for (RealmList::RealmMap::const_iterator l_It = sRealmList->begin(); l_It != sRealmList->end(); ++l_It)
        {
            const Realm & l_Realm = l_It->second;
            uint8 l_LockStatus = (l_Realm.allowedSecurityLevel > m_AccountSecurityLevel) ? 1 : 0;

            if (l_LockStatus)
                continue;

            uint32      l_Flags     = l_Realm.flag;
            std::string l_Name      = l_It->first;
            bool        l_Version   = false;

            BNet2::Packet l_Packet(BNet2::SMSG_LIST_UPDATE);

            l_Packet.WriteBits(true, 1);
            l_Packet.WriteBits(l_It->second.timezone, 32);                      ///< Timezone
            l_Packet.WriteBits<float>(l_It->second.populationLevel, 32);        ///< Population
            l_Packet.WriteBits(l_LockStatus, 8);                                ///< Lock
            l_Packet.WriteBits(0, 19);                                          ///< Unk
            l_Packet.WriteBits(0x80000000 + l_Realm.icon, 32);                  ///< type (maybe icon ?)
            l_Packet.WriteString(l_Name, 10, false);                            ///< name
            l_Packet.WriteBits(l_Version, 1);                                   ///< Version ? send id/port

            // Version block
            if (l_Version)
            {
                std::string l_Version = g_VersionStrByBuild[l_It->second.gamebuild];
                l_Packet.WriteString(l_Version, 5);

                ACE_INET_Addr l_Address;
                l_Address.string_to_addr(l_Realm.address.c_str());

                uint8_t l_Port[2];
                *(uint16_t*)l_Port = l_Address.get_port_number();
                std::reverse(l_Port, l_Port + sizeof(l_Port));

                uint32_t l_IpAddress = l_Address.get_ip_address();
                EndianConvertReverse(l_IpAddress);

                l_Packet.Write(l_IpAddress);
                l_Packet.AppendByteArray(l_Port, sizeof(l_Port));
            }

            l_Packet.WriteBits(l_It->second.flag, 8);                      ///< Flags

            l_Packet.WriteBits(0, 8);                                      ///< Region
            l_Packet.WriteBits(0, 12);                                     ///< unk
            l_Packet.WriteBits(0, 8);                                      ///< Battle group
            l_Packet.WriteBits(l_It->second.m_ID, 32);                     ///< Index

            Send(&l_Packet);
        }

        {
            BNet2::Packet l_Packet(BNet2::SMSG_LIST_COMPLETE);
            Send(&l_Packet);
        }

        return true;
    }
    /// Realm connection client request
    bool Session::WoW_Handle_JoinRequest(BNet2::Packet * p_Packet)
    {
        /// - Read packet data
        uint8_t l_ClientSalt[4];
        *(uint32_t*)l_ClientSalt    = p_Packet->ReadBits<uint32_t>(32);         ///< ClientSeed
        uint32_t    l_Unknow        = p_Packet->ReadBits<uint32_t>(20);         ///< Unknown
        uint8_t     l_Region        = p_Packet->ReadBits<uint8_t>(8);           ///< Region
        uint16_t    l_Unknow2       = p_Packet->ReadBits<uint16_t>(12);         ///< Unknown
        uint8_t     l_Battlegroup   = p_Packet->ReadBits<uint8_t>(8);           ///< Battle group
        uint32_t    l_Index         = p_Packet->ReadBits<uint32_t>(32);         ///< Index

        uint8_t l_ServerSalt[4];
        *(int32_t*)l_ServerSalt = rand();

        uint8_t l_SessionKey[0x28];
        memset(l_SessionKey, 0, sizeof(l_SessionKey));

        HmacHash l_Hmac(64, (uint8_t*)GetSRP()->SessionKey.AsByteArray(64));
        l_Hmac.UpdateData((const uint8_t*)"WoW", 4);
        l_Hmac.UpdateData(l_ClientSalt, sizeof(l_ClientSalt));
        l_Hmac.UpdateData(l_ServerSalt, sizeof(l_ServerSalt));
        l_Hmac.Finalize();

        memcpy(l_SessionKey, l_Hmac.GetDigest(), l_Hmac.GetLength());

        HmacHash l_Hmac2(64, (uint8_t*)GetSRP()->SessionKey.AsByteArray(64));
        l_Hmac2.UpdateData((const uint8_t*)"WoW", 4);
        l_Hmac2.UpdateData(l_ServerSalt, sizeof(l_ServerSalt));
        l_Hmac2.UpdateData(l_ClientSalt, sizeof(l_ClientSalt));
        l_Hmac2.Finalize();

        ASSERT(l_Hmac.GetLength() + l_Hmac2.GetLength() == sizeof(l_SessionKey));
        memcpy(l_SessionKey + l_Hmac.GetLength(), l_Hmac2.GetDigest(), l_Hmac2.GetLength());

        char sSessionKey[sizeof(l_SessionKey)* 3];
        for (uint32_t l_It = 0; l_It < sizeof(l_SessionKey); ++l_It)
        {
            sprintf(sSessionKey + l_It * 2, "%02X", l_SessionKey[l_It]);
        }

        sSessionKey[sizeof(sSessionKey)-1] = 0;

        BigNumber l_K = MakeBigNumber(sSessionKey);

        PreparedStatement * l_Stmt = LoginDatabase.GetPreparedStatement(LOGIN_UPD_LOGONPROOF);
        l_Stmt->setString(0, l_K.AsHexStr());
        l_Stmt->setString(1, GetSocket().getRemoteAddress().c_str());
        l_Stmt->setUInt32(2, GetLocaleByName(m_Locale));

        std::string l_PlateformName;

        switch (GetClientPlatform())
        {
            case BATTLENET2_PLATFORM_WIN:
                l_PlateformName = "Win";
                break;
            case BATTLENET2_PLATFORM_WIN64:
                l_PlateformName = "Wn64";
                break;
            case BATTLENET2_PLATFORM_MAC64:
                l_PlateformName = "Mc64";
                break;
            default:
                l_PlateformName = "unk";
                break;
        }

        l_Stmt->setString(3, l_PlateformName);
        l_Stmt->setString(4, m_AccountName);

        LoginDatabase.Query(l_Stmt);

        Realm const* l_RealmRequested   = nullptr;
        uint32_t     l_RealmCounter     = 0;

        for (RealmList::RealmMap::const_iterator l_It = sRealmList->begin(); l_It != sRealmList->end(); ++l_It)
        {
            if (l_Index == l_It->second.m_ID)
            {
                l_RealmCounter      = 1;
                l_RealmRequested    = &l_It->second;
                break;
            }
        }

        if (!l_RealmRequested)
            return false;

//         sReporter->Report(MS::Reporting::MakeReport<MS::Reporting::Opcodes::AuthChooseRealm>::Craft
//         (
//             m_AccountID,                    ///< AccountId
//             l_RealmRequested->name,         ///< Realm
//             l_PlateformName,                ///< ClientPlatform
//             m_Socket.getRemoteAddress(),    ///< IpToCountry
//             m_Locale                        ///< ClientLang
//         ));

        /// User reporting
        /// Step Login (5)
        /// We havn't script system in battle.net ...
        /// @TODO: Use node.js reporter webservice
        LoginDatabase.PExecute("UPDATE user_reporting SET step = 5, last_ip = '%s' WHERE account_id = %u AND step < 5", m_Socket.getRemoteAddress().c_str(),  m_AccountID);

        uint8 l_LockStatus = (l_RealmRequested->allowedSecurityLevel > m_AccountSecurityLevel) ? 1 : 0;

        BNet2::Packet l_Buffer(BNet2::SMSG_JOIN_RESPONSE);
        l_Buffer.WriteBits(l_LockStatus, 1);                        ///< Response code
        l_Buffer.WriteBits(*(uint32_t*)l_ServerSalt, 32);           ///< ServerSalt
        l_Buffer.WriteBits(l_LockStatus ? 0 : l_RealmCounter, 5);   ///< RealmCounter
        l_Buffer.FlushBits();

        if (l_RealmRequested != nullptr && !l_LockStatus)
        {
            ACE_INET_Addr l_Address;
            l_Address.string_to_addr(l_RealmRequested->address.c_str());

            uint8_t l_Port[2];
            *(uint16_t*)l_Port = l_Address.get_port_number();
            std::reverse(l_Port, l_Port + sizeof(l_Port));

            uint32_t l_IpAddress = l_Address.get_ip_address();
            EndianConvertReverse(l_IpAddress);

            l_Buffer.Write(l_IpAddress);                            ///< IP
            l_Buffer.AppendByteArray(l_Port, sizeof(l_Port));       ///< Port
        }

        l_Buffer.FlushBits();
        l_Buffer.WriteBits(0, 5);

        Send(&l_Buffer);
        return true;
    }

    //////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////

    //////////////////////////////////////////////////////////////////////////

    /// Get stream items
    bool Session::Cache_Handle_GetStreamItemsRequest(BNet2::Packet * p_Packet)
    {
        p_Packet->ReadBits<uint32>(31);
        uint32      l_Index             = p_Packet->ReadBits<uint32>(32);
        int32       l_ReferenceTime     = p_Packet->ReadBits<int32>(32) - std::numeric_limits<int32>::min();
        bool        l_StreamDirection   = p_Packet->ReadBits<bool>(1);    ///< StreamDirection
        uint8       l_ModuleCount       = p_Packet->ReadBits<uint8>(6);   ///< Module count, always 0
        std::string l_Locale            = p_Packet->ReadFourCC();

        sLog->outDebug(LOG_FILTER_AUTHSERVER, "Cache::GetStreamItemsRequest[Index: %u, ReferenceTime: %d, Locale: %s, ModuleCount: %u, StreamDirection : %u]", l_Index, l_ReferenceTime, l_Locale.c_str(), l_StreamDirection, l_ModuleCount);

        if (p_Packet->ReadBits<bool>(1))
        {
            std::string l_ItemName  = p_Packet->ReadFourCC();
            std::string l_Channel   = p_Packet->ReadFourCC();

            sLog->outDebug(LOG_FILTER_AUTHSERVER, "Cache::GetStreamItemsRequest[ItemName: %s, Channel: %s]", l_ItemName.c_str(), l_Channel.c_str());

            BNet2::Packet l_Response(BNet2::SMSG_JOIN_RESPONSE);
            l_Response.WriteBits<uint16>(0, 16);
            l_Response.WriteBits<uint16>(1, 16);
            l_Response.WriteBits<uint32>(l_Index, 32);
            l_Response.WriteBits<uint32>(1, 6);             ///< Module count
            Send(&l_Response);
        }
        else
            p_Packet->ReadBits<uint16>(16);

        return true;
    }


}
