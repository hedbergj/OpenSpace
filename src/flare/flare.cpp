/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014                                                                    *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <flare/flare.h>

#include <flare/Renderer.h>
#include <flare/Texture.h>
#include <flare/Texture2D.h>
#include <flare/ShaderProgram.h>
#include <flare/TransferFunction.h>
#include <flare/BrickManager.h>
#include <flare/TSP.h>
#include <flare/CLManager.h>
#include <flare/Utils.h>

#include <ghoul/filesystem/filesystem.h>

#include <string>
#include <cstdlib>
#include <vector>
#include <iostream>

namespace openspace {
using namespace osp;

Flare::Flare() {
	_leftMouseButton = false;
	_currentMouseX = 0;
	_currentMouseY = 0;
	_lastMouseX = 0;
	_lastMouseY = 0;
	_oldTime = 0.f;
	_currentTime = 0.f;

	initialize();
}

Flare::~Flare() {
	// Clean up like a good citizen
	delete _config;
	delete _raycaster;
	delete _animator;
}

void Flare::render() {
	// Reload config if flag is set
	if (_reloadFlag.getVal()) _raycaster->Reload();

	// Set model and view params
	_raycaster->SetModelParams(_pitch.getVal(),
			_yaw.getVal(),
			_roll.getVal());
	_raycaster->SetViewParams(_translateX.getVal(),
			_translateY.getVal(),
			_translateZ.getVal());

	// Render
	if (!_raycaster->Render(_elapsedTime.getVal()))	exit(1);

	// Save screenshot
	if (_config->TakeScreenshot()) sgct::Engine::instance()->takeScreenshot();

	// Update animator with synchronized time
	_animator->SetPaused(_animationPaused.getVal());
	_animator->SetFPSMode(_fpsMode.getVal());
	_animator->Update(_elapsedTime.getVal());
	_animator->ManualTimestep(_manualTimestep.getVal());
}

void Flare::setupNavigationParameters() {
	_animationPaused.setVal(false);
	_reloadFlag.setVal(false);

	// FPS mode should be OFF for cluster syncing
	_fpsMode.setVal(true);
	_manualTimestep.setVal(0);

	// Read initial values from config
	_pitch.setVal(_config->StartPitch());
	_yaw.setVal(_config->StartYaw());
	_roll.setVal(_config->StartRoll());

	_translateX.setVal(_config->TranslateX());
	_translateY.setVal(_config->TranslateY());
	_translateZ.setVal(_config->TranslateZ());
}

void Flare::initialize() {
	// Start with reading a config file
	_config = Config::New(absPath("${BASE_PATH}/config/flareConfig.txt"));
	if (!_config) exit(1);

	setupNavigationParameters();

	// Get the viewport coordinates from OpenGL
	GLint currentViewPort[4];
	glGetIntegerv( GL_VIEWPORT, currentViewPort);

	// Make sure texture width/height and global kernel worksizes are
	// multiples of the local worksize

	// Window dimensions
	unsigned int width = currentViewPort[2] - currentViewPort[0];
	unsigned int height = currentViewPort[3] - currentViewPort[1];
	unsigned int xFactor = width/_config->LocalWorkSizeX();
	unsigned int yFactor = height/_config->LocalWorkSizeY();
	width = xFactor * _config->LocalWorkSizeX();
	height = yFactor * _config->LocalWorkSizeY();
	width /= _config->TextureDivisionFactor();
	height /= _config->TextureDivisionFactor();


	// Create TSP structure from file
	TSP *tsp = TSP::New(_config);
	if (!tsp->ReadHeader()) exit(1);
	// Read cache if it exists, calculate otherwise
	if (tsp->ReadCache()) {
		INFO("\nUsing cached TSP file");
	} else {
		INFO("\nNo cached TSP file found");
		if (!tsp->Construct()) exit(1);
		if (_config->CalculateError() == 0) {
			INFO("Not calculating errors");
		} else {
			if (!tsp->CalculateSpatialError()) exit(1);
			if (!tsp->CalculateTemporalError()) exit(1);
			if (!tsp->WriteCache()) exit(1);
		}
	}

	// Create brick manager and init (has to be done after init OpenGL!)
	BrickManager *brickManager= BrickManager::New(_config);
	if (!brickManager->ReadHeader()) exit(1);
	if (!brickManager->InitAtlas()) exit(1);

	// Create shaders for color cube and output textured quad
	ShaderProgram *cubeShaderProgram = ShaderProgram::New();
	cubeShaderProgram->CreateShader(ShaderProgram::VERTEX,
			_config->CubeShaderVertFilename());
	cubeShaderProgram->CreateShader(ShaderProgram::FRAGMENT,
			_config->CubeShaderFragFilename());
	cubeShaderProgram->CreateProgram();

	ShaderProgram *quadShaderProgram = ShaderProgram::New();
	quadShaderProgram->CreateShader(ShaderProgram::VERTEX,
			_config->QuadShaderVertFilename());
	quadShaderProgram->CreateShader(ShaderProgram::FRAGMENT,
			_config->QuadShaderFragFilename());
	quadShaderProgram->CreateProgram();

	// Create two textures to hold the color cube
	std::vector<unsigned int> dimensions(2);
	dimensions[0] = width;
	dimensions[1] = height;
	Texture2D *cubeFrontTex = Texture2D::New(dimensions);
	Texture2D *cubeBackTex = Texture2D::New(dimensions);
	cubeFrontTex->Init();
	cubeBackTex->Init();

	// Create an output texture to write to
	Texture2D *quadTex = Texture2D::New(dimensions);
	quadTex->Init();

	// Create transfer functions
	TransferFunction *transferFunction = TransferFunction::New();
	transferFunction->SetInFilename(_config->TFFilename());
	if (!transferFunction->ReadFile()) exit(1);
	if (!transferFunction->ConstructTexture()) exit(1);


	// Create animator
	_animator = Animator::New(_config);
	// Use original (not adjusted) number of timesteps for animator
	_animator->SetNumTimesteps(brickManager->NumOrigTimesteps());

	// Create CL manager
	CLManager *clManager = CLManager::New();

	// Set up the raycaster
	_raycaster = Raycaster::New(_config);
	_raycaster->SetWinWidth(width);
	_raycaster->SetWinHeight(height);
	if (!_raycaster->InitCube()) exit(1);
	if (!_raycaster->InitQuad()) exit(1);
	_raycaster->SetBrickManager(brickManager);
	_raycaster->SetCubeFrontTexture(cubeFrontTex);
	_raycaster->SetCubeBackTexture(cubeBackTex);
	_raycaster->SetQuadTexture(quadTex);
	_raycaster->SetCubeShaderProgram(cubeShaderProgram);
	_raycaster->SetQuadShaderProgram(quadShaderProgram);
	if (!_raycaster->InitFramebuffers()) exit(1);
	_raycaster->SetAnimator(_animator);
	_raycaster->AddTransferFunction(transferFunction);

	// Tie CL manager to renderer
	_raycaster->SetCLManager(clManager);
	_raycaster->SetTSP(tsp);

	if (!_raycaster->InitCL()) exit(1);
	if (!_raycaster->InitPipeline()) exit(1);
}

void Flare::keyboard(int key, int action) {
	if (action == GLFW_PRESS) {
		switch(key) {
		case 32: // space bar
			// Toggle animation paused
			INFO("Pausing");
			_animationPaused.setVal(!_animationPaused.getVal());
			break;
		case 'Z':
		case 'z':
			// Decrease timestep
			// NOTE: Can't decrease timestep with double buffered approach atm
			//manualTimestep_.setVal(-1);
			break;
		case 'X':
		case 'x':
			// Increase timestep
			_manualTimestep.setVal(1);
			break;
		case 'D':
		case 'd':
			_translateX.setVal(_translateX.getVal() + _config->ZoomFactor());
			break;
		case 'A':
		case 'a':
			_translateX.setVal(_translateX.getVal() - _config->ZoomFactor());
			break;
		case 'W':
		case 'w':
			_translateY.setVal(_translateY.getVal() + _config->ZoomFactor());
			break;
		case 'S':
		case 's':
			_translateY.setVal(_translateY.getVal() - _config->ZoomFactor());
			break;
		case 'Q':
		case 'q':
			_translateZ.setVal(_translateZ.getVal() + _config->ZoomFactor());
			break;
		case 'E':
		case 'e':
			_translateZ.setVal(_translateZ.getVal() - _config->ZoomFactor());
			break;
		case 'R':
		case 'r':
			_reloadFlag.setVal(true);
			break;
		case 'F':
		case 'f':
			_fpsMode.setVal(!_fpsMode.getVal());
			if (_fpsMode.getVal()) {
				INFO("Updating animation ASAP");
			} else {
				INFO("Using refresh interval variable");
			}
			break;
		}
	}
}

void Flare::mouse(int button, int action) {
	switch (button) {
	case GLFW_MOUSE_BUTTON_LEFT:
		_leftMouseButton = (action == GLFW_PRESS) ? true : false;
		std::size_t winId = sgct::Engine::instance()->getActiveWindowPtr()->getId();
		sgct::Engine::getMousePos(winId, &_lastMouseX, &_lastMouseY);
	}
}

void Flare::preSync() {
	// Update time
	_oldTime = _currentTime;
	_currentTime = static_cast<float>(sgct::Engine::getTime());
	_elapsedTime.setVal(_currentTime - _oldTime);

	// Update automatic model transform
	if (!_animationPaused.getVal()) {
		_pitch.setVal(_pitch.getVal() + _config->PitchSpeed());
		_roll.setVal(_roll.getVal() + _config->RollSpeed());
		_yaw.setVal(_yaw.getVal() + _config->YawSpeed());
	}

	// Update mouse
	if (_leftMouseButton) {
		std::size_t winId = sgct::Engine::instance()->getActiveWindowPtr()->getId();
		sgct::Engine::getMousePos(winId ,&_currentMouseX, &_currentMouseY);
		_pitch.setVal(_pitch.getVal() + _config->MousePitchFactor() *
				static_cast<float>(_currentMouseX-_lastMouseX));
		_roll.setVal(_roll.getVal() + _config->MouseRollFactor() *
				static_cast<float>(_currentMouseY-_lastMouseY));
	}
}

void Flare::postDraw() {
	// Reset manual timestep
	_manualTimestep.setVal(0);

	// Reset reload flag
	_reloadFlag.setVal(false);
}

void Flare::encode() {
  sgct::SharedData::instance()->writeBool(&_animationPaused);
  sgct::SharedData::instance()->writeBool(&_fpsMode);
  sgct::SharedData::instance()->writeFloat(&_elapsedTime);
  sgct::SharedData::instance()->writeInt(&_manualTimestep);
  sgct::SharedData::instance()->writeFloat(&_pitch);
  sgct::SharedData::instance()->writeFloat(&_yaw);
  sgct::SharedData::instance()->writeFloat(&_roll);
  sgct::SharedData::instance()->writeFloat(&_translateX);
  sgct::SharedData::instance()->writeFloat(&_translateY);
  sgct::SharedData::instance()->writeFloat(&_translateZ);
  sgct::SharedData::instance()->writeBool(&_reloadFlag);
}

void Flare::decode() {
  sgct::SharedData::instance()->readBool(&_animationPaused);
  sgct::SharedData::instance()->readBool(&_fpsMode);
  sgct::SharedData::instance()->readFloat(&_elapsedTime);
  sgct::SharedData::instance()->readInt(&_manualTimestep);
  sgct::SharedData::instance()->readFloat(&_pitch);
  sgct::SharedData::instance()->readFloat(&_yaw);
  sgct::SharedData::instance()->readFloat(&_roll);
  sgct::SharedData::instance()->readFloat(&_translateX);
  sgct::SharedData::instance()->readFloat(&_translateY);
  sgct::SharedData::instance()->readFloat(&_translateZ);
  sgct::SharedData::instance()->readBool(&_reloadFlag);
}

} // namespace openspace


