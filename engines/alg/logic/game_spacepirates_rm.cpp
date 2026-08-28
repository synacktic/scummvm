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
#include "common/file.h"
#include "common/tokenizer.h"
#include "common/system.h"

#include "alg/graphics.h"
#include "alg/logic/game_spacepirates_rm.h"

namespace Alg {

GameSpacePiratesRM::GameSpacePiratesRM(AlgEngine *vm, const AlgGameDescription *gd) : GameSpacePirates(vm, gd) {
}

GameSpacePiratesRM::~GameSpacePiratesRM() {
}

void GameSpacePiratesRM::init() {
	// The split video files live in subdirectories of the game path.
	const Common::FSNode gameDir(ConfMan.getPath("path"));
	SearchMan.addSubDirectoryMatching(gameDir, "win");
	SearchMan.addSubDirectoryMatching(gameDir, "dos");

	GameSpacePirates::init();

	_mpeg = dynamic_cast<AlgMpegDecoder *>(_videoDecoder);
	assert(_mpeg);

	loadMap();
	// sp.scn counts 29.97fps master frames, three per .LIB picture; the decoder
	// reports positions in the same frames. _videoFrameSkip stays 3, so every
	// zone and pause tolerance keeps its DOS size.
	_mpeg->setFrameUnits(true);

	_videoPosX = kDstX;
	_videoPosY = kDstY;
	_mpeg->setDisplaySize(kDstW, kDstH);

	// No border art: black pillars and the HUD strip instead.
	_screen->fillRect(Common::Rect(0, 0, _screen->w, _screen->h), 0);
	hudInit();
}

void GameSpacePiratesRM::loadMap() {
	Common::File f;
	if (!f.open("sprm.map")) {
		error("GameSpacePiratesRM: cannot open sprm.map");
	}
	while (!f.eos()) {
		Common::String line = f.readLine();
		line.trim();
		if (line.empty()) {
			continue;
		}
		Common::StringTokenizer tok(line, " ");
		Common::String word = tok.nextToken();
		if (word == "FORMAT") {
			// informational
		} else if (word == "MAP") {
			Common::String scene = tok.nextToken();
			Common::String path = tok.nextToken();
			uint32 start = atoi(tok.nextToken().c_str());
			scene.toLowercase();
			// Subdirectories are on the search path, so the basename is enough.
			size_t slash = path.findLastOf('/');
			if (slash != Common::String::npos) {
				path = path.substr(slash + 1);
			}
			MapEntry entry;
			entry.file = path;
			entry.startUnit = start;
			_map[scene] = entry;
		} else if (word == "END") {
			break;
		} else {
			error("GameSpacePiratesRM: bad sprm.map line '%s'", line.c_str());
		}
	}
	debug("sprm.map: %d scenes", _map.size());
}

bool GameSpacePiratesRM::loadScene(Scene *scene) {
	if (scene->_startFrame == 0) {
		return false;
	}
	auto it = _map.find(scene->_name);
	if (it == _map.end()) {
		warning("GameSpacePiratesRM: scene %s not in sprm.map", scene->_name.c_str());
		return false;
	}
	// Same expected-length bookkeeping as the other ReelMagic paths.
	const uint32 span = scene->_endFrame - scene->_startFrame;
	_timedScene = scene->_name;
	_sceneStartMs = g_system->getMillis();
	_sceneExpectMs = (uint32)(((uint64)span * 1001) / 30);
	debug("loaded scene %s from %s@%uf (%u frames, %u ms expected)",
	      scene->_name.c_str(), it->_value.file.c_str(), it->_value.startUnit,
	      span, _sceneExpectMs);
	_mpeg->resetFrameCount();
	_mpeg->loadVideoFile(Common::Path(it->_value.file), it->_value.startUnit);
	_curStartUnit = it->_value.startUnit;
	return true;
}

uint32 GameSpacePiratesRM::getFrame(Scene *scene) {
	// The decoder reports 29.97fps frames into the current file; sp.scn counts
	// the same frames with the clip's first frame at scene->_startFrame.
	const uint32 pos = _videoDecoder->getCurrentFrame();
	if (pos <= _curStartUnit) {
		return scene->_startFrame;
	}
	return scene->_startFrame + (pos - _curStartUnit);
}

// ---- coordinates ----------------------------------------------------------

Common::Point GameSpacePiratesRM::transformInput(const Common::Point &point) const {
	// Physical screen -> the DOS build's 320x200 screen space.
	int32 x = kSrcX + ((int32)(point.x - kDstX) * kSrcW + kDstW / 2) / kDstW;
	int32 y = kSrcY + ((int32)(point.y - kDstY) * kSrcH + kDstH / 2) / kDstH;
	x = CLIP<int32>(x, 0, 319);
	y = CLIP<int32>(y, 0, 199);
	return Common::Point(x, y);
}

Common::Point GameSpacePiratesRM::dosToScreen(int16 x, int16 y) const {
	int32 sx = kDstX + ((int32)(x - kSrcX) * kDstW + kSrcW / 2) / kSrcW;
	int32 sy = kDstY + ((int32)(y - kSrcY) * kDstH + kSrcH / 2) / kSrcH;
	return Common::Point(CLIP<int32>(sx, 0, _screen->w - 1), CLIP<int32>(sy, 0, _screen->h - 1));
}

Common::Rect GameSpacePiratesRM::dosRectToScreen(const Common::Rect &rect) const {
	const Common::Point tl = dosToScreen(rect.left, rect.top);
	const Common::Point br = dosToScreen(rect.right, rect.bottom);
	return Common::Rect(tl.x, tl.y, MAX<int16>(br.x, tl.x + 1), MAX<int16>(br.y, tl.y + 1));
}

// ---- presentation ---------------------------------------------------------

void GameSpacePiratesRM::displayShotFiredImage(Common::Point *point) {
	// The point is in DOS screen space (transformInput). Show the hole where
	// the shot actually landed on this screen.
	if (point->x >= kSrcX && point->x <= kSrcX + kSrcW && point->y >= kSrcY && point->y <= kSrcY + kSrcH) {
		const Common::Point s = dosToScreen(point->x, point->y);
		addOverlayMark(_bulletholeIcon, s.x - 4, s.y - 4);
		AlgGraphics::drawImageCentered(_screen, _bulletholeIcon, s.x - 4, s.y - 4);
	}
}

bool GameSpacePiratesRM::weaponDown() {
	return _rightDown;
}

// Stretch-blit a region of the border art (DOS coordinates) to a screen rect.
// Used for the MENU and RELOAD button labels, so the strip shows the same
// affordances the DOS border did, in the spots the script already listens on.
void GameSpacePiratesRM::drawArtChip(int16 srcL, int16 srcT, int16 srcR, int16 srcB,
                                     int16 dstL, int16 dstT, int16 dstR, int16 dstB) {
	const int sw = srcR - srcL, sh = srcB - srcT;
	const int dw = dstR - dstL, dh = dstB - dstT;
	if (sw <= 0 || sh <= 0 || dw <= 0 || dh <= 0 || !_background) {
		return;
	}
	for (int y = 0; y < dh; y++) {
		const int sy = CLIP<int>(srcT + y * sh / dh, 0, _background->h - 1);
		const byte *src = (const byte *)_background->getBasePtr(0, sy);
		byte *dst = (byte *)_screen->getBasePtr(0, dstT + y);
		for (int x = 0; x < dw; x++) {
			dst[dstL + x] = src[CLIP<int>(srcL + x * sw / dw, 0, _background->w - 1)];
		}
	}
}

void GameSpacePiratesRM::drawWorldCrossout(int32 centerX, int32 centerY) {
	const Common::Point s = dosToScreen(centerX, centerY);
	AlgGraphics::drawImageCentered(_screen, (*_gun)[2], s.x - 16, s.y - 24);
}

void GameSpacePiratesRM::drawMenuBackground() {
	// The whole menu view is the DOS border art seen through the same mapping
	// the video uses, so the menu buttons sit exactly where clicks land.
	for (int y = 0; y < _screen->h; y++) {
		const int dy = CLIP<int>(kSrcY + ((y - kDstY) * kSrcH + kDstH / 2) / kDstH, 0, _background->h - 1);
		const byte *src = (const byte *)_background->getBasePtr(0, dy);
		byte *dst = (byte *)_screen->getBasePtr(0, y);
		for (int x = 0; x < _screen->w; x++) {
			const int dx = CLIP<int>(kSrcX + ((x - kDstX) * kSrcW + kDstW / 2) / kDstW, 0, _background->w - 1);
			dst[x] = src[dx];
		}
	}
}

void GameSpacePiratesRM::showDifficulty(uint8 newDifficulty, bool cursor) {
	drawMenuBackground();
	uint16 posY = 0x31;
	if (newDifficulty == 1) {
		posY = 0x5B;
	} else if (newDifficulty == 2) {
		posY = 0x86;
	}
	const Common::Point s = dosToScreen(0x0111, posY);
	AlgGraphics::drawImageCentered(_screen, _difficultyIcon, s.x, s.y);
	if (cursor) {
		updateCursor();
	}
}

void GameSpacePiratesRM::doMenu() {
	// Same flow as the DOS build's menu; only the backdrop draw differs.
	uint32 startTime = getMsTime();
	updateCursor();
	_inMenu = true;
	moveMouse();
	_videoDecoder->pauseAudio(true);
	drawMenuBackground();
	showDifficulty(_difficulty, false);
	while (_inMenu && !_vm->shouldQuit()) {
		Common::Point firedCoords;
		if (fired(&firedCoords)) {
			Rect *hitMenuRect = checkZone(_subMenuZone, &firedCoords);
			if (hitMenuRect != nullptr) {
				callScriptFunctionRectHit(hitMenuRect->_rectHit, hitMenuRect);
			}
		}
		if (_difficulty != _oldDifficulty) {
			changeDifficulty(_difficulty);
		}
		updateScreen();
		g_system->delayMillis(15);
	}
	updateCursor();
	_videoDecoder->pauseAudio(false);
	// Leaving the menu: back to black pillars, the key fill and the HUD
	// restore everything else on the next frame.
	_screen->fillRect(Common::Rect(0, 0, _screen->w, _screen->h), 0);
	if (_hadPause) {
		uint32 endTime = getMsTime();
		uint32 timeDiff = endTime - startTime;
		_pauseTime += timeDiff;
		_nextFrameTime += timeDiff;
	}
}

void GameSpacePiratesRM::debug_drawZoneRects() {
	if (!(_debug_drawRects || debugChannelSet(1, Alg::kAlgDebugGraphics))) {
		return;
	}
	// The picture's own edge
	debug_strokeRect(Common::Rect(kDstX, kDstY, kDstX + kDstW, kDstY + kDstH), 1, false);
	if (_inMenu) {
		if (_subMenuZone) {
			for (auto &rect : _subMenuZone->_rects) {
				debug_strokeRect(dosRectToScreen(*rect), 1, false);
			}
		}
		return;
	}
	if (_curScene.empty()) {
		return;
	}
	Scene *scene = _sceneInfo->findScene(_curScene);
	if (_menuZone) {
		for (auto &rect : _menuZone->_rects) {
			debug_strokeRect(dosRectToScreen(*rect), 1, false);
		}
	}
	for (auto &zone : scene->_zones) {
		if (_currentFrame + 30 >= zone->_startFrame && _currentFrame <= zone->_endFrame) {
			const bool live = _currentFrame >= zone->_startFrame && _currentFrame <= zone->_endFrame;
			for (auto &rect : zone->_rects) {
				Common::Rect r = rect->getInterpolatedRect(zone->_startFrame, zone->_endFrame, _currentFrame);
				const Common::Rect s = dosRectToScreen(r);
				debug_strokeRect(s, 1, live);
				if (rect->_score == 0 || rect->_rectHit.contains("INNOCENT")) {
					Common::Rect inner = s;
					inner.grow(-2);
					debug_strokeRect(inner, 1, false);
				}
			}
		}
	}
}

// ---- HUD -------------------------------------------------------------------

void GameSpacePiratesRM::hudInit() {
	// Palette indices no interface art uses, so recolouring them touches
	// nothing else. The composite's video key is chosen the same way, so it
	// is excluded here too.
	bool used[256];
	memset(used, 0, sizeof(used));
	used[0] = true; // transparent for drawImage, and our pillar black
	used[findUnusedPaletteIndex()] = true;
	Common::Array<Graphics::Surface *> art;
	art.push_back(_background);
	art.push_back(_shotIcon);
	art.push_back(_emptyIcon);
	art.push_back(_deadIcon);
	art.push_back(_liveIcon1);
	art.push_back(_liveIcon2);
	art.push_back(_liveIcon3);
	art.push_back(_difficultyIcon);
	art.push_back(_bulletholeIcon);
	if (_gun) {
		for (uint i = 0; i < _gun->size(); i++) {
			art.push_back((*_gun)[i]);
		}
	}
	if (_numbers) {
		for (uint i = 0; i < _numbers->size(); i++) {
			art.push_back((*_numbers)[i]);
		}
	}
	for (uint i = 0; i < art.size(); i++) {
		Graphics::Surface *surface = art[i];
		if (!surface || surface->format.bytesPerPixel != 1) {
			continue;
		}
		for (int y = 0; y < surface->h; y++) {
			const byte *line = (const byte *)surface->getBasePtr(0, y);
			for (int x = 0; x < surface->w; x++) {
				used[line[x]] = true;
			}
		}
	}
	static const byte kColors[kHudColorCount][3] = {
		{36, 36, 44},    // panel
		{70, 70, 82},    // panel light
		{130, 130, 144}, // panel edge
		{40, 255, 80},   // seven-seg on
		{20, 56, 26},    // seven-seg off
		{255, 216, 40},  // dot lit
		{72, 62, 28},    // dot off
		{255, 64, 48}    // red bar
	};
	int idx = 255;
	for (int c = 0; c < kHudColorCount; c++) {
		while (idx > 0 && used[idx]) {
			idx--;
		}
		if (idx <= 0) {
			// Out of free slots; extremely unlikely. Reuse the top ones.
			idx = 255 - c;
		}
		_hudCol[c] = (uint8)idx;
		used[idx] = true;
		_palette[idx * 3 + 0] = kColors[c][0];
		_palette[idx * 3 + 1] = kColors[c][1];
		_palette[idx * 3 + 2] = kColors[c][2];
	}
	_paletteDirty = true;
	_hudReady = true;
	debug("SPRM HUD palette: %d %d %d %d %d %d %d %d",
	      _hudCol[0], _hudCol[1], _hudCol[2], _hudCol[3], _hudCol[4], _hudCol[5], _hudCol[6], _hudCol[7]);
}

void GameSpacePiratesRM::drawSevenSegDigit(int x, int y, int h, int digit, uint8 on, uint8 off) {
	// Segment bits: A top, B top-right, C bottom-right, D bottom, E bottom-left,
	// F top-left, G middle.
	static const byte kSegs[10] = {
		0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F
	};
	const int w = h * 5 / 8;
	const int t = MAX(2, h / 8);
	const byte seg = (digit >= 0 && digit <= 9) ? kSegs[digit] : 0;
	const int midY = y + (h - t) / 2;
	struct SegRect {
		byte bit;
		int16 x, y, w, h;
	};
	const SegRect rects[7] = {
		{0x01, (int16)(x + 1), (int16)y, (int16)(w - 2), (int16)t},                                      // A
		{0x02, (int16)(x + w - t), (int16)(y + 1), (int16)t, (int16)((h / 2) - 1)},                      // B
		{0x04, (int16)(x + w - t), (int16)(midY + 1), (int16)t, (int16)(y + h - midY - 2)},              // C
		{0x08, (int16)(x + 1), (int16)(y + h - t), (int16)(w - 2), (int16)t},                            // D
		{0x10, (int16)x, (int16)(midY + 1), (int16)t, (int16)(y + h - midY - 2)},                        // E
		{0x20, (int16)x, (int16)(y + 1), (int16)t, (int16)((h / 2) - 1)},                                // F
		{0x40, (int16)(x + 1), (int16)midY, (int16)(w - 2), (int16)t}                                    // G
	};
	for (int i = 0; i < 7; i++) {
		const uint8 color = (seg & rects[i].bit) ? on : off;
		_screen->fillRect(Common::Rect(rects[i].x, rects[i].y, rects[i].x + rects[i].w, rects[i].y + rects[i].h), color);
	}
}

void GameSpacePiratesRM::drawHud() {
	if (!_hudReady) {
		return;
	}
	// The strip lives below the picture, so it can never cover a target.
	_screen->fillRect(Common::Rect(0, kHudTop, _screen->w, _screen->h), 0);

	// The DOS border's MENU and RELOAD labels, at the spots their input
	// already lands: the menu box's lower half maps into the strip's left
	// end, and the holster region into its right.
	drawArtChip(10, 189, 48, 200, 17, kHudTop + 1, 66, _screen->h - 1);
	drawArtChip(249, 189, 299, 200, 300, kHudTop + 1, 351, _screen->h - 1);

	const int panelW = 222;
	const int panelX = 72;
	const int panelY = kHudTop + 1;
	const int panelH = _screen->h - panelY - 1;
	_screen->fillRect(Common::Rect(panelX, panelY, panelX + panelW, panelY + panelH), _hudCol[kHudPanel]);
	// bevel
	_screen->hLine(panelX, panelY, panelX + panelW - 1, _hudCol[kHudPanelEdge]);
	_screen->hLine(panelX, panelY + panelH - 1, panelX + panelW - 1, _hudCol[kHudPanelLight]);
	_screen->vLine(panelX, panelY, panelY + panelH - 1, _hudCol[kHudPanelEdge]);
	_screen->vLine(panelX + panelW - 1, panelY, panelY + panelH - 1, _hudCol[kHudPanelLight]);

	// Score: five 7-seg digits, green on dark, like the Amiga panel.
	const int digitH = panelH - 6;
	const int digitW = digitH * 5 / 8 + 3;
	int x = panelX + 8;
	const int y = panelY + 3;
	int32 score = CLIP<int32>(_score, 0, 99999);
	for (int i = 0; i < 5; i++) {
		int div = 10000;
		for (int j = 0; j < i; j++) {
			div /= 10;
		}
		drawSevenSegDigit(x, y, digitH, (score / div) % 10, _hudCol[kHudGreen], _hudCol[kHudGreenDim]);
		x += digitW;
	}

	// Lives: three little bars right of the score, topmost lost first,
	// mirroring the DOS build's green/yellow/red stack.
	const int barX = x + 8;
	const uint8 barColors[3] = {_hudCol[kHudGreen], _hudCol[kHudYellow], _hudCol[kHudRed]};
	for (int i = 0; i < 3; i++) {
		const int needed = 3 - i; // top bar needs 3 lives, bottom needs 1
		const uint8 color = (_lives >= needed) ? barColors[i] : _hudCol[kHudDotOff];
		const int by = y + i * (digitH / 3);
		_screen->fillRect(Common::Rect(barX, by, barX + 14, by + digitH / 3 - 2), color);
	}

	// Shots: a 5x2 grid of dots, Amiga style.
	const int dotsX = barX + 24;
	const int dot = MAX(3, (digitH - 4) / 2);
	for (int i = 0; i < 10; i++) {
		const int dx = dotsX + (i % 5) * (dot + 4);
		const int dy = y + 1 + (i / 5) * (dot + 3);
		const uint8 color = (i < _shots) ? _hudCol[kHudYellow] : _hudCol[kHudDotOff];
		_screen->fillRect(Common::Rect(dx, dy, dx + dot, dy + dot), color);
	}
}

void GameSpacePiratesRM::drawInterface() {
	if (_inMenu) {
		return; // the menu owns the whole screen
	}
	// The difficulty screen and menu paint the whole screen with border art;
	// in play only the video rect is refilled, so clear the pillars here.
	_screen->fillRect(Common::Rect(0, 0, kDstX, kHudTop), 0);
	_screen->fillRect(Common::Rect(kDstX + kDstW, 0, _screen->w, kHudTop), 0);
	drawHud();
}

} // End of namespace Alg
