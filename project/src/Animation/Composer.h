#pragma once

namespace Animation
{
	class Composer
	{
	public:
		struct ActorData
		{
			RE::NiPointer<RE::Actor> actor;
			std::string animFile;
		};

		void Process();
		void StopAll();
		ActorData* AddActor(RE::Actor* a_actor);
		bool RemoveActor(RE::Actor* a_actor);

		std::vector<std::unique_ptr<ActorData>> members;
	};
}