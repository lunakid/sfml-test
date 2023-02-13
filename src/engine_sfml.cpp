#include "engine_sfml.hpp"

#include <SFML/Window/VideoMode.hpp>
#include <SFML/Window/Context.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Sleep.hpp>

#include <thread>
#include <mutex>

#include <memory>
	using std::make_shared;
#include <cstdlib>
	using std::rand; // and the RAND_MAX macro!
#include <iostream>
	using std::cerr, std::endl;
#include <cassert>

using namespace std;


//
// "App-local global" implementation-level (const) params...
//
static constexpr auto WINDOW_TITLE = "Out of Nothing";
namespace cfg {
	static constexpr auto FPS_THROTTLE = 30;
}

namespace sync {
	std::mutex Updating;
};


Engine_SFML::Engine_SFML()
		// Creating the window right away here (only) to support init-by-constr. for the HUDs:
		: window(sf::VideoMode({Renderer_SFML::VIEW_WIDTH, Renderer_SFML::VIEW_HEIGHT}), WINDOW_TITLE)
		//!!??	For SFML + OpenGL mixed mode (https://www.sfml-dev.org/tutorials/2.5/window-opengl.php):
		//!!??
		//sf::glEnable(sf::GL_TEXTURE_2D); //!!?? why is this needed, if SFML already draws into an OpenGL canvas?!
		//!!??	--> https://en.sfml-dev.org/forums/index.php?topic=11967.0

#ifndef DISABLE_HUD
			, debug_hud(window, -220)
			, help_hud(window, 10, HUD::DEFAULT_PANEL_TOP, 0x40d040ff, 0x40f040ff/4) // left = 10
#endif
{
		_setup();
}


bool Engine_SFML::run()
{
	//! The event loop will block and sleep.
	//! The update thread is safe to start before the event loop, but we should also draw something
	//! already before the first event, so we have to release the SFML (OpenGL) Window (crucial!),
	//! and unfreeze the update thread (which would wait on the first event by default).
	if (!window.setActive(false)) { //https://stackoverflow.com/a/23921645/1479945
		cerr << "\n- [main] sf::setActive(false) failed, WTF?! Terminating.\n";
		return false;
	}

	ui_event_state = SimApp::UIEventState::IDLE;

#ifndef DISABLE_THREADS
	std::thread engine_updates(&Engine_SFML::update_thread_main_loop, this);
			// &engine a) for `this`, b) when this wasn't a member fn, the value form vs ref was ambiguous and failed to compile,
			// and c) the thread ctor would copy the params (by default), and that would be really wonky for the entire engine! :)
#endif

	event_loop();

#ifndef DISABLE_THREADS
//cerr << "TRACE - before threads join\n";
	engine_updates.join();
#endif

	return true;
}

//----------------------------------------------------------------------------
void Engine_SFML::update_thread_main_loop()
{
	sf::Context context; //!! Seems redundant, as it can draw all right, but https://www.sfml-dev.org/documentation/2.5.1/classsf_1_1Context.php#details
	                     //!! The only change I can see is a different getActiveContext ID here, if this is enabled.

	std::unique_lock proc_lock{sync::Updating, std::defer_lock};

#ifndef DISABLE_THREADS
	while (!terminated()) {
#endif
		switch (ui_event_state) {
		case UIEventState::BUSY:
//cerr << " [[[...BUSY...]]] ";
			break;
		case UIEventState::IDLE:
		case UIEventState::EVENT_READY:
			try {
				proc_lock.lock();
			} catch (...) {
				cerr << "- WTF proc_lock failed?! (already locked? " << proc_lock.owns_lock() << ")\n";
			}
			updates_for_next_frame();
			//!!?? Why is this redundant?!
			if (!window.setActive(true)) { //https://stackoverflow.com/a/23921645/1479945
				cerr << "\n- [update_thread_main_loop] sf::setActive(false) failed!\n";
//?				terminate();
//?				return;
			}

			//!! This is problematic, as it currently relies on sf::setFrameRateLImit()
			//!! which would make the thread sleep -- but with still holding the lock! :-/
			//!! So... Either control the framerate ourselves (the upside of which is one
			//!! less external API dependency), or further separate rendering from actual
			//!! displaying (so we can release the update lock right after renedring)
			//!! -- AND THEN ALSO IMPLEMENTING SYNCING BETWEEN THOSE TWO!...
			draw();
			if (!window.setActive(false)) { //https://stackoverflow.com/a/23921645/1479945
				cerr << "\n- [update_thread_main_loop] sf::setActive(false) failed!\n";
//?				terminate();
//?				return;
			}
			proc_lock.unlock();
			break;
		default:
			assert(("[[[...!!UNKNOWN EVENT STATE!!...]]]", false));
		}

//cerr << "- releasing Events...\n";
		//sync::EventsFreeToGo.release();

/* Doing it with setFramerateLimit() now!
	//! If there's still time left from the frame slice:
	sf::sleep(sf::milliseconds(30)); //!! (remaining_time_ms)
		//! This won't stop the other (e.g. event loop) thread(s) from churning, though!
		//! -> use blocking/sleeping event query there!
		//! Nor does it ruin the smooth rendering! :-o WTF?!
		//! -> Because SFML double-buffers implicitly, AFAIK...
*/
//cerr << "sf::Context [update loop]: " << sf::Context::getActiveContextId() << endl;
#ifndef DISABLE_THREADS
	}
#endif
}

