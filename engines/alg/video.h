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

#ifndef ALG_VIDEO_H
#define ALG_VIDEO_H

#include "audio/audiostream.h"
#include "audio/mixer.h"

#include "common/file.h"
#include "common/substream.h"

namespace Video {
class MPEGPSDecoder;
}

namespace Alg {

/**
 * What the game logic needs of a video source. AlgVideoDecoder below plays
 * ALG's own codec out of a .LIB archive; the ReelMagic releases replace that
 * with one MPEG-1 program stream, so the calls are virtual and AlgMpegDecoder
 * substitutes for it.
 *
 * "Position" is deliberately vague: for the .LIB releases it is a frame number
 * and for the ReelMagic ones a byte offset into the MPEG, because that is what
 * each release's scene file counts in. The game logic only ever compares it
 * against the scene and zone bounds out of the same file, so either works.
 */
class AlgVideoDecoder {
public:
	AlgVideoDecoder();
	virtual ~AlgVideoDecoder();
	virtual void getNextFrame();
	virtual void loadVideoFromStream(uint32 offset);

	/**
	 * Play the byte range [start, end) of the input. Only the ReelMagic
	 * releases need the end, since their clips sit back to back in one stream
	 * with nothing but the scene file to say where one stops.
	 */
	virtual void loadVideoRange(uint32 start, uint32 end) {
		(void)end;
		loadVideoFromStream(start);
	}
	virtual void skipNumberOfFrames(uint32 num);
	virtual void setInputFile(Common::File *input) { _input = input; }
	virtual bool isFinished() const { return _bytesLeft == 0; }
	virtual Graphics::Surface *getVideoFrame() const { return _frame; }
	virtual void setPalette(uint8 *palette) { _palette = palette; }
	virtual bool isPaletteDirty() const { return _paletteDirty; }
	virtual void pauseAudio(bool pause) const { g_system->getMixer()->pauseHandle(_audioHandle, pause); }
	virtual uint16 getWidth() const { return _width; }
	virtual uint16 getHeight() const { return _height; }
	virtual uint32 getCurrentFrame() const { return _currentFrame; }

	/** Whether frames come back as RGB rather than 8 bit paletted. */
	virtual bool isTrueColor() const { return false; }

protected:
	Common::File *_input = nullptr;
	Graphics::Surface *_frame = nullptr;
	Audio::PacketizedAudioStream *_audioStream = nullptr;
	Audio::SoundHandle _audioHandle;
	uint8 *_palette = nullptr;
	bool _paletteDirty = false;
	bool _gotVideoFrame = false;
	uint32 _currentFrame = 0;
	uint32 _size = 0;
	uint32 _bytesLeft = 0;
	uint16 _currentChunk = 0;
	uint16 _numChunks = 0;
	uint16 _frameRate = 0;
	uint16 _videoMode = 0;
	uint16 _width = 0;
	uint16 _height = 0;
	uint16 _audioType = 0;

	void readNextChunk();
	void decodeIntraFrame(uint32 size, uint8 hh, uint8 hv);
	void decodeInterFrame(uint32 size, uint8 hh, uint8 hv);
	void updatePalette(uint32 size, bool partial);
	void readAudioData(uint32 size, uint16 rate);
};

/**
 * The ReelMagic releases of Crime Patrol and Drug Wars keep every clip in one
 * MPEG-1 program stream (CP.MPG / DW.MPG) instead of a .LIB archive, and their
 * scene file carries 1-based byte offsets into it where the .LIB releases carry
 * frame numbers. Clips sit back to back, each ending in a sequence-end and
 * program-end code, so a clip is played by handing the decoder a bounded view
 * of the file rather than the file itself.
 *
 * Position is reported in bytes consumed, which with Game::_videoFrameSkip set
 * to 1 makes Game::getFrame() produce absolute byte offsets - the same units the
 * scene and zone bounds are written in, so no game logic has to change.
 */
class AlgMpegDecoder : public AlgVideoDecoder {
public:
	/**
	 * The window the .LIB releases play in - Crime Patrol's own clips are
	 * 288x186 at (11,2). The ReelMagic pictures are 352x240 SIF, so they are
	 * scaled into this window on the way out, which also means getWidth() and
	 * getHeight() keep reporting what the interface art is laid out against.
	 */
	static const int kDisplayWidth = 288;
	static const int kDisplayHeight = 186;

	AlgMpegDecoder();
	~AlgMpegDecoder() override;

	void loadVideoRange(uint32 start, uint32 end) override;
	void loadVideoFromStream(uint32 offset) override { loadVideoRange(offset, 0); }
	void getNextFrame() override;
	void skipNumberOfFrames(uint32 num) override;
	bool isFinished() const override;
	void setPalette(uint8 *palette) override { (void)palette; }
	bool isPaletteDirty() const override { return false; }
	void pauseAudio(bool pause) const override;
	uint32 getCurrentFrame() const override { return _position; }
	bool isTrueColor() const override { return true; }

private:
	void closeClip();

	Video::MPEGPSDecoder *_mpeg = nullptr;
	Common::SeekableSubReadStream *_clip = nullptr;
	uint32 _position = 0;
	bool _ended = false;
	/** VideoDecoder's pause is counted, so track our own state - see pauseAudio(). */
	mutable bool _paused = false;
};

} // End of namespace Alg

#endif
