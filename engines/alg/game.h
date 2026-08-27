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

#ifndef ALG_GAME_H
#define ALG_GAME_H

#include "common/keyboard.h"
#include "common/random.h"

#include "audio/audiostream.h"
#include "audio/mixer.h"

#include "alg/alg.h"
#include "alg/scene.h"
#include "alg/video.h"

namespace Alg {

class Game {

public:
	Game(AlgEngine *vm);
	virtual ~Game();
	virtual void init();
	virtual Common::Error run();
	virtual bool saveState(Common::OutSaveFile *outSaveFile) = 0;
	virtual bool loadState(Common::InSaveFile *inSaveFile) = 0;
	bool debug_dumpLibFile();
	bool _debug_drawRects = false;
	bool _debug_godMode = false;
	bool _debug_unlimitedAmmo = false;
	/**
	 * Section skip for testing: pollEvents() latches number keys here and a
	 * game that supports warping consumes the value from its scene loop.
	 */
	int _debug_warpRequest = -1;

protected:
	AlgEngine *_vm = nullptr;
	AlgVideoDecoder *_videoDecoder = nullptr;
	SceneInfo *_sceneInfo = nullptr;
	Common::RandomSource *_rnd = nullptr;

	Common::File _libFile;
	Common::HashMap<Common::String, uint32> _libFileEntries;
	/** The ReelMagic releases play from one MPEG instead of a .LIB archive. */
	bool _reelMagic = false;

	/**
	 * ReelMagic compositing. The interface is 8 bit art and the video is RGB, so
	 * the paletted _screen is kept exactly as the .LIB releases use it and a
	 * true colour frame is assembled from it at the end of updateScreen():
	 * wherever _screen still holds _videoKeyIndex the video shows through,
	 * everywhere else the palette entry wins. That keeps every one of the
	 * engine's 8 bit drawing calls - and the other six games - untouched.
	 */
	Graphics::Surface *_outputScreen = nullptr;
	uint8 _videoKeyIndex = 0xFF;
	bool _videoKeyChosen = false;
	/** Palette entries pre-converted to screen pixels; rebuilt when it changes. */
	uint32 _paletteColors[256];
	bool _paletteColorsValid = false;

	/**
	 * Milliseconds the main loop aims to spend on a frame. The .LIB releases'
	 * own clips run at about 10fps, which is what the 100 here was for; the
	 * ReelMagic pictures are 29.97fps, so at 100 two out of every three would be
	 * decoded and thrown away.
	 */
	uint32 frameIntervalMs() const { return _reelMagic ? 33 : 100; }

	/** Palette index that no interface art uses, so it can key the video. */
	uint8 findUnusedPaletteIndex() const;
	void compositeReelMagicFrame();

	uint8 *_palette  = nullptr;
	bool _paletteDirty = false;

	Graphics::Surface *_background = nullptr;
	Graphics::Surface *_screen = nullptr;

	Audio::SoundHandle _sfxAudioHandle;

	Zone *_menuZone = nullptr;
	Zone *_subMenuZone = nullptr;

	bool _leftDown = false;
	bool _rightDown = false;
	Common::Point _mousePos;
	/**
	 * Latched on the press itself. Testing _leftDown misses a click whose
	 * press and release land in the same poll, which is how synthetic input
	 * and very quick taps arrive.
	 */
	/** Last key pressed, for a game that wants keys the base does not use. */
	Common::KeyCode _lastKey = Common::KEYCODE_INVALID;
	bool _clickPending = false;
	Common::Point _clickPos;

	const uint32 _pauseDiffScale[3] = {0x10000, 0x8000, 0x4000};
	const uint32 _rectDiffScale[3] = {0x10000, 0x0C000, 0x8000};

	void shutdown();
	bool pollEvents();
	void loadLibArchive(const Common::Path &path);

	/**
	 * The ReelMagic counterpart of loadLibArchive(): there is no directory to
	 * read, just the one stream that every clip lives in.
	 */
	void loadMpegFile(const Common::Path &path);

