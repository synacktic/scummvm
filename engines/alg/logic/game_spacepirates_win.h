/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef ALG_GAME_SPACEPIRATES_WIN_H
#define ALG_GAME_SPACEPIRATES_WIN_H

#include "common/hashmap.h"
#include "common/path.h"

#include "audio/audiostream.h"

namespace Graphics {
struct Cursor;
}

#include "alg/game.h"

namespace Alg {

namespace SPWin {
struct StateDef;
struct RecordDef;
struct ArmDef;
struct ActionDef;
} // End of namespace SPWin

/**
 * Space Pirates as reissued for Windows by Digital Leisure in 2003.
 *
 * That release is a fresh program rather than a port of the DOS game: one
 * MPEG-1 file per location, driven by a numeric state machine compiled into
 * SP.exe. The whole machine - 124 states, their interaction records, every
 * position- and click-triggered transition, the menus, the pools and the
 * pickers - was recovered from the executable and is documented in
 * doc/alg-reelmagic/SP-STATE-MACHINE.md. The tables in
 * game_spacepirates_win_data.h are generated from that recovery, and this
 * class is an interpreter for them that follows the original's control flow:
 * a per-tick position ladder, a record window lifecycle with a difficulty-
 * scaled hit box and grace period, an end-of-clip sequencer, and the death /
 * scold comment pickers.
 *
 * Known presentation differences from the original, all deliberate:
 * the bullet/score HUD is drawn on the frame instead of in a separate child
 * window; the light-gun (DirectInput) path is not implemented; window sizing
 * and fullscreen belong to ScummVM.
 */
class GameSpacePiratesWin : public Game {
public:
	GameSpacePiratesWin(AlgEngine *vm, const AlgGameDescription *gd) : Game(vm) {}
	~GameSpacePiratesWin() override;

	Common::Error run() override;
	void init() override;
	bool saveState(Common::OutSaveFile *outSaveFile) override;
	bool loadState(Common::InSaveFile *inSaveFile) override;

protected:
	void drawInterface() override;
	void debug_drawZoneRects() override;

private:
	// ---- playback ---------------------------------------------------------
	bool renderClip(const Common::String &clip, int32 seekTo = -1);
	void seekFrame(int32 frame);
	void setRun(bool run);
	uint32 pos() const;
	bool clipEnded();

	// ---- the machine ------------------------------------------------------
	void dispatch(int state);              // FUN_00403690
	void armRecords(int state, int variant);
	void tick();
	void tickRecords();
	void openWindow(const SPWin::RecordDef *r);
	void closeWindow(bool viaHit);
	void advanceRecord();
	void runArm(const SPWin::ArmDef &arm);
	bool tickSpecial();
	void autoPlayFinal();
	bool clickSpecial(int xPct, int yPct);
	void runActions(uint16 first, uint16 num);
	bool armGuardsPass(const SPWin::ArmDef &arm) const;
	void onClipComplete();                 // FUN_00407840
	void completeWorld(uint32 flagAddr);   // 62/63/64, 96, 103, 91, 185/84
	void toWorldsMenu();                   // FUN_00407090
	void sequenceChain(const uint32 *flags, int n, const int16 *next,
	                   int16 lastLoop, int16 lastDone);

	// ---- pools / pickers --------------------------------------------------
	void pickScene1Pool();                 // FUN_00406fb0
	void pickReaperPool();                 // FUN_00406db0
	void resetScene1Pool();                // FUN_00407030
	void resetReaperPool();                // FUN_00406e60
	void awardPicker();                    // FUN_004033f0
	void playOutcome(const Common::String &clip, int penalty); // FUN_00402a20
	void deathPicker();                    // FUN_00407290
	void scoldPicker();                    // FUN_00407740

	// ---- input ------------------------------------------------------------
	void handleClick(int xPct, int yPct);  // FUN_0040cbf0
	void handleRightClick();
	void handleKey(Common::KeyCode key);
	Common::Rect liveBoxFor(const SPWin::RecordDef *r) const;
	bool boxHit(int x1, int x2, int y1, int y2, int xPct, int yPct) const;

