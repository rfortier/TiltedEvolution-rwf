#include "PCH.h"
#include "Localization.h"

#undef DebugNotification

namespace TradeTogether::Localization
{
    namespace
    {
        void ReplaceAll(std::string& a_text, std::string_view a_from, std::string_view a_to)
        {
            if (a_from.empty()) {
                return;
            }

            std::size_t position = 0;
            while ((position = a_text.find(a_from, position)) != std::string::npos) {
                a_text.replace(position, a_from.size(), a_to);
                position += a_to.size();
            }
        }
    }

    std::string TranslateUserText(std::string_view a_text)
    {
        std::string result(a_text);

        static constexpr std::pair<std::string_view, std::string_view> replacements[]{
            { "TradeTogether: ciblage indisponible.", "TradeTogether: targeting unavailable." },
            { "TradeTogether: vise l'autre joueur.", "TradeTogether: target the other player." },
            { "TradeTogether: la cible n'est pas un acteur.", "TradeTogether: the target is not an actor." },
            { "Offre: Insert/Suppr objets | PavNum +/- or | T valider | Tab annuler", "Offer: Insert/Delete items | Numpad +/- gold | T review | Tab cancel" },
            { "TradeTogether: une offre a change, verifiez a nouveau.", "TradeTogether: an offer changed. Please review it again." },
            { "TradeTogether: l'offre est limitee a 24 lignes.", "TradeTogether: the offer is limited to 24 item lines." },
            { "TradeTogether: toute la pile est deja dans l'offre.", "TradeTogether: the entire stack is already in the offer." },
            { "TradeTogether: cet objet n'est pas dans l'offre.", "TradeTogether: this item is not in the offer." },
            { "TradeTogether: impossible d'envoyer la reponse.", "TradeTogether: could not send the response." },
            { "TradeTogether: cette demande a expire.", "TradeTogether: this trade request expired." },
            { "TradeTogether: la demande d'echange a expire.", "TradeTogether: the trade request expired." },
            { "TradeTogether: le joueur cible n'est plus disponible.", "TradeTogether: the target player is no longer available." },
            { "TradeTogether: la cible de l'echange a change.", "TradeTogether: the trade target changed." },
            { "TradeTogether: confirmation reseau indisponible.", "TradeTogether: network confirmation unavailable." },
            { "TradeTogether: la cible n'a pas de nom reseau.", "TradeTogether: the target has no network name." },
            { "TradeTogether: impossible d'envoyer la demande.", "TradeTogether: invalid target. Target a connected player." },
            { "TradeTogether: aucune reponse, demande expiree.", "TradeTogether: no response, trade request expired." },
            { "TradeTogether: impossible de lire tes septims.", "TradeTogether: could not read your gold." },
            { "CONFIRMATION FINALE", "FINAL CONFIRMATION" },
            { "OFFRES D'ECHANGE", "TRADE OFFERS" },
            { "Votre offre :", "Your offer:" },
            { "Offre de ", "Offer from " },
            { "Etat : vous ", "Status: you are " },
            { "etes pret", "ready" },
            { "est pret", "is ready" },
            { "modifiez", "editing" },
            { "modifie", "is editing" },
            { "(aucun objet)", "(no items)" },
            { "... et ", "... and " },
            { " autre(s)", " more" },
            { " souhaite echanger avec vous.", " wants to trade with you." },
            { "Accepter et composer votre offre ?", "Accept and compose your offer?" },
            { "offre prete, attente de ", "offer ready, waiting for " },
            { "echange avec ", "trade with " },
            { " annule (", " cancelled (" },
            { "transfert annule (", "transfer cancelled (" },
            { "echange automatique termine avec ", "automatic trade completed with " },
            { "offre invalide (", "invalid offer (" },
            { "confirmation envoyee, attente de ", "confirmation sent, waiting for " },
            { "Offre mise a jour: ", "Offer updated: " },
            { "Offre: ", "Offer: " },
            { "echange accepte pour ", "trade accepted for " },
            { "echange refuse pour ", "trade declined for " },
            { " a refuse l'echange.", " declined the trade." },
            { " a accepte. Compose ton offre.", " accepted. Compose your offer." },
            { "en attente de ", "waiting for " },
            { "demande envoyee a ", "request sent to " },
            { " a confirme l'echange.", " confirmed the trade." },
            { "septim(s)", "gold" },
            { "disponible(s)", "available" },
            { "offre locale invalide", "invalid local offer" },
            { "objet introuvable: ", "item not found: " },
            { "instance indisponible pour ", "instance unavailable for " },
            { " disponible, ", " available, " },
            { " requis)", " required)" },
            { "or introuvable pendant le transfert", "gold not found during transfer" },
            { "or introuvable", "gold not found" },
            { "or insuffisant (", "insufficient gold (" },
            { "joueur local indisponible", "local player unavailable" },
            { "proxy du partenaire introuvable", "partner proxy not found" },
            { "transfert incomplet pour ", "incomplete transfer for " },
            { " restant)", " remaining)" },
            { "annulation locale", "cancelled locally" },
            { "annulation distante", "cancelled remotely" },
            { "delai depasse", "timeout" },
            { "Pret", "Ready" },
            { "Modifier", "Modify" },
            { "Confirmer", "Confirm" },
            { "Accepter", "Accept" },
            { "Refuser", "Decline" }
        };

        for (const auto& [from, to] : replacements) {
            ReplaceAll(result, from, to);
        }
        return result;
    }
}

namespace RE
{
    void LocalizedDebugNotification(const char* a_message)
    {
        const auto translated = TradeTogether::Localization::TranslateUserText(
            a_message ? std::string_view(a_message) : std::string_view{});
        DebugNotification(translated.c_str());
    }
}
