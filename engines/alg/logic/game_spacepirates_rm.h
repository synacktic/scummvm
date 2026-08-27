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

#ifndef ALG_GAME_SPACEPIRATES_RM_H
#define ALG_GAME_SPACEPIRATES_RM_H

#include "alg/logic/game_spacepirates.h"

namespace Alg {

/**
 * SPRM: our own hybrid build of Space Pirates, for testing the DOS release's
 * logic against the Windows release's footage. NOT a real release and never
 * to be upstreamed - the DOS game (spmpeg.scn) is the only source of truth.
 *
 * The scene file is the DOS ReelMagic build's spmpeg.scn, verbatim, so every
 * scene and zone bound stays in that file's own units (byte offsets into the
 * original SP.MPG at 20000 bytes per tenth of a second). Only the pictures
 * come from somewhere else: sprm.map names, per scene, the .mpg file that
 * carries its footage and the tenth-of-a-second to start at. Windows clips
 * for the scenes the reissue kept, per-scene extracts of the DOS stream for
 * the rest, so an upgraded source for any scene is a drop-in.
 *
 * Presentation differs deliberately: no border art - the picture fills the
 * screen at (16,0) 320x218, aspect kept, with an Amiga-style HUD strip in
 * the letterbox below it, where it can never cover a target rectangle.
 * All input is mapped into the DOS build's 320x200 screen space before the
 * logic sees it, so rects, menus and cursor regions run untouched.
 */
class GameSpacePiratesRM : public GameSpacePirates {
public:
	GameSpacePiratesRM(AlgEngine *vm, const AlgGameDescription *gd);
	~GameSpacePiratesRM() override;

protected:
	// Geometry: the DOS build showed the 288x186 picture at (11,2) in its
	// 320x200 screen. Here the same picture is 320x218 at (16,0).
	static const int kSrcX = 11, kSrcY = 2, kSrcW = 288, kSrcH = 186;
	static const int kDstX = 16, kDstY = 0, kDstW = 320, kDstH = 218;
	static const int kHudTop = 218;

	void init() override;
	bool loadScene(Scene *scene) override;
	uint32 getFrame(Scene *scene) override;
	Common::Point transformInput(const Common::Point &point) const override;
	void drawInterface() override;
	void debug_drawZoneRects() override;

	// Presentation the base draws in DOS screen space
	void doMenu() override;
	void showDifficulty(uint8 newDifficulty, bool updateCursor) override;
	void displayLivesLeft() override {}
	void displayScores() override {}
	void displayShotsLeft() override {}
	void displayShotFiredImage(Common::Point *point) override;
	void drawWorldCrossout(int32 centerX, int32 centerY) override;

private:
	struct MapEntry {
		Common::String file;
		uint32 startUnit; // tenths of a second into that file
	};
	Common::HashMap<Common::String, MapEntry> _map;
	uint32 _mapUnit = 20000; // SP.MPG bytes per tenth (UNIT line of sprm.map)
	uint32 _curStartUnit = 0;
	AlgMpegDecoder *_mpeg = nullptr;

	void loadMap();
	Common::Point dosToScreen(int16 x, int16 y) const;
	Common::Rect dosRectToScreen(const Common::Rect &rect) const;
	void drawMenuBackground();

	// Amiga-style HUD
	void hudInit();
	void drawHud();
	void drawSevenSegDigit(int x, int y, int h, int digit, uint8 on, uint8 off);
	enum HudColor {
		kHudPanel = 0,
		kHudPanelLight,
		kHudPanelEdge,
		kHudGreen,
		kHudGreenDim,
		kHudYellow,
		kHudDotOff,
		kHudRed,
		kHudColorCount
	};
	uint8 _hudCol[kHudColorCount];
	bool _hudReady = false;
};

} // End of namespace Alg

#endif
