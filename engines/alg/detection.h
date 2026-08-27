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

#ifndef ALG_DETECTION_H
#define ALG_DETECTION_H

#include "engines/advancedDetector.h"

namespace Alg {

enum AlgGameType {
	GType_CRIME_PATROL,
	GType_DRUG_WARS,
	GType_WSJR,
	GType_LAST_BOUNTY_HUNTER,
	GType_MADDOG,
	GType_MADDOG2,
	GType_SPACE_PIRATES,
};

enum AlgGameFeatures {
	/**
	 * The ReelMagic release of a game: one MPEG-1 program stream holding every
	 * clip, in place of the .LIB video archive, with the scene file carrying
	 * byte offsets into it rather than frame numbers.
	 */
	GF_REELMAGIC = 1 << 0,
	/**
	 * The Digital Leisure Windows reissue: one MPEG-1 file per state instead of
	 * a single stream, and a scene file recovered from the executable.
	 */
	GF_WINDOWS = 1 << 1
};

struct AlgGameDescription {
	AD_GAME_DESCRIPTION_HELPERS(desc);

	ADGameDescription desc;
	uint8 gameType;
	uint32 features;
};

#define GAMEOPTION_SINGLE_SPEED_VERSION		GUIO_GAMEOPTIONS1

} // End of namespace Alg

#endif // ALG_DETECTION_H
