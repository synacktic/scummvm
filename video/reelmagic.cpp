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

#include "video/reelmagic.h"

#include "common/array.h"
#include "common/debug.h"
#include "common/memstream.h"
#include "common/stream.h"
#include "common/textconsole.h"

namespace Video {

// MPEG-1 start codes (ISO/IEC 11172), preceded by the 00 00 01 prefix
enum {
	kStartCodePicture  = 0x00,
	kStartCodeSequence = 0xB3,
	kStartCodeEnd      = 0xB9,
	kStartCodePack     = 0xBA,
	kStartCodeVideoMin = 0xE0,
	kStartCodeVideoMax = 0xEF,
	kStartCodeAudioMin = 0xC0,
	kStartCodeAudioMax = 0xDF
};

// Picture coding types (ISO/IEC 11172-2 table 6-12). Only P and B pictures
// carry f_code fields, since only they can reference other pictures.
enum {
	kPictureTypeI = 1,
	kPictureTypeP = 2,
	kPictureTypeB = 3
};

// A frame rate code of 0x9 or above is reserved by the standard; the ReelMagic
// assets use that to flag a stream as "magical".
static const byte kMagicalFrameRateCode = 0x9;

// The "delta/delta even pattern" belonging to a magic key. The relation between
// the two is not understood, so the known pairs are simply tabulated.
struct MagicKeyPattern {
	uint32 key;
	byte evenPattern[4];
};

static const MagicKeyPattern kMagicKeyPatterns[] = {
	{ 0x40044041, { 4, 3, 2, 3 } },	// Card default, provisioned by Return to Zork
	{ 0xC39D7088, { 1, 3, 3, 3 } }	// Only seen in The Horde so far
};

// The delta pattern repeats after this many temporal sequence numbers.
static const uint kDeltaPeriod = 56;

/**
 * Computes the f_code delta belonging to a picture's temporal sequence number.
 */
static byte computeDeltaFCode(uint tsn, const byte *evenPattern) {
	uint result = 2;
	for (uint i = 0; i <= tsn; i++) {
		if ((i & 1) == 0)
			result += evenPattern[(i >> 1) & 3];
		else
			result += 6;	// Every odd entry of the pattern is always 6
	}
	return result % 7;
}

/**
 * Applies a delta to a scrambled f_code. Legal f_code values are 1 to 7, so the
 * addition wraps within that range.
 */
static byte recoverFCode(byte encoded, byte delta) {
	int value = ((int)encoded - 1 + (int)delta) % 7;
	if (value < 0)
		value += 7;
	return (byte)(value + 1);
}

/** A stretch of video elementary stream data inside the source buffer. */
struct VideoRange {
	uint32 offset;
	uint32 size;
};

/**
 * Collects the video elementary stream payloads of an MPEG-1 program stream.
 *
 * For a bare elementary stream (the short overlay animations of the game are
 * stored that way) the whole buffer is returned as a single range.
 */
static void collectPesRanges(const byte *data, uint32 size, bool audio, Common::Array<VideoRange> &ranges) {
	const byte streamMin = audio ? kStartCodeAudioMin : kStartCodeVideoMin;
	const byte streamMax = audio ? kStartCodeAudioMax : kStartCodeVideoMax;

	// A bare elementary stream starts with a sequence header rather than a pack.
	// It is video by definition, and carries no audio to collect.
	if (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 1 && data[3] != kStartCodePack) {
		if (audio)
			return;

		VideoRange range;
		range.offset = 0;
		range.size = size;
		ranges.push_back(range);
		return;
	}

	uint32 pos = 0;
	while (pos + 4 <= size) {
		if (data[pos] != 0 || data[pos + 1] != 0 || data[pos + 2] != 1) {
			pos++;
			continue;
		}

		const byte streamId = data[pos + 3];
		pos += 4;

		if (streamId == kStartCodePack) {
			pos += 8;	// System clock reference and multiplex rate
			continue;
		}

		if (streamId == kStartCodeEnd)
			break;

		if (pos + 2 > size)
			break;

		const uint32 packetSize = (data[pos] << 8) | data[pos + 1];
		pos += 2;
		if (pos + packetSize > size)
			break;

		if (streamId >= streamMin && streamId <= streamMax) {
			// Skip the MPEG-1 packet header: stuffing, an optional buffer scale
			// and size, and optional presentation/decoding time stamps.
			uint32 skip = 0;
			while (skip < packetSize && data[pos + skip] == 0xFF)
				skip++;
			if (skip < packetSize && (data[pos + skip] & 0xC0) == 0x40)
				skip += 2;
			if (skip < packetSize) {
				const byte flags = data[pos + skip] & 0xF0;
				if (flags == 0x20)
					skip += 5;
				else if (flags == 0x30)
					skip += 10;
				else if (data[pos + skip] == 0x0F)
					skip += 1;
			}

			if (skip < packetSize) {
				VideoRange range;
				range.offset = pos + skip;
				range.size = packetSize - skip;
				ranges.push_back(range);
			}
		}

		pos += packetSize;
	}
}

/** Layer II bit rates in kbit/s, ISO/IEC 11172-3 table 3-B.2, MPEG-1. */
static const uint16 kLayer2BitRates[] = {
	0, 32, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 0
};

/** Sampling frequencies in Hz, ISO/IEC 11172-3 table 3-B.1, MPEG-1. */
static const uint32 kLayer2SampleRates[] = { 44100, 48000, 32000, 0 };

/** Whether a Layer II frame header for MPEG-1 audio starts here. */
static bool isLayer2Header(const byte *p) {
	// Sync word, then version 3 (MPEG-1) and layer 2 (Layer II)
	return p[0] == 0xFF && (p[1] & 0xE0) == 0xE0 &&
	       ((p[1] >> 3) & 0x03) == 0x03 && ((p[1] >> 1) & 0x03) == 0x02;
}

uint32 ReelMagicStream::fixAudioPadding(byte *data, uint32 size) {
	Common::Array<VideoRange> ranges;
	collectPesRanges(data, size, true, ranges);
	if (ranges.empty())
		return 0;

	// As with the video, copy the elementary stream out of its packets so that
	// frames straddling a packet boundary can be followed. Only single bits are
	// flipped, so every byte maps straight back onto the byte it came from.
	uint32 audioSize = 0;
	for (uint i = 0; i < ranges.size(); i++)
		audioSize += ranges[i].size;

	byte *audio = (byte *)malloc(audioSize);
	if (!audio)
		return 0;

	Common::Array<uint32> starts;
	starts.reserve(ranges.size());
	uint32 audioPos = 0;
	for (uint i = 0; i < ranges.size(); i++) {
		starts.push_back(audioPos);
		memcpy(audio + audioPos, data + ranges[i].offset, ranges[i].size);
		audioPos += ranges[i].size;
	}

	uint rangeHint = 0;
	auto audioToSource = [&](uint32 offset) -> uint32 {
		while (rangeHint + 1 < ranges.size() && starts[rangeHint + 1] <= offset)
			rangeHint++;
		while (rangeHint > 0 && starts[rangeHint] > offset)
			rangeHint--;
		return ranges[rangeHint].offset + (offset - starts[rangeHint]);
	};

	uint32 patched = 0;
	uint32 pos = 0;

	while (pos + 4 <= audioSize) {
		if (!isLayer2Header(audio + pos)) {
			pos++;
			continue;
		}

		const uint16 bitRate = kLayer2BitRates[(audio[pos + 2] >> 4) & 0x0F];
		const uint32 sampleRate = kLayer2SampleRates[(audio[pos + 2] >> 2) & 0x03];
		if (bitRate == 0 || sampleRate == 0) {
			pos++;
			continue;
		}

		const bool padded = ((audio[pos + 2] >> 1) & 0x01) != 0;
		const uint32 base = (144 * (uint32)bitRate * 1000) / sampleRate;
		uint32 length = base + (padded ? 1 : 0);

		// The encoder of these assets sets the padding bit on frames it did not
		// actually pad. A decoder believing it steps one byte past the next
		// frame, loses the sync word, and has to hunt for it again, dropping
		// audio each time: 245 such frames in the intro cost 12.8 seconds of its
		// 104. If the frame really is a byte shorter, correct the bit.
		if (padded && pos + length + 4 <= audioSize &&
		    !isLayer2Header(audio + pos + length) &&
		    isLayer2Header(audio + pos + length - 1)) {
			data[audioToSource(pos + 2)] &= ~0x02;
			audio[pos + 2] &= ~0x02;
			length--;
			patched++;
		}

		pos += length;
	}

	free(audio);
	return patched;
}

uint32 ReelMagicStream::unlockBuffer(byte *data, uint32 size, uint32 magicKey) {
	const byte *evenPattern = nullptr;
	for (uint i = 0; i < ARRAYSIZE(kMagicKeyPatterns); i++) {
		if (kMagicKeyPatterns[i].key == magicKey) {
			evenPattern = kMagicKeyPatterns[i].evenPattern;
			break;
		}
	}

	if (!evenPattern) {
		warning("ReelMagicStream: unknown magic key 0x%08X, falling back to the default key", magicKey);
		evenPattern = kMagicKeyPatterns[0].evenPattern;
	}

	byte deltaTable[kDeltaPeriod];
	for (uint tsn = 0; tsn < kDeltaPeriod; tsn++)
		deltaTable[tsn] = computeDeltaFCode(tsn, evenPattern);

	Common::Array<VideoRange> ranges;
	collectPesRanges(data, size, false, ranges);
	if (ranges.empty())
		return 0;

	// Copy the elementary stream out of its packets so that picture headers
	// straddling a packet boundary can be parsed contiguously. Patching never
	// changes a field's width, so every patched byte maps straight back onto
	// the byte it came from.
	uint32 videoSize = 0;
	for (uint i = 0; i < ranges.size(); i++)
		videoSize += ranges[i].size;

	byte *video = (byte *)malloc(videoSize);
	if (!video)
		return 0;

	Common::Array<uint32> starts;
	starts.reserve(ranges.size());
	uint32 videoPos = 0;
	for (uint i = 0; i < ranges.size(); i++) {
		starts.push_back(videoPos);
		memcpy(video + videoPos, data + ranges[i].offset, ranges[i].size);
		videoPos += ranges[i].size;
	}

	// Maps an offset within the elementary stream back onto the source buffer
	uint rangeHint = 0;
	auto videoToSource = [&](uint32 offset) -> uint32 {
		while (rangeHint + 1 < ranges.size() && starts[rangeHint + 1] <= offset)
			rangeHint++;
		while (rangeHint > 0 && starts[rangeHint] > offset)
			rangeHint--;
		return ranges[rangeHint].offset + (offset - starts[rangeHint]);
	};

	uint32 patchedPictures = 0;
	for (uint32 pos = 0; pos + 9 <= videoSize; pos++) {
		if (video[pos] != 0 || video[pos + 1] != 0 || video[pos + 2] != 1)
			continue;

		const byte streamId = video[pos + 3];
		const byte *header = video + pos + 4;

		if (streamId == kStartCodeSequence) {
			// Byte 3 of the sequence header holds the aspect ratio in its high
			// nibble and the frame rate code in its low nibble.
			if ((header[3] & 0x0F) >= kMagicalFrameRateCode)
				data[videoToSource(pos + 7)] = header[3] & 0xF7;
			continue;
		}

		if (streamId != kStartCodePicture)
			continue;

		const uint tsn = (header[0] << 2) | (header[1] >> 6);
		const byte pictureType = (header[1] >> 3) & 0x07;
		if (pictureType != kPictureTypeP && pictureType != kPictureTypeB)
			continue;

		// The f_code fields sit at fixed bit positions: the forward code spans
		// the low two bits of byte 3 and the top bit of byte 4, the backward
		// code the next three bits of byte 4. Both must be read before either
		// is written, since writing the forward code masks the backward one.
		const byte delta = deltaTable[tsn % kDeltaPeriod];
		const byte forward = ((header[3] & 0x03) << 1) | (header[4] >> 7);
		const byte backward = (header[4] >> 3) & 0x07;

		const byte newForward = recoverFCode(forward, delta);
		byte byte3 = (header[3] & 0xFC) | ((newForward >> 1) & 0x03);
		byte byte4 = (header[4] & 0x47) | ((newForward & 0x01) << 7);

		if (pictureType == kPictureTypeB) {
			const byte newBackward = recoverFCode(backward, delta);
			byte4 = (byte4 & 0xC7) | ((newBackward & 0x07) << 3);
		} else {
			// A P picture has no backward code; bits 5 to 0 are zero padding
			byte4 = (byte4 & 0xC7) | (header[4] & 0x38);
		}

		data[videoToSource(pos + 7)] = byte3;
		data[videoToSource(pos + 8)] = byte4;
		patchedPictures++;
	}

	free(video);
	return patchedPictures;
}

bool ReelMagicStream::getFrameDuration(Common::SeekableReadStream &stream, uint32 &num, uint32 &den) {
	// ISO/IEC 11172-2 table 6-4. The entries whose rate is a multiple of 1000/1001
	// are expressed with a numerator of 1001.
	static const struct {
		uint32 num, den;
	} kFrameDurations[] = {
		{    0,  0 },	// forbidden
		{ 1001, 24 },	// 24000/1001
		{ 1000, 24 },
		{ 1000, 25 },
		{ 1001, 30 },	// 30000/1001
		{ 1000, 30 },
		{ 1000, 50 },
		{ 1001, 60 },	// 60000/1001
		{ 1000, 60 }
	};

	const int64 startPos = stream.pos();
	bool found = false;

	const uint32 kScanSize = 16 * 1024;
	const uint32 size = MIN<uint32>(kScanSize, (uint32)(stream.size() - startPos));
	byte *buffer = (byte *)malloc(size);
	if (buffer) {
		const uint32 read = stream.read(buffer, size);
		for (uint32 i = 0; i + 8 <= read; i++) {
			if (buffer[i] == 0 && buffer[i + 1] == 0 && buffer[i + 2] == 1 &&
			    buffer[i + 3] == kStartCodeSequence) {
				// Mask off the "magical" bit in case the stream is still locked
				const byte code = buffer[i + 7] & 0x07;
				if (code > 0 && code < ARRAYSIZE(kFrameDurations)) {
					num = kFrameDurations[code].num;
					den = kFrameDurations[code].den;
					found = true;
				}
				break;
			}
		}
		free(buffer);
	}

	stream.seek(startPos);
	return found;
}

bool ReelMagicStream::isProgramStream(const byte *header) {
	return header[0] == 0 && header[1] == 0 && header[2] == 1 && header[3] == kStartCodePack;
}

bool ReelMagicStream::isMagical(Common::SeekableReadStream &stream) {
	const int64 startPos = stream.pos();
	bool magical = false;

	// The sequence header is close to the start of the stream; scanning the
	// first few kilobytes is enough to find it for every known asset.
	const uint32 kScanSize = 16 * 1024;
	const uint32 size = MIN<uint32>(kScanSize, (uint32)(stream.size() - startPos));
	byte *buffer = (byte *)malloc(size);
	if (buffer) {
		const uint32 read = stream.read(buffer, size);
		for (uint32 i = 0; i + 8 <= read; i++) {
			if (buffer[i] == 0 && buffer[i + 1] == 0 && buffer[i + 2] == 1 &&
			    buffer[i + 3] == kStartCodeSequence) {
				magical = (buffer[i + 7] & 0x0F) >= kMagicalFrameRateCode;
				break;
			}
		}
		free(buffer);
	}

	stream.seek(startPos);
	return magical;
}

Common::SeekableReadStream *ReelMagicStream::unlock(Common::SeekableReadStream &stream, uint32 magicKey, bool magical) {
	const int64 startPos = stream.pos();
	const int64 size = stream.size() - startPos;
	if (size <= 0)
		return nullptr;

	byte *data = (byte *)malloc((uint32)size);
	if (!data)
		return nullptr;

	if (stream.read(data, (uint32)size) != (uint32)size) {
		free(data);
		return nullptr;
	}

	if (magical) {
		const uint32 patched = unlockBuffer(data, (uint32)size, magicKey);
		debug(1, "ReelMagicStream: restored the f_code of %d picture headers", patched);
	}

	const uint32 repadded = fixAudioPadding(data, (uint32)size);
	if (repadded)
		debug(1, "ReelMagicStream: corrected the padding bit of %d audio frames", repadded);

	return new Common::MemoryReadStream(data, (uint32)size, DisposeAfterUse::YES);
}
} // End of namespace Video