	// ---- menus / front matter ---------------------------------------------
	enum Screen {
		kScreenNone = 0,      // a state is active
		kScreenAttract,       // SPIntro playing, or the game-over card
		kScreenMain,          // MENUS/Main.mpg @15, paused         (0x415598)
		kScreenDifficulty,    // MENUS/Difficulty.mpg @15, paused   (0x415594)
		kScreenContinue,      // MENUS/Continue.mpg @15, paused     (0x41559c)
		kScreenTrailers,      // MENUS/Trailers.mpg @5, paused      (0x4155a0)
		kScreenTrailerPlaying,//                                     (0x4155a4)
		kScreenWorlds         // World Menu/worldsmenu.MPG          (0x4154e0)
	};
	void boot();                           // FUN_004081b0
	void gameOverToAttract();              // FUN_004071d0
	void showStill(const Common::String &clip, uint32 frame);
	void showMainMenu();                   // FUN_00408540
	void showDifficulty();                 // FUN_004084b0
	void showContinue();                   // FUN_004085d0
	void showTrailersMenu();               // FUN_00407150
	void startNewGame();                   // FUN_00408650 (+ FUN_00408220 reset)
	void jumpToState(int state);           // debug: -b N / number keys
	void resetForNewGame();                // FUN_00408220
	void menuClick(int xPct, int yPct);

	// ---- presentation ------------------------------------------------------
	void drawShotFeedback();
	void drawBox(int16 x, int16 y, int16 w, int16 h, uint8 color, bool filled);
	void drawText(const Common::String &text, int16 x, int16 y, uint8 color);
	void drawCrosshair(int16 x, int16 y);
	Audio::SeekableAudioStream *loadWavFile(const Common::Path &path);
	void playSfx(Audio::SeekableAudioStream *sfx);

	// ---- state -------------------------------------------------------------
	int _state = 0;                        // 0x41524c
	int _variant = 0;                      // 0x4153b0
	int _uiScreen = kScreenAttract;
	Common::String _curClip;

	int _lives = 3;                        // 0x4153fc
	int _shots = 6;                        // 0x415444
	int _difficulty = 1;                   // 0x415448: 1 easy, 2 hard
	int _continues = 0;                    // 0x415400
	int32 _score = 0;                      // 0x4153f0
	int32 _scoreEasy = 0, _scoreHard = 0;  // 0x4153f4 / 0x4153f8
	uint32 _shotsFired = 0;                // 0x415410

	// counters the machine branches on
	int _hitCount = 0;                     // 0x415460
	int _reaperPass = 0;                   // 0x41546c
	int _reaperKills = 0;                  // 0x415130
	int _reaperPick = 0;                   // 0x415148
	int _stateKills = 0;                   // 0x415464

	// every named binary flag, keyed by its SP.exe address
	Common::HashMap<uint32, int> _flags;
	int flag(uint32 addr) const;
	void setFlag(uint32 addr, int v);

	bool _poolS1Used[4];
	bool _poolReaperUsed[9];
	bool _awardUsed[3];                    // 0x415208/20c/210
	bool _finalLock = false;               // 0x415120

	// record window lifecycle
	const SPWin::RecordDef *_curRecords[24];
	int _numRecords = 0, _recIdx = 0;
	bool _windowOpen = false;
	bool _hitRegistered = false;           // 0x4154a4
	bool _graceArmed = false;              // 0x415564
	uint32 _graceUntil = 0;
	bool _frozen = false;                  // video paused for the grace period

	// outcome / comment clips
	bool _outcomePlaying = false;          // 0x41558c
	int _outcomePenalty = 0;               // 0x415248
	bool _commentPlaying = false;          // 0x415484 (dc clip, lives left)
	bool _terminalPlaying = false;         // 0x415590 (dc20 / last-life scold)
	bool _ecHandled = false;
	uint32 _seekFloor = 0;
	uint32 _freezeAtPos = 0;

	// input pacing
	uint32 _fireReadyAt = 0;               // timer 0xD0: 200ms between shots
	uint32 _emptyReadyAt = 0;              // timer 0xC9: empty-click cue limit
	uint32 _flashUntil = 0, _hitMarkUntil = 0;
	Common::Point _flashAt, _hitAt;

	Audio::SeekableAudioStream *_gunSound = nullptr;
	Audio::SeekableAudioStream *_emptySound = nullptr;

	void loadCursors();
	void setGunCursor(bool empty);
	Graphics::Cursor *_cursorLoaded = nullptr;
	Graphics::Cursor *_cursorEmpty = nullptr;
	int _cursorIsEmpty = -1;

	// debug
	bool _debugJumpEnabled = true;
	bool _autoPlay = false;
	int _lastAutoShot = -1;
	uint32 _armCooldownPos = 0;
	const char *_lastArmEv = nullptr;
};

} // End of namespace Alg

#endif
