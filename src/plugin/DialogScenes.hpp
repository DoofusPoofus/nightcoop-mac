/*
 dialog scenes the mac take on nightcoops dialogvote and the scene bits around it (script half: NightCoopDialogs in
 scripts/NightCoopMac/NightCoopDialogs.reds)
 bodies: evry 250 ms a partner body inside 0.5 m of our player gets hidden and comes back past 1.0 m (one spawn point for
   both) and while our scene tier is 3 or more (a cutscene or a seat that places our V) bodies within 30 m are hidden too.
   hiding turns the visual parts of the body and its slot items off (SetBodyHidden) nothing in RemoteBodies changes
 votes (host decides): evry 100 ms the script reads the active choice window. when one opens on the host and a partner is a
   voter (in the hosts world and within 60 m or anyone with dialog_votes everyone) the host opens vote #N: DialogBallot to
   evryone DialogTally on evry change VoteModeWord once per peer. evryone votes with the games own keys: arrows + F (or
   enter) the real F / enter presses are swallowed by KeyWatch while the vote is open and our selected row becomes our vote
   (DialogVote to the host on a guest). majority decides (2 players = both) after 60 s (or before a game timer runs out) the
   most votes or the hosts pick win. the decision carries a shared time and each machine presses it in its own window then:
   arrow keys until the selected row is the option then F checked by the window going away (a script call that picks a
   choice does not exist). a guest matches the hosts option by its text else by the row count else says it does not have it.
   a guest press before the ballot is there waits up to 8 s for it and then goes thru as in the game
 subtitles: the host sends evry line it shows (types 1 2 10 11 13) as DialogLine a guest shows it when its own scene was
   quiet for 5 s and the host is within 40 m and takes it off after its seconds. types 1 and 2 go up as ownerless lines (10)
   cuz the guest has no speaker object for them
 dialogs everyone (session.txt) counts evry partner as in the hosts world and near for testing with a probe
 skip: when our scene fast forward starts a SceneLineSkip press goes out and when it ends one with how long it ran. a partners
   press runs our fast forward (when our scene lets it) until their end a choice window or 15 s. no places or may skip checks
 scene ids stay 0 / 1 here (no quest scenes on mac yet) so windows peers cant match them to their scenes
*/
#pragma once

#include "plugin/RemoteBodies.hpp"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace nightcoop_mac::net
{
class RelaySession;
}

namespace nightcoop_mac::dialog_scenes
{
using LogFunction = std::function<void(const std::string&)>;

struct DialogSettings
{
    bool Votes = true;
    bool AnyPartnerCounts = false;
};

bool Handles(uint8_t messageType);
void Tick(uint64_t nowMs, bool inSave, const DialogSettings& settings, const net::PlayerState* localPlayer, const std::vector<RemotePeerView>& peers,
          RemoteBodies& bodies, net::RelaySession& session, const LogFunction& log);
void OnMessage(uint32_t fromPeer, const std::vector<uint8_t>& message, net::RelaySession& session, const LogFunction& log);
}