//----------------------------------------------------------------------------
void Engine_SFML::draw()
{
	renderer.render(*this);
		// Was in updates_for_next_frame(), but pause should not stop UI updates.
		//!!...raising the question: at which point should UI rendering be separated from world rendering?

	window.clear();

	renderer.draw(*this);
#ifndef DISABLE_HUD
	if (_show_huds) {
		debug_hud.draw(window);
		if (help_hud.active()) help_hud.draw(window); //!! the active-chk is redundant, the HUD does the same; TBD, who's boss!
		                                              //!! "activity" may mean more than drawing. so... actually both can do it?
	}
#endif

	window.display();
}

//----------------------------------------------------------------------------
void Engine_SFML::updates_for_next_frame()
// Should be idempotent -- which doesn't matter normally, but testing could reveal bugs if it isn't!
{
	if (physics_paused()) {
		sf::sleep(sf::milliseconds(50)); //!!that direct 50 is gross, but...
		return;
	}
	auto frame_delay = clock.getElapsedTime().asSeconds();
	clock.restart(); //! Must also be duly restarted on unpausing!
	avg_frame_delay.update(frame_delay);

	// Saving the old superglobe position for things like auto-scroll:
	auto p0 = world.bodies[globe_ndx]->p;

	world.recalc_next_state(frame_delay, *this);

	// Auto-scroll to follow player movement:
	//!!Unfortunately, the perfect key -- Scroll Lock -- doesn't produce a valid keykode
	//!!in SFML... :-( But perhaps we could still use it, as not very many other keys
	//!!give -1! :))
	if (kbd_state[KBD_STATE::SCROLL_LOCK] || kbd_state[KBD_STATE::SHIFT]) {
		pan_follow_body(globe_ndx, p0.x, p0.y);
	}
}


void Engine_SFML::pan_center_body(auto body_id)
{
	const auto& body = world.bodies[body_id];
	_OFFSET_X = - body->p.x * _SCALE;
	_OFFSET_Y = - body->p.y * _SCALE;
}

void Engine_SFML::pan_follow_body(auto body_id, float old_x, float old_y)
{
	const auto& body = world.bodies[body_id];
	_OFFSET_X -= (body->p.x - old_x) * _SCALE;
	_OFFSET_Y -= (body->p.y - old_y) * _SCALE;
}



