#include "Chat.h"
#include "Config.h"
#include "Player.h"
#include "ScriptMgr.h"

using namespace Acore::ChatCommands;

uint32 rafDuration;
uint32 rafAge;
uint32 rafRewardDays;
bool rafRewardSwiftZhevra;
bool rafRewardTouringRocket;
bool rafRewardCelestialSteed;
uint32 rafRealmId;

enum ReferralStatus
{
    STATUS_REFERRAL_PENDING = 1,
    STATUS_REFERRAL_ACTIVE  = 2,
    STATUS_REFERRAL_EXPIRED = 3
};

class RecruitAFriendCommand : public CommandScript
{
public:
    RecruitAFriendCommand() : CommandScript("RecruitAFriendCommand") {}

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable recruitCommandTable =
        {
            { "aceptar", HandleRecruitAcceptCommand, SEC_PLAYER, Console::No },
            { "declinar", HandleRecruitDeclineCommand, SEC_PLAYER, Console::No },
            { "amigo", HandleRecruitFriendCommand, SEC_PLAYER, Console::No },
            { "ayuda", HandleRecruitHelpCommand, SEC_PLAYER, Console::No },
            { "estado", HandleRecruitStatusCommand, SEC_PLAYER, Console::No },
        };

        static ChatCommandTable commandTable =
        {
            { "reclutar", recruitCommandTable }
        };

