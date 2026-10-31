// team build the script half of nightcoops teambuild (BuildStats 141 the plugin half is src/plugin/TeamBuild.cpp)
// an attribute check of a dialog option or a scene asks RPGManager.GetBuildScore for our V. in co-op the best of the team counts:
// the plugin hands over the best score of the partners per build (cool netrunner reflexes solo techie) worked out like
// nightcoop does (attribute + its skill check bonus 20 at most) and the wrap gives the higher of ours and theirs.
// checks on anything but our own player stay as they are

public class NightCoopTeamBuild extends ScriptableService {
  private let m_on: Bool;
  private let m_teamBest: array<Int32>;
  private let m_lifted: Int32;
  private let m_lastOurs: Int32;
  private let m_lastTeam: Int32;
  private let m_lastBuild: Int32;

  public static func Get() -> ref<NightCoopTeamBuild> {
    return GameInstance.GetScriptableServiceContainer().GetService(n"NightCoopTeamBuild") as NightCoopTeamBuild;
  }

  public func UseTeamBest(on: Bool, cool: Int32, netrunner: Int32, reflexes: Int32, solo: Int32, techie: Int32) {
    this.m_on = on;
    ArrayClear(this.m_teamBest);
    ArrayPush(this.m_teamBest, cool);
    ArrayPush(this.m_teamBest, netrunner);
    ArrayPush(this.m_teamBest, reflexes);
    ArrayPush(this.m_teamBest, solo);
    ArrayPush(this.m_teamBest, techie);
  }

  public static func BuildIndex(build: TweakDBID) -> Int32 {
    if Equals(build, t"PlayerBuilds.Cool") {
      return 0;
    }
    if Equals(build, t"PlayerBuilds.Netrunner") {
      return 1;
    }
    if Equals(build, t"PlayerBuilds.Reflexes") {
      return 2;
    }
    if Equals(build, t"PlayerBuilds.Solo") {
      return 3;
    }
    if Equals(build, t"PlayerBuilds.Techie") {
      return 4;
    }
    return -1;
  }

  public func Better(player: ref<GameObject>, build: ref<PlayerBuild_Record>, ours: Int32) -> Int32 {
    if !this.m_on || !IsDefined(player) || !IsDefined(build) {
      return ours;
    }
    let ourPlayer = GetPlayer(player.GetGame());
    if !IsDefined(ourPlayer) || !Equals(ourPlayer.GetEntityID(), player.GetEntityID()) {
      return ours;
    }
    let index = NightCoopTeamBuild.BuildIndex(build.GetID());
    if index < 0 || index >= ArraySize(this.m_teamBest) || this.m_teamBest[index] <= ours {
      return ours;
    }
    this.m_lifted += 1;
    this.m_lastOurs = ours;
    this.m_lastTeam = this.m_teamBest[index];
    this.m_lastBuild = index;
    return this.m_teamBest[index];
  }
}

@wrapMethod(RPGManager)
public final static func GetBuildScore(player: ref<GameObject>, buildToCheck: ref<PlayerBuild_Record>) -> Int32 {
  let ours = wrappedMethod(player, buildToCheck);
  let team = NightCoopTeamBuild.Get();
  if IsDefined(team) {
    return team.Better(player, buildToCheck, ours);
  }
  return ours;
}