//----------------------------------------------------------------------------
void Engine_SFML::event_loop()
{
	sf::Context context; //!! Seems redundant; it can draw all right, but https://www.sfml-dev.org/documentation/2.5.1/classsf_1_1Context.php#details

	std::unique_lock noproc_lock{sync::Updating, std::defer_lock};

	while (window.isOpen() && !terminated()) {
			sf::Event event;

#ifndef DISABLE_THREADS
		if (!window.waitEvent(event)) {
			cerr << "- Event processing failed. WTF?! Terminating.\n";
			exit(-1);
		}

		ui_event_state = UIEventState::BUSY;
//cerr << "- acquiring lock for events...\n";
		noproc_lock.lock();

#else
/*!!?? Why did this (always?) fail?!
		if (!window.pollEvent(event)) {
			cerr << "- Event processing failed. WTF?! Terminating.\n";
			exit(-1);
		}
??!!*/			

		// This inner loop is only for non-threading mode, to prevent event processing
		// (reaction) delays (or even loss) due to accumulating events coming faster
		// than 1/frame (for a long enough period to cause noticable jam/stutter)!
		for (; window.pollEvent(event);) {

		ui_event_state = UIEventState::BUSY;
//cerr << "- acquiring events...\n";
#endif
			if (!window.setActive(false)) { //https://stackoverflow.com/a/23921645/1479945
				cerr << "\n- [event_loop] sf::setActive(false) failed!\n";
//?				terminate();
//?				return;
			}

			//!! The update thread may still be busy calculating, so we can't just go ahead and change things!
			//!! But... then again, how come this thing still works at all?! :-o
			//!! Clearly there must be cases when processing the new event here overlaps with an ongoing update
			//!! cycle before they notice our "BUSY" state change here, and stop! :-o
			//!! So... a semaphore from their end must be provided to this point of this thread, too!
			//!! And it may not be just as easy as sg. like:
			//!!while (engine.busy_updating())
			//!!	;
			//!! A much cleaner way would be pushing the events received here
			//!! into a queue in the update/processing thread, like:
			//!! engine.inputs.push(event);
			//!! (And then the push here and the pop there must be synchronized -- hopefully just <atomic> would do.)

			switch (event.key.code) {
				case sf::Keyboard::Up: case sf::Keyboard::W:
				    if (event.type == sf::Event::KeyPressed)
						up_thruster_start(); else up_thruster_stop();
					break;
				case sf::Keyboard::Down: case sf::Keyboard::S:
				    if (event.type == sf::Event::KeyPressed)
						down_thruster_start(); else down_thruster_stop();
					break;
				case sf::Keyboard::Left:  case sf::Keyboard::A:
				    if (event.type == sf::Event::KeyPressed)
						left_thruster_start(); else left_thruster_stop();
					break;
				case sf::Keyboard::Right: case sf::Keyboard::D:
				    if (event.type == sf::Event::KeyPressed)
						right_thruster_start(); else right_thruster_stop();
					break;
			}
			switch (event.type)
			{
			case sf::Event::KeyReleased:
				switch (event.key.code) {
				case sf::Keyboard::LShift:   kbd_state[KBD_STATE::LSHIFT] = false; break;
				case sf::Keyboard::RShift:   kbd_state[KBD_STATE::RSHIFT] = false; break;
				case sf::Keyboard::LControl: kbd_state[KBD_STATE::LCTRL]  = false; break;
				case sf::Keyboard::RControl: kbd_state[KBD_STATE::RCTRL]  = false; break;
				case sf::Keyboard::LAlt:     kbd_state[KBD_STATE::LALT]   = false; break;
				case sf::Keyboard::RAlt:     kbd_state[KBD_STATE::RALT]   = false; break;
//				case -1:
//cerr << "INVALID KEYPRESS -1 is assumed to be Scroll Lock!... ;-o \n";
// (But actually Caps and Num Lock also give -1...)
//					kbd_state[KBD_STATE::SCROLL_LOCK] = false;
//					break;
				}
				kbd_state[KBD_STATE::SHIFT] = kbd_state[KBD_STATE::LSHIFT] || kbd_state[KBD_STATE::RSHIFT]; //!! SFML/Windows BUG: https://github.com/SFML/SFML/issues/1301
				kbd_state[KBD_STATE::CTRL]  = kbd_state[KBD_STATE::LCTRL]  || kbd_state[KBD_STATE::RCTRL];
				kbd_state[KBD_STATE::ALT]   = kbd_state[KBD_STATE::LALT]   || kbd_state[KBD_STATE::RALT];
				break;
			case sf::Event::KeyPressed:
				switch (event.key.code) {
				case sf::Keyboard::LShift:   kbd_state[KBD_STATE::SHIFT] = kbd_state[KBD_STATE::LSHIFT] = true; break;
				case sf::Keyboard::RShift:   kbd_state[KBD_STATE::SHIFT] = kbd_state[KBD_STATE::RSHIFT] = true; break;
				case sf::Keyboard::LControl: kbd_state[KBD_STATE::CTRL]  = kbd_state[KBD_STATE::LCTRL]  = true; break;
				case sf::Keyboard::RControl: kbd_state[KBD_STATE::CTRL]  = kbd_state[KBD_STATE::RCTRL]  = true; break;
				case sf::Keyboard::LAlt:     kbd_state[KBD_STATE::ALT]   = kbd_state[KBD_STATE::LALT]   = true; break;
				case sf::Keyboard::RAlt:     kbd_state[KBD_STATE::ALT]   = kbd_state[KBD_STATE::RALT]   = true; break;

				case sf::Keyboard::Escape: //!!Merge with Closed!
					terminate();
					// [fix-setactive-fail] -> DON'T: window.close();
					break;

				case sf::Keyboard::Up:
					if (event.key.shift) pan_down();
					break;
				case sf::Keyboard::Down:
					if (event.key.shift) pan_up();
					break;
				case sf::Keyboard::Left:
					if (event.key.shift) pan_right();
					break;
				case sf::Keyboard::Right:
					if (event.key.shift) pan_left();
					break;

				case sf::Keyboard::Home: pan_reset(); break;
				case sf::Keyboard::F12: toggle_huds(); break;
				case sf::Keyboard::F11: toggle_fullscreen(); break;

				case -1:
cerr << "INVALID KEYPRESS -1 is assumed to be Scroll Lock!... ;-o \n";
					kbd_state[KBD_STATE::SCROLL_LOCK] = !kbd_state[KBD_STATE::SCROLL_LOCK];
					break;

//				default:
//cerr << "UNHANDLED KEYPRESS: " << event.key.code << endl;
				}
				break;

			case sf::Event::MouseWheelScrolled:
				if (event.mouseWheelScroll.delta > 0) zoom_in(); else zoom_out();
//				renderer.p_alpha += (uint8_t)event.mouseWheelScroll.delta * 4; //! seems to always be 1 or -1...
				break;

			case sf::Event::TextEntered:
				if (event.text.unicode > 128) break; // non-ASCII!
				switch (static_cast<char>(event.text.unicode)) {
				case 'N': spawn(); break;
				case 'n': spawn(100); break;
				case 'R': remove_body(); break;
				case 'r': remove_bodies(100); break;
				case 'i': toggle_interact_all(); break;
				case 'f': world.FRICTION -= 0.01f; break;
				case 'F': world.FRICTION += 0.01f; break;
				case '+': zoom_in(); break;
				case '-': zoom_out(); break;
				case 'h': pan_center_body(globe_ndx); break;
				case ' ': toggle_physics(); break;
				case 'm': toggle_music(); break;
				case 'M': toggle_sound_fxs(); break;
				case '?': toggle_help(); break;
				}
				break;

			case sf::Event::LostFocus:
				renderer.p_alpha = Renderer_SFML::ALPHA_INACTIVE;
				break;

			case sf::Event::GainedFocus:
				renderer.p_alpha = Renderer_SFML::ALPHA_ACTIVE;
				break;

			case sf::Event::Closed: //!!Merge with key:Esc!
				terminate();
//cerr << "BEGIN sf::Event::Closed\n"; //!!this frame is to trace an error from SFML/OpenGL
				window.close();
//cerr << "END sf::Event::Closed\n";
				break;

			default:
				ui_event_state = UIEventState::IDLE;

				break;
			}

//cerr << "sf::Context [event loop]: " << sf::Context::getActiveContextId() << endl;

#ifndef DISABLE_THREADS
		ui_event_state = UIEventState::EVENT_READY;
//cerr << "- freeing proc lock...\n";
		noproc_lock.unlock();
#else
		} // for
		ui_event_state = UIEventState::EVENT_READY;
		update_thread_main_loop(); // <- not looping when threads disabled
//!!test idempotency:
//!!	draw();

#endif			
	} // while
}


