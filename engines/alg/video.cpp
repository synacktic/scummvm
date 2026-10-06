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
	// - or one picture, for a script that counts in frames.
	_sceneUnit = _frameUnits ? MAX<uint32>(1, (uint32)(((uint64)_bytesPerSecond * 1001) / 30000))
	                         : MAX<uint32>(1, (_bytesPerSecond * 3) / 30);
	debug(1, "ReelMagic stream: %u bytes/s, scene unit %u bytes", _bytesPerSecond, _sceneUnit);
}

uint32 AlgMpegDecoder::findEntryPoint(uint32 wantByte) {
	// Decoding can only pick up at a sequence header - these streams repeat one
	// per GOP, roughly every 130K - and the demuxer needs to start on a pack.
	// A GOP is under a second, so starting a shade early is harmless, whereas
	// starting late would skip the section's first target.
	if (wantByte == 0 || !_input) {
		return 0;
	}
	const uint32 kWindow = 512 * 1024;
	const uint32 from = wantByte > kWindow ? wantByte - kWindow : 0;
	const uint32 len = wantByte - from;
	byte *buf = new byte[len];
	_input->seek(from);
	const uint32 got = _input->read(buf, len);
	int32 seq = -1;
	for (uint32 i = 0; i + 3 < got; i++) {
		if (!buf[i] && !buf[i + 1] && buf[i + 2] == 1 && buf[i + 3] == 0xB3) {
			seq = (int32)i;
		}
	}
	uint32 res = 0;
	for (int32 i = seq; i >= 0; i--) {
		if (!buf[i] && !buf[i + 1] && buf[i + 2] == 1 && buf[i + 3] == 0xBA) {
			res = from + (uint32)i;
			break;
		}
	}
	delete[] buf;
	return res;
}

void AlgMpegDecoder::loadVideoFile(const Common::Path &path, uint32 startUnit) {
	closeClip();
	if (_ownFile) {
		delete _ownFile;
		_ownFile = nullptr;
	}
	_ownFile = new Common::File();
	if (!_ownFile->open(path)) {
		warning("AlgMpegDecoder: can't open '%s'", path.toString().c_str());
		delete _ownFile;
		_ownFile = nullptr;
		return;
	}
	setInputFile(_ownFile);
	// loadVideoRange() counts from one, and the clip runs to the end of the file
	// - the section's end bound is enforced by the game loop, not by cutting the
	// stream, so a section that overruns still shows a picture.
	const uint32 entry = findEntryPoint(startUnit * _sceneUnit);
	loadVideoRange(entry + 1, (uint32)_ownFile->size() + 1);
	// Playback time restarts at zero for the substream, so put back what was
	// skipped: the script's positions are absolute within the file.
	_positionBias = entry / _sceneUnit;
	_position = _positionBias;

	// Decoding can only enter at the GOP before the target, but showing the
	// frames between would fast-forward the picture while the (already
	// dropped) audio starts - a visible stutter on every section seek. Burn
	// through them before pacing starts; nobody sees them.
	if (_mpeg && startUnit > _positionBias) {
		uint32 discard = startUnit - _positionBias;
		if (discard > 90)
			discard = 90;
		while (discard-- > 0 && !_mpeg->endOfVideo()) {
			if (!_mpeg->decodeNextFrame())
				break;
			_framesDecoded++;
			_clipFrames++;
		}
		const uint32 bytes = (uint32)(((uint64)_mpeg->getTime() * _bytesPerSecond) / 1000);
		_position = _positionBias + bytes / _sceneUnit;
	}
	debug(2, "ReelMagic seek: unit %u -> byte %u (bias %u)", startUnit, entry, _positionBias);
}

