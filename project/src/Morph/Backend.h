#pragma once
#include "Values.h"

// Applies actors' morph values (Values.h) to their meshes. Fallout 4: once per frame, on the main thread just before the
// world is drawn, every actor whose morph totals changed has them applied (ShapeMorpher.h). A mesh can be morphed when it
// has a TRI file: the one its BODYTRI extra data names (as BodySlide builds them for LooksMenu), or any in
// Data/NAF/Morphs with a shape of the same name. Other games don't apply morphs yet.
namespace Morph::Backend
{
	struct ShapeInfo
	{
		std::string name;
		uint32_t vertices = 0;
		bool takenOver = false;
		std::string state;
		size_t active = 0;
		size_t moved = 0;
		std::vector<std::string> morphs;
	};

	// The actor's morphable shapes, binding them first if needed. Main thread.
	std::vector<ShapeInfo> GetShapes(RE::Actor* a_actor);
}