//----------------------------------------------------------------------------
void Engine_SFML::spawn(size_t n)
{
	add_bodies(n);
}

//----------------------------------------------------------------------------
size_t Engine_SFML::add_body(World_SFML::Body&& obj)
{
	auto ndx = world.add_body(std::forward<decltype(obj)>(obj));
	// Pre-cache shapes for rendering... (!! Likely pointless, but this is just what I started with...)
	renderer.create_cached_body_shape(*this, obj, ndx);
	return ndx;
}

size_t Engine_SFML::add_body()
{
	auto r_min = CFG_GLOBE_RADIUS / 10;
	auto r_max = CFG_GLOBE_RADIUS * 0.5;
	auto p_range = CFG_GLOBE_RADIUS * 5;
	auto v_range = CFG_GLOBE_RADIUS * 10; //!!Stop depending on GLOBE_RADIUS so directly/cryptically!

//cerr << "Adding new object #" << world.bodies.size() + 1 << "...\n";

	return add_body({
		.r = (float) ((rand() * (r_max - r_min)) / RAND_MAX ) //! suppress warning "conversion from double to float..."
				+ r_min,
		.p = { (rand() * p_range) / RAND_MAX - p_range/2 + world.bodies[globe_ndx]->p.x,
		       (rand() * p_range) / RAND_MAX - p_range/2 + world.bodies[globe_ndx]->p.y },
		.v = { (rand() * v_range) / RAND_MAX - v_range/2 + world.bodies[globe_ndx]->v.x * 0.1f,
		       (rand() * v_range) / RAND_MAX - v_range/2 + world.bodies[globe_ndx]->v.y * 0.1f },
		.color = (uint32_t) (float)0xffffff * rand(),
	});
}

