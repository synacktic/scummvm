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

#include "audio/audiostream.h"
#include "audio/decoders/raw.h"
#include "common/events.h"
#include "common/substream.h"
#include "graphics/cursorman.h"
#include "graphics/paletteman.h"

#include "alg/graphics.h"
#include "alg/scene.h"

#include "alg/game.h"

namespace Alg {

Game::Game(AlgEngine *vm) {
	_vm = vm;
}

Game::~Game() {
	if (_outputScreen) {
		_outputScreen->free();
		delete _outputScreen;
	}
	_libFile.close();
	_libFileEntries.clear();
	delete _rnd;
	delete[] _palette;
	delete _videoDecoder;
	delete _sceneInfo;
	if (_background) {
		_background->free();
		delete _background;
	}
	if (_screen) {
		_screen->free();
		delete _screen;
	}
}

void Game::init() {
	_inMenu = false;
	_palette = new uint8[257 * 3]();
	// blue for rect display
	_palette[5] = 0xFF;
	_paletteDirty = true;
	_screen = new Graphics::Surface();
	_rnd = new Common::RandomSource("alg");
	_screen->create(320, 200, Graphics::PixelFormat::createFormatCLUT8());
	_reelMagic = _vm->isReelMagic();
	if (_reelMagic) {
		_videoDecoder = new AlgMpegDecoder();
		// _videoFrameSkip is set from the stream once it is open - see
		// loadMpegFile(). It has to stay the size of a few pictures, because the
		// hit and pause tolerances are measured in it as well as the position.
	} else {
		_videoDecoder = new AlgVideoDecoder();
	}
	_videoDecoder->setPalette(_palette);
	_sceneInfo = new SceneInfo();
}

Common::Error Game::run() {
	return Common::kNoError;
}

void Game::shutdown() {
	g_system->getMixer()->stopAll();
	_vm->quitGame();
}

bool Game::pollEvents() {
	Common::Event event;
	bool hasEvents = false;
	while (g_system->getEventManager()->pollEvent(event)) {
		if (event.type == Common::EVENT_MOUSEMOVE) {
			_mousePos = event.mouse;
		} else if (event.type == Common::EVENT_LBUTTONDOWN) {
			_leftDown = true;
			_mousePos = event.mouse;
		} else if (event.type == Common::EVENT_RBUTTONDOWN) {
			_rightDown = true;
			_mousePos = event.mouse;
		} else if (event.type == Common::EVENT_LBUTTONUP) {
			_leftDown = false;
			_mousePos = event.mouse;
		} else if (event.type == Common::EVENT_RBUTTONUP) {
			_rightDown = false;
			_mousePos = event.mouse;
		}
		hasEvents = true;
	}
	return hasEvents;
}

void Game::loadLibArchive(const Common::Path &path) {
	debug("loading lib archive: %s", path.toString().c_str());
	if (!_libFile.open(path)) {
		error("Game::loadLibArchive(): Can't open library file '%s'", path.toString().c_str());
	}
	uint16 magicBytes = _libFile.readSint16LE();
	uint32 indexOffset = _libFile.readSint32LE();
	assert(magicBytes == 1020);
	(void)magicBytes;
	_libFile.seek(indexOffset);
	uint16 indexSize = _libFile.readSint16LE();
	assert(indexSize > 0);
	(void)indexSize;
	while (true) {
		uint32 entryOffset = _libFile.readSint32LE();
		Common::String entryName = _libFile.readStream(13)->readString();
		if (entryName.empty()) {
			break;
		}
		entryName.toLowercase();
		_libFileEntries[entryName] = entryOffset;
	}
	_libFile.seek(0);
	_videoDecoder->setInputFile(&_libFile);
}

bool Game::loadScene(Scene *scene) {
	if (_reelMagic) {
		// No archive directory to consult: the scene's own bounds are 1-based
		// byte offsets into the single MPEG, and the clip runs up to the byte
		// the following one begins at.
		if (scene->_startFrame == 0) {
			return false;
		}
		debug("loaded scene %s from %u..%u", scene->_name.c_str(), scene->_startFrame, scene->_endFrame);
		_videoDecoder->loadVideoRange(scene->_startFrame, scene->_endFrame);
		return true;
	}

	Common::String sceneFileName = Common::String::format("%s.mm", scene->_name.c_str());
	auto it = _libFileEntries.find(sceneFileName);
	if (it != _libFileEntries.end()) {
		debug("loaded scene %s", scene->_name.c_str());
		_videoDecoder->loadVideoFromStream(it->_value);
		return true;
	} else {
		return false;
	}
}

void Game::loadMpegFile(const Common::Path &path) {
	debug("loading mpeg stream: %s", path.toString().c_str());
	if (!_libFile.open(path)) {
		error("Game::loadMpegFile(): Can't open '%s'", path.toString().c_str());
	}
	_videoDecoder->setInputFile(&_libFile);

	// The scene file counts in bytes, so one position unit is however many bytes
	// the decoder reports per unit - a few pictures' worth, which keeps the hit
	// and pause tolerances the same real size they have for the .LIB releases.
	AlgMpegDecoder *mpeg = dynamic_cast<AlgMpegDecoder *>(_videoDecoder);
	if (mpeg) {
		_videoFrameSkip = mpeg->sceneUnitBytes();
		debug("scene unit %u bytes", _videoFrameSkip);
	}
}

void Game::updateScreen() {
	if (!_inMenu) {
		Graphics::Surface *frame = _videoDecoder->getVideoFrame();
		if (_reelMagic) {
			// The frame is RGB and the wrong size for the paletted screen, so
			// leave the key colour here and mix the picture in below, after all
			// the interface art has been drawn over it.
			_screen->fillRect(Common::Rect(_videoPosX, _videoPosY,
			                               _videoPosX + _videoDecoder->getWidth(),
			                               _videoPosY + _videoDecoder->getHeight()),
			                  _videoKeyIndex);
		} else if (frame) {
			_screen->copyRectToSurface(frame->getPixels(), frame->pitch, _videoPosX, _videoPosY, frame->w, frame->h);
		}
	}
	debug_drawZoneRects();

	if (_reelMagic) {
		compositeReelMagicFrame();
		g_system->copyRectToScreen(_outputScreen->getPixels(), _outputScreen->pitch, 0, 0, _outputScreen->w, _outputScreen->h);
		g_system->updateScreen();
		return;
	}

	if (_paletteDirty || _videoDecoder->isPaletteDirty()) {
		g_system->getPaletteManager()->setPalette(_palette, 0, 256);
		_paletteDirty = false;
	}
	g_system->copyRectToScreen(_screen->getPixels(), _screen->pitch, 0, 0, _screen->w, _screen->h);
	g_system->updateScreen();
}

uint8 Game::findUnusedPaletteIndex() const {
	// Anything the interface art actually draws must survive the composite, so
	// the key has to be an index none of it uses. The menu background is by far
	// the largest piece of art, so a colour missing from that is a safe bet;
	// fall back to 255 if it somehow uses all of them.
	bool used[256];
	memset(used, 0, sizeof(used));
	if (_background && _background->format.bytesPerPixel == 1) {
		for (int y = 0; y < _background->h; y++) {
			const byte *line = (const byte *)_background->getBasePtr(0, y);
			for (int x = 0; x < _background->w; x++) {
				used[line[x]] = true;
			}
		}
	}
	for (int i = 255; i >= 0; i--) {
		if (!used[i]) {
			return (uint8)i;
		}
	}
	return 0xFF;
}

void Game::compositeReelMagicFrame() {
	if (!_videoKeyChosen) {
		// Deferred until now: the per-game init loads the menu background after
		// Game::init() runs, and that is what the key is chosen against.
		_videoKeyIndex = findUnusedPaletteIndex();
		_videoKeyChosen = true;
		debug("ReelMagic video key index %d", _videoKeyIndex);
	}

	const Graphics::PixelFormat format = g_system->getScreenFormat();
	if (!_outputScreen) {
		_outputScreen = new Graphics::Surface();
		_outputScreen->create(_screen->w, _screen->h, format);
	}

	// Converting a palette entry per pixel is far too slow to keep up with a
	// 30fps picture, so the whole palette is converted once instead.
	if (!_paletteColorsValid || _paletteDirty) {
		for (int i = 0; i < 256; i++) {
			const uint8 *entry = _palette + i * 3;
			_paletteColors[i] = format.RGBToColor(entry[0], entry[1], entry[2]);
		}
		_paletteColorsValid = true;
	}

	// The decoder has already scaled the picture into the interface's window
	Graphics::Surface *frame = _videoDecoder->getVideoFrame();
	const bool sameFormat = frame && frame->format == format;

	for (int y = 0; y < _screen->h; y++) {
		const byte *src = (const byte *)_screen->getBasePtr(0, y);
		const int vy = y - _videoPosY;
		const bool videoRow = frame && vy >= 0 && vy < frame->h;
		const void *videoLine = videoRow ? frame->getBasePtr(0, vy) : nullptr;

		if (format.bytesPerPixel == 2) {
			uint16 *dst = (uint16 *)_outputScreen->getBasePtr(0, y);
			for (int x = 0; x < _screen->w; x++) {
				const int vx = x - _videoPosX;
				if (src[x] == _videoKeyIndex && videoRow && vx >= 0 && vx < frame->w) {
					const uint16 pixel = ((const uint16 *)videoLine)[vx];
					if (sameFormat) {
						dst[x] = pixel;
					} else {
						byte r, g, b;
						frame->format.colorToRGB(pixel, r, g, b);
						dst[x] = (uint16)format.RGBToColor(r, g, b);
					}
				} else {
					dst[x] = (uint16)_paletteColors[src[x]];
				}
			}
		} else {
			uint32 *dst = (uint32 *)_outputScreen->getBasePtr(0, y);
			for (int x = 0; x < _screen->w; x++) {
				const int vx = x - _videoPosX;
				if (src[x] == _videoKeyIndex && videoRow && vx >= 0 && vx < frame->w) {
					const uint32 pixel = ((const uint32 *)videoLine)[vx];
					if (sameFormat) {
						dst[x] = pixel;
					} else {
						byte r, g, b;
						frame->format.colorToRGB(pixel, r, g, b);
						dst[x] = format.RGBToColor(r, g, b);
					}
				} else {
					dst[x] = _paletteColors[src[x]];
				}
			}
		}
	}
}

uint32 Game::getMsTime() {
	return g_system->getMillis();
}

bool Game::fired(Common::Point *point) {
	_fired = false;
	pollEvents();
	if (_leftDown == true) {
		if (_buttonDown) {
			return false;
		}
		_fired = true;
		point->x = _mousePos.x;
		point->y = _mousePos.y;
		_buttonDown = true;
		return true;
	} else {
		_buttonDown = false;
		return false;
	}
}

Rect *Game::checkZone(Zone *zone, Common::Point *point) {
	for (auto &rect : zone->_rects) {
		if (rect->contains(*point)) {
			return rect;
		}
	}
	return nullptr;
}

uint32 Game::getFrame(Scene *scene) {
	if (_videoDecoder->getCurrentFrame() == 0) {
		return scene->_startFrame;
	}
	return scene->_startFrame + (_videoDecoder->getCurrentFrame() * _videoFrameSkip) - _videoFrameSkip;
}

int8 Game::skipToNewScene(Scene *scene) {
	if (!_gameInProgress || _sceneSkipped) {
		return 0;
	}
	if (scene->_dataParam2 == -1) {
		_sceneSkipped = true;
		return -1;
	} else if (scene->_dataParam2 > 0) {
		uint32 startFrame = scene->_dataParam3;
		if (startFrame == 0) {
			startFrame = scene->_startFrame + 15;
		}
		uint32 endFrame = scene->_dataParam4;
		if (_currentFrame < endFrame && _currentFrame > startFrame) {
			_sceneSkipped = true;
			return 1;
		}
	}
	return 0;
}

// Sound
Audio::SeekableAudioStream *Game::loadSoundFile(const Common::Path &path) {
	Common::File *file = new Common::File();
	if (!file->open(path)) {
		warning("Game::loadSoundFile(): Can't open sound file '%s'", path.toString().c_str());
		delete file;
		return nullptr;
	}
	return Audio::makeRawStream(file, 8000, Audio::FLAG_UNSIGNED, DisposeAfterUse::YES);
}

void Game::playSound(Audio::SeekableAudioStream *stream) {
	if (stream != nullptr) {
		stream->rewind();
		g_system->getMixer()->stopHandle(_sfxAudioHandle);
		g_system->getMixer()->playStream(Audio::Mixer::kSFXSoundType, &_sfxAudioHandle, stream, -1, Audio::Mixer::kMaxChannelVolume, 0, DisposeAfterUse::NO);
	}
}

// Script functions: Zone
void Game::zoneGlobalHit(Common::Point *point) {
	// do nothing
}

// Script functions: RectHit
void Game::rectHitDoNothing(Rect *rect) {
	// do nothing
}

// Script functions: Scene PreOps
void Game::scenePsoDrawRct(Scene *scene) {
}

void Game::scenePsoPause(Scene *scene) {
	_hadPause = false;
	_pauseTime = 0;
}

void Game::scenePsoDrawRctFadeIn(Scene *scene) {
	scenePsoDrawRct(scene);
	scenePsoFadeIn(scene);
}

void Game::scenePsoFadeIn(Scene *scene) {
	// do nothing
}

void Game::scenePsoPauseFadeIn(Scene *scene) {
	scenePsoPause(scene);
	scenePsoFadeIn(scene);
}

void Game::scenePsoPreRead(Scene *scene) {
	// do nothing
}

void Game::scenePsoPausePreRead(Scene *scene) {
	scenePsoPause(scene);
	scenePsoPreRead(scene);
}

// Script functions: Scene Scene InsOps
void Game::sceneIsoDoNothing(Scene *scene) {
	// do nothing
}

void Game::sceneIsoStartGame(Scene *scene) {
	_startScene = scene->_insopParam;
}

void Game::sceneIsoPause(Scene *scene) {
	bool checkPause = true;
	if (_hadPause) {
		checkPause = false;
	}
	if (_currentFrame > scene->_endFrame) {
		checkPause = false;
	}
	if (scene->_dataParam1 <= 0) {
		checkPause = false;
	}
	if (checkPause) {
		uint32 pauseStart = atoi(scene->_insopParam.c_str());
		uint32 pauseEnd = atoi(scene->_insopParam.c_str()) + _videoFrameSkip + 1;
		if (_currentFrame >= pauseStart && _currentFrame < pauseEnd && !_hadPause) {
			uint32 pauseDuration = scene->_dataParam1 * 0x90FF / 1000;
			_pauseTime = pauseDuration;
			_nextFrameTime += pauseDuration;
			_pauseTime += getMsTime();
			_hadPause = true;
		}
	}
	if (_pauseTime != 0) {
		if (getMsTime() > _pauseTime) {
			_pauseTime = 0;
		}
	}
}

// Script functions: Scene NxtScn
void Game::sceneNxtscnDoNothing(Scene *scene) {
	// do nothing
}

void Game::sceneDefaultNxtscn(Scene *scene) {
	_curScene = scene->_next;
}

// Script functions: ShowMsg
void Game::sceneSmDonothing(Scene *scene) {
	// do nothing
}

// Script functions: ScnNxtFrm
void Game::sceneNxtfrm(Scene *scene) {
}

// debug methods
void Game::debug_drawZoneRects() {
	if (_debug_drawRects || debugChannelSet(1, Alg::kAlgDebugGraphics)) {
		if (_inMenu) {
			for (auto rect : _subMenuZone->_rects) {
				_screen->drawLine(rect->left, rect->top, rect->right, rect->top, 1);
				_screen->drawLine(rect->left, rect->top, rect->left, rect->bottom, 1);
				_screen->drawLine(rect->right, rect->bottom, rect->right, rect->top, 1);
				_screen->drawLine(rect->right, rect->bottom, rect->left, rect->bottom, 1);
			}
		} else if (_curScene != "") {
			Scene *targetScene = _sceneInfo->findScene(_curScene);
			for (auto &zone : targetScene->_zones) {
				for (auto rect : zone->_rects) {
					// only draw frames that appear soon or are current
					if (_currentFrame + 30 >= zone->_startFrame && _currentFrame <= zone->_endFrame) {
						Common::Rect interpolated = rect->getInterpolatedRect(zone->_startFrame, zone->_endFrame, _currentFrame);
						_screen->drawLine(interpolated.left, interpolated.top, interpolated.right, interpolated.top, 1);
						_screen->drawLine(interpolated.left, interpolated.top, interpolated.left, interpolated.bottom, 1);
						_screen->drawLine(interpolated.right, interpolated.bottom, interpolated.right, interpolated.top, 1);
						_screen->drawLine(interpolated.right, interpolated.bottom, interpolated.left, interpolated.bottom, 1);
					}
				}
			}
		}
	}
}

bool Game::debug_dumpLibFile() {
	Common::DumpFile dumpFile;
	for (auto &entry : _libFileEntries) {
		_libFile.seek(entry._value, SEEK_SET);
		uint32 size = _libFile.readUint32LE();
		Common::Path dumpFileName(Common::String::format("libDump/%s", entry._key.c_str()));
		dumpFile.open(dumpFileName, true);
		assert(dumpFile.isOpen());
		dumpFile.writeStream(_libFile.readStream(size));
		dumpFile.flush();
		dumpFile.close();
	}
	return true;
}

} // End of namespace Alg