        return commandTable;
    }

    static bool HandleRecruitAcceptCommand(ChatHandler* handler)
    {
        uint32 recruitedAccountId = handler->GetSession()->GetAccountId();

        QueryResult result = LoginDatabase.Query("SELECT `account_id`, `recruiter_id` FROM `recruit_a_friend_accounts` WHERE `account_id` = {} AND `status` = {}", recruitedAccountId, STATUS_REFERRAL_PENDING);
        if (result)
        {
            Field* fields = result->Fetch();
            std::string referralDate = fields[0].Get<std::string>();
            uint32 accountId = fields[0].Get<uint32>();
            uint32 recruiterId = fields[1].Get<uint32>();

            LoginDatabase.Execute("DELETE FROM `recruit_a_friend_accounts` WHERE `account_id` = {} AND `status` = {}", accountId, STATUS_REFERRAL_PENDING);
            LoginDatabase.Execute("UPDATE `account` SET `recruiter` = {} WHERE `id` = {}", recruiterId, accountId);
            LoginDatabase.Execute("INSERT INTO `recruit_a_friend_accounts` (`account_id`, `recruiter_id`, `status`) VALUES ({}, {}, {})", accountId, recruiterId, STATUS_REFERRAL_ACTIVE);
            ChatHandler(handler->GetSession()).SendSysMessage("Has |cff4CFF00aceptado|r la solicitud de reclutamiento.");
            ChatHandler(handler->GetSession()).SendSysMessage("Tienes que cerrar la sesión y volver a iniciarla para que los cambios surtan efecto.");
            return true;
        }

        ChatHandler(handler->GetSession()).SendSysMessage("No tiene una solicitud de reclutamiento pendiente.");
        return true;
    }

    static bool HandleRecruitDeclineCommand(ChatHandler* handler)
    {
        uint32 recruitedAccountId = handler->GetSession()->GetAccountId();

        QueryResult result = LoginDatabase.Query("SELECT `account_id` FROM `recruit_a_friend_accounts` WHERE `account_id` = {} AND `status` = {}", recruitedAccountId, STATUS_REFERRAL_PENDING);
        if (result)
        {
            LoginDatabase.Execute("DELETE FROM `recruit_a_friend_accounts` WHERE `account_id` = {} AND `status` = {}", recruitedAccountId, STATUS_REFERRAL_PENDING);
            ChatHandler(handler->GetSession()).SendSysMessage("Has |cffFF0000declinado|r la solicitud de reclutamiento.");
            return true;
        }

        ChatHandler(handler->GetSession()).SendSysMessage("No tiene una solicitud de reclutamiento pendiente.");
        return true;
    }

    static bool HandleRecruitFriendCommand(ChatHandler* handler, Optional<PlayerIdentifier> target)
    {
        if (!target || !target->IsConnected() || target->GetConnectedPlayer()->GetSession()->GetSecurity() != SEC_PLAYER)
        {
            handler->SendSysMessage(LANG_PLAYER_NOT_FOUND);
            handler->SetSentErrorMessage(true);
            return false;
        }

        if (handler->GetSession()->GetSecurity() != SEC_PLAYER)
        {
            ChatHandler(handler->GetSession()).SendSysMessage("No podes reclutar un jugador porque sos un |cffFF0000GM|r!");
            return true;
        }

        uint32 recruiterAccountId = handler->GetSession()->GetAccountId();
        uint32 recruitedAccountId = target->GetConnectedPlayer()->GetSession()->GetAccountId();

        if (recruiterAccountId == recruitedAccountId)
        {
            ChatHandler(handler->GetSession()).SendSysMessage("No puedes reclutarte a ti |cffFF0000mismo|r!");
            return true;
        }

        uint32 referralStatus = ReferralStatus(recruitedAccountId);
        if (referralStatus > 0)
        {
            if (referralStatus == STATUS_REFERRAL_PENDING)
                ChatHandler(handler->GetSession()).SendSysMessage("Un reclutamiento de esa cuenta esta actualmente |cffFF0000pendiente|r.");
            else if (referralStatus == STATUS_REFERRAL_ACTIVE)
                ChatHandler(handler->GetSession()).SendSysMessage("Un reclutamiento de esa cuenta esta actualmente |cff4CFF00activo|r.");
            else
                ChatHandler(handler->GetSession()).SendSysMessage("Un reclutamiento de esa cuenta esta |cffFF0000expirado|r.");

            return true;
        }

        if (WhoRecruited(recruiterAccountId) == recruitedAccountId)
        {
            ChatHandler(handler->GetSession()).PSendSysMessage("No puedes reclutar a |cff4CFF00%s|r porque te han reclutado.", target->GetConnectedPlayer()->GetName());
            return true;
        }

        if (!IsReferralValid(recruitedAccountId) && rafAge > 0)
        {
            ChatHandler(handler->GetSession()).PSendSysMessage("No puedes reclutar a |cffFF0000%s|r porque su cuenta ha sido creada hace mas de %i dias atras.", target->GetConnectedPlayer()->GetName(), rafAge);
            return true;
        }

        if (IsReferralPending(recruitedAccountId))
        {
            ChatHandler(handler->GetSession()).PSendSysMessage("No puedes reclutar a |cffFF0000%s|r porque ya tienen una solicitud pendiente.", target->GetConnectedPlayer()->GetName());
            return true;
        }

        LoginDatabase.Execute("INSERT INTO `recruit_a_friend_accounts` (`account_id`, `recruiter_id`, `status`) VALUES ({}, {}, {})", recruitedAccountId, recruiterAccountId, STATUS_REFERRAL_PENDING);
        ChatHandler(handler->GetSession()).PSendSysMessage("Has enviado una solicitud de reclutamiento a |cff4CFF00%s|r.", target->GetConnectedPlayer()->GetName());
        ChatHandler(handler->GetSession()).SendSysMessage("El jugador debe |cff4CFF00aceptar|r, o |cff4CFF00declinar|r, la solicitud pendiente.");
        ChatHandler(handler->GetSession()).SendSysMessage("Si aceptan la solicitud, debe cerrar la sesión y volver a iniciarla para que los cambios surtan efecto.");

        ChatHandler(target->GetConnectedPlayer()->GetSession()).PSendSysMessage("|cff4CFF00%s|r te ha enviado una solicitud de reclutamiento.", handler->GetPlayer()->GetName());
        ChatHandler(target->GetConnectedPlayer()->GetSession()).SendSysMessage("Usa |cff4CFF00.reclutar aceptar|r para aceptar or |cff4CFF00.reclutar declinar|r para declinar la solicitud.");
        return true;
    }

    static bool HandleRecruitHelpCommand(ChatHandler* handler)
    {
        ChatHandler(handler->GetSession()).SendSysMessage("Puedes reclutar un amigo usando |cff4CFF00.reclutar amigo <nombre>|r.");
        ChatHandler(handler->GetSession()).SendSysMessage("Puedes aceptar una solicitud pendiente con |cff4CFF00.reclutar aceptar|r.");
        ChatHandler(handler->GetSession()).SendSysMessage("Puedes declinar una solicitud pendiente con |cff4CFF00.reclutar declinar|r.");
        ChatHandler(handler->GetSession()).PSendSysMessage("Ambos recibiran un bonus de experiencia y reputacion hasta el nivel %i.", sWorld->getIntConfig(CONFIG_MAX_RECRUIT_A_FRIEND_BONUS_PLAYER_LEVEL));

        if (rafDuration > 0)
        {
            ChatHandler(handler->GetSession()).PSendSysMessage("El reclutamiento expira luego de %i dias.", rafDuration);
        }
        else
        {
            ChatHandler(handler->GetSession()).SendSysMessage("El reclutamiento nunca expira.");
        }

        ChatHandler(handler->GetSession()).SendSysMessage("Puedes ver el estado del reclutamiento usando |cff4CFF00.reclutar estado|r.");
        return true;
    }

    static bool HandleRecruitStatusCommand(ChatHandler* handler)
    {
        uint32 accountId = handler->GetSession()->GetAccountId();

        QueryResult result = LoginDatabase.Query("SELECT `referral_date`, `referral_date` + INTERVAL {} DAY, `status` FROM `recruit_a_friend_accounts` WHERE `account_id` = {}", rafDuration, accountId);
        if (result)
        {
            Field* fields = result->Fetch();
            std::string referralDate = fields[0].Get<std::string>();
            std::string expirationDate = fields[1].Get<std::string>();
            uint8 status = fields[2].Get<int8>();

            if (status == STATUS_REFERRAL_EXPIRED)
            {
                ChatHandler(handler->GetSession()).PSendSysMessage("Fuiste reclutado el |cff4CFF00%s|r y expiro el |cffFF0000%s|r.", referralDate, expirationDate);
            }
            else if (status == STATUS_REFERRAL_ACTIVE)
            {
                if (rafDuration > 0)
                {
                    ChatHandler(handler->GetSession()).PSendSysMessage("Fuiste reclutado el |cff4CFF00%s|r y expira el |cffFF0000%s|r.", referralDate, expirationDate);
                }
                else
                {
                    ChatHandler(handler->GetSession()).PSendSysMessage("Fuiste reclutado el |cff4CFF00%s|r y |cffFF0000nunca|r expira.", referralDate, expirationDate);
                }
            }
            else
            {
                ChatHandler(handler->GetSession()).SendSysMessage("No has sido reclutado pero tienes una solicitud |cff4CFF00pendiente|r para aceptar/declinar.");
            }
        }
        else
        {
            ChatHandler(handler->GetSession()).PSendSysMessage("|cffFF0000No|r has sido reclutado.");
        }

        return true;
    }

