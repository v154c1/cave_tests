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
#ifdef CAVE_VERSION
#include <cave_ogl.h>
#else
#include <GL/glut.h>
#endif



namespace CAVE {

#ifndef CAVE_VERSION
//! Pointer to an instance of Application (for GLUT only)
Application* Application::instance = nullptr;
#endif

namespace {


//! Number of particles to spwan each second
const size_t particles_per_second = 400;
//! PI constant
const float pi_constant = 3.14159265f;
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
/*!
 * Creates new particle using provided generator and distribution objects.
 * @param d_position  Distribution object for generating particle position
 * @param d_direction Distribution object for generating particle direction
 * @param generator   Generator providing random numbers
 * @return
 */
template<class Generator>
Particle create_particle(std::uniform_real_distribution<float>& d_position,
		std::uniform_real_distribution<float>& d_direction,
		Generator& generator)
{
	point3 position;
	position.x = d_position(generator);
	position.y = d_position(generator);
	position.z = d_position(generator);
	point3 direction;
	direction.x = d_direction(generator);
	direction.y = d_direction(generator)*2.0f+2.0;
	direction.z = d_direction(generator);
	return {position, direction};
}
}


Application::Application(int argc, char** argv):
distribution_position_(0.0, 1.0), distribution_direction_(-1.0, 1.0),last_time_(0.0)
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
		generator_.seed(seed);

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
		particles_.clear();
	}

	for (size_t i = 0; i < state_.particles_to_create; ++i) {
		particles_.emplace_back(create_particle(distribution_position_, distribution_direction_, generator_));
	}
	for (auto& p: particles_) {
		p.update(state_.time_delta);
	}
	particles_.erase(std::remove_if(particles_.begin(), particles_.end(),
			[](Particle& p){return p.dead();}), particles_.end());
	reset(false);
}

void Application::update_time(double current_time)
{
	state_.time_delta = current_time - last_time_;
	last_time_ = current_time;
	state_.particles_to_create = static_cast<size_t>(particles_per_second * state_.time_delta);
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
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	/*
	 * For the sake of simplicity, the old OpenGL matrix stack is used here.
	 *
	 * In OpenGL 3.0, where the matrices are handled in shaders,
	 * will things get a little bit more complicated.
	 *
	 * As CAVElib is not aware of OpenGL 3.0+, it always uses the matrix stack.
	 * So in order to get the matrices, we have to read them from the stack.
	 * For example:
	 *
	 * std::array<float, 16> modelview_matrix;
	 * glGetFloatv(GL_MODELVIEW_MATRIX, modelview_matrix.data());
	 * std::array<float, 16> projection_matrix;
	 * glGetFloatv(GL_PROJECTION_MATRIX, projection_matrix.data());
	 *
	 */

	glRotatef(-state_.rotation_y  * 180.0f / pi_constant, 0.0f, 1.0f, 0.0f);
	glTranslatef(state_.position.x, state_.position.y, state_.position.z);

	// The important part here is that particles are handled only through const references.
	// And the vector is never modified.
	for (const auto& p: particles_) {
		p.draw();
	}

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


	init_gl();
	std::random_device rd;
	generator_.seed(rd());
	glutDisplayFunc(render_glut);
	glutReshapeFunc(resize_glut);
	glutIdleFunc(render_glut);


	glutKeyboardFunc(keyboard_glut);

	glutMainLoop();
	return 0;
#endif
}


}