void AlgMpegDecoder::loadVideoRange(uint32 start, uint32 end) {
	closeClip();
	_position = 0;
	_positionBias = 0;
	_clipFrames = 0;
	_ended = false;
	_endHoldPosition = 0;
	_endHoldFrom = 0;
	_endHoldStartMs = 0;
	_endHoldPauseStartMs = 0;

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
	if (!_mpeg || _paused) {
		return;
	}

	// The game loop paces itself at roughly 10fps, which suited ALG's own clips,
	// but these pictures are 29.97fps and carry their own audio. So let the
	// decoder's clock decide: catch up on every frame that has come due, and
	// keep showing the last one when none has.
	// Catch up on whatever has come due, showing the newest picture. Decoding
	// only one per call cannot recover from a slow pass through the game loop,
	// so the video judders while the clip still ends on time - position comes
	// from playback time, not from frames drawn. The cap keeps a long stall
	// from fast-forwarding through the clip.
	const Graphics::Surface *decoded = nullptr;
	int caughtUp = 0;
	while (_mpeg->needsUpdate() && caughtUp < 4) {
		const Graphics::Surface *next = _mpeg->decodeNextFrame();
		if (!next) {
			_ended = true;
			break;
		}
		decoded = next;
		_framesDecoded++;
		caughtUp++;
	}
	if (!decoded) {
		// endOfVideo() is the real signal: once the clip is spent needsUpdate()
		// goes false, so decodeNextFrame() is never reached to report it.
		if (_clip && (_ended || _mpeg->endOfVideo())) {
			if (_endHoldPosition && _position <= _endHoldPosition) {
				if (_endHoldStartMs == 0) {
					_endHoldStartMs = g_system->getMillis();
					_endHoldFrom = _position;
				}
				const uint32 elapsed = g_system->getMillis() - _endHoldStartMs;
				const uint32 advance = _frameUnits
					? (uint32)(((uint64)elapsed * 30) / 1001)
					: (uint32)(((uint64)elapsed * _bytesPerSecond) /
					           (1000 * _sceneUnit));
				_position = MIN(_endHoldPosition + 1, _endHoldFrom + advance);
				return;
			}
			// Push past the scene's end bound so the game loop's
			// "current <= endFrame" test finally fails; landing exactly on it
			// would leave the scene running for ever.
			_position = _positionBias + (uint32)(_clip->size() / _sceneUnit) + 2;
		}
		return;
	}

	if (!_frame) {
		_frame = new Graphics::Surface();
	}
	if (_frame->w != _displayW || _frame->h != _displayH || _frame->format != decoded->format) {
		_frame->free();
		_frame->create(_displayW, _displayH, decoded->format);
	}

	// Scale the 352x240 picture into the window the interface is drawn around
	const uint16 bpp = decoded->format.bytesPerPixel;
	for (int y = 0; y < _displayH; y++) {
		const int sy = y * decoded->h / _displayH;
		if (bpp == 2) {
			const uint16 *src = (const uint16 *)decoded->getBasePtr(0, sy);
			uint16 *dst = (uint16 *)_frame->getBasePtr(0, y);
			for (int x = 0; x < _displayW; x++) {
				dst[x] = src[x * decoded->w / _displayW];
			}
		} else {
			const uint32 *src = (const uint32 *)decoded->getBasePtr(0, sy);
			uint32 *dst = (uint32 *)_frame->getBasePtr(0, y);
			for (int x = 0; x < _displayW; x++) {
				dst[x] = src[x * decoded->w / _displayW];
			}
		}
	}

	_width = _displayW;
	_height = _displayH;

	// The shown picture runs a constant ~3 frames ahead of the audio clock
	// (the B-frame presentation/coded-order offset in the timing path). It is
	// stable per clip, so measure it once early and trim the video clock; the
	// probe stays available for verification.
	_clipFrames++;
	if (_clipFrames == 20 && _mpeg->getAudioTrackCount() > 0) {
		const int32 vms = (int32)((uint64)_mpeg->getCurFrame() * 1001 / 30);
		const int32 ams = (int32)_mpeg->getTime();
		const int32 skew = vms - ams;
		if (skew > 40 && skew < 400) {
			debug(2, "avsync: trimming %+dms video lead", skew);
			_mpeg->nudgeVideoTimeBase(skew);
		}
	}
	if (gDebugLevel >= 2 && (_clipFrames % 60) == 50 && _mpeg->getAudioTrackCount() > 0) {
		const int32 vms = (int32)((uint64)_mpeg->getCurFrame() * 1001 / 30);
		const int32 ams = (int32)_mpeg->getTime();
		debug(2, "avsync: clipframe %d skew %+dms", _clipFrames, vms - ams);
	}

	// Position from playback time and the mux rate, in scene units. Counting
	// bytes read would sit a whole prebuffer ahead of the picture - 150 packets,
	// about 1.5 seconds, which is longer than 110 of Crime Patrol's 476 scenes.
	const uint32 bytes = (uint32)(((uint64)_mpeg->getTime() * _bytesPerSecond) / 1000);
	_position = _positionBias + bytes / _sceneUnit;
}

void AlgMpegDecoder::skipNumberOfFrames(uint32 num) {
	for (uint32 i = 0; i < num && !isFinished(); i++) {
		getNextFrame();
	}
}

bool AlgMpegDecoder::isFinished() const {
	if (!_mpeg)
		return true;
	const bool streamEnded = _ended || _mpeg->endOfVideo();
	return streamEnded && (!_endHoldPosition || _position > _endHoldPosition);
}

void AlgMpegDecoder::pauseAudio(bool pause) const {
	// The game calls this every iteration with the current state, but
	// VideoDecoder::pauseVideo() counts its pauses - so passing true repeatedly
	// stacks the pause level and the picture never resumes. Only act on a change.
	if (_mpeg && pause != _paused) {
		const uint32 now = g_system->getMillis();
		if (pause && _endHoldStartMs) {
			_endHoldPauseStartMs = now;
		} else if (!pause && _endHoldPauseStartMs) {
			_endHoldStartMs += now - _endHoldPauseStartMs;
			_endHoldPauseStartMs = 0;
		}
		_mpeg->pauseVideo(pause);
		_paused = pause;
	}
}

} // End of namespace Alg