void Engine_SFML::add_bodies(size_t n)
{
	while (n--) add_body();
}

void Engine_SFML::remove_body(size_t ndx)
{
	world.remove_body(ndx);
	renderer.delete_cached_body_shape(*this, ndx);
}

void Engine_SFML::remove_body()
{
	if (world.bodies.size() < 2) { // Leave the "globe" (so not ".empty()")!
cerr <<	"No more \"free\" items to delete.\n";
		return;
	}

	auto ndx = 1/*leave the globe!*/ + rand() * ((world.bodies.size()-1) / (RAND_MAX + 1));
//cerr << "Deleting object #"	 << ndx << "...\n";
	assert(ndx > 0);
	assert(ndx < world.bodies.size());
	remove_body(ndx);
}

void Engine_SFML::remove_bodies(size_t n)
{
	while (n--) remove_body();
}

//----------------------------------------------------------------------------
size_t Engine_SFML::add_player(World_SFML::Body&& obj)
{
	// These are the only modelling differences for now:
	obj.add_thrusters();
	obj.superpower.gravity_immunity = true;

	return add_body(std::forward<World_SFML::Body>(obj));
}

void Engine_SFML::remove_player(size_t ndx)
{ndx;
}

bool Engine_SFML::touch_hook(World* w, World::Body* obj1, World::Body* obj2)
{w;
	if (obj1->is_player() || obj2->is_player()) {
		audio.play_sound(clack_sound);
	}
	return false; //!!this is not used yet, but I will forget this when it gets to be... :-/
}


//----------------------------------------------------------------------------
void Engine_SFML::toggle_fullscreen()
{
	static bool is_full = false;

	if (!window.setActive(false)) { //https://stackoverflow.com/a/23921645/1479945
cerr << "\n- [update_thread_main_loop] sf::setActive(false) failed!\n";
	}

	is_full = !is_full;

	window.create(
		is_full ? sf::VideoMode::getDesktopMode() : sf::VideoMode({Renderer_SFML::VIEW_WIDTH, Renderer_SFML::VIEW_HEIGHT}),
		WINDOW_TITLE,
		is_full ? sf::Style::Fullscreen|sf::Style::Resize : sf::Style::Resize
	);
	window.setFramerateLimit(cfg::FPS_THROTTLE);

	onResize();

	if (!window.setActive(true)) { //https://stackoverflow.com/a/23921645/1479945
cerr << "\n- [update_thread_main_loop] sf::setActive(false) failed!\n";
	}

//	if (!(is_full = !is_full) /* :) */) {
//		// full
//	} else {
//		// windowed
//	}
}

void Engine_SFML::onResize()
{
	debug_hud.onResize(window);
	help_hud.onResize(window);
}

//----------------------------------------------------------------------------
void Engine_SFML::_setup()
{
	//! Note: the window itself has just been created by the ctor.!
	//! But... it will also be recreated each time the fullscreen/windowed
	//! mode is toggled, so this will need to be repeated after every
	//! `window.create` call (i.e. in `toggle_fullscreen`):
	window.setFramerateLimit(cfg::FPS_THROTTLE);

	// globe:
	globe_ndx = add_player({ .r = CFG_GLOBE_RADIUS, .density = world.DENSITY_ROCK, .p = {0,0}, .v = {0,0}, .color = 0xb02000});
	// moons:
	add_body({ .r = CFG_GLOBE_RADIUS/10, .p = {CFG_GLOBE_RADIUS * 2, 0}, .v = {0, -CFG_GLOBE_RADIUS * 2}, .color = 0x14b0c0});
	add_body({ .r = CFG_GLOBE_RADIUS/7,  .p = {-CFG_GLOBE_RADIUS * 1.6f, +CFG_GLOBE_RADIUS * 1.2f}, .v = {-CFG_GLOBE_RADIUS*1.8, -CFG_GLOBE_RADIUS*1.5},
				.color = 0xa0f000});

	clack_sound = audio.add_sound("asset/sound/clack.wav");

	audio.play_music("asset/music/default.ogg");
	/*
	static sf::Music m2; if (m2.openFromFile("asset/music/extra sonic layer.ogg")) {
		m2.setLoop(false); m2.play();
	}
	*/

	_setup_huds();
}

