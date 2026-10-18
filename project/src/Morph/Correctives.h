#pragma once
#include "Drivers.h"

// Runs the corrective drivers (Drivers.h) on every loaded actor, whether NAF animates it or not. Fallout 4: once per
// frame, on the main thread just before morphs are applied (Backend.h), every high process actor that's visible has its
// race skeleton's drivers evaluated on the bones' local transforms, read straight from its skeleton nodes after the
// game's (or NAF's) animation update, and the results written to its Layer::kCorrective. Actors that aren't visible keep
// their last values and cost nothing; actors that leave the high process, or lose their 3D, have their correctives
// cleared. Other games don't run correctives yet.
namespace Morph::Correctives
{
	// Main thread.
	void Update();

	// Evaluates the actor's drivers on its current pose without writing anything, for the console. Empty if its skeleton
	// has no drivers. Main thread.
	std::vector<DriverDebug> Debug(RE::Actor* a_actor);
}
