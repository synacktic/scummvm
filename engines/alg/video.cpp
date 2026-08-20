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

#include "common/textconsole.h"
#include "graphics/surface.h"
#include "audio/decoders/raw.h"
#include "alg/video.h"
#include "common/textconsole.h"
#include "graphics/surface.h"
#include "video/mpegps_decoder.h"

namespace Alg {

AlgVideoDecoder::AlgVideoDecoder() {
	_frame = nullptr;
	_audioStream = nullptr;
}

AlgVideoDecoder::~AlgVideoDecoder() {
	if (_frame) {
		_frame->free();
		delete _frame;
	}

	delete _audioStream;
}

void AlgVideoDecoder::loadVideoFromStream(uint32 offset) {
	_input->seek(offset);
	_size = _input->readUint32LE();
	_currentFrame = 0;
	uint16 chunkType = _input->readUint16LE();
	uint32 chunkSize = _input->readUint32LE();
	_numChunks = _input->readUint16LE();
	_frameRate = _input->readUint16LE();
	_videoMode = _input->readUint16LE();
	_width = _input->readUint16LE();
	_height = _input->readUint16LE();
	uint16 typeRaw = _input->readUint16LE();
	uint16 typeInter = _input->readUint16LE();
	uint16 typeIntraHh = _input->readUint16LE();
	uint16 typeInterHh = _input->readUint16LE();
	uint16 typeIntraHhv = _input->readUint16LE();
	uint16 typeInterHhv = _input->readUint16LE();
	(void)chunkType;
	(void)typeRaw;
	(void)typeInter;
	(void)typeIntraHh;
	(void)typeInterHh;
	(void)typeIntraHhv;
	(void)typeInterHhv;
	if (chunkSize == 0x18) {
		_audioType = _input->readUint16LE();
	}
	assert(chunkType == 0x00);
	assert(chunkSize == 0x16 || chunkSize == 0x18);
	assert(_frameRate == 10);
	assert(_videoMode == 0x13);
	assert(typeRaw == 0x02);
	assert(typeInter == 0x05);
	assert(typeIntraHh == 0x0c);
	assert(typeInterHh == 0x0d);
	assert(typeIntraHhv == 0x0e);
	assert(typeInterHhv == 0x0f);
	_currentChunk = 0;
	_bytesLeft = _size - chunkSize - 6;
	if (_frame) {
		_frame->free();
		delete _frame;
	}
	delete _audioStream;
	_frame = new Graphics::Surface();
	_frame->create(_width, _height, Graphics::PixelFormat::createFormatCLUT8());
	_audioStream = makePacketizedRawStream(8000, Audio::FLAG_UNSIGNED);
	g_system->getMixer()->stopHandle(_audioHandle);
	g_system->getMixer()->playStream(Audio::Mixer::kPlainSoundType, &_audioHandle, _audioStream, -1, Audio::Mixer::kMaxChannelVolume, 0, DisposeAfterUse::NO);
}

void AlgVideoDecoder::skipNumberOfFrames(uint32 num) {
	uint32 videoFramesSkipped = 0;
	while (videoFramesSkipped < num && _bytesLeft > 0) {
		uint16 chunkType = _input->readUint16LE();
		uint32 chunkSize = _input->readUint32LE();
		_currentChunk++;
		switch (chunkType) {
		case MKTAG16(0x00, 0x08):
		case MKTAG16(0x00, 0x0c):
		case MKTAG16(0x00, 0x0e):
		case MKTAG16(0x00, 0x05):
		case MKTAG16(0x00, 0x0d):
		case MKTAG16(0x00, 0x0f):
		case MKTAG16(0x00, 0x02):
			videoFramesSkipped++;
			_currentFrame++;
			break;
		}
		_input->skip(chunkSize);
		_bytesLeft -= chunkSize + 6;
	}
	// find next keyframe
	bool nextKeyframeFound = false;
	while (!nextKeyframeFound && _bytesLeft > 0) {
		uint16 chunkType = _input->readUint16LE();
		uint32 chunkSize = _input->readUint32LE();
		_currentChunk++;
		switch (chunkType) {
		case MKTAG16(0x00, 0x08):
		case MKTAG16(0x00, 0x0c):
		case MKTAG16(0x00, 0x0e):
			nextKeyframeFound = true;
			_input->seek(-6, SEEK_CUR);
			break;
		case MKTAG16(0x00, 0x05):
		case MKTAG16(0x00, 0x0d):
		case MKTAG16(0x00, 0x0f):
		case MKTAG16(0x00, 0x02):
			_input->skip(chunkSize);
			_bytesLeft -= chunkSize + 6;
			videoFramesSkipped++;
			_currentFrame++;
			break;
		default:
			_input->skip(chunkSize);
			_bytesLeft -= chunkSize + 6;
		}
	}
}

void AlgVideoDecoder::readNextChunk() {
	uint16 chunkType = _input->readUint16LE();
	uint32 chunkSize = _input->readUint32LE();
	_currentChunk++;
	switch (chunkType) {
	case MKTAG16(0x00, 0x00):
		error("AlgVideoDecoder::readNextChunk(): got repeated header chunk");
		break;
	case MKTAG16(0x00, 0x30):
		updatePalette(chunkSize, false);
		break;
	case MKTAG16(0x00, 0x31):
		updatePalette(chunkSize, true);
		break;
	case MKTAG16(0x00, 0x15):
		readAudioData(chunkSize, 8000);
		break;
	case MKTAG16(0x00, 0x16):
		readAudioData(chunkSize, 11000);
		break;
	case MKTAG16(0x00, 0x08):
		decodeIntraFrame(chunkSize, 0, 0);
		_gotVideoFrame = true;
		break;
	case MKTAG16(0x00, 0x0c):
		decodeIntraFrame(chunkSize, 1, 0);
		_gotVideoFrame = true;
		break;
	case MKTAG16(0x00, 0x0e):
		decodeIntraFrame(chunkSize, 1, 1);
		_gotVideoFrame = true;
		break;
	case MKTAG16(0x00, 0x05):
		decodeInterFrame(chunkSize, 0, 0);
		_gotVideoFrame = true;
		break;
	case MKTAG16(0x00, 0x0d):
		decodeInterFrame(chunkSize, 1, 0);
		_gotVideoFrame = true;
		break;
	case MKTAG16(0x00, 0x0f):
		decodeInterFrame(chunkSize, 1, 1);
		_gotVideoFrame = true;
		break;
	case MKTAG16(0x00, 0x02):
		warning("AlgVideoDecoder::readNextChunk(): raw video not supported");
		_input->skip(chunkSize);
		break;
	default:
		error("AlgVideoDecoder::readNextChunk(): Unknown chunk encountered: %d", chunkType);
	}
	_bytesLeft -= chunkSize + 6;
}

void AlgVideoDecoder::getNextFrame() {
	_paletteDirty = false;
	_gotVideoFrame = false;
	while (!_gotVideoFrame && _bytesLeft > 0) {
		readNextChunk();
	}
	_currentFrame++;
}

void AlgVideoDecoder::decodeIntraFrame(uint32 size, uint8 hh, uint8 hv) {
	uint16 x = 0, y = 0;
	int32 bytesRemaining = size;
	int32 runLength = 0;
	uint8 readByte, color = 0;
	while (bytesRemaining > 0) {
		readByte = _input->readByte();
		if (readByte & 0x80) {
			runLength = 1;
			color = readByte;
			bytesRemaining--;
		} else {
			runLength = (readByte & 0x7F) + 2;
			color = _input->readByte();
			bytesRemaining -= 2;
		}
		if (color > 0) {
			memset(_frame->getBasePtr(x, y), color, runLength * (1 + hh));
			if (hv) {
				memset(_frame->getBasePtr(x, y + 1), color, runLength * (1 + hh));
			}
		}
		x += runLength + (hh * runLength);
		if (x >= _width) {
			x = 0;
			y += 1 + hv;
		}
	}
	assert(bytesRemaining == 0);
	(void)bytesRemaining;
}

void AlgVideoDecoder::decodeInterFrame(uint32 size, uint8 hh, uint8 hv) {
	uint32 bytesRead = 0;
	uint16 length = 0, x = 0, y = 0, replacementBytesLeft = 0;
	replacementBytesLeft = _input->readUint16LE();
	bytesRead += 2;
	if (replacementBytesLeft == 0) {
		_input->skip(size - 2);
		return;
	}
	Common::SeekableReadStream *replacement = _input->readStream(replacementBytesLeft);
	bytesRead += replacementBytesLeft;
	while (replacementBytesLeft > 1) {
		length = replacement->readByte();
		x = replacement->readByte() + ((length & 0x80) << 1);
		length &= 0x7F;
		replacementBytesLeft -= 2;
		if (length == 0) {
			y += x;
			continue;
		}
		for (uint32 i = 0; i < length; i++) {
			uint8 replaceArray = replacement->readByte();
			for (uint8 j = 0x80; j > 0; j = j >> 1) {
				if (replaceArray & j) {
					uint8 color = _input->readByte();
					bytesRead++;
					memset(_frame->getBasePtr(x, y), color, (1 + hh));
					if (hv) {
						memset(_frame->getBasePtr(x, y + 1), color, (1 + hh));
					}
				}
				x += 1 + hh;
			}
		}
		y += 1 + hv;
	}
	delete replacement;
	assert(bytesRead == size);
	(void)bytesRead;
}

void AlgVideoDecoder::updatePalette(uint32 size, bool partial) {
	_paletteDirty = true;
	uint32 bytesRead = 0;
	uint16 start = 0, count = 256;
	if (partial) {
		start = _input->readUint16LE();
		count = _input->readUint16LE();
		bytesRead += 4;
	}
	uint16 paletteIndex = start * 3;
	for (uint16 i = 0; i < count; i++) {
		uint8 r = _input->readByte() * 4;
		uint8 g = _input->readByte() * 4;
		uint8 b = _input->readByte() * 4;
		_palette[paletteIndex++] = r;
		_palette[paletteIndex++] = g;
		_palette[paletteIndex++] = b;
		bytesRead += 3;
	}
	assert(bytesRead == size);
	(void)bytesRead;
}

void AlgVideoDecoder::readAudioData(uint32 size, uint16 rate) {
	assert(_audioType == 21);
	(void)_audioType;
	_audioStream->queuePacket(_input->readStream(size));
}

// ---------------------------------------------------------------------------
// AlgMpegDecoder - the ReelMagic releases: one MPEG-1 program stream, clips
// addressed by byte offset. See the class comment in video.h.
// ---------------------------------------------------------------------------

AlgMpegDecoder::AlgMpegDecoder() {
}

AlgMpegDecoder::~AlgMpegDecoder() {
	closeClip();
}

void AlgMpegDecoder::closeClip() {
	delete _mpeg;
	_mpeg = nullptr;
	// The decoder owns whatever stream it was given, so the view is gone with it
	_clip = nullptr;
}

void AlgMpegDecoder::setInputFile(Common::File *input) {
	_input = input;

	// Read the program mux rate out of the first pack header. Position has to be
	// reported in the scene file's byte units, and deriving it from playback
	// time and this rate is exact - where counting bytes handed to the demuxer
	// is not, because it prebuffers 150 packets before the first picture.
	_bytesPerSecond = 0;
	if (_input) {
		const int32 saved = _input->pos();
		_input->seek(0);
		byte head[12];
		if (_input->read(head, sizeof(head)) == sizeof(head) &&
		    head[0] == 0 && head[1] == 0 && head[2] == 1 && head[3] == 0xBA) {
			// mux_rate is 22 bits, in units of 50 bytes per second
			const uint32 muxRate = ((head[9] & 0x7F) << 15) | (head[10] << 7) | (head[11] >> 1);
			_bytesPerSecond = muxRate * 50;
		}
		_input->seek(saved);
	}
	if (_bytesPerSecond == 0) {
		warning("AlgMpegDecoder: no pack header at the start, assuming 1.5Mbit/s");
		_bytesPerSecond = 187500;
	}
	// Roughly three pictures at 29.97fps, the granularity the .LIB releases use
	_sceneUnit = MAX<uint32>(1, (_bytesPerSecond * 3) / 30);
	debug(1, "ReelMagic stream: %u bytes/s, scene unit %u bytes", _bytesPerSecond, _sceneUnit);
}

void AlgMpegDecoder::loadVideoRange(uint32 start, uint32 end) {
	closeClip();
	_position = 0;
	_ended = false;

	if (!_input) {
		warning("AlgMpegDecoder: no input file set");
		return;
	}

	// The scene file counts from one, and a clip runs up to the byte the next
	// one starts at. Without an end - nothing in the game does that, but be
	// safe - play to the end of the file.
	const uint32 first = start > 0 ? start - 1 : 0;
	const uint32 last = end > first ? end - 1 : (uint32)_input->size();
	if (first >= (uint32)_input->size()) {
		warning("AlgMpegDecoder: clip starts past the end of the file (%u)", first);
		return;
	}

	// A bounded view is what stops the decoder running on into the next clip:
	// the clips are merely concatenated, so nothing else marks where to stop.
	_clip = new Common::SeekableSubReadStream(_input, first, MIN<uint32>(last, (uint32)_input->size()));

	_mpeg = new Video::MPEGPSDecoder();
	if (!_mpeg->loadStream(_clip)) {
		warning("AlgMpegDecoder: could not open the clip at %u", first);
		closeClip();
		return;
	}
	_mpeg->start();
	debug(2, "ReelMagic clip %u..%u: %u audio track(s)", first, last, _mpeg->getAudioTrackCount());
}

void AlgMpegDecoder::getNextFrame() {
	if (!_mpeg) {
		return;
	}

	// The game loop paces itself at roughly 10fps, which suited ALG's own clips,
	// but these pictures are 29.97fps and carry their own audio. So let the
	// decoder's clock decide: catch up on every frame that has come due, and
	// keep showing the last one when none has.
	// One picture per call. Draining every frame that has come due looks like
	// the right thing but is self-defeating: it consumes several frames' worth
	// of stream, so nothing is due again for as long, and two out of three
	// pictures get decoded and thrown away.
	const Graphics::Surface *decoded = nullptr;
	if (_mpeg->needsUpdate()) {
		decoded = _mpeg->decodeNextFrame();
		if (!decoded) {
			_ended = true;
		}
	}
	if (!decoded) {
		// endOfVideo() is the real signal: once the clip is spent needsUpdate()
		// goes false, so decodeNextFrame() is never reached to report it.
		if (_clip && (_ended || _mpeg->endOfVideo())) {
			// Push past the scene's end bound so the game loop's
			// "current <= endFrame" test finally fails; landing exactly on it
			// would leave the scene running for ever.
			_position = (uint32)(_clip->size() / _sceneUnit) + 2;
		}
		return;
	}

	if (!_frame) {
		_frame = new Graphics::Surface();
	}
	if (_frame->w != kDisplayWidth || _frame->h != kDisplayHeight || _frame->format != decoded->format) {
		_frame->free();
		_frame->create(kDisplayWidth, kDisplayHeight, decoded->format);
	}

	// Scale the 352x240 picture into the window the interface is drawn around
	const uint16 bpp = decoded->format.bytesPerPixel;
	for (int y = 0; y < kDisplayHeight; y++) {
		const int sy = y * decoded->h / kDisplayHeight;
		if (bpp == 2) {
			const uint16 *src = (const uint16 *)decoded->getBasePtr(0, sy);
			uint16 *dst = (uint16 *)_frame->getBasePtr(0, y);
			for (int x = 0; x < kDisplayWidth; x++) {
				dst[x] = src[x * decoded->w / kDisplayWidth];
			}
		} else {
			const uint32 *src = (const uint32 *)decoded->getBasePtr(0, sy);
			uint32 *dst = (uint32 *)_frame->getBasePtr(0, y);
			for (int x = 0; x < kDisplayWidth; x++) {
				dst[x] = src[x * decoded->w / kDisplayWidth];
			}
		}
	}

	_width = kDisplayWidth;
	_height = kDisplayHeight;

	// Position from playback time and the mux rate, in scene units. Counting
	// bytes read would sit a whole prebuffer ahead of the picture - 150 packets,
	// about 1.5 seconds, which is longer than 110 of Crime Patrol's 476 scenes.
	const uint32 bytes = (uint32)(((uint64)_mpeg->getTime() * _bytesPerSecond) / 1000);
	_position = bytes / _sceneUnit;
}

void AlgMpegDecoder::skipNumberOfFrames(uint32 num) {
	for (uint32 i = 0; i < num && !isFinished(); i++) {
		getNextFrame();
	}
}

bool AlgMpegDecoder::isFinished() const {
	return _ended || !_mpeg || _mpeg->endOfVideo();
}

void AlgMpegDecoder::pauseAudio(bool pause) const {
	// The game calls this every iteration with the current state, but
	// VideoDecoder::pauseVideo() counts its pauses - so passing true repeatedly
	// stacks the pause level and the picture never resumes. Only act on a change.
	if (_mpeg && pause != _paused) {
		_mpeg->pauseVideo(pause);
		_paused = pause;
	}
}

} // End of namespace Alg