void Engine_SFML::_setup_huds()
{
#ifndef DISABLE_HUD
	//!!?? Why do all these member pointers just work, also without so much as a warning,
	//!!?? in this generic pointer passing context?!
	debug_hud.add("Press ? for help...");

	debug_hud.add("FPS", [this](){ return to_string(1 / (float)this->avg_frame_delay); });
	debug_hud.add("# of objs.", [this](){ return to_string(this->world.bodies.size()); });
	debug_hud.add("Friction", [this](){ return to_string(this->world.FRICTION); });

//!!This one still crashes (both in debug & release builds)! :-o
//!!debug_hud.add("globe R", &CFG_GLOBE_RADIUS);

	debug_hud.add("Globe R", &world.bodies[globe_ndx]->r); //!!and also this did earlier! :-o WTF??!?! how come ->mass didn't then?!?!
	                                                       //!!??and how come it doesn't again after a recompilation?!?!?!?!
	debug_hud.add("      m", &world.bodies[globe_ndx]->mass);
	debug_hud.add("      x",    &world.bodies[globe_ndx]->p.x);
	debug_hud.add("      y",    &world.bodies[globe_ndx]->p.y);
	debug_hud.add("      vx",   &world.bodies[globe_ndx]->v.x);
	debug_hud.add("      vy",   &world.bodies[globe_ndx]->v.y);

	debug_hud.add("All-body interactions", &world._interact_all);

//	debug_hud.add("pan X", &_OFFSET_X);
//	debug_hud.add("pan Y", &_OFFSET_Y);
	debug_hud.add("SCALE", &_SCALE);
//	debug_hud.add("SHIFT", (bool*)&kbd_state[KBD_STATE::SHIFT]);
//	debug_hud.add("ALT", (bool*)&kbd_state[KBD_STATE::ALT]);


//	help_hud.add("THIS IS NOT A TOY. SMALL ITEMS. DO NOT SWALLOW.");
//	help_hud.add("");
	help_hud.add("AWSD (or arrows): thrust");
	help_hud.add("N:      add 100 objects (Shift+N: only 1)");
	help_hud.add("R:      remove 100 objects (Shift+R: only 1)");
	help_hud.add("I:      toggle all-body interactions");
	help_hud.add("F:      decrease, Shift+F: increase friction");
	help_hud.add("Space:  pause the physics");
	help_hud.add("Shift+arrows: pan, Scroll Lock: autoscroll");
	help_hud.add("mouse wheel (or +/-): zoom");
	help_hud.add("H:      home in on the globe");
	help_hud.add("Home:   reset view position (not the zoom)");
	help_hud.add("M:      (un)mute music (Shift+M: same for snd. fx.)");
	help_hud.add("F11:    toggle fullscreen");
	help_hud.add("F12:    toggle HUDs");
	help_hud.add("Esc:    quit");
	help_hud.add("");
	help_hud.add("Command-line options: oon.exe /?");

	help_hud.active(true);
#endif
}

void Engine_SFML::up_thruster_start()    { world.bodies[globe_ndx]->thrust_up.thrust_level(CFG_THRUST_FORCE); }
void Engine_SFML::down_thruster_start()  { world.bodies[globe_ndx]->thrust_down.thrust_level(CFG_THRUST_FORCE); }
void Engine_SFML::left_thruster_start()  { world.bodies[globe_ndx]->thrust_left.thrust_level(CFG_THRUST_FORCE); }
void Engine_SFML::right_thruster_start() { world.bodies[globe_ndx]->thrust_right.thrust_level(CFG_THRUST_FORCE); }

void Engine_SFML::up_thruster_stop()     { world.bodies[globe_ndx]->thrust_up.thrust_level(0); }
void Engine_SFML::down_thruster_stop()   { world.bodies[globe_ndx]->thrust_down.thrust_level(0); }
void Engine_SFML::left_thruster_stop()   { world.bodies[globe_ndx]->thrust_left.thrust_level(0); }
void Engine_SFML::right_thruster_stop()  { world.bodies[globe_ndx]->thrust_right.thrust_level(0); }
