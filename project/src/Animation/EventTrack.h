#pragma once
#include "IAnimEventHandler.h"

namespace Animation
{
	class EventTrack
	{
	public:
		struct Key
		{
			float time;
			RE::BSFixedString eventName;
			RE::BSFixedString arg;
		};

		std::vector<Key> keys;

		size_t GetSize() const;
		void InitKeys(float a_duration);
		void SampleEvents(float a_currentTime, float a_timeStep, IAnimEventHandler* a_eventHandler);
	};
}