	// Timing a ReelMagic scene against what its byte range implies, so a clip
	// that ends early can be named rather than guessed at.
	void reportSceneEnd();

	/**
	 * Freeze playback so the screen can be studied. Holds inside the call,
	 * still servicing events, until the key is pressed again.
	 */
	void pauseGame(bool pause);

	/**
	 * Marks drawn over the ReelMagic picture - bullet holes, and the trail of
	 * them that makes up an enemy's shot. The key colour is refilled every
	 * frame, so a mark drawn straight onto the screen survives only one 30fps
	 * frame. The .LIB releases draw onto the decoder's own frame, which at
	 * 10fps keeps a mark up about a tenth of a second, so these are replayed
	 * for a comparable time instead.
	 */
	struct OverlayMark {
		Graphics::Surface *image;
		int16 x, y;
		uint32 when;
	};
	Common::Array<OverlayMark> _overlayMarks;
	void addOverlayMark(Graphics::Surface *image, int16 x, int16 y);
	void replayOverlayMarks();
	bool _paused = false;

	Common::String _timedScene;
	uint32 _sceneStartMs = 0;
	uint32 _sceneExpectMs = 0;
	Audio::SeekableAudioStream *loadSoundFile(const Common::Path &path);
	void playSound(Audio::SeekableAudioStream *stream);
	virtual bool loadScene(Scene *scene);
	virtual void updateScreen();
	uint32 getMsTime();
	bool fired(Common::Point *point);
	Rect *checkZone(Zone *zone, Common::Point *point);
	virtual uint32 getFrame(Scene *scene);
	/**
	 * Every mouse event passes through here before the game logic sees it. A
	 * game whose scene file was written for a different screen layout maps the
	 * physical position into the scene file's own space, and then rects, menu
	 * boxes and the hardcoded cursor regions all work untouched.
	 */
	virtual Common::Point transformInput(const Common::Point &point) const { return point; }
	int8 skipToNewScene(Scene *scene);
	virtual void debug_drawZoneRects();
	/**
	 * Draw the cursor, ammo and score. Called with the video area still
	 * holding the key colour, so anything drawn here survives the mix.
	 */
	virtual void drawInterface() {}
	void debug_strokeRect(const Common::Rect &rect, uint8 color, bool emphasize);

	// Script functions: Zone
	void zoneGlobalHit(Common::Point *point);
	// Script functions: RectHit
	void rectHitDoNothing(Rect *rect);
	void rectNewScene(Rect *rect);
	// Script functions: Scene PreOps
	void scenePsoDrawRct(Scene *scene);
	void scenePsoPause(Scene *scene);
	void scenePsoDrawRctFadeIn(Scene *scene);
	void scenePsoFadeIn(Scene *scene);
	void scenePsoPauseFadeIn(Scene *scene);
	void scenePsoPreRead(Scene *scene);
	void scenePsoPausePreRead(Scene *scene);
	// Script functions: Scene Scene InsOps
	void sceneIsoDoNothing(Scene *scene);
	void sceneIsoStartGame(Scene *scene);
	void sceneIsoPause(Scene *scene);
	// Script functions: Scene Scene NxtScn
	void sceneNxtscnDoNothing(Scene *scene);
	void sceneDefaultNxtscn(Scene *scene);
	// Script functions: ShowMsg
	void sceneSmDonothing(Scene *scene);
	// Script functions: ScnNxtFrm
	void sceneNxtfrm(Scene *scene);

	bool _buttonDown = false;
	bool _fired = 0;
	uint32 _currentFrame = 0;
	bool _gameInProgress = false;
	bool _hadPause = false;
	bool _inMenu = false;
	uint32 _pauseTime = 0;
	bool _sceneSkipped = false;
	uint32 _videoFrameSkip = 3;
	uint32 _nextFrameTime = 0;
	uint16 _videoPosX = 0;
	uint16 _videoPosY = 0;

	Common::String _curScene;
	Common::String _startScene;
};

} // End of namespace Alg

#endif
