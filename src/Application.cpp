/*!
 * @file 		Application.cpp
 * @author 		Zdenek Travnicek <travnicek@iim.cz>
 * @date 		7.1.2014
 * @copyright	Institute of Intermedia, CTU in Prague, 2013
 * 				Distributed under BSD Licence, details in file doc/LICENSE
 *
 */

#include <GL/glew.h>
#include "Application.h"
#include <functional>
#include <iostream>
#include <algorithm>
#include <cmath>
#include "platform.h"


namespace CAVE {

#ifndef CAVE_VERSION
//! Pointer to an instance of Application (for GLUT only)
Application* Application::instance = nullptr;
#endif

namespace {


//! Number of particles to spwan each second
const size_t particles_per_second = 400;
//! Rotation speed (in rad/s)
const float rotation_per_second = pi_constant/2.0f;
//! Default position in the scene (for reset())
const point3 default_position = {0.0f, 0.0f, -5.0f};

#ifdef CAVE_VERSION
//! Communication channel for CAVElib
const int comm_channel = 37;


struct dispatch_data_t {
	std::function<void()> fun;
};

/*!
 * A workaround for the need to use C-style function pointers in CAVElib.
 * Not usable in GLUT as there isn't any way to pass data to the callback.
 *
 * @param data Pointer to dispatch_data_t containing function to execute.
 */
void dispatcher(void* data) {
	if (data) {
		dispatch_data_t* dispatch_data = static_cast<dispatch_data_t*>(data);
		dispatch_data->fun();
	}
}
#endif
}

Application::Application(int argc, char** argv):
last_time_(0.0),scene_(particles_per_second)
{
#ifdef CAVE_VERSION
	CAVEConfigure(&argc,argv,nullptr);
#else
	glutInit(&argc, argv);
	instance = this;
#endif
}

void Application::init_gl()
{
	glClearColor(.0f, .0f, .0f, .0f);
	glClearDepth(1.0);
	glDepthFunc(GL_LESS);
	glEnable(GL_DEPTH_TEST);
	glShadeModel(GL_SMOOTH);
	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);
	scene_.prepare_details();
}

#ifdef CAVE_VERSION
void Application::init_cave()
{
	if (CAVEMasterDisplay()) {
		glewInit();
		unsigned int seed;
		// Open communication channel
		CAVEDistribOpenConnection(comm_channel);
		if (CAVEDistribMaster()) {
			std::random_device rd;
			seed = rd();
			buttons_.resize(CAVEController->num_buttons);
			CAVEDistribWrite(comm_channel, &seed, sizeof(seed));
		} else {
			CAVEDistribRead(comm_channel, &seed, sizeof(seed));
		}
		scene_.set_seed(seed);

	}
	CAVEDisplayBarrier();
	init_gl();
}

void Application::update_cave()
{
	/*
	 * The CAVE application can generally run in multiple instances
	 * and every instance can possibly have multiple threads.
	 *
	 * In CAVElib, there are two methods to limit execution to only some threads/instances.
	 *  - CAVEDistribMaster() returns true in every thread on exactly one instance.
	 *  - CAVEMasterDisplay() returns true in exactly one thread on every instance.
	 */

	if (CAVEMasterDisplay()) { // Only one thread should update the scene

		if (CAVEDistribMaster()) { // Only the master instance should compute the update
			for (size_t i = 0; i < buttons_.size(); ++i) {
				buttons_[i].update(CAVEController->button[i]);
			}

			reset(buttons_[0].was_pressed);

			const double current_time = CAVEGetTime();
			update_time(current_time);

			const float joystick_x = CAVEController->valuator[0];
			const float joystick_y = CAVEController->valuator[1];

			if (std::abs(joystick_x) > 0.1f) {
				state_.rotation_y += joystick_x * rotation_per_second * state_.time_delta;
			}
			const point3 move_vector {std::sin(state_.rotation_y), 0.0f, std::cos(state_.rotation_y)};
			if (std::abs(joystick_y) > 0.1f) {
				state_.position = state_.position + joystick_y * state_.time_delta * move_vector;
			}

			CAVEDistribWrite(comm_channel, &state_, sizeof(state_));
		} else { // Other instances should just receive updates from master
			CAVEDistribRead(comm_channel, &state_, sizeof(state_));
		}

		// And evaluate the update
		update();
	}
	CAVEDisplayBarrier();
}
#else
void Application::render_glut()
{
	if (!instance) return;
	double current_time = glutGet(GLUT_ELAPSED_TIME) / 1000.0;
	instance->update_time(current_time);

	instance->update();
	glLoadIdentity();
	instance->render();
	glutSwapBuffers();
}

