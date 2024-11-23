#pragma once

namespace Animation
{
	class IAnimEventHandler
	{
	public:
		virtual ~IAnimEventHandler() = default;
		virtual void QueueEvent(const RE::BSFixedString& a_event, const RE::BSFixedString& a_arg) = 0;
	};
}