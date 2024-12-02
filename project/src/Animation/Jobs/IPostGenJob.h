#pragma once

namespace Animation
{
	class IPostGenJob
	{
	public:
		struct Context
		{
			ozz::math::SoaTransform* localTransforms;
			uint64_t localCount;
			ozz::math::Float4x4* modelSpaceMatrices;
			uint64_t modelSpaceCount;
			ozz::math::Float4x4 rootMatrix;
			ozz::math::Float4x4 invertedRootMatrix;
			ozz::animation::Skeleton* skeleton;
			float deltaTime;
		};

		virtual bool Run(const Context& a_context) = 0;
		virtual uint64_t GetGUID() = 0;
		virtual void Destroy() = 0;
	};
}