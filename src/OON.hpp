#ifndef __OON__
#define __OON__

#include "SimApp.hpp"

//============================================================================
class OON : public SimApp
{
//------------------------------------------------------------------------
// Model event hooks (callbacks)...
//------------------------------------------------------------------------
public:
	void interaction_hook(Model::World* w, Model::World::Event event, Model::World::Body* obj1, Model::World::Body* obj2, ...) override;

//------------------------------------------------------------------------
// API Ops...
//------------------------------------------------------------------------
public:
	//------------------------------------------------------------------------
	// Player (gameplay) actions:
	virtual void spawn(size_t n = 1);
	virtual void exhaust_burst();

	auto toggle_interact_all()  { world._interact_all = !world._interact_all; }

	//! These should be idempotent, to tolerate keyboard repeats (which could be disabled, but better be robust)!
	//!!Also -> #105: sanitize thrusters...
	void    up_thruster_start();
	void  down_thruster_start();
	void  left_thruster_start();
	void right_thruster_start();
	void    up_thruster_stop();
	void  down_thruster_stop();
	void  left_thruster_stop();
	void right_thruster_stop();

	auto pan_up()     { _OFFSET_Y -= CFG_PAN_STEP; }
	auto pan_down()   { _OFFSET_Y += CFG_PAN_STEP; }
	auto pan_left()   { _OFFSET_X -= CFG_PAN_STEP; }
	auto pan_right()  { _OFFSET_X += CFG_PAN_STEP; }
	auto pan_reset()  { _OFFSET_X = _OFFSET_Y = 0; }
	void _pan_adjust_after_zoom() { /* !!?? */ }

	//------------------------------------------------------------------------
	// Internals: not even user actions (Well, some still are, basically for testing.)
	//!!Make a proper distinction between these and the player/user actions!
	//!!(One thing's that those tend/should go through the UI, whereas these shouldn't.)
	size_t add_body(); // add a random one
	void   add_bodies(size_t n);
	void   remove_body(); // delete a random one
	void   remove_bodies(size_t n = -1); // -1 -> all
	virtual size_t add_body(Model::World::Body&& obj) = 0;
	virtual void   remove_body(size_t ndx) = 0;

//------------------------------------------------------------------------
// C++ mechanics...
//------------------------------------------------------------------------
public:
	OON() = default;
	OON(const OON&) = delete;

//------------------------------------------------------------------------
// Data / Internals...
//------------------------------------------------------------------------
protected:
	float _SCALE = CFG_DEFAULT_SCALE;
	float _OFFSET_X = 0, _OFFSET_Y = 0;

	size_t globe_ndx = 0; // paranoid safety init (see _setup()!)
	size_t clack_sound = 0; // paranoid safety init (see _setup()!)
};

#endif // __OON__
