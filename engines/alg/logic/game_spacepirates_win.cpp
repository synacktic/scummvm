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

#include "common/config-manager.h"
#include "common/events.h"
#include "common/file.h"
#include "common/fs.h"
#include "common/savefile.h"
#include "common/system.h"

#include "audio/decoders/wave.h"

#include "graphics/font.h"
#include "graphics/fontman.h"
#include "graphics/cursor.h"
#include "graphics/cursorman.h"
#include "graphics/surface.h"

#include "image/icocur.h"

#include "alg/alg.h"
#include "alg/logic/game_spacepirates_win.h"
#include "alg/logic/game_spacepirates_win_data.h"
#include "alg/video.h"

namespace Alg {

// Addresses of the binary's named globals, used as flag keys so every access
// can be checked against doc/alg-reelmagic/SP-STATE-MACHINE.md.
static const uint32 kFlagWorldDoors = 0x414ae4;
static const uint32 kFlagWorldMountain = 0x4151d8;
static const uint32 kFlagWorldReaper = 0x415150;
static const uint32 kFlagWorldScrapyard = 0x414a7c;
static const uint32 kFlagWorldFellina = 0x415264;
static const uint32 kFlagFinal684 = 0x415684;
static const uint32 kFlag413034 = 0x413034;

// The sequencing chains the end-of-clip handler walks (FUN_00407840).
static const uint32 kAsteroidFlags[5] = { 0x415670, 0x415674, 0x415678, 0x41567c, 0x415680 };
static const int16 kAsteroidNext[4] = { 400, 401, 402, 403 };
static const uint32 kFellinaFlags[6] = { 0x415644, 0x415648, 0x41564c, 0x415650, 0x415654, 0x415658 };
static const int16 kFellinaNext[5] = { 86, 87, 88, 89, 90 };

GameSpacePiratesWin::~GameSpacePiratesWin() {
	delete _gunSound;
	delete _emptySound;
	delete _cursorLoaded;
	delete _cursorEmpty;
}

void GameSpacePiratesWin::loadCursors() {
	struct Load {
		const char *file;
		Graphics::Cursor **into;
	} loads[2] = {
		{ "cursor1.cur", &_cursorLoaded },
		{ "cursor2.cur", &_cursorEmpty },
	};
	for (int i = 0; i < 2; i++) {
		Common::File f;
		if (!f.open(loads[i].file)) {
			warning("SPWin: missing %s", loads[i].file);
			continue;
		}
		Image::IcoCurDecoder dec;
		if (dec.open(f) && dec.numItems() > 0)
			*loads[i].into = dec.loadItemAsCursor(0);
	}
}

void GameSpacePiratesWin::setGunCursor(bool empty) {
	Graphics::Cursor *c = empty ? _cursorEmpty : _cursorLoaded;
	if (_cursorIsEmpty == (empty ? 1 : 0))
		return;
	_cursorIsEmpty = empty ? 1 : 0;
	if (c) {
		CursorMan.replaceCursor(c);
		CursorMan.showMouse(true);
	}
}

void GameSpacePiratesWin::init() {
	Game::init();

	// The clips live in subdirectories ("Doors/Scene 1", "World/...").
	const Common::FSNode gameDataDir(ConfMan.getPath("path"));
	SearchMan.addDirectory(gameDataDir, 0, 4, false);

	_videoDecoder = new AlgMpegDecoder();
	AlgMpegDecoder *mpeg = (AlgMpegDecoder *)_videoDecoder;
	mpeg->setDisplaySize(_screen->w, _screen->h);
	// SP.exe seeks with IMediaSeeking in TIME_FORMAT_FRAME: every number in
	// the recovered tables is a frame at 29.97fps.
	mpeg->setFrameUnits(true);

	_gunSound = loadWavFile("GUNSHOT.wav");
	_emptySound = loadWavFile("nobullets.wav");

	// The interface draws in palette indices and the ReelMagic composite
	// converts them through _palette - which nothing fills for this game, so
	// define the handful of colours the HUD and feedback use.
	static const byte kUiPal[8][3] = {
		{ 255, 255, 255 }, // 240 white
		{ 255, 232, 128 }, // 241 flash core
		{ 255, 128, 32 },  // 242 flash rim / bullets
		{ 224, 32, 32 },   // 243 red (lives, live target box)
		{ 64, 255, 64 },   // 244 green (debug: click arms)
		{ 64, 200, 255 },  // 245 cyan (debug text)
		{ 255, 64, 255 },  // 246 magenta (debug: grace)
		{ 24, 24, 24 },    // 247 bullet-hole grey
	};
	for (int i = 0; i < 8; i++) {
		_palette[(240 + i) * 3 + 0] = kUiPal[i][0];
		_palette[(240 + i) * 3 + 1] = kUiPal[i][1];
		_palette[(240 + i) * 3 + 2] = kUiPal[i][2];
	}
	_paletteDirty = true;

	// The original's own crosshairs: cursor1.cur while loaded, cursor2.cur
	// once the gun is empty (SetCursor at 0x401040).
	loadCursors();
	setGunCursor(false);

	for (int i = 0; i < 4; i++)
		_poolS1Used[i] = false;
	for (int i = 0; i < 9; i++)
		_poolReaperUsed[i] = false;
	for (int i = 0; i < 3; i++)
		_awardUsed[i] = false;

	_autoPlay = ConfMan.hasKey("spwin_auto_play") && ConfMan.getBool("spwin_auto_play");

	_gameInProgress = true;
	boot();

	// Debug: boot straight into a state ("spwin_boot_state=N" in scummvm.ini),
	// with the machinery reset as if a game had been started.
	// Jump straight into a state: "scummvm -b N spwin" or
	// spwin_boot_state=N in the ini. In-game, the number keys do the same.
	if (ConfMan.hasKey("spwin_boot_state")) {
		jumpToState(ConfMan.getInt("spwin_boot_state"));
	} else if (ConfMan.hasKey("boot_param")) {
		jumpToState(ConfMan.getInt("boot_param"));
	}
}

void GameSpacePiratesWin::jumpToState(int state) {
	const int diff = _difficulty;
	resetForNewGame();
	_difficulty = diff;
	if (state == 9 || state == 201 || (state >= 65 && state <= 74)) {
		// The endgame presumes a finished game.
		setFlag(kFlagWorldDoors, 1);
		setFlag(kFlagWorldMountain, 1);
		setFlag(kFlagWorldReaper, 1);
		setFlag(kFlagWorldScrapyard, 1);
		setFlag(kFlagWorldFellina, 1);
		setFlag(0x415628, 1); // green crystal
	}
	if (state == 0) {
		// 0 = the worlds hub, with the doors world behind you.
		setFlag(kFlagWorldDoors, 1);
		toWorldsMenu();
		return;
	}
	debug(1, "jump to state %d", state);
	dispatch(state);
	setRun(true);
}

// ---------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------

bool GameSpacePiratesWin::renderClip(const Common::String &clip, int32 seekTo) {
	AlgMpegDecoder *mpeg = (AlgMpegDecoder *)_videoDecoder;
	_curClip = clip;
	_frozen = false;
	_ecHandled = false;
	debug(1, "render %s%s", clip.c_str(),
	      seekTo >= 0 ? Common::String::format(" @%d", seekTo).c_str() : "");
	_seekFloor = seekTo > 0 ? (uint32)seekTo : 0;
	_freezeAtPos = 0;
	mpeg->loadVideoFile(Common::Path(clip, '/'), seekTo > 0 ? (uint32)seekTo : 0);
	return true;
}

void GameSpacePiratesWin::seekFrame(int32 frame) {
	// A DirectShow seek amounts to reopening the stream at an entry point for
	// these files, which is exactly what the decoder does.
	AlgMpegDecoder *mpeg = (AlgMpegDecoder *)_videoDecoder;
	_seekFloor = frame > 0 ? (uint32)frame : 0;
	mpeg->loadVideoFile(Common::Path(_curClip, '/'), frame > 0 ? (uint32)frame : 0);
	_frozen = false;
	_ecHandled = false;
}

void GameSpacePiratesWin::setRun(bool run) {
	_frozen = !run;
	AlgMpegDecoder *mpeg = (AlgMpegDecoder *)_videoDecoder;
	mpeg->pauseAudio(_frozen);
}

uint32 GameSpacePiratesWin::pos() const {
	// The decoder can only enter a stream at the GOP before the requested
	// frame, so a seek lands up to ~0.6s early. DirectShow reports the
	// requested position immediately after SetPositions, and the machine
	// depends on that (a seek past a window must not land inside it), so the
	// reported position is floored at the last seek target.
	const uint32 raw = _videoDecoder->getCurrentFrame();
	return MAX(raw, _seekFloor);
}

bool GameSpacePiratesWin::clipEnded() {
	AlgMpegDecoder *mpeg = (AlgMpegDecoder *)_videoDecoder;
	return !_frozen && mpeg->isFinished();
}

// ---------------------------------------------------------------------------
// The dispatcher (FUN_00403690): render the state's clip, seek, arm records
// ---------------------------------------------------------------------------

void GameSpacePiratesWin::dispatch(int state) {
	_state = state;
	_uiScreen = kScreenNone;
	const SPWin::StateDef *def = nullptr;
	for (uint i = 0; i < ARRAYSIZE(SPWin::kStates); i++) {
		if (SPWin::kStates[i].id == state) {
			def = &SPWin::kStates[i];
			break;
		}
	}
	if (!def) {
		// FUN_00403690 has no block for some states (350, 250, the award
		// router pseudo-states): the original falls straight through, leaving
		// the current clip playing - the caller's SEEK does the work.
		debug(1, "state %d has no dispatcher block; clip continues", state);
		return;
	}
	debug(1, "dispatch state %d", state);
	renderClip(def->clip, def->seek);
	_numRecords = 0;
	_recIdx = 0;
	_windowOpen = _hitRegistered = _graceArmed = false;
	if (def->armRecords)
		armRecords(state, _variant);
	// RenderFile leaves the graph playing; only the menu screens pause, and
	// they do it explicitly after dispatching.
	setRun(true);
	// The state-9 dispatcher block arms the final level's positional
	// right-click rule (0x413034).
	if (state == 9)
		setFlag(kFlag413034, 1);
	_lastArmEv = nullptr;
}

void GameSpacePiratesWin::armRecords(int state, int variant) {
	_numRecords = 0;
	_recIdx = 0;
	for (uint i = 0; i < ARRAYSIZE(SPWin::kRecords) && _numRecords < 24; i++) {
		const SPWin::RecordDef &r = SPWin::kRecords[i];
		if (r.state != state)
			continue;
		// The pool states (984, 225) arm exactly one variant's record.
		if ((state == 984 || state == 225) && r.variant != variant)
			continue;
		_curRecords[_numRecords++] = &r;
	}
	_windowOpen = false;
	_hitRegistered = false;
	debug(2, "armed %d records for state %d v%d", _numRecords, state, variant);
}

// ---------------------------------------------------------------------------
// Record window lifecycle (the 0xCA tick's record half)
// ---------------------------------------------------------------------------

Common::Rect GameSpacePiratesWin::liveBoxFor(const SPWin::RecordDef *r) const {
	int x1 = r->x1, x2 = r->x2, y1 = r->y1, y2 = r->y2;
	if (_difficulty == 1) {
		// Easy grows the box by half again about its centre, clamped 0..100
		// (0x40bd8c in SP.exe).
		int w = x2 - x1, cx2 = x1 + x2;
		x1 = MAX(0, (cx2 - (w + w / 2)) / 2);
		x2 = MIN(100, (cx2 + (w + w / 2)) / 2);
		int h = y2 - y1, cy2 = y1 + y2;
		y1 = MAX(0, (cy2 - (h + h / 2)) / 2);
		y2 = MIN(100, (cy2 + (h + h / 2)) / 2);
	}
	return Common::Rect(x1, y1, x2 + 1, y2 + 1); // percent space
}

void GameSpacePiratesWin::tickRecords() {
	if (_outcomePlaying || _commentPlaying || _terminalPlaying)
		return;
	if (_recIdx >= _numRecords)
		return;
	const SPWin::RecordDef *r = _curRecords[_recIdx];
	uint32 p = pos();

	if (_graceArmed) {
		if (_hitRegistered || g_system->getMillis() >= _graceUntil)
			closeWindow(_hitRegistered);
		return;
	}
	if (!_windowOpen) {
		if (p >= r->f0 && p <= r->f1) {
			openWindow(r);
		} else if (p > r->f1) {
			// A skip-ahead seek can jump a window entirely; the original's
			// pending-window flag simply never triggers for it.
			advanceRecord();
		}
		return;
	}
	// window open
	if (_hitRegistered) {
		closeWindow(true);
		return;
	}
	if (p > r->f1) {
		if (r->penalty != 0) {
			// A colonist's window closes at once - holding fire on someone
			// you were not meant to shoot is what you were supposed to do.
			closeWindow(false);
		} else {
			// The picture freezes for one last chance: 600ms on easy, 200ms
			// on hard (timer 0xCD).
			setRun(false);
			_graceArmed = true;
			_graceUntil = g_system->getMillis() + (_difficulty == 1 ? 600 : 200);
		}
	}
}

void GameSpacePiratesWin::openWindow(const SPWin::RecordDef *r) {
	_windowOpen = true;
	_hitRegistered = false;
	debug(2, "window open: rec %d/%d f%d..%d", _recIdx + 1, _numRecords, r->f0, r->f1);
}

void GameSpacePiratesWin::closeWindow(bool viaHit) {
	const SPWin::RecordDef *r = _curRecords[_recIdx];
	const bool wasGrace = _graceArmed;
	_windowOpen = false;
	_graceArmed = false;
	if (wasGrace) {
		// Resume past the window (SEEK f1+1 if the freeze was inside it).
		if (pos() <= r->f1)
			seekFrame(r->f1 + 1);
		setRun(true);
	}
	if (viaHit) {
		_score += r->score;
		if (_difficulty == 1)
			_scoreEasy += r->score;
		else
			_scoreHard += r->score;
		// State 37's kill counter, checked against the reaper quota (0x40bfc9).
		if (_state == 37) {
			_stateKills++;
			if (_stateKills > _reaperPick + 3) {
				playOutcome("World/Bottomleft-reaper/d13.mpg", 0);
				return;
			}
		}
		debug(1, "hit rec %d/%d (+%d)", _recIdx + 1, _numRecords, r->score);
		_hitRegistered = false;
		advanceRecord();
	} else if (r->penalty == 0) {
		// Window expired unhit on an enemy: he shoots back.
		playOutcome(r->outcome, 0);
	} else {
		// Unhit colonist: nothing happens, next record.
		advanceRecord();
	}
}

void GameSpacePiratesWin::advanceRecord() {
	_recIdx++;
	_windowOpen = false;
	_hitRegistered = false;
}

// ---------------------------------------------------------------------------
// Position/click arms (the ladder in the 0xCA tick and the click handler)
// ---------------------------------------------------------------------------

bool GameSpacePiratesWin::armGuardsPass(const SPWin::ArmDef &arm) const {
	if (arm.variant >= 0 && arm.variant != _variant)
		return false;
	uint32 p = pos();
	if (arm.posGt >= 0 && !((int32)p > arm.posGt))
		return false;
	if (arm.posLt >= 0 && !((int32)p < arm.posLt))
		return false;
	if (arm.flagA && arm.wantA >= 0 && ((flag(arm.flagA) != 0) != (arm.wantA != 0)))
		return false;
	if (arm.flagB && arm.wantB >= 0 && ((flag(arm.flagB) != 0) != (arm.wantB != 0)))
		return false;
	if (arm.countCmp == 1 && !(_hitCount < 3))
		return false;
	if (arm.countCmp == 2 && !(_hitCount >= 3))
		return false;
	return true;
}

void GameSpacePiratesWin::runActions(uint16 first, uint16 num) {
	for (uint16 i = first; i < first + num; i++) {
		const SPWin::ActionDef &a = SPWin::kActions[i];
		switch (a.op) {
		case SPWin::kOpState:
			_state = a.a;
			break;
		case SPWin::kOpDispatch:
			dispatch(_state);
			break;
		case SPWin::kOpSeek:
			seekFrame(a.a);
			break;
		case SPWin::kOpRun:
			setRun(true);
			break;
		case SPWin::kOpPause:
			setRun(false);
			break;
		case SPWin::kOpOutcome:
			playOutcome(a.s, 0);
			break;
		case SPWin::kOpFlag:
			setFlag((uint32)a.a >> 8, a.a & 0xff);
			break;
		case SPWin::kOpInc:
			if ((uint32)a.a == 0x415460)
				_hitCount++;
			else if ((uint32)a.a == 0x415130)
				_reaperKills++;
			else if ((uint32)a.a == 0x415464)
				_stateKills++;
			else if ((uint32)a.a == 0x415444)
				_shots++; // refund: offsets the click's cost
			break;
		case SPWin::kOpPoolScene1:
			pickScene1Pool();
			break;
		case SPWin::kOpPoolReaper:
			pickReaperPool();
			break;
		case SPWin::kOpPoolReaperReset:
			resetReaperPool();
			break;
		case SPWin::kOpPoolS1Reset:
			resetScene1Pool();
			break;
		case SPWin::kOpAward:
			awardPicker();
			break;
		case SPWin::kOpDeath:
			deathPicker();
			break;
		case SPWin::kOpScold:
			scoldPicker();
			break;
		case SPWin::kOpStartGame:
			startNewGame();
			break;
		case SPWin::kOpMenuDifficulty:
			showDifficulty();
			break;
		case SPWin::kOpMenuTrailers:
			showTrailersMenu();
			break;
		default:
			break;
		}
	}
}

void GameSpacePiratesWin::runArm(const SPWin::ArmDef &arm) {
	debug(2, "arm %s fires (state %d pos %u)", arm.ev, _state, pos());
	_lastArmEv = arm.ev;
	runActions(arm.firstAction, arm.numActions);
}

// ---------------------------------------------------------------------------
// The per-frame tick
// ---------------------------------------------------------------------------

void GameSpacePiratesWin::tick() {
	if (_freezeAtPos && !_frozen &&
	    _videoDecoder->getCurrentFrame() >= _freezeAtPos) {
		setRun(false);
	}
	if (_uiScreen != kScreenNone) {
		// Debug auto-player: walk the front matter the way a player does.
		if (_autoPlay && _uiScreen == kScreenAttract && pos() > 60) {
			menuClick(50, 50); // any click: intro -> main menu
			return;
		}
		if (_autoPlay && _frozen && _uiScreen == kScreenMain) {
			menuClick(78, 42); // PLAY
			return;
		}
		if (_autoPlay && _frozen && _uiScreen == kScreenDifficulty) {
			menuClick(20, 77); // EASY
			return;
		}
		// Debug auto-player: march through the worlds menu.
		if (_autoPlay && _uiScreen == kScreenWorlds) {
			struct W { int x, y; uint32 f; };
			static const W kW[4] = { { 34, 25, 0x4151d8 }, { 71, 26, 0x414a7c },
			                         { 34, 68, 0x415150 }, { 71, 68, 0x415264 } };
			for (uint i = 0; i < ARRAYSIZE(kW); i++) {
				if (!flag(kW[i].f)) {
					menuClick(kW[i].x, kW[i].y);
					return;
				}
			}
		}
		// Menus are paused stills; the attract intro and trailers just play.
		if (clipEnded()) {
			if (_uiScreen == kScreenAttract) {
				renderClip("Trailers/SPintro.mpg"); // loop the attract reel
			} else if (_uiScreen == kScreenTrailerPlaying) {
				showTrailersMenu();
			} else if (_uiScreen == kScreenWorlds) {
				renderClip("World Menu/worldsmenu.MPG");
			}
		}
		return;
	}

	if (_outcomePlaying || _commentPlaying || _terminalPlaying) {
		if (clipEnded())
			onClipComplete();
		return;
	}

	tickRecords();
	if (_frozen)
		return;

	// Debug auto-player: a perfect run, for walking the whole machine
	// hands-off ("spwin_auto_play=true" in scummvm.ini).
	if (_autoPlay) {
		if (_windowOpen && !_hitRegistered && _recIdx < _numRecords &&
		    _curRecords[_recIdx]->penalty == 0) {
			_hitRegistered = true;
		}
		for (uint i = 0; i < ARRAYSIZE(SPWin::kArms); i++) {
			const SPWin::ArmDef &arm = SPWin::kArms[i];
			if (arm.state != _state || !arm.click || arm.x1 < 0)
				continue;
			if (!armGuardsPass(arm) || _lastArmEv == arm.ev)
				continue;
			// Punishment boxes (their action is an outcome clip) are the ones
			// a perfect player never touches.
			if (arm.numActions && SPWin::kActions[arm.firstAction].op == SPWin::kOpOutcome)
				continue;
			_shots = 6;
			runArm(arm);
			return;
		}
		if (_state == 9)
			autoPlayFinal();
		if ((_state == 44 || _state == 45) && _reaperKills < _reaperPick + 3) {
			// The counting game: a perfect player stops at the quota.
			static const int kCentersX[8] = { 83, 25, 80, 66, 23, 59, 69, 85 };
			static const int kCentersY[8] = { 58, 70, 35, 14, 35, 30, 44, 58 };
			static const uint32 kF0[8] = { 2695, 2778, 2834, 2898, 2966, 3042, 3109, 3194 };
			static const uint32 kF1[8] = { 2776, 2832, 2895, 2964, 3039, 3107, 3191, 3273 };
			const uint32 pp = pos();
			for (uint i = 0; i < 8; i++) {
				if (pp > kF0[i] && pp < kF1[i] && _lastAutoShot != (int)(100 + i)) {
					_lastAutoShot = 100 + i;
					clickSpecial(kCentersX[i], kCentersY[i]);
					return;
				}
			}
		}
	}

	if (tickSpecial())
		return;

	// The position ladder: first matching arm wins. Arms that change state or
	// seek are naturally self-disarming; guard against refiring the same arm
	// while nothing changed.
	for (uint i = 0; i < ARRAYSIZE(SPWin::kArms); i++) {
		const SPWin::ArmDef &arm = SPWin::kArms[i];
		if (arm.state != _state || arm.click)
			continue;
		if (!armGuardsPass(arm))
			continue;
		if (_lastArmEv == arm.ev)
			continue;
		runArm(arm);
		return;
	}

	if (clipEnded() && !_ecHandled) {
		_ecHandled = true;
		onClipComplete();
	}
}

// ---------------------------------------------------------------------------
// Hand-coded position logic, from the adversarially verified reading
// (doc/alg-reelmagic/spwin/analysis/verdicts.json). These are the sites the
// generic arm table cannot express: rand branches and hit-count forks.
// ---------------------------------------------------------------------------

bool GameSpacePiratesWin::tickSpecial() {
	const uint32 p = pos();
	switch (_state) {
	case 3: // scene2 opening: rand picks the middle section (C8a)
		if (p > 1049) {
			dispatch(_rnd->getRandomBit() ? 54 : 53);
			return true;
		}
		return false;
	case 8: // fellina: rand picks one of four stills; scene end -> award (C8b)
		if (p > 1629 && p < 3462) {
			dispatch(46 + _rnd->getRandomNumber(3));
			return true;
		}
		if (p >= 3462) {
			_state = 208;
			awardPicker();
			return true;
		}
		return false;
	case 14: // scene2 mid: rand picks the closing stretch (C7/C11 context)
		if (p > 3224) {
			static const uint32 kVariantFlag[3] = { 0x415614, 0x415618, 0x41561c };
			const int r = _rnd->getRandomNumber(2);
			setFlag(kVariantFlag[r], 1);
			dispatch(55 + r);
			return true;
		}
		return false;
	case 33: // mountain endgame bounce segment (C2)
		if (p > 3000) {
			if (_hitCount == 0) {
				seekFrame(2661);
			} else if (_hitCount <= 2) {
				dispatch(34);
			} else {
				_hitCount = 0;
				dispatch(35);
			}
			return true;
		}
		return false;
	case 34:
		if (p > 3056) {
			dispatch(33);
			return true;
		}
		return false;
	case 84: // scrapyard last window (C3): the fork lives on this state
		if (p > 3516 && p < 3608) {
			// falls through to the generic arms for the hit box; the fork
			// below fires once the window lapses
			return false;
		}
		if (p >= 3608) {
			if (_hitCount < 3) {
				dispatch(80);
			} else {
				_hitCount = 0;
				seekFrame(3610); // stays in state 84; EOF completes the world
			}
			return true;
		}
		return false;
	case 184:
		if (p >= 3608) {
			if (_hitCount < 3) {
				dispatch(80);
			} else {
				_hitCount = 0;
				dispatch(185);
			}
			return true;
		}
		return false;
	case 90: // fellina last window (C5): 3 hits of 6 pass
		if (p > 5046) {
			if (_hitCount >= 3) {
				_hitCount = 0;
				dispatch(91);
			} else {
				dispatch(85); // no reset: hits accumulate across passes
			}
			return true;
		}
		return false;
	case 95: // mountain rocks last still (C4)
		if (p > 2990) {
			if (_hitCount >= 3) {
				_hitCount = 0;
				dispatch(96);
			} else {
				dispatch(92);
			}
			return true;
		}
		return false;
	case 195:
		if (p > 3056) {
			if (_hitCount >= 3) {
				_hitCount = 0;
				dispatch(96);
			} else {
				dispatch(92);
			}
			return true;
		}
		return false;
	case 102: // reaper mines: timer resets on the restart arm (C6)
		if (p > 5023) {
			if (_hitCount < 3) {
				_hitCount = 0;
				dispatch(36);
			} else {
				dispatch(103);
			}
			return true;
		}
		return false;
	case 44: // the reaper counting game: exactly quota kills of 8 (C-facts)
	case 45:
		if (p >= 3274) {
			if (_reaperKills == _reaperPick + 3) {
				_state = (_state == 44) ? 244 : 245;
				awardPicker();
			} else {
				playOutcome("World/Bottomleft-reaper/d13.mpg", 0);
			}
			return true;
		}
		return false;
	case 350: // reaper stills: rand picks, and seeds the kill quota (C15)
		if (p > 1408) {
			_reaperPick = _rnd->getRandomNumber(5);
			dispatch(38 + _reaperPick);
			return true;
		}
		return false;
	case 403: // last asteroid window lapsed (C1)
		if (p > 678) {
			if (_hitCount >= 3) {
				_hitCount = 0;
				dispatch(405);
			} else {
				_state = 1;
				seekFrame(287);
				setRun(true);
			}
			return true;
		}
		return false;
	case 9: // the final level (C10)
		if (p >= 195 && p <= 197) {
			_shots = 6;
			dispatch(65);
			return true;
		}
		if (p >= 684 && p <= 686 && _shots != 1) {
			_shots = 1;
			return false;
		}
		if (p >= 1073 && !flag(kFlagFinal684)) {
			setFlag(kFlagFinal684, 1);
			return false;
		}
		// Attack timeout windows: the miss consequence plays.
		if (p >= 1243 && p <= 1245 && !flag(0x41566c)) {
			playOutcome("Final/d1.mpg", 0);
			return true;
		}
		if (p >= 1486 && p <= 1488 && !flag(0x41566c)) {
			playOutcome("Final/d2.mpg", 0);
			return true;
		}
		if (p >= 1693 && p <= 1695 && !flag(0x41566c)) {
			playOutcome("Final/d3.mpg", 0);
			return true;
		}
		if (p >= 3090) {
			dispatch(201);
			return true;
		}
		return false;
	default:
		return false;
	}
}

void GameSpacePiratesWin::autoPlayFinal() {
	const uint32 p = pos();
	struct Shot { uint32 f0, f1; int x, y; };
	static const Shot kShots[5] = {
		{ 783, 795, 54, 36 }, { 976, 1001, 55, 53 },
		{ 1228, 1242, 48, 41 }, { 1476, 1485, 39, 58 }, { 1685, 1692, 68, 22 },
	};
	for (uint i = 0; i < ARRAYSIZE(kShots); i++) {
		if (p >= kShots[i].f0 && p <= kShots[i].f1 && _lastAutoShot != (int)i) {
			_lastAutoShot = i;
			_shots = 6;
			clickSpecial(kShots[i].x, kShots[i].y);
			return;
		}
	}
}

// Click handling for the final level's hard-coded boxes (C10) and the reaper
// counting game (states 44/45: shoot EXACTLY [0x415148]+3 of the 8 targets).
bool GameSpacePiratesWin::clickSpecial(int xPct, int yPct) {
	if (_state == 44 || _state == 45) {
		struct Skeet {
			uint32 f0, f1;
			int x1, x2, y1, y2;
			int32 next;
		};
		static const Skeet kSkeet[8] = {
			{ 2695, 2776, 73, 93, 40, 77, 2778 },
			{ 2778, 2832, 15, 36, 41, 99, 2834 },
			{ 2834, 2895, 74, 86, 15, 55, 2898 },
			{ 2898, 2964, 61, 71, 3, 25, 2966 },
			{ 2966, 3039, 18, 29, 16, 54, 3042 },
			{ 3042, 3107, 53, 65, 21, 40, 3109 },
			{ 3109, 3191, 63, 76, 22, 67, 3194 },
			{ 3194, 3273, 75, 95, 40, 76, 3274 },
		};
		const uint32 p9 = pos();
		for (uint i = 0; i < ARRAYSIZE(kSkeet); i++) {
			if (p9 < kSkeet[i].f0 || p9 > kSkeet[i].f1)
				continue;
			if (!boxHit(kSkeet[i].x1, kSkeet[i].x2, kSkeet[i].y1, kSkeet[i].y2, xPct, yPct))
				return false; // a stray shot is just a miss
			_reaperKills++;
			const bool last = (i == ARRAYSIZE(kSkeet) - 1);
			const bool bad = last ? (_reaperKills != _reaperPick + 3)
			                      : (_reaperKills > _reaperPick + 3);
			if (bad)
				playOutcome("World/Bottomleft-reaper/d13.mpg", 0);
			else
				seekFrame(kSkeet[i].next);
			return true;
		}
		return false;
	}
	if (_state != 9)
		return false;
	const uint32 p = pos();
	if (p >= 783 && p <= 795) {
		// This beat disarms the player either way.
		_shots = 0;
		setFlag(0x415668, 1);
		if (boxHit(38, 70, 21, 51, xPct, yPct))
			seekFrame(799);
		return true;
	}
	if (p >= 976 && p <= 1001) {
		if (boxHit(40, 70, 38, 68, xPct, yPct))
			seekFrame(1005);
		else
			setFlag(0x415668, 1);
		return true;
	}
	struct FinalBox {
		uint32 f0, f1;
		int x1, x2, y1, y2;
		int32 seekTo;
		const char *missClip;
	};
	static const FinalBox kAttack[3] = {
		{ 1228, 1242, 37, 60, 28, 55, 1246, "Final/d1.mpg" },
		{ 1476, 1485, 29, 50, 48, 69, 1489, "Final/d2.mpg" },
		{ 1685, 1692, 55, 81, 11, 34, 1696, "Final/d3.mpg" },
	};
	for (uint i = 0; i < ARRAYSIZE(kAttack); i++) {
		if (p < kAttack[i].f0 || p > kAttack[i].f1)
			continue;
		if (boxHit(kAttack[i].x1, kAttack[i].x2, kAttack[i].y1, kAttack[i].y2, xPct, yPct)) {
			setFlag(0x41566c, 1);
			seekFrame(kAttack[i].seekTo);
		} else {
			playOutcome(kAttack[i].missClip, 0);
		}
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
// End-of-clip sequencing (FUN_00407840)
// ---------------------------------------------------------------------------

void GameSpacePiratesWin::sequenceChain(const uint32 *flags, int n, const int16 *next,
                                        int16 lastLoop, int16 lastDone) {
	// A flag fires only when set with every later flag clear; the last flag
	// additionally checks the hit counter against 3.
	for (int i = 0; i < n - 1; i++) {
		if (!flag(flags[i]))
			continue;
		bool laterSet = false;
		for (int j = i + 1; j < n; j++)
			laterSet = laterSet || (flag(flags[j]) != 0);
		if (laterSet)
			continue;
		setFlag(flags[i], 0);
		dispatch(next[i]);
		return;
	}
	if (flag(flags[n - 1])) {
		if (_hitCount >= 3) {
			// The original leaves the last flag set on the way out.
			_hitCount = 0;
			dispatch(lastDone);
		} else {
			setFlag(flags[n - 1], 0);
			dispatch(lastLoop);
		}
	}
}

void GameSpacePiratesWin::completeWorld(uint32 flagAddr) {
	setFlag(flagAddr, 1);
	toWorldsMenu();
}

void GameSpacePiratesWin::toWorldsMenu() {
	// FUN_00407090: reset the per-world counters, then either the hub or -
	// with all five worlds done - the final level.
	_reaperKills = 0;
	_hitCount = 0;
	if (flag(kFlagWorldDoors) && flag(kFlagWorldMountain) && flag(kFlagWorldReaper) &&
	    flag(kFlagWorldScrapyard) && flag(kFlagWorldFellina)) {
		dispatch(9);
		setRun(true);
		return;
	}
	_uiScreen = kScreenWorlds;
	renderClip("World Menu/worldsmenu.MPG");
	setRun(true);
}

void GameSpacePiratesWin::onClipComplete() {
	debug(1, "clip complete: state %d out=%d com=%d term=%d",
	      _state, _outcomePlaying, _commentPlaying, _terminalPlaying);

	// Terminal comment (dc20, or a last-life scold) finished.
	if (_terminalPlaying) {
		_terminalPlaying = false;
		if (_continues == 0) {
			gameOverToAttract();
		} else {
			_continues--;
			_lives = 3;
			showContinue();
		}
		return;
	}

	// A you-got-shot / colonist outcome finished: pay the life, then the
	// comment clip.
	if (_outcomePlaying) {
		_outcomePlaying = false;
		_lives--;
		if (_outcomePenalty == 1 || _outcomePenalty == 2)
			scoldPicker();
		else
			deathPicker();
		_outcomePenalty = 0;
		return;
	}

	// A death/scold comment finished. The original's ladder (FUN_00407840,
	// keyed on the 0x415484 "comment played" flag) restarts the section for
	// the doors states, and the whole LEVEL for the grouped world states.
	const bool busy = _commentPlaying;
	_commentPlaying = false;

	switch (_state) {
	// Own-arm states: comment -> replay the same section; normal end ->
	// scripted advance or loop.
	case 2:
	case 10:
	case 11:
	case 14:
	case 16:
	case 7:
	case -1:
		dispatch(_state);
		return;
	case 13:
		if (busy)
			dispatch(13);
		else
			dispatch(3);
		return;
	case 15:
		if (busy)
			dispatch(15);
		else
			dispatch(4);
		return;
	case 3:
	case 53:
	case 54:
		dispatch(3);
		return;
	case 4:
	case 97:
	case 58:
	case 250:
		dispatch(4);
		return;
	case 12:
	case 984:
		dispatch(12);
		return;
	case 76:
	case 77:
	case 78:
	case 79:
	case 80:
	case 81:
	case 82:
	case 83:
		dispatch(76);
		return;
	case 6:
	case 225:
	case 36:
	case 98:
	case 99:
	case 100:
	case 101:
	case 102:
	case 37:
	case 38:
	case 39:
	case 40:
	case 41:
	case 42:
	case 43:
	case 44:
	case 45:
	case 75:
		// Death or clip end anywhere in the reaper level restarts the level.
		_reaperPass = 0;
		dispatch(6);
		return;
	case 8:
	case 46:
	case 47:
	case 48:
	case 49:
	case 50:
	case 85:
	case 86:
	case 87:
	case 88:
	case 89:
	case 90:
		dispatch(8);
		return;
	case 5:
	case 92:
	case 93:
	case 94:
	case 95:
	case 32:
	case 33:
	case 34:
	case 35:
		dispatch(5);
		return;
	case 9:
		// After a death the final level resumes at its checkpoint (C10).
		dispatch(9);
		if (busy && flag(kFlagFinal684)) {
			seekFrame(1073);
			setRun(true);
		}
		return;
	case 201:
		if (busy) {
			dispatch(201);
			return;
		}
		// The ending played: lock the final level, back to the attract reel.
		_finalLock = true;
		gameOverToAttract();
		return;
	case 384: // reaper mine explosion
		if (_hitCount < 3) {
			dispatch(98 + _hitCount);
		} else {
			_hitCount = 0;
			dispatch(103);
		}
		return;
	case 404: // asteroid explosion
		sequenceChain(kAsteroidFlags, 5, kAsteroidNext, 399, 405);
		return;
	case 51: // fellina shooter explosion
		sequenceChain(kFellinaFlags, 6, kFellinaNext, 85, 91);
		return;
	case 405:
		dispatch(2);
		return;
	case 62:
	case 63:
	case 64:
		completeWorld(kFlagWorldDoors);
		return;
	case 96:
		completeWorld(kFlagWorldMountain);
		return;
	case 103:
		completeWorld(kFlagWorldReaper);
		return;
	case 91:
		completeWorld(kFlagWorldFellina);
		return;
	case 84:
	case 185:
		completeWorld(kFlagWorldScrapyard);
		return;
	case 68:
	case 71:
	case 74:
		// All three crystals placed: back into the final level.
		_shots = 1;
		dispatch(9);
		seekFrame(198);
		setRun(true);
		return;
	default:
		break;
	}

	// Common tail: with every world done and the final not yet locked, the
	// final level unlocks.
	if (flag(kFlagWorldDoors) && flag(kFlagWorldMountain) && flag(kFlagWorldReaper) &&
	    flag(kFlagWorldScrapyard) && flag(kFlagWorldFellina) && !_finalLock) {
		dispatch(9);
	}
}

// ---------------------------------------------------------------------------
// Pools and pickers
// ---------------------------------------------------------------------------

void GameSpacePiratesWin::resetScene1Pool() {
	for (int i = 0; i < 4; i++)
		_poolS1Used[i] = false;
	pickScene1Pool();
}

void GameSpacePiratesWin::resetReaperPool() {
	for (int i = 0; i < 9; i++)
		_poolReaperUsed[i] = false;
	pickReaperPool();
}

void GameSpacePiratesWin::pickScene1Pool() {
	static const int32 kSeek[4] = { 2386, 2643, 2776, 2885 };
	int unused = 0;
	for (int i = 0; i < 4; i++)
		unused += _poolS1Used[i] ? 0 : 1;
	if (unused == 0) {
		dispatch(13);
		return;
	}
	int v;
	do {
		v = _rnd->getRandomNumber(3);
	} while (_poolS1Used[v]);
	_poolS1Used[v] = true;
	_variant = v + 1;
	debug(1, "scene1 pool -> variant %d", _variant);
	armRecords(984, _variant);
	seekFrame(kSeek[v]);
	setRun(true);
}

void GameSpacePiratesWin::pickReaperPool() {
	static const int32 kSeek[9] = { 352, 427, 558, 661, 759, 849, 1127, 1190, 1318 };
	int unused = 0;
	for (int i = 0; i < 9; i++)
		unused += _poolReaperUsed[i] ? 0 : 1;
	if (unused == 0) {
		_reaperPass++;
		if (_reaperPass < 2) {
			// A short reprise of the opening, then the whole pool again.
			for (int i = 0; i < 9; i++)
				_poolReaperUsed[i] = false;
			dispatch(6);
			seekFrame(94);
			setRun(true);
			return;
		}
		dispatch(350);
		seekFrame(1408);
		setRun(true);
		return;
	}
	int v;
	do {
		v = _rnd->getRandomNumber(8);
	} while (_poolReaperUsed[v]);
	_poolReaperUsed[v] = true;
	_variant = v + 1;
	debug(1, "reaper pool -> variant %d (pass %d)", _variant, _reaperPass + 1);
	armRecords(225, _variant);
	seekFrame(kSeek[v]);
	setRun(true);
}

void GameSpacePiratesWin::awardPicker() {
	// FUN_004033f0: three award slots shared by every world, cleared once per
	// game. The decision point is the state the caller just stored.
	struct AwardMap {
		int16 preState[4];
		int16 slotState[3];
		int16 exhausted;
	};
	static const AwardMap kMap[] = {
		{ { 205, -1, -1, -1 }, { 20, 22, 21 }, 32 },
		{ { 244, 245, -1, -1 }, { 23, 25, 24 }, 75 },
		{ { 207, 208, -1, -1 }, { 26, 28, 27 }, 76 },
		{ { 246, 247, 248, 249 }, { 29, 30, 31 }, 50 },
	};
	const AwardMap *m = nullptr;
	for (uint i = 0; i < ARRAYSIZE(kMap); i++) {
		for (int j = 0; j < 4; j++) {
			if (kMap[i].preState[j] == _state)
				m = &kMap[i];
		}
	}
	if (!m) {
		warning("SPWin: award picker from unexpected state %d", _state);
		toWorldsMenu();
		return;
	}
	if (_awardUsed[0] && _awardUsed[1] && _awardUsed[2]) {
		dispatch(m->exhausted);
		setRun(true);
		return;
	}
	int slot;
	do {
		slot = _rnd->getRandomNumber(2);
	} while (_awardUsed[slot]);
	_awardUsed[slot] = true;
	debug(1, "crystal award slot %d -> state %d", slot, m->slotState[slot]);
	dispatch(m->slotState[slot]);
	setRun(true);
}

void GameSpacePiratesWin::playOutcome(const Common::String &clip, int penalty) {
	debug(1, "outcome: %s (penalty %d)", clip.c_str(), penalty);
	if (_state == 9)
		_shots = 0;
	_outcomePlaying = true;
	_outcomePenalty = penalty;
	_windowOpen = false;
	_graceArmed = false;
	renderClip(clip);
	setRun(true);
}

void GameSpacePiratesWin::deathPicker() {
	// FUN_00407290: comment pools keyed by the state the player died in.
	if (_lives == 0) {
		_terminalPlaying = true;
		renderClip("Death Comments/dc20.mpg");
		setRun(true);
		return;
	}
	static const char *const kDoors[4] = { "dc7", "dc8", "dc12", "dc13" };
	static const char *const kCrystal[9] = { "dc5", "dc6", "dc7", "dc8", "dc9",
	                                         "dc10", "dc11", "dc12", "dc13" };
	static const char *const kFinal[4] = { "dc5", "dc6", "dc10", "dc11" };
	static const char *const kDefault[8] = { "dc1", "dc2", "dc3", "dc4",
	                                         "dc5", "dc6", "dc7", "dc8" };
	const char *pick;
	const int s = _state;
	const bool doorsGroup =
	    s == 2 || s == 3 || s == 4 || (s >= 10 && s <= 16) || s == 53 || s == 54 ||
	    (s >= 55 && s <= 59) || s == 61 || (s >= 62 && s <= 64) || s == 97 ||
	    s == 984 || s == 3000;
	if (doorsGroup)
		pick = kDoors[_rnd->getRandomNumber(3)];
	else if (s >= 65 && s <= 74)
		pick = kCrystal[_rnd->getRandomNumber(8)];
	else if (s == 9) {
		// The final level empties the gun with you (0x40755e).
		_shots = 0;
		pick = kFinal[_rnd->getRandomNumber(3)];
	} else if (s == 201)
		pick = "dc9";
	else
		pick = kDefault[_rnd->getRandomNumber(7)];
	_commentPlaying = true;
	renderClip(Common::String::format("Death Comments/%s.mpg", pick));
	setRun(true);
}

void GameSpacePiratesWin::scoldPicker() {
	// FUN_00407740: dc50..dc53. The ladder re-dispatches when the comment
	// ends, which re-arms the records.
	static const char *const kScold[4] = { "dc50", "dc51", "dc52", "dc53" };
	const char *pick = kScold[_rnd->getRandomNumber(3)];
	if (_lives == 0)
		_terminalPlaying = true;
	else
		_commentPlaying = true;
	renderClip(Common::String::format("Death Comments/%s.mpg", pick));
	setRun(true);
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

void GameSpacePiratesWin::handleClick(int xPct, int yPct) {
	// Rate limit: timer 0xD0, one shot per 200ms.
	uint32 now = g_system->getMillis();
	if (now < _fireReadyAt)
		return;
	_fireReadyAt = now + 200;

	if (_uiScreen != kScreenNone) {
		menuClick(xPct, yPct);
		return;
	}

	// A click during an outcome or comment skips it (SEEK 1000 in SP.exe).
	if (_outcomePlaying || _commentPlaying || _terminalPlaying) {
		playSfx(_gunSound);
		onClipComplete();
		return;
	}

	if (_shots == 0) {
		if (now >= _emptyReadyAt) {
			playSfx(_emptySound);
			_emptyReadyAt = now + 200;
		}
		return;
	}
	_shots--;
	_shotsFired++;
	if (_shots == 0)
		setGunCursor(true);
	playSfx(_gunSound);
	_flashAt = Common::Point((int16)(xPct * _screen->w / 100), (int16)(yPct * _screen->h / 100));
	_flashUntil = now + 90;

	// The live record box first.
	if ((_windowOpen || _graceArmed) && _recIdx < _numRecords) {
		const SPWin::RecordDef *r = _curRecords[_recIdx];
		Common::Rect box = liveBoxFor(r);
		if (box.contains(Common::Point((int16)xPct, (int16)yPct))) {
			if (r->penalty == 0) {
				_hitRegistered = true;
				_hitAt = _flashAt;
				_hitMarkUntil = now + 400;
			} else if (r->outcome && r->outcome[0]) {
				playOutcome(r->outcome, r->penalty);
			} else {
				_lives--;
				scoldPicker();
			}
			return;
		}
	}

	if (clickSpecial(xPct, yPct))
		return;

	// Then the state's click arms.
	for (uint i = 0; i < ARRAYSIZE(SPWin::kArms); i++) {
		const SPWin::ArmDef &arm = SPWin::kArms[i];
		if (arm.state != _state || !arm.click)
			continue;
		if (arm.x1 >= 0 && !boxHit(arm.x1, arm.x2, arm.y1, arm.y2, xPct, yPct))
			continue;
		if (!armGuardsPass(arm))
			continue;
		_hitAt = _flashAt;
		_hitMarkUntil = now + 400;
		runArm(arm);
		return;
	}
	// A wild shot in the crystal room draws fire (C9).
	if (_state == 65 || _state == 66 || _state == 67 || _state == 69 ||
	    _state == 70 || _state == 72 || _state == 73) {
		deathPicker();
		return;
	}
	// Plain miss: the shot is already spent, nothing else happens.
}

bool GameSpacePiratesWin::boxHit(int x1, int x2, int y1, int y2, int xPct, int yPct) const {
	return xPct >= x1 && xPct <= x2 && yPct >= y1 && yPct <= y2;
}

void GameSpacePiratesWin::handleRightClick() {
	// Reload. In the final level's dogfight stretch the gun takes a single
	// shell (0x40c3c2..0x40c412).
	if (_uiScreen != kScreenNone)
		return;
	uint32 p = pos();
	if (_state == 9 && flag(kFlag413034) && p > 689 && p < 1875)
		_shots = 1;
	else
		_shots = 6;
	setGunCursor(false);
}

void GameSpacePiratesWin::handleKey(Common::KeyCode key) {
	switch (key) {
	case Common::KEYCODE_l:
		// The original binds L to lives:=3 - a leftover developer key.
		_lives = 3;
		break;
	case Common::KEYCODE_1:
	case Common::KEYCODE_2:
	case Common::KEYCODE_3:
	case Common::KEYCODE_4:
	case Common::KEYCODE_5:
	case Common::KEYCODE_6:
	case Common::KEYCODE_7:
	case Common::KEYCODE_8:
	case Common::KEYCODE_9:
	case Common::KEYCODE_0: {
		// Not in the original: jump keys for testing.
		// 1 practice, 2-4 door scenes, 5 mountain, 6 reaper, 7 scrapyard,
		// 8 fellina, 9 final (finished-game flags pre-set), 0 worlds hub.
		static const int kJump[10] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
		jumpToState(kJump[key - Common::KEYCODE_0]);
		break;
	}
	case Common::KEYCODE_p:
		if (_uiScreen == kScreenNone || _uiScreen == kScreenMain || _uiScreen == kScreenAttract)
			showDifficulty();
		break;
	case Common::KEYCODE_t:
		showTrailersMenu();
		break;
	case Common::KEYCODE_ESCAPE:
		showMainMenu();
		break;
	default:
		break;
	}
}

// ---------------------------------------------------------------------------
// Menus / front matter
// ---------------------------------------------------------------------------

void GameSpacePiratesWin::boot() {
	// FUN_004081b0: the attract reel.
	_state = 0;
	_uiScreen = kScreenAttract;
	renderClip("Trailers/SPintro.mpg");
	setRun(true);
}

void GameSpacePiratesWin::gameOverToAttract() {
	// FUN_004071d0: the intro file's frame 1212 is the title card, held
	// paused until a click brings up the menu.
	_state = 0;
	_uiScreen = kScreenAttract;
	// Enter at the GOP before the card and hold on it.
	renderClip("Trailers/SPintro.mpg", 1190);
	_freezeAtPos = 1212;
	setRun(true);
}

void GameSpacePiratesWin::showStill(const Common::String &clip, uint32 frame) {
	// The original renders, seeks and pauses; DirectShow still displays the
	// frame it paused on. Our decoder shows nothing until it decodes, so play
	// up to the frame and hold it there (the freeze check uses the raw
	// decoder position, not the floored one).
	renderClip(clip);
	_freezeAtPos = frame;
	setRun(true);
}

void GameSpacePiratesWin::showMainMenu() {
	// FUN_00408540: a paused frame of MENUS/Main.mpg is the menu.
	_shots = 6;
	_uiScreen = kScreenMain;
	showStill("MENUS/Main.mpg", 15);
}

void GameSpacePiratesWin::showDifficulty() {
	// FUN_004084b0.
	_shots = 6;
	_uiScreen = kScreenDifficulty;
	showStill("MENUS/Difficulty.mpg", 15);
}

void GameSpacePiratesWin::showContinue() {
	// FUN_004085d0.
	_shots = 6;
	_uiScreen = kScreenContinue;
	showStill("MENUS/Continue.mpg", 15);
}

void GameSpacePiratesWin::showTrailersMenu() {
	// FUN_00407150.
	_uiScreen = kScreenTrailers;
	showStill("MENUS/Trailers.mpg", 5);
}

void GameSpacePiratesWin::resetForNewGame() {
	// FUN_00408220. The final-level checkpoint flag 0x415684 is absent from
	// the original's reset block, so it survives into later games in the same
	// session - kept for fidelity.
	const int checkpoint = flag(kFlagFinal684);
	_flags.clear();
	if (checkpoint)
		setFlag(kFlagFinal684, 1);
	_finalLock = false;
	_continues = 7;
	_lives = 3;
	_shots = 6;
	_score = _scoreEasy = _scoreHard = 0;
	_hitCount = _reaperPass = _reaperKills = _reaperPick = _stateKills = 0;
	_variant = 0;
	for (int i = 0; i < 4; i++)
		_poolS1Used[i] = false;
	for (int i = 0; i < 9; i++)
		_poolReaperUsed[i] = false;
	for (int i = 0; i < 3; i++)
		_awardUsed[i] = false;
	_outcomePlaying = _commentPlaying = _terminalPlaying = false;
	_outcomePenalty = 0;
}

void GameSpacePiratesWin::startNewGame() {
	// FUN_00408650.
	resetForNewGame();
	dispatch(1);
	setRun(true);
}

void GameSpacePiratesWin::menuClick(int xPct, int yPct) {
	playSfx(_gunSound);
	switch (_uiScreen) {
	case kScreenAttract:
		showMainMenu();
		return;
	case kScreenMain:
		if (boxHit(70, 87, 37, 47, xPct, yPct)) { // PLAY
			showDifficulty();
		} else if (boxHit(65, 95, 52, 63, xPct, yPct)) { // TRAILERS
			showTrailersMenu();
		} else if (boxHit(71, 86, 68, 78, xPct, yPct)) { // QUIT
			g_system->quit();
		}
		return;
	case kScreenDifficulty:
		if (boxHit(9, 30, 70, 84, xPct, yPct)) {
			_difficulty = 1;
			startNewGame();
		} else if (boxHit(67, 92, 70, 84, xPct, yPct)) {
			_difficulty = 2;
			startNewGame();
		}
		return;
	case kScreenContinue:
		if (boxHit(9, 30, 70, 83, xPct, yPct)) {
			// Left box: back through difficulty select (a fresh game).
			showDifficulty();
		} else if (boxHit(50, 92, 70, 83, xPct, yPct)) {
			// Right box: carry on - replay the section the player died in.
			_uiScreen = kScreenNone;
			dispatch(_state);
		}
		return;
	case kScreenTrailers: {
		static const char *const kGrid[3][4] = {
			{ "DLintro", "MDintro", "WSJRintro", "SPintro" },
			{ "SAintro", "MD2intro", "CPintro", "otintro" },
			{ "DL2intro", "bhintro", "DWintro", "fdintro" },
		};
		static const int kColX[3][2] = { { 7, 35 }, { 36, 64 }, { 65, 95 } };
		static const int kRowY[4][2] = { { 17, 32 }, { 33, 48 }, { 49, 64 }, { 65, 82 } };
		if (boxHit(37, 63, 87, 100, xPct, yPct)) { // BACK
			showMainMenu();
			return;
		}
		for (int c = 0; c < 3; c++) {
			if (xPct < kColX[c][0] || xPct > kColX[c][1])
				continue;
			for (int r = 0; r < 4; r++) {
				if (yPct >= kRowY[r][0] && yPct <= kRowY[r][1]) {
					_uiScreen = kScreenTrailerPlaying;
					renderClip(Common::String::format("Trailers/%s.mpg", kGrid[c][r]));
					setRun(true);
					return;
				}
			}
		}
		return;
	}
	case kScreenTrailerPlaying:
		showTrailersMenu();
		return;
	case kScreenWorlds: {
		struct WorldBox {
			int x1, x2, y1, y2;
			int16 state;
			uint32 doneFlag;
		};
		static const WorldBox kWorlds[4] = {
			{ 18, 51, 7, 44, 5, kFlagWorldMountain },
			{ 55, 88, 8, 45, 7, kFlagWorldScrapyard },
			{ 18, 51, 50, 87, 6, kFlagWorldReaper },
			{ 55, 88, 50, 87, 8, kFlagWorldFellina },
		};
		for (uint i = 0; i < ARRAYSIZE(kWorlds); i++) {
			if (boxHit(kWorlds[i].x1, kWorlds[i].x2, kWorlds[i].y1, kWorlds[i].y2, xPct, yPct) &&
			    !flag(kWorlds[i].doneFlag)) {
				dispatch(kWorlds[i].state);
				setRun(true);
				return;
			}
		}
		return;
	}
	default:
		return;
	}
}

// ---------------------------------------------------------------------------
// Flags
// ---------------------------------------------------------------------------

int GameSpacePiratesWin::flag(uint32 addr) const {
	Common::HashMap<uint32, int>::const_iterator it = _flags.find(addr);
	return it == _flags.end() ? 0 : it->_value;
}

void GameSpacePiratesWin::setFlag(uint32 addr, int v) {
	_flags[addr] = v;
}

// ---------------------------------------------------------------------------
// Presentation
// ---------------------------------------------------------------------------

Audio::SeekableAudioStream *GameSpacePiratesWin::loadWavFile(const Common::Path &path) {
	Common::File *file = new Common::File();
	if (!file->open(path)) {
		warning("GameSpacePiratesWin: can't open sound '%s'", path.toString().c_str());
		delete file;
		return nullptr;
	}
	return Audio::makeWAVStream(file, DisposeAfterUse::YES);
}

void GameSpacePiratesWin::playSfx(Audio::SeekableAudioStream *sfx) {
	if (!sfx)
		return;
	sfx->rewind();
	g_system->getMixer()->stopHandle(_sfxAudioHandle);
	g_system->getMixer()->playStream(Audio::Mixer::kSFXSoundType, &_sfxAudioHandle,
	                                 sfx, -1, Audio::Mixer::kMaxChannelVolume, 0,
	                                 DisposeAfterUse::NO);
}

void GameSpacePiratesWin::drawBox(int16 x, int16 y, int16 w, int16 h, uint8 color, bool filled) {
	Common::Rect r(x, y, x + w, y + h);
	r.clip(Common::Rect(0, 0, _screen->w, _screen->h));
	if (r.isEmpty()) {
		return;
	}
	if (filled) {
		_screen->fillRect(r, color);
		return;
	}
	_screen->drawLine(r.left, r.top, r.right - 1, r.top, color);
	_screen->drawLine(r.left, r.bottom - 1, r.right - 1, r.bottom - 1, color);
	_screen->drawLine(r.left, r.top, r.left, r.bottom - 1, color);
	_screen->drawLine(r.right - 1, r.top, r.right - 1, r.bottom - 1, color);
}

void GameSpacePiratesWin::drawText(const Common::String &text, int16 x, int16 y, uint8 color) {
	const Graphics::Font *font = FontMan.getFontByUsage(Graphics::FontManager::kConsoleFont);
	if (!font) {
		return;
	}
	font->drawString(_screen, text, x, y, _screen->w - x, color);
}

void GameSpacePiratesWin::drawCrosshair(int16 x, int16 y) {
	const uint8 c = 240;
	_screen->drawLine(x - 6, y, x - 2, y, c);
	_screen->drawLine(x + 2, y, x + 6, y, c);
	_screen->drawLine(x, y - 6, x, y - 2, c);
	_screen->drawLine(x, y + 2, x, y + 6, c);
	drawBox(x - 3, y - 3, 7, 7, c, false);
}

void GameSpacePiratesWin::drawShotFeedback() {
	uint32 now = g_system->getMillis();
	if (now < _flashUntil) {
		// A quick burst: bright core with an orange rim and spikes.
		drawBox(_flashAt.x - 5, _flashAt.y - 5, 11, 11, 242, true);
		drawBox(_flashAt.x - 3, _flashAt.y - 3, 7, 7, 241, true);
		drawBox(_flashAt.x - 1, _flashAt.y - 1, 3, 3, 240, true);
		_screen->drawLine(_flashAt.x - 9, _flashAt.y, _flashAt.x + 9, _flashAt.y, 241);
		_screen->drawLine(_flashAt.x, _flashAt.y - 9, _flashAt.x, _flashAt.y + 9, 241);
	} else if (now < _flashUntil + 600) {
		// then the hole it left
		drawBox(_flashAt.x - 1, _flashAt.y - 1, 3, 3, 247, true);
	}
	if (now < _hitMarkUntil) {
		drawBox(_hitAt.x - 5, _hitAt.y - 5, 11, 11, 243, false);
	}
}

void GameSpacePiratesWin::drawInterface() {
	// The original draws bullets and score in a child window; we draw on the
	// frame.
	if (_uiScreen == kScreenNone && !_outcomePlaying && !_commentPlaying && !_terminalPlaying) {
		for (int i = 0; i < _shots; i++)
			drawBox(4 + i * 6, _screen->h - 12, 4, 8, 242, true);
		for (int i = 0; i < _lives; i++)
			drawBox(_screen->w - 12 - i * 8, _screen->h - 12, 6, 6, 243, true);
		drawText(Common::String::format("%d", _score), 4, 2, 240);
	}
	drawShotFeedback();
	// The gun cursor is the hardware cursor (cursor1/cursor2.cur); fall back
	// to a drawn crosshair only if the .cur files were missing.
	if (!_cursorLoaded)
		drawCrosshair(_mousePos.x, _mousePos.y);
}

void GameSpacePiratesWin::debug_drawZoneRects() {
	if (!_debug_drawRects)
		return;
	const int w = _screen->w, h = _screen->h;
	// The live record window.
	if ((_windowOpen || _graceArmed) && _recIdx < _numRecords) {
		Common::Rect b = liveBoxFor(_curRecords[_recIdx]);
		drawBox(b.left * w / 100, b.top * h / 100,
		        (b.right - b.left) * w / 100, (b.bottom - b.top) * h / 100,
		        _graceArmed ? 246 : 243, false);
	}
	// Click arms currently in their window.
	for (uint i = 0; i < ARRAYSIZE(SPWin::kArms); i++) {
		const SPWin::ArmDef &arm = SPWin::kArms[i];
		if (arm.state != _state || !arm.click || arm.x1 < 0)
			continue;
		if (!armGuardsPass(arm))
			continue;
		drawBox(arm.x1 * w / 100, arm.y1 * h / 100,
		        (arm.x2 - arm.x1) * w / 100, (arm.y2 - arm.y1) * h / 100, 244, false);
	}
	drawText(Common::String::format("st %d v%d pos %u ui %d", _state, _variant,
	                                pos(), _uiScreen),
	         3, 12, 245);
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------

Common::Error GameSpacePiratesWin::run() {
	init();

	while (!_vm->shouldQuit()) {
		pollEvents();

		const Common::KeyCode key = _lastKey;
		_lastKey = Common::KEYCODE_INVALID;
		if (key != Common::KEYCODE_INVALID)
			handleKey(key);

		AlgMpegDecoder *mpeg = (AlgMpegDecoder *)_videoDecoder;
		if (!_frozen)
			mpeg->getNextFrame();
		_currentFrame = pos();

		if (_clickPending) {
			_clickPending = false;
			const int xPct = _clickPos.x * 100 / _screen->w;
			const int yPct = _clickPos.y * 100 / _screen->h;
			handleClick(xPct, yPct);
		}
		if (_rightDown) {
			_rightDown = false;
			handleRightClick();
		}

		tick();
		updateScreen();
		g_system->delayMillis(10);
	}
	return Common::kNoError;
}

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

bool GameSpacePiratesWin::saveState(Common::OutSaveFile *out) {
	out->writeSint32LE(_state);
	out->writeSint32LE(_variant);
	out->writeSint32LE(_uiScreen);
	out->writeSint32LE(_lives);
	out->writeSint32LE(_shots);
	out->writeSint32LE(_difficulty);
	out->writeSint32LE(_continues);
	out->writeSint32LE(_score);
	out->writeSint32LE(_hitCount);
	out->writeSint32LE(_reaperPass);
	out->writeSint32LE(_reaperKills);
	out->writeSint32LE(_reaperPick);
	out->writeByte(_finalLock ? 1 : 0);
	for (int i = 0; i < 4; i++)
		out->writeByte(_poolS1Used[i] ? 1 : 0);
	for (int i = 0; i < 9; i++)
		out->writeByte(_poolReaperUsed[i] ? 1 : 0);
	for (int i = 0; i < 3; i++)
		out->writeByte(_awardUsed[i] ? 1 : 0);
	out->writeUint32LE(_flags.size());
	for (Common::HashMap<uint32, int>::const_iterator it = _flags.begin(); it != _flags.end(); ++it) {
		out->writeUint32LE(it->_key);
		out->writeSint32LE(it->_value);
	}
	return true;
}

bool GameSpacePiratesWin::loadState(Common::InSaveFile *in) {
	_state = in->readSint32LE();
	_variant = in->readSint32LE();
	_uiScreen = in->readSint32LE();
	_lives = in->readSint32LE();
	_shots = in->readSint32LE();
	_difficulty = in->readSint32LE();
	_continues = in->readSint32LE();
	_score = in->readSint32LE();
	_hitCount = in->readSint32LE();
	_reaperPass = in->readSint32LE();
	_reaperKills = in->readSint32LE();
	_reaperPick = in->readSint32LE();
	_finalLock = in->readByte() != 0;
	for (int i = 0; i < 4; i++)
		_poolS1Used[i] = in->readByte() != 0;
	for (int i = 0; i < 9; i++)
		_poolReaperUsed[i] = in->readByte() != 0;
	for (int i = 0; i < 3; i++)
		_awardUsed[i] = in->readByte() != 0;
	_flags.clear();
	const uint32 n = in->readUint32LE();
	for (uint32 i = 0; i < n; i++) {
		const uint32 k = in->readUint32LE();
		_flags[k] = in->readSint32LE();
	}
	_outcomePlaying = _commentPlaying = _terminalPlaying = false;
	if (_uiScreen == kScreenNone)
		dispatch(_state);
	else
		boot();
	return true;
}

} // End of namespace Alg