void resize_glut(int w, int h)
{
	if (h == 0)	h = 1;
	float ratio =  w * 1.0 / h;
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glViewport(0, 0, w, h);
	gluPerspective(45.0f, ratio, 0.1f, 100.0f);
	glMatrixMode(GL_MODELVIEW);
}
void Application::keyboard_glut(unsigned char key, int /*x*/, int /*y*/)
{
	switch (key) {
	case 27: //Escape
		exit(0);break;
	case ' ':
		instance->reset(true);
		break;
	case 'w':
		instance->state_.position.x+=std::sin(instance->state_.rotation_y);
		instance->state_.position.z+=std::cos(instance->state_.rotation_y);
		break;
	case 's':
		instance->state_.position.x-=std::sin(instance->state_.rotation_y);
		instance->state_.position.z-=std::cos(instance->state_.rotation_y);
		break;
	case 'a':
		instance->state_.rotation_y +=rotation_per_second/20;
		break;
	case 'd':
		instance->state_.rotation_y -=rotation_per_second/20;
		break;
	}
}

#endif

void Application::update()
{
	if (state_.reset_scene) {
		scene_.reset();
		reset(false);
	}
	scene_.update(state_.time_delta);
}

void Application::update_time(double current_time)
{
	state_.time_delta = current_time - last_time_;
	last_time_ = current_time;
	//state_.particles_to_create = static_cast<size_t>(particles_per_second * state_.time_delta);
}
void Application::reset(bool value)
{
	state_.reset_scene = value;
	if (value) {
		state_.position = default_position;
		state_.rotation_y = 0.0f;
	}
}
void Application::render() const
{
	scene_.render(state_.position, state_.rotation_y);
}

int Application::run()
{
#ifdef CAVE_VERSION
	// Here we prepare the functions to be called in the callbacks
	dispatch_data_t dispatch_init{[&](){this->init_cave();}};
	dispatch_data_t dispatch_update{[&](){this->update_cave();}};
	dispatch_data_t dispatch_display{[&](){this->render();}};

	CAVEInitApplication(reinterpret_cast<CAVECALLBACK>(dispatcher), 1, static_cast<void*>(&dispatch_init));
	CAVEDisplay(reinterpret_cast<CAVECALLBACK>(dispatcher), 1, static_cast<void*>(&dispatch_display));
	CAVEFrameFunction(reinterpret_cast<CAVECALLBACK>(dispatcher), 1, static_cast<void*>(&dispatch_update));

	CAVEInit();
	std::cout << "Starting up main loop\n";
 	if (CAVEDistribMaster())
		while (!CAVEgetbutton(CAVE_ESCKEY)) {
			CAVEUSleep(10);
		}
	else while (!CAVESync->Quit) CAVEUSleep(15);
	std::cout<< "Cleaning up.\n";
	CAVEExit();
	return 0;
#else
	glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE | GLUT_RGBA);
	glutInitWindowPosition(100,100);
	glutInitWindowSize(800,600);
	resize_glut(800,600);
	glutCreateWindow("CAVElib example");

	glewInit();
	init_gl();
	std::random_device rd;
	scene_.set_seed(rd());
	glutDisplayFunc(render_glut);
	glutReshapeFunc(resize_glut);
	glutIdleFunc(render_glut);


	glutKeyboardFunc(keyboard_glut);

	glutMainLoop();
	return 0;
#endif
}


}




