﻿#include "Args.hpp"
#include "engine_sfml.hpp"

#include <iostream> // cerr
#include <string>
#include <cstdlib> // atof

using namespace std;


//!!...
extern
#include "../out/commit_hash.inc"


//============================================================================
int main(int argc, char* argv[])
//============================================================================
{
	Args args(argc, argv, {
			{"moons", 1}, // number of moons to start with
	});
	//auto exename = args.exename();
	if (args["?"] || args["h"] || args["help"]) {
		cout << "Usage: [-V] [--moons n]" << endl;
		return 0;
	}
	if (args["V"]) {
		cout << "Version: " << LAST_COMMIT_HASH << endl;
		return 0;
	}

	Engine_SFML engine;/*({
		.moons = args("moons")
	});*/
	if (args["moons"]) {
		engine.add_bodies( ::atoi(args("moons").c_str()) );
	}

	engine.run();

	return 0;
}
//----------------------------------------------------------------------------