private:
    static bool IsReferralValid(uint32 accountId)
    {
        QueryResult result = LoginDatabase.Query("SELECT * FROM `account` WHERE `id` = {} AND `joindate` > NOW() - INTERVAL {} DAY", accountId, rafAge);

        if (!result)
            return false;

        return true;
    }

    static int ReferralStatus(uint32 accountId)
    {
        QueryResult result = LoginDatabase.Query("SELECT `status` FROM `recruit_a_friend_accounts` WHERE `account_id` = {}", accountId);

        if (!result)
            return 0;

        Field* fields = result->Fetch();
        uint32 status = fields[0].Get<uint32>();

        return status;
    }

    static uint32 WhoRecruited(uint32 accountId)
    {
        QueryResult result = LoginDatabase.Query("SELECT `recruiter_id` FROM `recruit_a_friend_accounts` WHERE `account_id` = {}", accountId);

        if (!result)
            return 0;

        Field* fields = result->Fetch();
        uint32 referrerAccountId = fields[0].Get<uint32>();

        return referrerAccountId;
    }

    static bool IsReferralPending(uint32 accountId)
    {
        QueryResult result = LoginDatabase.Query("SELECT `account_id` FROM `recruit_a_friend_accounts` WHERE `account_id` = {} AND `status` = {}", accountId, STATUS_REFERRAL_PENDING);

        if (!result)
            return false;

        return true;
    }
};

class RecruitAFriendPlayer : public PlayerScript
{
public:
    RecruitAFriendPlayer() : PlayerScript("RecruitAFriendPlayer") {}

    void OnLogin(Player* player) override
    {
        ChatHandler(player->GetSession()).PSendSysMessage("Recluta-Un-Amigo <WoWSur> Este servidor permite reclutar a tu amigo para obtener bonus de experiencia y reputacion.");
        ChatHandler(player->GetSession()).PSendSysMessage("Recluta-Un-Amigo <WoWSur> Usa el comando |cff4CFF00.reclutar ayuda|r para empezar.");

        if (rafRewardDays > 0)
        {
            if (!IsEligible(player->GetSession()->GetAccountId()))
                return;

            if (IsRewarded(player))
                return;

            if (rafRewardSwiftZhevra)
                SendMailTo(player, "Swift Zhevra", "Encontré a esta Zhevra perdida caminando por Los Baldíos, sin rumbo fijo. ¡Pensé que tú, si alguien, podría darle un buen hogar!", 37719, 1);

            if (rafRewardTouringRocket)
                SendMailTo(player, "X-53 Touring Rocket", "Este cohete fue encontrado volando alrededor de Rasganorte, aparentemente sin ningún propósito. ¿Quizás podrías darle un buen uso?", 54860, 1);

            if (rafRewardCelestialSteed)
                SendMailTo(player, "Celestial Steed", "Se encontró un extraño corcel vagando por Rasganorte, apareciendo y desapareciendo gradualmente. Supuse que estarías interesado en un compañero así.", 54811, 1);


            LoginDatabase.Execute("INSERT INTO `recruit_a_friend_rewarded` (`account_id`, `realm_id`, `character_guid`) VALUES ({}, {}, {})", player->GetSession()->GetAccountId(), rafRealmId, player->GetGUID().GetCounter());
        }
    }

private:
    bool IsEligible(uint32 accountId)
    {
        QueryResult result = LoginDatabase.Query("SELECT * FROM `recruit_a_friend_accounts` WHERE `referral_date` < NOW() - INTERVAL {} DAY AND (`account_id` = {} OR `recruiter_id` = {}) AND `status` NOT LIKE {}", rafRewardDays, accountId, accountId, STATUS_REFERRAL_PENDING);

        if (!result)
            return false;

        return true;
    }

