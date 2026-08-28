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
 * The scene file is the real DOS release's sp.scn, verbatim, so every scene
 * and zone bound stays in its own units: 29.97fps master frames, three per
 * .LIB picture, with each clip's first frame at the scene's start value.
 * Only the pictures come from somewhere else: sprm.map names, per scene, the
 * .mpg file that carries its footage and the master frame to start at.
 * Windows clips for the scenes the reissue kept at full length, per-scene
 * transcodes of the DOS .MM clips for the rest, so an upgraded source for
 * any scene is a drop-in. (The SPMPEG.SCN/SP.MPG transcode is NOT used - its
 * scene file is known-broken.)
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
	// For now a right click reloads wherever it lands - there is no visible
	// holster region without the border art.
	bool weaponDown() override;

private:
	struct MapEntry {
		Common::String file;
		uint32 startUnit; // 29.97fps frames into that file
		// The reels carry each take's perfect run; the miss consequence is a
		// separate insert. When the scene clock passes tailAt (scene-relative)
		// the pictures switch to tailFile - only a miss ever gets there.
		Common::String tailFile;
		uint32 tailStart = 0;
		int32 tailAt = -1;
	};
	Common::HashMap<Common::String, MapEntry> _map;
	uint32 _curStartUnit = 0;
	int32 _tailAt = -1;            // scene-relative switch point, -1 = none
	Common::String _tailFile;
	uint32 _tailStart = 0;
	uint32 _tailBase = 0;          // scene-relative offset the tail resumes at
	AlgMpegDecoder *_mpeg = nullptr;

	void loadMap();
	Common::Point dosToScreen(int16 x, int16 y) const;
	Common::Rect dosRectToScreen(const Common::Rect &rect) const;
	void drawMenuBackground();

	// Amiga-style HUD
	void hudInit();
	void drawHud();
	void drawArtChip(int16 srcL, int16 srcT, int16 srcR, int16 srcB,
	                 int16 dstL, int16 dstT, int16 dstR, int16 dstB);
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
