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
#include "common/savefile.h"
#include "engines/util.h"

#include "alg/alg.h"
#include "alg/detection.h"
#include "alg/game.h"
#include "alg/logic/game_bountyhunter.h"
#include "alg/logic/game_crimepatrol.h"
#include "alg/logic/game_drugwars.h"
#include "alg/logic/game_johnnyrock.h"
#include "alg/logic/game_maddog.h"
#include "alg/logic/game_maddog2.h"
#include "alg/logic/game_spacepirates.h"
#include "alg/logic/game_spacepirates_rm.h"
#include "alg/logic/game_spacepirates_win.h"

namespace Alg {

AlgEngine::AlgEngine(OSystem *syst, const AlgGameDescription *gd)
	: Engine(syst), _gameDescription(gd) {
	switch (gd->gameType) {
	case GType_CRIME_PATROL: {
		GameCrimePatrol *game = new GameCrimePatrol(this, gd);
		_debugger = new DebuggerCrimePatrol(game);
		_game = game;
		break;
	}
	case GType_DRUG_WARS: {
		GameDrugWars *game = new GameDrugWars(this, gd);
		_debugger = new DebuggerDrugWars(game);
		_game = game;
		break;
	}
	case GType_WSJR: {
		GameJohnnyRock *game = new GameJohnnyRock(this, gd);
		_debugger = new DebuggerJohnnyRock(game);
		_game = game;
		break;
	}
	case GType_LAST_BOUNTY_HUNTER: {
		GameBountyHunter *game = new GameBountyHunter(this, gd);
		_debugger = new DebuggerBountyHunter(game);
		_game = game;
		break;
	}
	case GType_MADDOG: {
		GameMaddog *game = new GameMaddog(this, gd);
		_debugger = new DebuggerMaddog(game);
		_game = game;
		break;
	}
	case GType_MADDOG2: {
		GameMaddog2 *game = new GameMaddog2(this, gd);
		_debugger = new DebuggerMaddog2(game);
		_game = game;
		break;
	}
	case GType_SPACE_PIRATES: {
		if (isWindows()) {
			// The Windows reissue is a different program with the same content,
			// so it gets its own logic rather than flags inside the DOS one.
			_game = new GameSpacePiratesWin(this, gd);
		} else if (isHybrid()) {
			// SPRM: the DOS logic playing the Windows release's footage.
			GameSpacePirates *game = new GameSpacePiratesRM(this, gd);
			_debugger = new DebuggerSpacePirates(game);
			_game = game;
		} else {
			GameSpacePirates *game = new GameSpacePirates(this, gd);
			_debugger = new DebuggerSpacePirates(game);
			_game = game;
		}
		break;
	}
	}
}

AlgEngine::~AlgEngine() {
	delete _game;
}

Common::Error AlgEngine::run() {
	if (isReelMagic()) {
		// The ReelMagic releases mix RGB MPEG pictures with the 8 bit interface
		// art, so the composite Game::updateScreen() builds needs true colour.
		Graphics::PixelFormat format(2, 5, 6, 5, 0, 11, 5, 0, 0);
		Common::List<Graphics::PixelFormat> supported = _system->getSupportedFormats();
		for (Common::List<Graphics::PixelFormat>::const_iterator it = supported.begin(); it != supported.end(); ++it) {
			if (it->bytesPerPixel == 2 || it->bytesPerPixel == 4) {
				format = *it;
				break;
			}
		}
		// The Windows reissue draws its own 352x240 picture rather than the
		// DOS releases' 320x200 screen, so give it the picture's own size and
		// leave the video unscaled.
		const bool fullPicture = isWindows() || isHybrid();
		initGraphics(fullPicture ? 352 : 320, fullPicture ? 240 : 200, &format);
	} else {
		initGraphics(320, 200);
	}
	setDebugger(_debugger);
	if (ConfMan.hasKey("single_speed_videos")) {
		_useSingleSpeedVideos = ConfMan.getBool("single_speed_videos");
	}
	return _game->run();
}

Common::Error AlgEngine::loadGameState(int slot) {
	Common::InSaveFile *inSaveFile = _saveFileMan->openForLoading(getSaveStateName(0));

	Common::Error result = _game->loadState(inSaveFile) ? Common::kNoError : Common::kReadingFailed;

	delete inSaveFile;
	return result;
}

bool AlgEngine::canLoadGameStateCurrently(Common::U32String *msg) {
	return true;
}

Common::Error AlgEngine::saveGameState(int slot, const Common::String &desc, bool isAutosave) {
	Common::OutSaveFile *outSaveFile = _saveFileMan->openForSaving(getSaveStateName(0));

	if (!outSaveFile) {
		return Common::kWritingFailed;
	}

	Common::Error result = _game->saveState(outSaveFile) ? Common::kNoError : Common::kWritingFailed;
	if (result.getCode() == Common::kNoError) {
		getMetaEngine()->appendExtendedSave(outSaveFile, getTotalPlayTime(), desc, isAutosave);
	}

	delete outSaveFile;
	return result;
}

bool AlgEngine::canSaveGameStateCurrently(Common::U32String *msg) {
	return true;
}

bool AlgEngine::hasFeature(EngineFeature f) const {
	return (f == kSupportsLoadingDuringRuntime) ||
		   (f == kSupportsSavingDuringRuntime);
}

} // End of namespace Alg