    bool IsRewarded(Player* player)
    {
        QueryResult result = LoginDatabase.Query("SELECT * FROM `recruit_a_friend_rewarded` WHERE `account_id` = {} AND `realm_id` = {} AND `character_guid` = {}", player->GetSession()->GetAccountId(), rafRealmId, player->GetGUID().GetCounter());

        if (result)
            return true;

        return false;
    }

    void SendMailTo(Player* receiver, std::string subject, std::string body, uint32 itemId, uint32 itemCount)
    {
        uint32 guid = receiver->GetGUID().GetCounter();

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        MailDraft* mail = new MailDraft(subject, body);
        ItemTemplate const* pProto = sObjectMgr->GetItemTemplate(itemId);
        if (pProto)
        {
            Item* mailItem = Item::CreateItem(itemId, itemCount);
            if (mailItem)
            {
                mailItem->SaveToDB(trans);
                mail->AddItem(mailItem);
            }
        }

        mail->SendMailTo(trans, receiver ? receiver : MailReceiver(guid), MailSender(MAIL_NORMAL, 0, MAIL_STATIONERY_GM), MAIL_CHECK_MASK_RETURNED);
        delete mail;
        CharacterDatabase.CommitTransaction(trans);
    }
};

class RecruitAFriendWorld : public WorldScript
{
public:
    RecruitAFriendWorld() : WorldScript("RecruitAFriendWorld")
    {
        timeDelay = 1h;
        currentTime = timeDelay;
    }

    void OnAfterConfigLoad(bool /*reload*/) override
    {
        rafDuration = sConfigMgr->GetOption<int32>("RecruitAFriend.Duration", 90);
        rafAge = sConfigMgr->GetOption<int32>("RecruitAFriend.MaxAccountAge", 7);
        rafRewardDays = sConfigMgr->GetOption<int32>("RecruitAFriend.Rewards.Days", 30);
        rafRewardSwiftZhevra = sConfigMgr->GetOption<bool>("RecruitAFriend.Rewards.SwiftZhevra", 1);
        rafRewardTouringRocket = sConfigMgr->GetOption<bool>("RecruitAFriend.Rewards.TouringRocket", 1);
        rafRewardCelestialSteed = sConfigMgr->GetOption<bool>("RecruitAFriend.Rewards.CelestialSteed", 1);
        rafRealmId = sConfigMgr->GetOption<uint32>("RealmID", 0);
    }

    void OnStartup() override
    {
        LoginDatabase.Execute("DELETE FROM `recruit_a_friend_accounts` WHERE `status` = {}", STATUS_REFERRAL_PENDING);
    }

    void OnUpdate(uint32 diff) override
    {
        if (rafDuration > 0)
        {
            currentTime += Milliseconds(diff);

            if (currentTime > timeDelay)
            {
                LoginDatabase.Execute("UPDATE `account` SET `recruiter` = 0 WHERE `id` IN (SELECT `account_id` FROM `recruit_a_friend_accounts` WHERE `referral_date` < NOW() - INTERVAL {} DAY AND status = {})", rafDuration, STATUS_REFERRAL_ACTIVE);
                LoginDatabase.Execute("UPDATE `recruit_a_friend_accounts` SET `status` = {} WHERE `referral_date` < NOW() - INTERVAL {} DAY AND `status` = {}", STATUS_REFERRAL_EXPIRED, rafDuration, STATUS_REFERRAL_ACTIVE);

                currentTime = 0s;
            }
        }
    }

private:
    Milliseconds currentTime;
    Milliseconds timeDelay;
};

void AddRecruitAFriendScripts()
{
    new RecruitAFriendCommand();
    new RecruitAFriendPlayer();
    new RecruitAFriendWorld();
}
