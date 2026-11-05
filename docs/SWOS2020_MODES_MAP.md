# SWOS2020_MODES_MAP.md — SWOS 2020's practice, challenges and speedrun, mapped onto the engine

Status: **reference for porting, 2026-11-04.** Nothing here is built. It is the companion of
docs/SWOS2020_MODS.md (which decided that `practice`, `challenges` and `speedrun` are canonical
OpenSWOS mods, and named hooks H1-H10) and of docs/MODS.md §8 and §16. For each mode it says what
SWOS 2020 does, where, and which engine site of ours that maps to; then what those sites cannot
express (the gaps), each with the smallest hook that would cover it.

## 0. How to read the citations

| prefix | file |
|---|---|
| `g:` | `C:\Projects\GitHub\swos-2020\src\game.cpp` |
| `asm:` | `swos-2020\swos\swos.asm` (the DOS game with SWOS 2020's patches in it) |
| `sym:` | `swos-2020\swos\symbols.txt`: `Proc, N` makes a C++ hook `SWOS::Proc_N()` called at line N of `Proc` |
| `ctl:` | `swos-2020\src\controls\controls.cpp` |
| `mp:` | `swos-2020\src\mpgame.cpp` |
| `php:` | `swos-2020\php_manager\PHPManager\PHPManager.cpp` (`.h` = its header) |
| `ini:` | `swos-2020\challenges\challenges.ini` |
| `src/…`, `include/…` | this tree (SWOS United = OpenSWOS's engine) |

**The template on our side is the arcade** (`src/arcade.c`): a mode that owns a run of matches.

| arcade site | file:line | what it gives a mode |
|---|---|---|
| `arc_start_game` | src/arcade.c:1112 | the mode's loop: load teams, set rules, `InitAndPlayGame`, read the winner, loop (= retry) or stop |
| `arc_begin_match_rules` | src/arcade.c:1006 | per-match rule latch before kick-off (`_autoReplays`, limits) |
| match globals | src/arcade.c:1150-1156 | `_gameType`, `_friendlyPitchType`, `_min/_maxSubstitutes`, `_gameLength` |
| `arc_load_team` | src/arcade.c:937 | a team into `_selectedTeams[slot]` with its controls |
| SetupPlayers branch | src/game_init.c:808-830 | no pre-match menu; who is human / CPU from `teamControls` |
| game name | src/game_init.c:966-970 | `_aArcadeMatchName` as `_gameName` |
| `Arcade_MatchTick` | src/arcade.c:1025, called src/game_loop.c:786 (after `UpdateTime`, before the sim) | per-frame rules: frame count from kick-off, end the match (`_playGame = 0`), `StartPenalties()` |
| `Arcade_GoalScored` | src/arcade.c:1082, called src/goal_scored.c:172 | a non-penalty goal |
| `arc_match_winner` | src/arcade.c:1096 | result from `_team1/2TotalGoals`, then `_team1/2PenaltyGoals` |
| replay-menu skip | src/game_loop.c:997-1008 | no WATCH REPLAY / EXIT menu after the match |

**The other sites the owner named** (confirmed):

- **Starting score and minute: the end of `InitGameRestoreTeams`** (src/match_setup.c:850), after
  `StartingMatch()` at :989 (and `PlaySample1IfNotTraining` at :990). Before it, :945-957 clear the
  scores and copy the first leg into `_team1/2TotalGoals`, and `ResetTime()` (:984, defined
  src/game_state.c:199) zeroes `_gameTime`.
- **Events:** `GoalScored(scoringTeam, scorerSprite)` src/goal_scored.c:90; `TryBookingThePlayer`
  src/tackle_foul.c:293 and `TrySendingOffThePlayer` :390 (both return 0 when the card is given;
  called :641 and :648); `UpdateTime` src/game_state.c:779 (half time :855, full time :867, ET
  :922 and :933, `EndOfGame`/`StartPenalties` from there); `NextPenalty` src/goalkeeper.c:344
  (shoot-out over at `end_shootout` :420).

Two more sites matter and are not in the owner's list:

- **`InitAndPlayGame`** src/game_init.c:954 clears `_extraTimeState`, `_penaltiesState`,
  `_secondLeg`, `_team1/2GoalsFirstLeg` (:980-985) and then lets `NetplaySetup_TakeTieRules()`
  (:988-992) put the online lobby's tie rules back. **A mode's ET / pens / two-leg rules belong at
  that call** (it is where SWOS 2020's `InitAndPlayGame_90` hook, `ChallengeGame::setDiyParameters`
  g:26597, and its second-leg set g:26302-26310 sit).
- **`InitPlayersBeforeEnteringPitch`** src/match_setup.c:369, run by `StartingMatch` (:477): it
  places all 22 sprites from `_teamPlayingUp` (:375) and, at `ST_STARTING_GAME`, clears every
  sprite's `sentAway`, `cards`, `injuryLevel` (:407-411). So **sides and the kick-off team must be
  set before :989**, and **sent-off players after it**.

## 1. SWOS 2020's shared machinery

All three modes run through one object, `ChallengeGame cGame` (class g:438-480, `run()` g:6342-7216,
`close()` g:6143-6176). A caller fills `gameMode` (0 score attack, 1 penalty shootout, 2 speedrun,
3 team training; g:6354-6383), `teamSelect` (0 random, 1 by name, 2 menu, 3 `.DIY` file, 4 speedrun
difficulty, 5 training), `control1/2` (1 CPU, 2 player), `score1/2`, `time1/2/3` (the three BCD
digits of the starting minute). `run()` copies them into the DOS globals `g_score1/2`,
`g_time1/2/3` (g:7012-7019) and calls `InitTeams(); updateCardsInjuriesAndEtc(); PlayFriendly();`
(g:7097-7099).

- The starting score is applied by the patched `InitGame` (asm:96617-96627: `statsTeam1Goals`,
  `team1GoalsDigit2`, `team1Goals` = `g_score1`, then overwritten with the first leg if
  `secondLeg`, asm:96629-96635). Ours: the end of `InitGameRestoreTeams` (`_statsTeam1Goals`,
  `_team1GoalsDigit1/2`, `_team1TotalGoals`). Note SWOS 2020 writes digit1 = 0, digit2 = score
  (asm:96619-96620): a starting score above 9 is drawn wrongly there (UNCONFIRMED in play); ours
  must split the digits as `GoalScored` does (src/goal_scored.c:157-162).
- The starting minute: `SWOS::ResetTime` puts `g_time1/2/3` into `gameTime[1..3]` and redraws the
  clock sprite (g:15439-15449). Ours: write `_gameTime` through the `GAMETIME_*` byte macros
  (src/game_state.c:154-156; BCD, BE layout) after `ResetTime`, and set `_copyTimeSprite = 1`
  (ResetTime sets it, src/game_state.c:204) so the clock is redrawn (UNCONFIRMED that this
  alone refreshes the digits).
- `StartingMatch` is patched to kick off at once: `stoppageEventTimer = 1` instead of 100 when
  `g_ExtraGame` (asm:103830-103833). Ours: src/match_setup.c:461 (`_stoppageEventTimer = 100`).
- Pre-match screens are skipped by `g_SkipVersusMenu/StadiumMenu/PlayMatchMenu` (asm:251219-251221,
  set g:28233-28257). Ours: the SetupPlayers branch (src/game_init.c:808).
- Team buffers are 684-byte `TeamFileEx` (g:436-437, `copyTeamBufferToSelTeamPtr` g:6178).
  Sources: random (`getRandomTeamBuffer` g:5644), by name (g:5786), from a `.DIY` (g:6077), by
  global team number from `TEAM.072` (g:6109). **`TEAM.072` is in neither tree** (UNCONFIRMED where
  it ships; it holds the training/speedrun teams 1547-1571).
- Randomness in setup uses `srand(time(NULL))` (g:14231, g:17716, g:30748): not reproducible.

## 2. PRACTICE (team training, penalty shootout, demo game)

Menu `practice.mnu`: FRIENDLY MATCH, TEAM TRAINING, PENALTY SHOOTOUT, DEMO GAME (help text
g:11509-11535). Entry `SWOS::practiceMainMenu_onSelect` g:11236 → `selectPracticeMenu` g:11228.

### 2.1 Team training (`trainingModePractice_OnSelect` g:11538)

**1. Before kick-off.**
- Tackle injuries off: `kTackleInjuryProbability[0..3] = 0` (g:11540-11542, restored g:11564-11568;
  also `setInjuryProbTableForTrainingMode` g:13744).
- `gameMode = 3, teamSelect = 5, control1 = 2, control2 = 1`, score 0-0, minute 0 (g:11545-11551);
  `run()` sets `g_TrainingMode = 1`, `g_SkipStadiumMenu = 1` (g:6377-6383), `g_Injury = 0` (g:6430).
- `practiceSetting.mnu` (g:6426-6436): ATTACK GOAL `g_TeamPlayingUp` (g:12991), CPU SKILL
  `g_GoalkeeperSkill` 0-5 (5 = random, g:13025), the human team's lines on/off `g_Defense0/
  g_Midfield0/g_Attack0` and the CPU's `g_Defense/g_Midfield/g_Attack` (g:13032-13072, globals
  g:1411-1423), CPU GHOSTED `g_GhostPlayers` (g:13002), TACKLE ALLOWED
  `g_TrainingModeTackleAllowed` (g:13007), PITCH TYPE `g_PitchTypeCustom` (g:13012), then CHOOSE
  TEAM / RANDOM TEAM / the online custom team (`g_PracticeSetting` 0/1/2, g:6438-6493).
- Opponent: by CPU SKILL, global team 1548/1567/1570/1569/1557 from `TEAM.072` (g:6495-6526).
- `teamPlayingUp` from ATTACK GOAL and `teamStarting = 1` in `SWOS::InitGame_202` (g:14188-14202;
  sym:2014). Ours: after src/match_setup.c:916-917 (where `Rand()` picks them), before :989.
- Pitch type: `friendlyPitchType = g_PitchTypeCustom` (g:7078-7081). Ours: arcade.c:1153.
- Players removed by line: `setHidePlayersForTrainingMode` g:19568, from
  `InitPlayersBeforeEnteringPitch_55` (g:19614-19617, sym:1995), per player `D1` (position index)
  and `D7` (team) against `isShowPlayer` g:19405-19538 (CPU: positions 6-9 defence, 2-5 midfield,
  0-1 attack; the human team's ranges follow its tactics, g:19447-19516). A hidden player gets
  `z = 20000, sentAway = 1, cards = -1` (g:19541-19548). The goalkeepers always stay (`D1 != 10`).
- Match length: the user's option, untouched (UNCONFIRMED that nothing else sets it).

**2. Each tick / event.**
- **The clock is frozen**: the HUD routine `showClockScoresAndOthers` (g:17797) sets
  `gameTime[1..3] = 0` every frame in training (g:17998-18003). The match never reaches half time.
- Restarts are immediate: `RunStoppageEventsAndSetAnimationTables_43` (g:19698-19727, sym:2021) sets
  `GS_825_750 = 0` at kick-off and corner states, 750 otherwise. Ours: `_gsKickoffStoppage`
  (src/dos_gameplay.c:45, read src/game_timers.c:496).
- Hidden players are skipped by the engine: AI (asm:118580-118584, 118640-118644 in `DoAI`; ours
  `AI_SetControlsDirection` src/ai.c:327, `keepers_ball` :442), `FindClosestPlayerToBallFacing`
  (asm:119924; ours src/ai.c:255), tackling (asm:106477-106489 `PlayerTacklingBall`, asm:107064-107075
  `PlayerTacklingTestFoul`, both also honour TACKLE ALLOWED; ours src/update_players.c:1950-1991 and
  src/tackle_foul.c:485).
- No cards in training: asm:107336 and asm:107356 skip `TryBookingThePlayer` /
  `TrySendingOffThePlayer`. Ours: src/tackle_foul.c:641/648.
- **Drills from the keyboard**, in the SDL event loop (ctl:1874-2172), only in training: F1-F7 a
  free kick at one of 7 fixed spots (`g_AutoFreeKick = 1..7` + `callTestFoulForPenaltyAndFreeKick`,
  ctl:2116-2152), ALT a free kick where the controlled player stands (`g_AutoFreeKickX/Y`, ctl:2153-2171),
  F8/F9 corners (ball placed with `SetBallPosition`, `g_AutoCornerKick = 1/2`,
  `CheckIfBallOutOfPlay`, ctl:1906-1993), 9 / 0 / MINUS / EQUALS goal kicks home and away
  (ctl:1994-2113), F10 a goal kick (ctl:1896-1905), F11 a high kick (a synthetic fire written into
  player 1's control state, ctl:1887-1895), F12 back to kick-off (`PrepareForInitialKick`,
  ctl:1881-1886). The free kick is a fake foul: the
  patched `TestFoulForPenaltyAndFreeKick` (asm:107720-107857) picks a fouled sprite and team and
  overwrites D1/D2 with the spot; `freeKickTweak1/2` (g:29398, g:29573-29595) do the same in C++.
  The corner: `CheckIfBallOutOfPlay` with `g_AutoCornerKick` (asm:111787-111856). Key list:
  `teamTrainingControls.mnu` (text at :66-320).
- Ghosts: `showPlayersInTrainingMode` sets a CPU player `visible = 0` when CPU GHOSTED (g:19550-19566)
  — the player still plays.
- HUD: no result banner (`ShowResult` asm:98265-98270), no spinning S (asm:103336, asm:7956), no
  VIEW OPPONENT (asm:52244), no score in the scoreboard (g:18026-18033).

**3. Win / fail.** None. **4. After the match:** `goto trainingMode_label` (g:7226-7228): back to
the practice settings; EXIT there leaves (`close()`, g:6431-6435). **5. Persistent data:** none
found besides the options (`g_PracticeSetting` etc. are globals, not saved; UNCONFIRMED).

### 2.2 Penalty shootout alone (`challengeGame2Practice_OnSelect` g:11572)

**1.** `g_PenaltyShootoutMenu = g_PenaltyShootoutStartUp = -1`; `gameMode = 1`, `teamSelect = 2`
(team menu), minute **120** (`time1 = 1, time2 = 2`, g:11574-11583). `run()` sets
`g_PenaltyShootout = -1`, **`g_GKManual = -1`**, `GS_GK_55_50 = 1` (g:6358-6364); controls from
the team menu, coach becomes player (g:6674-6683); game name "SHOOTOUT" (g:7082-7084).
**2.** The patched `UpdateTime` jumps straight to penalties at the first clock tick when
`g_PenaltyShootoutStartUp == -1` (asm:97799-97800, and at minute 120 asm:97946-97951). Ours:
call `StartPenalties()` (src/match_setup.c:753) from the mode's tick, as the arcade does
(src/arcade.c:1076); UNCONFIRMED that it is safe on the first frame, before `ST_GAME_IN_PROGRESS`.
`GS_GK_55_50 = 1` shortens the stoppage when the ball stands still (asm:101719-101721; ours
`_gsStoppageEventReload`, src/dos_gameplay.c:46, src/game_timers.c:305).
**Manual goalkeeper:** each frame `showClockScoresAndOthers` (g:17874-17950) decides whose keeper is
on the line and passes that player's stick (`pl1FinalStatus`) to `setPlayerGKControl` (g:16060):
direction → `g_JumpManually`, `g_JumpOrNot`, `g_JumpHeight` (high/low), `g_JumpDir2/6`, plus a
drawn arrow. The DOS code reads them in `ShouldGoalkeeperDive` (asm:106817-106831), the dive height
compare (asm:106306) and the dive direction (`UpdatePlayersAndBall`, per the comment at asm:251030). Ours:
`ShouldGoalkeeperDive` src/goalkeeper.c:160 (called src/update_players.c:785), `GoalkeeperJumping`
src/goalkeeper.c:561. The shoot-out tally HUD is `showPenaltyGoalsInfo` (g:16752).
**3.** None; the shoot-out's own result. **4.** `close()` (g:7229), back to the practice menu.
**5.** None.

### 2.3 Demo game (`demoGamePractice_OnSelect` g:11594)

**1.** `teamSelect = 0` (two random teams), `control1 = control2 = 1` (CPU v CPU), 0-0 at minute 0,
`g_DemoGame = 1` (g:11596-11605); `teamStarting = 1` (g:14204-14206); game name "DEMO GAME"
(g:7085-7087). Ours: the arcade SetupPlayers branch already makes a side with
`teamControls == TEAM_COMPUTER` CPU (src/game_init.c:819-826). **2.** F9 (spinning S) is ignored
(asm:7958). **3-5.** None.

## 3. CHALLENGES (scenario mode and the custom-challenge editor)

Entry `selectScenarioMode` g:29063 → `ScenarioGame::process` g:29024 (requires an internet log-in,
`scenarioModeLogIn` g:28426) → category → scenario (`scenarioModeMainMenu`) → `mainLoop` g:28943.
A scenario is a `Scenario` (struct g:556-617, also src/scenario.h).

### 3.1 The fields and where each lands

`ScenarioGame::getScenario` g:31825 reads an `[SCENARIO nnn]` section (ini:1-66 documents them).

| field | applied by | effect (SWOS 2020) | our site |
|---|---|---|---|
| TEAM FILE NAME, TEAM1/2 NAME | `set` g:1945-1947, `run` teamSelect 3 g:6714-6745 | teams from `challenges\<file>` (`.DIY`, `.EDT`); CUSTOM: two `.TEAM` files (`customLoadTeamFile` g:32176) | `arc_load_team`-like; the `.DIY`/`.EDT` reader is base (SWOS2020_MODS.md §2) |
| CPU TEAM | `set` g:2005-2010 | `control1/2` | SetupPlayers branch (src/game_init.c:808) |
| TEAM1/2 SCORE | `set` g:1949-1950 → asm:96617 | starting score | end of `InitGameRestoreTeams` |
| INITIAL TIME (0-120) | `set` g:1953-1970 → g:15444 | starting minute | same, `_gameTime` |
| SECOND LEG, TEAM1/2 FIRST LEG SCORE | `setScenarioSecondLeg` g:1909-1925 (called g:26309) | `secondLeg = 1`, `team1GoalsFirstLeg = first leg + starting score` | `_secondLeg`, `_team1/2GoalsFirstLeg` at src/game_init.c:988 |
| EXTRA TIME, PENALTY AFTER EXTRA TIME, OVERRIDE… | `set` g:1972-2002 → `etPen`; `setDiyParameters` g:26597-26635 (sym:2024) | `extraTimeState`/`penaltiesState`; `extraTimeState = -1` when ET and minute ≥ 90 (g:26627-26633); OVERRIDE = shoot-out start (`etPen = 4`, `g_PenaltyShootoutStartUp = -1`) | `_extraTimeState`, `_penaltiesState` at src/game_init.c:988 |
| MIN SUBS, MAX SUBS | `setScenarioSubsNotAllowed` g:26054 (D1/D2 into `InitializeInGameTeamsAndStartGame`, g:26310) | | `_minSubstitutes`/`_maxSubstitutes` (src/arcade.c:1155-1156) |
| GAME LENGTH | `set` g:2013-2027 | `g_gameLength` 0-3 | `_gameLength` (src/arcade.c:1157) |
| PITCH TYPE / PITCH SET | `set` g:2030-2045; `setPitchSet` g:28624 (7-Zip extracts a pitch set over the game's files, g:28595-28622) | | `_friendlyPitchType`; the pitch set is a DLC mod's files |
| TEAM PLAYING UP, TEAM STARTING | `InitGame_202` g:14221-14249; again at the shoot-out `StartPenalties_24` g:30734-30760 | sides and kick-off team, RANDOM by `srand(time)` | before src/match_setup.c:989; and after :767-768 for the shoot-out |
| CAMERA STARTING | `GameLoop_130` g:14173-14182 | camera starts at the top or bottom | UNCONFIRMED: our site not traced |
| SENT OFF / INJURY PLAYERS (+LEVELS) | `setCardsInjuriesForScenario` g:31628-31738 (team file `cardsInjuries = 1` red, `level << 5` injury); `setHidePlayersForScenario` g:19680 from `InitPlayersBeforeEnteringPitch_78` (sym:1996) moves the sprite to x = -20, y = 449, `sentAway = 1`, `cards = -1`, `playerState = 12` (g:19628-19640) | `PlayerFile.cardsInjuries` (include/game_types.h:589) before `InitAndPlayGame`; sprites after :989 |
| CPU INJURY SPEED | `runScenario` forces `g_EnableCPUInjury = 1` (g:28224) | an injured CPU player is slowed too (asm:105318-105323 `UpdatePlayerSpeed`) | `UpdatePlayerSpeed` src/ball_possession.c:684 |
| DIRECT RED CARD | g:28226-28231 | every foul a straight red (asm:107167, 107188, 107321, 107348) | src/tackle_foul.c:600-660 |
| CPU KEEPER LEVEL | `KeeperAI::set` g:516-534 | **disabled**: its hook is under `#if (0)` (g:31769-31822) | — |
| STARTING EVENT, CUSTOM FREE KICK X/Y | `setScenarioModeStartingEventTrigger` g:17704, called each frame from the HUD (g:18371) | after 3 s (left) / 5 s (right) of `frameCount`, scaled per style (`eventTriggerReady` g:17451-17467): a fake foul (`runAutoFreeKick` g:17470, spots asm:107761-107857, custom/penalty spot g:29573-29595), corner (g:17640), goal kick, throw-in (g:17498); RANDOM picks one (g:14251-14260) | gap G6 |
| WIN CONDITION, WIN SCORE1/2 | `scenarioWin` g:28477; golden/silver goal g:30762, g:30821 | below | below |
| DIFFICULTY (1-5), DESCRIPTION1-5, WIN MESSAGE, LOCKED | menus; stars drawn from DIFFICULTY (`getStarsWidth` g:29082) | display, locks | the mod's menus |

`runScenario` g:28211 also turns off auto replays and "all player teams equal" for the match
(g:28264-28268, restored g:28290-28294) and names the game "CHALLENGE" (g:28272).

**2. Each tick / event.**
- Golden goal: `endGoldenGoalGame` from `SWOS::UpdateTime_1` (g:15589; sym:2043): a goal in extra
  time (`extraTimeState == -1`) with the scores apart → whistle, `EndOfGame`, `winningTeamPtr`
  (g:30762-30787). Silver goal: `EndFirstExtraTimeEx` (g:30846) ends the match at the end of the
  first ET half if someone leads (g:30821-30844). Ours: `GoalScored` (src/goal_scored.c:172) and the
  minute-105 branch of `UpdateTime` (src/game_state.c:922-930).
- Normal end: `SWOS::UpdateTime_240` (mp:11187-11198; sym:1924) sets `g_ScenarioEndGame` at the
  final whistle; `SWOS::NextPenalty_20` (mp:11200-11215; sym:1920) at the end of a shoot-out, with
  `g_ScenarioPenaltyEndGame`. Ours: `EndOfGame` from `UpdateTime` (src/game_state.c:917, 966, 982)
  and `end_shootout` (src/goalkeeper.c:420). (The exact asm line of `UpdateTime, 240` is
  UNCONFIRMED; its body says final whistle.)
- In a shoot-out a sent-off player stays off: `StartPenalties_44/_60` clear `sentAway` only outside
  scenarios (g:35863-35889), and `GetPenaltyShooterSpriteForScenario` (g:27263) skips sent-away
  shooters. **Ours clears them all** (src/match_setup.c:775-785).
- The penalty switch `activatePenaltyShootoutMode` (g:26637) runs from the HUD at minute 120/90
  (g:17821-17837): it only turns on the manual keeper; the shoot-out itself is the engine's.

**3. Win / fail.** `scenarioWin` g:28477-28521: scores are `team1Goals/team2Goals` (the aggregate,
first leg included) or, after a shoot-out, the penalty goals. CUSTOM: exactly WIN SCORE1-WIN SCORE2;
AT LEAST DRAW: ≥ (≤ when the human is TEAM2); otherwise strictly ahead. Ours: `_team1TotalGoals`
(src/match_setup.c:954-957 includes the first leg) and `_team1PenaltyGoals`, as
`arc_match_winner` (src/arcade.c:1096). No timer, no score beyond pass/fail.
Observation, UNCONFIRMED in play: `set` derives `g_ExtraGame` from PENALTY AFTER EXTRA TIME
(g:1984-2001), but `run` sets `g_ExtraGame = 1` afterwards (g:6354), and `setDiyParameters` gives
`penaltiesState = 1` for every `etPen == 1` (g:26614-26617): a scenario with ET = YES, PEN = NO
looks like it still goes to penalties.

**4. Retry and after the match** (`mainLoop` g:28943-29022):
- `g_exitGameFlag != 0` (left before kick-off): nothing (g:29016-29018).
- Ended and won: online record (`insertNewScenarioRecord` g:28676, `readScenarioOnlineStats`,
  Discord post g:28758; skipped in the editor's TEST, `creationMode`), "YOU BEAT THE CHALLENGE"
  (g:28334), back to the scenario action menu.
- Lost, or quit mid-match: `retryScenarioMenu` g:28914 shows `retryExitEx.mnu`: QUICK RETRY
  (`g_RetryExit = 1`: versus, stadium and PLAY MATCH screens skipped), CHANGE TACTICS (`2`: PLAY
  MATCH shown), EXIT (`0`) (g:12764-12780, flags g:28233-28257). A retry replays from
  `retry_scenario` (g:28957) with the teams as last played (`playMatchTeam1/2Ptr`, g:6730-6741, so
  tactics changes stay) and re-applies the sent-off/injured list (g:31627 defines
  `UPDATE_setCardsInjuriesForScenario`, so g:31659-31662 reset then re-set every run).
- Arcade mapping: the `for (;;)` of `arc_start_game` (src/arcade.c:1184): `continue` = QUICK RETRY,
  CHANGE TACTICS = the same with the PLAY MATCH screen (i.e. not the SetupPlayers short-cut),
  `break` = EXIT; the replay-menu skip (src/game_loop.c:997-1008) is the mod owning the after-match.

**5. Persistent data.** **Completion is server-side only**: `get_scenario_completed_ex` and
`insert_new_scenario_record` on `galoreapi_v15.php` (g:26073-26128, g:28676-28738), counted per
user into `completedCnt` (g:1681-1694). No local file was found (UNCONFIRMED that none exists).
Locks are derived, not stored: a scenario is locked while any easier one in its category is
uncompleted (`setScenariosLocked` g:31122-31138); a category by `LOCKED = YES` (g:29843-29861;
a developer key file unlocks, g:31110). Category completion is all-completed (g:31595-31620).
The built-in list is `challenges\challenges.ini`, sections `Scenario 001`-`999` read by
`ScenarioGame::load` (g:1667-1708; case-insensitive section names by CSimpleIni, UNCONFIRMED).

### 3.2 The custom-challenge editor

`customChallengesMain.mnu` → CREATE / SHARE / PLAY (g:33529-33549); new or load
(g:33590, g:33843); edit (`customChallengesEdit.mnu`, handlers g:32592-33481). Defaults:
`customMakeDefaultData` g:31928-32012 (RETRO KICKERS v VILLAIN TEAM, 1-2 at 55', random free kick,
3 mins, difficulty 3). Fields: nickname (+ random name), the two teams and which is CPU, both
scores (±1/±10/reset), second leg and its scores, ET, pens, DIRECT PEN = OVERRIDE (sets minute 120,
g:33118-33163), subs min/max, sent-off/injury grid (`customChallengesSentOffInjury*.mnu`,
g:33933-34830), CPU injury + red card, attack goal, starting team, starting event (cycle
KICK OFF → RANDOM FREE KICK → … → RANDOM, g:33258-33274; locked when OVERRIDE or a kick-off minute,
`isKickOffInitialTime` g:35984), win condition (WIN BY ANY SCORE → AT LEAST DRAW → GOLDEN GOAL →
SILVER GOAL; golden/silver force ET and level aggregate scores, silver caps the minute at 104;
g:33276-33301), difficulty, uniforms, pitch type/set, TEST (plays it with `creationMode`, g:33314),
SAVE, DELETE. **File:** `customWriteData` g:32082-32163 writes `challenges\custom\work\challenge.ini`
(one section named after the scenario, every field above) and 7-Zips the work folder (that ini +
the two 684-byte `<team>.TEAM` files, g:32021-32032) into `challenges\custom\<name>.SCN`; SHARE
uploads it (`uploadSCNFile` g:35596, `uploadScenarioInfoToServer` g:35648).

## 4. SPEEDRUN

Entry `SWOS::speedrunMainMenu_onSelect` g:11222 → `challengeGame0Extras_OnSelect` (g:11936; it sets
`gameMode = 2`, `teamSelect = 4`, `control1 = 2, control2 = 1`, 0-0 at minute 0, g:11969-11977)
→ `run()` (g:11982).

**1. Before kick-off.**
- Category: `speedrunMain.mnu` → `g_Speedrun = 100 + g_CurrentGameMode` (g:6747-6759): 100 1 GOAL,
  101 HEADER, 102 3 GOALS, 103 TACKLE, 104 10 GOALS, 105 YELLOW CARD, 106 MATCH, 107 RED CARD
  (enum g:310-313, names src/PixelmapAndStrings.h:1391-1393).
- Difficulty (`selectSpeedrunDifficulty.mnu`, g:6833-6930): BEGINNER…MASTER = fixed `TEAM.072`
  pairs (1558 v 1548, 1571 v 1567, 1568 v 1570, 1566 v 1569, 1547 v 1557; g:6847-6871), or 5 =
  choose two teams, the difficulty then derived from the skill-point difference (≥375 beginner …
  ≤-375 master, g:6909-6924; `getTeamSkillPoints` g:7259) and confirmed (`difficultySetting.mnu`).
  YELLOW/RED CARD: always choose teams (`selectSpeedrunDifficultyCard.mnu`), difficulty 0
  (g:6773-6831). Human = team 1 v CPU (`control1 = 2, control2 = 1`).
- Match: `g_gameLength = 0` (3 mins), normal pitch, subs 3-5, no auto replays, frame counter 0
  (g:7037-7076); restored after (g:7104-7112). `teamStarting = 1`, the human kicks off
  (g:14197-14199). Bench closed while the ball is dead (asm:91779-91784, asm:93660-93665).

**2. Each tick / event** (`SWOS::UpdateTime_1` g:15583-15656, sym:2043):
- `checkSpeedrunWinningCondition` g:15495-15553 each tick until it fires, then `endSpeedrun`
  (g:15461-15482: whistle, `EndOfGame`, `winningTeamPtr`; MATCH mode just plays to full time).
  1/3/10 GOALS: `goalScored && statsTeam1Goals >= n` (g:15485-15493). HEADER: a goal while
  `g_LastPlayedComment == kHeader`, or the human team's `lastHeadingPlayer` is the first scorer
  (g:15502-15519). TACKLE: a goal while the last comment is good/dirty tackle (g:15524-15528).
  YELLOW / RED CARD: the last comment is a yellow / red card (g:15544-15549). MATCH:
  `gameState == kGameEnded` (g:15536-15538).
- **It keys off the commentary** (`g_LastPlayedComment`), not the events. Ours: header
  `DOS_SOUND(DOS_EV_HEADER)` src/headers.c:225, :381 and `player->heading` (:223) /
  `team->lastHeadingTacklingPlayer` (include/game_types.h:197); good tackle `TS_GOOD_TACKLE`
  (src/tackle_foul.c:155, :753, `DOS_EV_GOOD_TACKLE` :752); cards: `TryBookingThePlayer` /
  `TrySendingOffThePlayer` returning 0; goals: `GoalScored`.
- The timer: `g_FrameCount` counts recorded highlight frames (`addTimeIndexNow`,
  src/replays/replays.cpp:2569-2574, called once per frame :3254). Start = the frame the clock reads
  0:00 in `kGameStarting` (g:15635-15644); stop when the condition fires (g:15620-15630) or at the
  result. Shown as `frames / fps(style)` (70 DOS, 50 Amiga, 60 the others; `getFrameTimerString`
  g:2143-2176) by `showSpeedrunTimerInfo` (g:16442). **But the record is `g_ElapsTime`, wall-clock
  `clock()`** (g:16452), and that is what decides a best (php:1810-1815). Ours: count frames from
  kick-off deterministically, as `s_arcFramesPlayed` (src/arcade.c:1066-1074).

**3. Win / fail.** Decided in the HUD at `kResultAfterTheGame` by `showSpeedrunShowResult`
(g:13368-13530): goal modes need exactly n goals for team 1 (g:13394-13420), MATCH a win
(g:13422-13428), HEADER/TACKLE the last comment, cards `g_SpeedrunNormallyEnded`.
`g_SpeedrunResult` 1 = completed, 2 = failed.

**4. Retry and after the match.** Won: "RECORD AND REPLAY WILL BE UPLOADED" if logged in,
`cPHPManager.saveRecordData(...)` then `viewMyRecordsMenu` (g:7130-7172). Not won and the timer ran:
`retryExit.mnu` TRY AGAIN / EXIT (g:7175-7222); TRY AGAIN restores the two teams
(`srTeam1/2Buffer`, g:7006-7007) and jumps to `speedrun_retry` (g:7011) — straight to `InitTeams();
PlayFriendly();`; which pre-match screens show then is UNCONFIRMED. Leaving the difficulty menu goes
back to the category menu (g:6826-6830, g:6840-6845). Arcade mapping: as challenges (§3.1.4).

**5. Persistent data.** `SpeedrunRecord speedrunRecords[5][8]` (difficulty x category;
php.h:5-14, :57): `played`, `bestRecord`, `currentRecord` (doubles, seconds), `rank`,
`bestReplayFilename[64]`, `currentReplayFilename[64]`, and best/current frame start/stop/elapsed.
**File:** `records\offline_v2x_<style>.dat` offline, `records\<user>_<style>.dat` logged in — a
raw `fwrite` of the 40 structs, category-major (`for j<8 for i<5`; php:1666-1725), so its layout is
the MSVC struct's (padding UNCONFIRMED). New file: best/current 10000, frames 99999 (php:1640-1663).
`saveRecordData` (php:1788-1860) counts `played`, keeps the best when `new <= best`, replaces the
replay files, and online syncs (`syncRow_v2`), deletes old replays and posts to Discord.
Leaderboard and MY RECORDS sync/reset are server calls (g:7954-8087, g:8953).

## 5. Gaps and the smallest hook for each

"Match state" = it changes what the simulation does, so it must be deterministic, register with
`src/sim_state.c` (MODS.md §8.6) and keep the mod offline until the mod list travels in the
handshake. A frame may be re-simulated (src/netplay_match.c), so an event hook must not act twice.

**G1 Sides and kick-off team before the players are placed.** SWOS 2020: `InitGame_202`
g:14186-14262. Ours picks them by `Rand()` at src/match_setup.c:916-917 and places the players from
them in `StartingMatch` (:989 → :375); the end of `InitGameRestoreTeams` is too late. *Hook:*
`ModHooks_MatchSides(&teamPlayingUp, &teamStarting)` at src/match_setup.c:918, and the same after
`StartPenalties`'s draw (:768) for the shoot-out (SWOS 2020's `StartPenalties_24`, g:30734). Writes
two words; match state; the mod's choice made before frame 0 (no `srand(time)`).

**G2 Starting state the post-:989 site needs help with.** Minute: also refresh the clock sprite
(g:15444-15448). A start at or past a period boundary skips that boundary, because `UpdateTime`
tests minute equality (src/game_state.c:847-935): start ≥ 46 never swaps sides (`_halfNumber`
stays 1); start ≥ 90 with ET never calls `StartFirstExtraTime` (SWOS 2020 accepts this and marks
`extraTimeState = -1`, g:26627-26633). *Hook:* H2 as an API, `MatchSetup_Start(score1, score2,
minute, half)` called from the post-:989 site, which sets the digits, `_gameTime`, `_halfNumber` and
`_extraTimeState` consistently. ET / pens / legs: a sibling of `NetplaySetup_TakeTieRules`
(src/game_init.c:988). Match state.

**G3 Players sent off or injured before kick-off, and in the shoot-out.** Ours clears sprite cards
at :407-411 and again at `StartPenalties` (:775-785). SWOS 2020 re-hides them every time players
enter (g:19628-19640) and skips them as shooters (g:27263-27317). *Hook:* `MatchSetup_SentOff(team,
shirt)` kept by the engine and applied in `InitPlayersBeforeEnteringPitch` (src/match_setup.c:369,
after :411) and `StartPenalties` (after :785); injuries go in the team file's `cardsInjuries`
before `InitAndPlayGame`. Writes sprites; match state.

**G4 Removing outfielders (training lines, CPU v GK).** SWOS 2020: `z = 20000, sentAway = 1`
per player (g:19541-19611) plus z checks in AI, closest-player and tackling (asm:118580, 118640,
119924, 106477, 107064). *Hook:* `ModHooks_PlayerPresent(team, ordinal) -> bool`, asked at
`InitPlayersBeforeEnteringPitch` (src/match_setup.c:369); an absent player is parked off the pitch
with `sentAway = 1`. UNCONFIRMED whether our AI (src/ai.c:255, :327) and tackling
(src/update_players.c:1950) already ignore `sentAway` players; if not, the same predicate is asked
there. Match state.

**G5 Ghost players (playing but not drawn).** SWOS 2020 writes `visible = 0` (g:19556-19560). Ours
registers `Sprite.visible` as match state (src/sim_state.c:299, :311), so that write would change the
draw list in the rollback state. *Hook:* a draw-time filter `ModHooks_SpriteHidden(sprite)` in the
sprite draw loop. Cosmetic: not match state, may run on re-simulated frames because it only draws.

**G6 Forced restart at x,y (drills, starting events, custom free kick, penalty kick).** SWOS 2020 fakes
a foul (asm:107720-107857, g:17470-17491, g:29573-29595) or an out-of-play (asm:111787-111856,
g:17640-17700), and drives it from the SDL keyboard (ctl:1874-2172) or from the HUD routine
(g:18371). *Hook:* `MatchRestart_Force(kind, team, x, y)` — kind = free kick, penalty, corner, goal
kick, throw-in, kick-off — callable only from the mode's tick (src/game_loop.c:786), built on
`TestFoulForPenaltyAndFreeKick` (src/tackle_foul.c:768), `CheckIfBallOutOfPlay`
(src/game_timers.c:637) and `PrepareForInitialKick` (src/match_setup.c:720). Drill keys must reach
the tick as frame input (`MatchPlayerInput.key`, include/match_step.h:30-33), never from the event
loop. The 3 s / 5 s delay (g:17451-17467) becomes a frame count in the mode. Match state.

**G7 Frozen clock.** SWOS 2020 zeroes `gameTime` in a HUD routine every frame (g:17998-18003).
*Hook:* a ruleset flag `clock_stopped`, read in `UpdateTime` before the accumulator
(src/game_state.c:807-812). Match state.

**G8 Manual goalkeeper dive.** SWOS 2020 reads the stick in the HUD (g:17894-17950, g:16060-16103)
and overrides `ShouldGoalkeeperDive` (asm:106817-106831), the dive height (asm:106306) and
direction. *Hook:* `ModHooks_KeeperDive(keeper, team, &dive, &high, &dir)` at the end of
`ShouldGoalkeeperDive` (src/goalkeeper.c:160) and in `GoalkeeperJumping` (:561), fed from the
frame's input for that keeper's human. Match state. The arrow and "LOW RIGHT" text are FrameDrawn.

**G9 Rule overrides scoped to the match.** Restart timers (`GS_825_750 = 0`, g:19704-19726;
`GS_GK_55_50 = 1`, g:6363; kick-off `stoppageEventTimer = 1`, asm:103830), no cards (asm:107336,
107356), every foul a red (asm:107167-107348), tackles off (asm:106485, 107072), tackle injuries off
(g:11540-11542), CPU injuries slow (asm:105318). Ours: the style constants `_gsKickoffStoppage`,
`_gsStoppageEventReload` (src/dos_gameplay.c:45-46) are per style, and a mod writing them would leak
past the match; the no-cards rule exists as a test rule (include/test_rules.h). *Hook:* the ruleset
of MODS.md §8.3 with these fields — `kickoff_stoppage`, `dead_ball_stoppage`, `cards` (normal /
none / straight red), `tackles`, `tackle_injuries`, `cpu_injury_slowdown` — read at src/game_timers.c:305
and :496, src/tackle_foul.c:600-660, src/update_players.c:1950-1991, src/ball_possession.c:684.
Match state; it travels with the mod.

**G10 End the match now, with a winner.** Golden goal, silver goal, a speedrun target reached:
SWOS 2020 calls `EndOfGame` and sets `winningTeamPtr` (g:30772-30786, g:15461-15482). The arcade
ends with `_playGame = 0` (src/arcade.c:1059, :1079), which skips the result screen. *Hook:*
`MatchEnd_Now(winner)` from the tick: `_winningTeamPtr` + `EndOfGame` (src/match_setup.c:549).
Plus a period-end event (H3) so silver goal needs no minute polling. Match state.

**G11 Classified events.** Speedrun reads the commentary (g:15495-15553); ours has no comment state
in the sim. *Hook:* H3 with a payload: goal (team, scorer, header = scorer's `heading` /
`lastHeadingTacklingPlayer`, penalty, own goal; from `GoalScored`'s `goalType`,
src/goal_scored.c:99), tackle (good / foul, src/tackle_foul.c:155, :753), card (yellow / red, the
two `Try*` returning 0), period end, shoot-out end. Read-only; never on a re-simulated frame (or
the mod counts only confirmed frames).

**G12 Result and after-match owned by the mode.** SWOS 2020 decides speedrun results in the HUD
(g:13368-13530) and routes retries itself (g:28914, g:7175). *Hook:* H4 at
`end_of_match_flow` (src/game_loop.c:987): the mode returns "skip the friendly replay menu" (as
:997-1008 does for the arcade) and gets the result; retry is its own loop around `InitAndPlayGame`,
as `arc_start_game` (src/arcade.c:1184-1258). Not match state.

**G13 A mode's match name, screens and HUD.** "CHALLENGE", "SPEEDRUN", "TEAM TRAINING", "SHOOTOUT",
"DEMO GAME" (g:7064-7087, g:28272); no result banner / spinning S / VIEW OPPONENT in training
(asm:98265-98270, 103336, 52244); the timer, shoot-out tally and win banner (g:16442, 16752,
13533). *Hook:* `_gameName` from the mode (the arcade's `_aArcadeMatchName`, src/game_init.c:966-970,
generalised); the HUD through `ModHooks_FrameDrawn` (include/mod_hooks.h). Not match state.

**G14 Persistent records and stars.** Speedrun: a raw struct dump (php:1697-1725); challenges: server
only. *Hook:* H8 (`Mods_SavePath(slug, name)`), JSON of named fields (CLAUDE.md "Save/load is typed
JSON"), never the struct. Not match state.

**G15 Setup randomness.** `srand(time(NULL))` for sides, starting event, kick-off team (g:14231,
17716, 30748); MODS.md §16 forbids taking the game's `Rand`. *Hook:* none new: the mod draws with its
own generator before frame 0 and passes values through G1/G2; the recording keeps them (MODS.md §8.6).

Radar in training (`m_radarInTeamTraining`, options.cpp:97, :1304) is commented out at its key
(ctl:1876-1879): not ported. CPU KEEPER LEVEL is disabled (g:31769). CAMERA STARTING needs our
initial-camera site found (UNCONFIRMED).

## 6. Summary: every gap, merged

| # | gap | modes | SWOS 2020 | proposed hook (fires at) | may read / write | match state? |
|---|---|---|---|---|---|---|
| G1 | sides and kick-off team before placement; again at the shoot-out | P (training, demo), C, S | g:14186-14262, g:30734 | `ModHooks_MatchSides` (src/match_setup.c:918, :768) | writes `_teamPlayingUp`, `_teamStarting` | yes |
| G2 | starting score/minute/half/ET/legs consistent with `UpdateTime` | P (shootout), C | g:1936-2045, g:26597-26635, asm:96617 | `MatchSetup_Start` (after :989) + tie rules beside src/game_init.c:988 | writes score digits, `_gameTime`, `_halfNumber`, ET/pens/legs | yes |
| G3 | pre-sent-off/injured players kept off, also in the shoot-out | C | g:31628-31738, g:19628, g:27263, g:35863 | `MatchSetup_SentOff`, applied in src/match_setup.c:369 and :753 | writes sprites, team-file `cardsInjuries` | yes |
| G4 | outfielders absent (lines off, CPU only GK) | P (training) | g:19541-19611, asm:118580/119924/106477/107064 | `ModHooks_PlayerPresent` (src/match_setup.c:369; AI/tackle if needed) | parks sprites, `sentAway` | yes |
| G5 | ghost players (play, not drawn) | P (training) | g:19550-19566 | `ModHooks_SpriteHidden` (sprite draw) | draws only | no |
| G6 | forced restart at x,y: drills, starting events, custom FK, penalty | P (training), C | asm:107720-107857, asm:111787-111856, g:17470-17700, g:29573, ctl:1874-2172 | `MatchRestart_Force` (from the tick, src/game_loop.c:786); keys via `MatchPlayerInput.key` | writes restart state | yes |
| G7 | frozen clock | P (training) | g:17998-18003 | ruleset `clock_stopped` (src/game_state.c:807) | read by `UpdateTime` | yes |
| G8 | goalkeeper dive under the human's control | P (shootout), C (pens) | g:16060, g:17894-17950, asm:106817/106306 | `ModHooks_KeeperDive` (src/goalkeeper.c:160, :561) | writes the dive decision, from frame input | yes |
| G9 | scoped rule overrides: restart timers, cards, tackles, injuries, CPU injury slowdown | P, C, S | g:19704, g:6363, asm:103830, 107336, 107167, 106485, 105318, g:11540 | ruleset fields (MODS.md §8.3) at src/game_timers.c:305/496, src/tackle_foul.c:600-660, src/update_players.c:1950, src/ball_possession.c:684 | read by the engine | yes |
| G10 | end now with a winner (golden/silver goal, speedrun target) + period-end event | C, S | g:30762-30844, g:15461-15482 | `MatchEnd_Now(winner)` from the tick | writes `_winningTeamPtr`, calls `EndOfGame` | yes |
| G11 | classified events (header goal, good tackle, card colour, period/shoot-out end) | S, C | g:15495-15553 (commentary), mp:11187-11215 | H3 payload from `GoalScored`, tackle_foul.c, `UpdateTime`, `NextPenalty` | read-only | no (confirmed frames only) |
| G12 | the mode owns the result and the after-match (retry / quick retry / change tactics / exit) | P, C, S | g:28914-29022, g:7130-7228, g:13368 | H4 at src/game_loop.c:987-1008; retry = the mode's loop as src/arcade.c:1184 | result read-only; returns where to go | no |
| G13 | match name, hidden HUD items, mode HUD (timer, tally, banners) | P, C, S | g:7064-7087, asm:98265, g:16442, g:16752 | mode `_gameName` (src/game_init.c:966); `ModHooks_FrameDrawn` | draws only | no |
| G14 | records, completion, stars on disk | C, S | php:1666-1725; server for C | H8 `Mods_SavePath`, named-field JSON | the mod's file | no |
| G15 | setup randomness reproducible | P, C | g:14231, 17716, 30748 | none: mod generator before frame 0, values through G1/G2 | — | its outputs are |

Already covered by the arcade's sites, no new hook: team loading and controls (src/arcade.c:937,
src/game_init.c:808-830; the `.DIY`/`.EDT` reader is base), pitch type, subs, game length
(src/arcade.c:1150-1157), the shoot-out start from the tick (src/arcade.c:1076; UNCONFIRMED on the
first frame), a deterministic frame timer (src/arcade.c:1066-1074), goal and card events at the
named sites (read-only use), the winner (src/arcade.c:1096).
