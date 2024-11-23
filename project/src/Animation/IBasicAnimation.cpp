#include "IBasicAnimation.h"
#include "Generator.h"

namespace Animation
{
	std::unique_ptr<Generator> IBasicAnimation::CreateGenerator()
	{
		return std::make_unique<LinearClipGenerator>(std::static_pointer_cast<IBasicAnimation>(shared_from_this()));
	}
}