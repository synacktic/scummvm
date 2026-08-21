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

#ifndef VIDEO_REELMAGIC_H
#define VIDEO_REELMAGIC_H

#include "common/scummsys.h"

/*
 * ReelMagic was a family of hardware MPEG-1 decoder cards by Sigma Designs,
 * built around their EM7010/EM7011 chips. It is a platform rather than one
 * game's quirk - a handful of DOS titles shipped assets that only its cards
 * would play - so the stream handling lives here rather than in any one engine.
 *
 * The format's deviations from MPEG-1 were first worked out by Jon Dennis
 * (jrdennisoss) for his ReelMagic fork of DOSBox, later merged into DOSBox
 * Staging. No code is taken from either; this is an independent implementation
 * from the format description, and the debt is one of documentation, not source.
 */

namespace Common {
class SeekableReadStream;
}

namespace Video {

/**
 * Turns "magical" ReelMagic MPEG-1 streams back into standard ones.
 *
 * Titles shipped for these cards commonly protected their assets so that only a
 * ReelMagic card would play them. Such a stream deviates from MPEG-1 in two
 * ways: the sequence header carries a reserved frame rate code (> 0x8), and the
 * f_code fields of every P and B picture header hold scrambled values. The card
 * recovered the real values from a 32 bit "magic key" that the title handed to
 * FMPDRV.EXE before opening an asset - a different key per title, though most
 * use the same one (see kDefaultMagicKey).
 *
 * Both deviations are plain bit fields at fixed positions in their headers, so
 * a stream can be restored without re-encoding any picture data, which lets the
 * ordinary MPEG decoder play the original assets. Not every asset of a given
 * title is necessarily protected - isMagical() answers that per stream.
 */
class ReelMagicStream {
public:
	/**
	 * The card's own default key, which most titles set explicitly anyway.
	 *
	 * The key is a property of the card, not of any one game: the driver takes
	 * it through a documented call (FMPDRV.EXE function 9, subfunction 0210h
	 * "Set Magic Key") and defaults to this value when a title provides none.
	 * Titles do choose their own - The Horde uses 0xC39D7088 - so a future
	 * consumer should pass its own key rather than assume this one.
	 */
	static const uint32 kDefaultMagicKey = 0x40044041;

	/**
	 * Checks whether a stream is a "magical" MPEG-1 stream, i.e. whether its
	 * sequence header carries a reserved frame rate code. The stream position
	 * is preserved.
	 */
	static bool isMagical(Common::SeekableReadStream &stream);

	/**
	 * Reads a stream into memory and makes it playable by a standard decoder:
	 * with @p magical the frame rate code and every P/B picture f_code are
	 * restored, and in all cases the audio padding bits are corrected. Returns
	 * nullptr if the stream could not be read. The caller owns the result.
	 */
	static Common::SeekableReadStream *unlock(Common::SeekableReadStream &stream, uint32 magicKey,
	                                          bool magical = true);

	/**
	 * Patches an in-memory MPEG-1 program stream in place. Exposed separately
	 * so that it can be exercised without a stream. Returns the number of
	 * picture headers that were patched.
	 */
	static uint32 unlockBuffer(byte *data, uint32 size, uint32 magicKey);

	/**
	 * Corrects the padding bit of the MPEG-1 Layer II audio frames of an
	 * in-memory program stream, and returns how many needed it.
	 *
	 * The encoder of these assets marks frames as padded which it then did not
	 * pad. Every standard decoder trusts the bit, steps a byte beyond the next
	 * frame, and has to hunt for the sync word again, losing audio each time.
	 * That is what the sound stuttering through a cutscene is, and it drags the
	 * lip sync along with it, since the audio loses time the picture does not.
	 * No frame data is touched, only the one bit in each header.
	 */
	static uint32 fixAudioPadding(byte *data, uint32 size);

	/**
	 * Reads the frame duration out of the sequence header, in milliseconds
	 * expressed as @p num / @p den. Returns false if no sequence header was
	 * found. Works on both plain and still "magical" streams.
	 */
	static bool getFrameDuration(Common::SeekableReadStream &stream, uint32 &num, uint32 &den);

	/**
	 * Whether the first four bytes of a stream form an MPEG-1 pack header, i.e.
	 * whether it is a program stream rather than a bare video elementary
	 * stream. Callers need the distinction because a program stream carries
	 * audio, so it has to be read into memory for its padding bits to be
	 * corrected even when its video is not "magical".
	 */
	static bool isProgramStream(const byte *header);
};

} // End of namespace Video

#endif // VIDEO_REELMAGIC_H
